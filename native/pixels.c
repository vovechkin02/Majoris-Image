#include <math.h>
#include <stdio.h>
#include <string.h>

#include <glib/gstdio.h>

#include "bridge.h"

gboolean image_codec_write_pixels(VipsImage *image, const char *path, GError **error)
{
    int width = vips_image_get_width(image), height = vips_image_get_height(image);
    int bands = vips_image_get_bands(image);
    FILE *file = NULL;
    float *decoded = NULL;
    float *row = NULL;
    gboolean ok = FALSE;

    if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
        (guint64)width * height > 16777216 || (bands != 3 && bands != 4) ||
        vips_image_get_format(image) != VIPS_FORMAT_FLOAT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Invalid linear image geometry or layout");
        return FALSE;
    }
    file = g_fopen(path, "wb");
    if (!file) {
        goto done;
    }
    guint32 dimensions[] = {GUINT32_TO_BE(width), GUINT32_TO_BE(height)};
    if (fwrite("MIF1", 1, 4, file) != 4 || fwrite(dimensions, 4, 2, file) != 2) {
        goto done;
    }
    size_t size = 0;
    decoded = vips_image_write_to_memory(image, &size);
    if (!decoded || size != (size_t)width * height * bands * sizeof(float)) {
        goto done;
    }
    row = g_new(float, width * 4);
    for (int y = 0; y < height; y++) {
        const float *in = decoded + (size_t)y * width * bands;
        for (int x = 0; x < width; x++, in += bands) {
            for (int c = 0; c < 3; c++) {
                row[4 * x + c] = isfinite(in[c]) ? in[c] : 0;
            }
            row[4 * x + 3] = bands == 4 && isfinite(in[3]) ? CLAMP(in[3], 0, 1) : 1;
        }
        if (fwrite(row, 4 * sizeof(float), width, file) != (size_t)width) {
            goto done;
        }
    }
    ok = TRUE;
done:
    if (file && fclose(file)) {
        ok = FALSE;
    }
    g_free(row);
    g_free(decoded);
    if (!ok) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Cannot write decoded pixels: %s",
                    vips_error_buffer());
        vips_error_clear();
    }
    return ok;
}

VipsImage *image_codec_read_pixels(const char *path, GError **error)
{
    GMappedFile *mapping = g_mapped_file_new(path, FALSE, error);
    if (!mapping) {
        return NULL;
    }
    gsize length = g_mapped_file_get_length(mapping);
    const char *data = g_mapped_file_get_contents(mapping);
    guint32 dimensions[2] = {0};
    if (length >= 12 && memcmp(data, "MIF1", 4) == 0) {
        memcpy(dimensions, data + 4, 8);
    }
    guint32 width = GUINT32_FROM_BE(dimensions[0]), height = GUINT32_FROM_BE(dimensions[1]);
    if (!width || !height || width > 4096 || height > 4096 ||
        length != 12 + (guint64)width * height * 16) {
        g_mapped_file_unref(mapping);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Invalid linear RGBA input buffer");
        return NULL;
    }
    VipsImage *image =
        vips_image_new_from_memory(data + 12, length - 12, width, height, 4, VIPS_FORMAT_FLOAT);
    if (!image) {
        g_mapped_file_unref(mapping);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, vips_error_buffer());
        return NULL;
    }
    g_object_set_data_full(G_OBJECT(image), "frame-mapping", mapping,
                           (GDestroyNotify)g_mapped_file_unref);
    image->Type = VIPS_INTERPRETATION_scRGB;
    vips_image_set_int(image, "majoris-image-working-linear", 1);
    return image;
}
