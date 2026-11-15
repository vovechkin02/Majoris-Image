#include "bridge.h"
#include <math.h>
#ifdef MAJORIS_AVIF_SEQUENCE
#include <avif/avif.h>
#endif
#ifdef MAJORIS_HEIF_SEQUENCE
#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#endif

#define FRAME_LIMIT 10000
#define PIXEL_LIMIT 16777216

#if defined(MAJORIS_AVIF_SEQUENCE) || defined(MAJORIS_HEIF_SEQUENCE)
static VipsImage *make_image(const guint8 *data, int width, int height, size_t stride,
                             int depth, int bands, GError **error)
{
    if (!data || width < 1 || height < 1 || (guint64)width * height > PIXEL_LIMIT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Sequence frame exceeds pixel budget");
        return NULL;
    }
    gboolean wide = depth > 8;
    size_t row = (size_t)width * bands * (wide ? 2 : 1);
    if (stride < row) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid sequence frame stride");
        return NULL;
    }
    guint8 *pixels = g_try_malloc_n(row, height);
    if (!pixels) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate sequence frame");
        return NULL;
    }
    for (int y = 0; y < height; y++)
        memcpy(pixels + y * row, data + y * stride, row);

    VipsImage *image = vips_image_new_from_memory(pixels, row * height, width, height,
                                                   bands, wide ? VIPS_FORMAT_USHORT : VIPS_FORMAT_UCHAR);
    if (!image) {
        g_free(pixels);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, vips_error_buffer());
        return NULL;
    }
    g_object_set_data_full(G_OBJECT(image), "sequence-pixels", pixels, g_free);
    image->Type = wide ? VIPS_INTERPRETATION_RGB16 : VIPS_INTERPRETATION_sRGB;
    vips_image_set_int(image, "majoris-image-bit-depth", depth);
    return image;
}

static gboolean set_colour(VipsImage *image, const void *profile, size_t size,
                            int primaries, int transfer, GError **error)
{
    gboolean hdr = transfer == 16 || transfer == 18;
    vips_image_set_int(image, "majoris-image-hdr", hdr);
    if (size > 4 * 1024 * 1024) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Sequence ICC profile exceeds size budget");
        return FALSE;
    }
    if (profile && size && !hdr) {
        vips_image_set_blob_copy(image, VIPS_META_ICC_NAME, profile, size);
        return TRUE;
    }
    if (primaries == 2 || primaries == 0) primaries = 1;
    if (transfer == 2 || transfer == 0) transfer = 13;
    if ((primaries != 1 && primaries != 9 && primaries != 12) ||
        (transfer != 1 && transfer != 6 && transfer != 8 && transfer != 13 &&
         transfer != 14 && transfer != 15 && !hdr)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Sequence colour profile uses unsupported primaries or transfer");
        return FALSE;
    }
    vips_image_set_int(image, "majoris-image-primaries", primaries);
    vips_image_set_int(image, "majoris-image-transfer", transfer);
    return TRUE;
}

static void set_timing(VipsImage *image, const char *loader, int count, int loops,
                       double timestamp, double duration)
{
    vips_image_set_string(image, "vips-loader", loader);
    vips_image_set_int(image, "n-pages", count);
    vips_image_set_int(image, "loop", loops);
    vips_image_set_double(image, "majoris-image-frame-pts", timestamp);
    vips_image_set_double(image, "majoris-image-frame-seconds", duration);
}
#endif

#ifdef MAJORIS_AVIF_SEQUENCE
static VipsImage *load_avif(const char *path, int frame, GError **error)
{
    avifDecoder *decoder = avifDecoderCreate();
    if (!decoder) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate AVIF decoder");
        return NULL;
    }
    VipsImage *image = NULL;
    avifRGBImage rgb = {0};
    decoder->maxThreads = 2;
    decoder->imageSizeLimit = PIXEL_LIMIT;
    decoder->imageDimensionLimit = 16384;
    decoder->imageCountLimit = FRAME_LIMIT;
    decoder->ignoreExif = AVIF_TRUE;
    decoder->ignoreXMP = AVIF_TRUE;
    avifResult result = avifDecoderSetSource(decoder, AVIF_DECODER_SOURCE_TRACKS);
    if (result == AVIF_RESULT_OK) result = avifDecoderSetIOFile(decoder, path);
    if (result == AVIF_RESULT_OK) result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) goto failed;
    if (frame < 0 || frame >= decoder->imageCount) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "AVIF frame index is outside the sequence");
        goto done;
    }
    result = avifDecoderNthImage(decoder, (uint32_t)frame);
    if (result != AVIF_RESULT_OK) goto failed;
    avifImage *src = decoder->image;
    avifRGBImageSetDefaults(&rgb, src);
    gboolean alpha = src->alphaPlane != NULL;
    rgb.format = alpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
    rgb.alphaPremultiplied = AVIF_FALSE;
    rgb.depth = src->depth > 8 ? 16 : 8;
    result = avifRGBImageAllocatePixels(&rgb);
    if (result == AVIF_RESULT_OK) result = avifImageYUVToRGB(src, &rgb);
    if (result != AVIF_RESULT_OK) goto failed;
    image = make_image(rgb.pixels, src->width, src->height, rgb.rowBytes,
                        src->depth, alpha ? 4 : 3, error);
    if (image && !set_colour(image, src->icc.data, src->icc.size,
                              src->colorPrimaries, src->transferCharacteristics, error))
        g_clear_object(&image);
    if (image) {
        int repeats = decoder->repetitionCount;
        int loops = repeats == AVIF_REPETITION_COUNT_INFINITE ? 0 : repeats < 0 ? 1 : MIN(repeats, G_MAXINT - 1) + 1;
        set_timing(image, "libavif-sequence", decoder->imageCount, loops,
                   decoder->imageTiming.pts, decoder->imageTiming.duration);
    }
    goto done;

failed:
    g_set_error(error, G_IO_ERROR,
                result == AVIF_RESULT_NO_CODEC_AVAILABLE ? G_IO_ERROR_NOT_SUPPORTED : G_IO_ERROR_INVALID_DATA,
                "AVIF sequence: %s", avifResultToString(result));
done:
    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(decoder);
    return image;
}
#endif

#ifdef MAJORIS_HEIF_SEQUENCE
static VipsImage *load_heif(const char *path, int frame, int known_count, GError **error)
{
    heif_context *context = heif_context_alloc();
    heif_track *decode_track = NULL;
    heif_decoding_options *options = NULL;
    heif_image *decoded = NULL;
    VipsImage *image = NULL;
    gpointer profile = NULL;
    heif_color_profile_nclx *colour = NULL;
    if (!context) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate HEIF decoder");
        goto done;
    }
    struct heif_error result = heif_context_read_from_file(context, path, NULL);
    if (result.code != heif_error_Ok) goto failed;
    decode_track = heif_context_get_track(context, 0);
    if (!decode_track) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "HEIF container has no visual track");
        goto done;
    }
    uint32_t timescale = heif_track_get_timescale(decode_track);
    uint16_t width, height;
    result = heif_track_get_image_resolution(decode_track, &width, &height);
    if (result.code != heif_error_Ok) goto failed;
    if (!timescale || !width || !height || (guint64)width * height > PIXEL_LIMIT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "HEIF track exceeds geometry or timing budget");
        goto done;
    }

    options = heif_decoding_options_alloc();
    if (!options) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate HEIF options");
        goto done;
    }
    options->ignore_sequence_editlist = 1;
    gboolean alpha = heif_track_has_alpha_channel(decode_track);
    int count = 0;
    uint64_t elapsed = 0, timestamp = 0;
    // Walking the media timeline once avoids counting edit-list repetitions and
    // reports presentation order even for tracks with reordered frames.
    for (;;) {
        heif_image *next = NULL;
        result = heif_track_decode_next_image(decode_track, &next, heif_colorspace_RGB,
                                               heif_chroma_444, options);
        if (result.code == heif_error_End_of_sequence) break;
        if (result.code != heif_error_Ok) goto failed;
        uint32_t duration = heif_image_get_duration(next);
        if (count == frame) {
            decoded = next;
            timestamp = elapsed;
        } else {
            heif_image_release(next);
        }
        elapsed += duration;
        count++;
        if (known_count > frame && decoded) { count = known_count; break; }
        if (count > FRAME_LIMIT) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "HEIF track exceeds frame budget");
            goto done;
        }
    }
    if (!decoded) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "HEIF frame index is outside the sequence");
        goto done;
    }
    const enum heif_channel channels[] = {heif_channel_R, heif_channel_G, heif_channel_B, heif_channel_Alpha};
    int depth = heif_image_get_bits_per_pixel_range(decoded, heif_channel_R);
    int w = heif_image_get_width(decoded, heif_channel_R), h = heif_image_get_height(decoded, heif_channel_R);
    if (depth < 8 || depth > 16 || w < 1 || h < 1 || (guint64)w * h > PIXEL_LIMIT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Unsupported HEIF frame layout");
        goto done;
    }
    int bands = alpha ? 4 : 3, component = depth > 8 ? 2 : 1;
    size_t row = (size_t)w * bands * component;
    guint8 *packed = g_try_malloc_n(row, h);
    if (!packed) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Could not allocate HEIF plane buffer");
        goto done;
    }
    for (int c = 0; c < bands; c++) {
        int stride = 0;
        int plane_depth = heif_image_get_bits_per_pixel_range(decoded, channels[c]);
        const guint8 *plane = heif_image_get_plane_readonly(decoded, channels[c], &stride);
        int plane_component = plane_depth > 8 ? 2 : 1;
        if (!plane || plane_depth < 8 || plane_depth > 16 || stride < w * plane_component ||
            heif_image_get_width(decoded, channels[c]) != w || heif_image_get_height(decoded, channels[c]) != h) {
            g_free(packed);
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Missing or invalid HEIF colour plane");
            goto done;
        }
        unsigned max = (1u << plane_depth) - 1;
        unsigned output_max = component == 1 ? 255 : 65535;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const guint8 *src = plane + (size_t)y * stride + x * plane_component;
                guint8 *dst = packed + (size_t)y * row + (x * bands + c) * component;
                guint16 value = *src;
                if (plane_component == 2) memcpy(&value, src, 2);
                value = (guint16)((MIN(value, max) * output_max + max / 2) / max);
                if (component == 1) *dst = (guint8)value;
                else memcpy(dst, &value, 2);
            }
        }
    }
    if (alpha && heif_image_is_premultiplied_alpha(decoded)) {
        unsigned max = component == 1 ? 255 : 65535;
        for (guint64 p = 0; p < (guint64)w * h; p++) {
            guint8 *pixel = packed + p * 4 * component;
            guint16 opacity = pixel[3 * component];
            if (component == 2) memcpy(&opacity, pixel + 3 * component, 2);
            for (int c = 0; c < 3; c++) {
                guint8 *sample = pixel + c * component;
                guint16 value = *sample;
                if (component == 2) memcpy(&value, sample, 2);
                value = opacity ? (guint16)MIN(max, (value * max + opacity / 2) / opacity) : 0;
                if (component == 1) *sample = (guint8)value;
                else memcpy(sample, &value, 2);
            }
        }
    }
    image = make_image(packed, w, h, row, depth, bands, error);
    g_free(packed);
    if (!image) goto done;
    size_t size = heif_image_get_raw_color_profile_size(decoded);
    if (size && size <= 4 * 1024 * 1024) {
        profile = g_malloc(size);
        result = heif_image_get_raw_color_profile(decoded, profile);
        if (result.code != heif_error_Ok) { g_clear_object(&image); goto failed; }
    }
    heif_image_get_nclx_color_profile(decoded, &colour);
    if (!set_colour(image, profile, size, colour ? colour->color_primaries : 1,
                     colour ? colour->transfer_characteristics : 13, error)) {
        g_clear_object(&image);
        goto done;
    }
    uint32_t repetitions = heif_track_get_number_of_repetitions(decode_track);
    int loops = repetitions == heif_sequence_track_number_of_repetitions_infinite ? 0 :
        (int)CLAMP(repetitions, 1u, (uint32_t)G_MAXINT);
    set_timing(image, "libheif-sequence", count, loops,
               (double)timestamp / timescale, (double)heif_image_get_duration(decoded) / timescale);
    goto done;

failed:
    g_set_error(error, G_IO_ERROR, result.code == heif_error_Unsupported_feature ?
                G_IO_ERROR_NOT_SUPPORTED : G_IO_ERROR_INVALID_DATA, "HEIF sequence: %s", result.message);
done:
    g_free(profile);
    if (colour) heif_nclx_color_profile_free(colour);
    if (decoded) heif_image_release(decoded);
    if (options) heif_decoding_options_free(options);
    if (decode_track) heif_track_release(decode_track);
    if (context) heif_context_free(context);
    return image;
}
#endif

VipsImage *majoris_codec_sequence(const char *path, int frame, gboolean avif, int known_count, GError **error)
{
#ifdef MAJORIS_AVIF_SEQUENCE
    if (avif) return load_avif(path, frame, error);
#endif
#ifdef MAJORIS_HEIF_SEQUENCE
    return load_heif(path, frame, known_count, error);
#else
    (void)path; (void)frame; (void)avif; (void)known_count; (void)error;
    return NULL;
#endif
}
