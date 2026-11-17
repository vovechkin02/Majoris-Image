#include <math.h>
#include <stdio.h>
#include <string.h>

#include <ultrahdr_api.h>

#include "bridge.h"

// GNU11 binary16 samples match libuhdr's half-float layout before C23.
__extension__ typedef _Float16 HalfFloat;

static gboolean ultra_error(uhdr_error_info_t status, GError **error)
{
    if (status.error_code == UHDR_CODEC_OK) {
        return FALSE;
    }
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Ultra HDR: %s",
                status.has_detail ? status.detail : "Codec operation failed");
    return TRUE;
}

static uhdr_codec_private_t *open_ultra(const char *path, GMappedFile **mapping, GError **error)
{
    *mapping = g_mapped_file_new(path, FALSE, error);
    if (!*mapping) {
        return NULL;
    }
    uhdr_codec_private_t *decoder = uhdr_create_decoder();
    if (!decoder) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "Cannot allocate Ultra HDR decoder");
        return NULL;
    }
    uhdr_compressed_image_t source = {0};
    source.data = g_mapped_file_get_contents(*mapping);
    source.data_sz = source.capacity = g_mapped_file_get_length(*mapping);
    source.cg = UHDR_CG_UNSPECIFIED;
    source.ct = UHDR_CT_UNSPECIFIED;
    source.range = UHDR_CR_UNSPECIFIED;
    if (ultra_error(uhdr_dec_set_image(decoder, &source), error)) {
        uhdr_release_decoder(decoder);
        return NULL;
    }
    return decoder;
}

gboolean image_codec_ultra_probe(const char *path)
{
    // Do not map and copy huge native/TIFF/EXR intermediates into a JPEG/HEIF
    // parser. Only the actual container signature can select this backend.
    guint8 signature[16];
    FILE *file = fopen(path, "rb");
    if (!file) {
        return FALSE;
    }
    size_t length = fread(signature, 1, sizeof(signature), file);
    fclose(file);
    if (length < 12 || !((signature[0] == 0xff && signature[1] == 0xd8 && signature[2] == 0xff) ||
                         !memcmp(signature + 4, "ftyp", 4))) {
        return FALSE;
    }
    GMappedFile *mapping = NULL;
    GError *error = NULL;
    uhdr_codec_private_t *decoder = open_ultra(path, &mapping, &error);
    gboolean result = decoder && !ultra_error(uhdr_dec_probe(decoder), &error) &&
                      uhdr_dec_get_gainmap_width(decoder) > 0;
    if (decoder) {
        uhdr_release_decoder(decoder);
    }
    if (mapping) {
        g_mapped_file_unref(mapping);
    }
    g_clear_error(&error);
    return result;
}

VipsImage *image_codec_ultra_decode(const char *path, gboolean sdr, GError **error)
{
    GMappedFile *mapping = NULL;
    VipsImage *image = NULL;
    VipsImage *linear = NULL;
    uhdr_codec_private_t *decoder = open_ultra(path, &mapping, error);
    if (!decoder) {
        goto done;
    }
    if (ultra_error(uhdr_dec_set_out_img_format(decoder, sdr ? UHDR_IMG_FMT_32bppRGBA8888
                                                             : UHDR_IMG_FMT_64bppRGBAHalfFloat),
                    error) ||
        ultra_error(uhdr_dec_set_out_color_transfer(decoder, sdr ? UHDR_CT_SRGB : UHDR_CT_LINEAR),
                    error) ||
        ultra_error(uhdr_dec_probe(decoder), error)) {
        goto done;
    }
    int width = uhdr_dec_get_image_width(decoder), height = uhdr_dec_get_image_height(decoder);
    if (width <= 0 || height <= 0 || (guint64)width * height > 67108864) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "Gain-map reconstruction exceeds the worker memory budget");
        goto done;
    }
    if (ultra_error(uhdr_decode(decoder), error)) {
        goto done;
    }
    uhdr_raw_image_t *decoded = uhdr_get_decoded_image(decoder);
    if (!decoded || !decoded->w || !decoded->h || decoded->w > G_MAXINT || decoded->h > G_MAXINT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Invalid Ultra HDR dimensions");
        goto done;
    }
    size_t pixels = (size_t)decoded->w * decoded->h, bytes = pixels * 4 * (sdr ? 1 : sizeof(float));
    if (pixels > G_MAXSIZE / 16) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "Ultra HDR image exceeds address space");
        goto done;
    }
    void *buffer = g_try_malloc(bytes);
    if (!buffer) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "Insufficient memory for Ultra HDR");
        goto done;
    }
    for (unsigned y = 0; y < decoded->h; y++) {
        if (sdr) {
            memcpy((guint8 *)buffer + (size_t)y * decoded->w * 4,
                   (guint8 *)decoded->planes[0] + (size_t)y * decoded->stride[0] * 4,
                   decoded->w * 4);
        } else {
            const guint16 *source =
                (guint16 *)decoded->planes[0] + (size_t)y * decoded->stride[0] * 4;
            float *target = (float *)buffer + (size_t)y * decoded->w * 4;
            for (unsigned x = 0; x < decoded->w * 4; x++) {
                HalfFloat half;
                memcpy(&half, source + x, 2);
                target[x] = (float)half;
            }
        }
    }
    image = vips_image_new_from_memory(buffer, bytes, decoded->w, decoded->h, 4,
                                       sdr ? VIPS_FORMAT_UCHAR : VIPS_FORMAT_FLOAT);
    if (!image) {
        g_free(buffer);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, vips_error_buffer());
        goto done;
    }
    g_object_set_data_full(G_OBJECT(image), "majoris-image-ultra-pixels", buffer, g_free);
    image->Type = sdr ? VIPS_INTERPRETATION_sRGB : VIPS_INTERPRETATION_scRGB;
    if (!sdr) {
        vips_image_set_int(image, "majoris-image-transfer", 8);
        vips_image_set_int(image, "majoris-image-primaries",
                           decoded->cg == UHDR_CG_BT_2100      ? 9
                           : decoded->cg == UHDR_CG_DISPLAY_P3 ? 12
                                                               : 1);
    }
    linear = image_codec_to_linear(image, error);
    if (linear) {
        vips_image_set_int(linear, "majoris-image-gain-map", 1);
        vips_image_set_int(linear, "majoris-image-hdr", sdr ? 0 : 1);
        uhdr_gainmap_metadata_t *metadata = uhdr_dec_get_gainmap_metadata(decoder);
        if (metadata) {
            vips_image_set_double(linear, "majoris-image-gainmap-capacity-min",
                                  metadata->hdr_capacity_min);
            vips_image_set_double(linear, "majoris-image-gainmap-capacity-max",
                                  metadata->hdr_capacity_max);
            vips_image_set_double(linear, "majoris-image-gainmap-gamma", metadata->gamma[0]);
        }
    }
done:
    g_clear_object(&image);
    if (decoder) {
        uhdr_release_decoder(decoder);
    }
    if (mapping) {
        g_mapped_file_unref(mapping);
    }
    return linear;
}

gboolean image_codec_ultra_encode(VipsImage *image, const char *path, int format, int quality,
                                  GError **error)
{
    VipsImage *linear = image_codec_to_linear(image, error);
    if (!linear) {
        return FALSE;
    }
    uhdr_codec_private_t *encoder = uhdr_create_encoder();
    VipsRegion *region = NULL;
    guint16 *buffer = NULL;
    gboolean ok = FALSE;
    if (!encoder) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "Cannot allocate Ultra HDR encoder");
        goto done;
    }
    size_t count = (size_t)linear->Xsize * linear->Ysize * 4;
    buffer = g_try_malloc_n(count, sizeof(guint16));
    if (!buffer) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                            "Ultra HDR encoding exceeds memory");
        goto done;
    }
    region = vips_region_new(linear);
    for (int y = 0; y < linear->Ysize; y++) {
        VipsRect row = {0, y, linear->Xsize, 1};
        if (vips_region_prepare(region, &row)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, vips_error_buffer());
            goto done;
        }
        const float *source = (float *)VIPS_REGION_ADDR(region, 0, y);
        for (int x = 0; x < linear->Xsize; x++, source += linear->Bands) {
            if (linear->Bands == 4 && source[3] < .99999f) {
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                    "This gain-map encoder cannot preserve transparency; choose "
                                    "OpenEXR, TIFF or JPEG XL");
                goto done;
            }
            double rgb[4] = {.627404 * source[0] + .329283 * source[1] + .043313 * source[2],
                             .069097 * source[0] + .919540 * source[1] + .011362 * source[2],
                             .016391 * source[0] + .088013 * source[1] + .895595 * source[2], 1};
            for (int c = 0; c < 4; c++) {
                if (!isfinite(rgb[c]) || rgb[c] < -.001 || rgb[c] > 65504) {
                    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                        "Pixel is outside the gain-map encoder's Rec.2020 "
                                        "half-float gamut; export linear EXR instead");
                    goto done;
                }
                HalfFloat half = (HalfFloat)MAX(rgb[c], 0);
                memcpy(buffer + ((size_t)y * linear->Xsize + x) * 4 + c, &half, 2);
            }
        }
    }
    uhdr_raw_image_t raw = {0};
    raw.fmt = UHDR_IMG_FMT_64bppRGBAHalfFloat;
    raw.cg = UHDR_CG_BT_2100;
    raw.ct = UHDR_CT_LINEAR;
    raw.range = UHDR_CR_FULL_RANGE;
    raw.w = linear->Xsize;
    raw.h = linear->Ysize;
    raw.planes[0] = buffer;
    raw.stride[0] = raw.w;
#ifndef IMAGE_CODEC_UHDR_CONTAINERS
    if (format != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Installed gain-map codec only supports JPEG output");
        goto done;
    }
#endif
    uhdr_codec_t container = UHDR_CODEC_JPG;
#ifdef IMAGE_CODEC_UHDR_CONTAINERS
    if (format == 1) {
        container = UHDR_CODEC_HEIF;
    }
    if (format == 2) {
        container = UHDR_CODEC_AVIF;
    }
#endif
    if (ultra_error(uhdr_enc_set_raw_image(encoder, &raw, UHDR_HDR_IMG), error) ||
        ultra_error(uhdr_enc_set_output_format(encoder, container), error) ||
        ultra_error(uhdr_enc_set_quality(encoder, quality, UHDR_BASE_IMG), error) ||
        ultra_error(uhdr_encode(encoder), error)) {
        goto done;
    }
    uhdr_compressed_image_t *encoded = uhdr_get_encoded_stream(encoder);
    if (!encoded || !encoded->data_sz) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Ultra HDR encoder returned no data");
        goto done;
    }
    ok = g_file_set_contents(path, encoded->data, encoded->data_sz, error);
done:
    g_clear_object(&region);
    g_clear_object(&linear);
    g_free(buffer);
    if (encoder) {
        uhdr_release_encoder(encoder);
    }
    return ok;
}
