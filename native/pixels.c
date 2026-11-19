#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <glib/gstdio.h>

#include "bridge.h"

/** Owns one output row buffer and error state for bounded MIF1 writes. */
typedef struct {
    FILE *file;
    float *row;
    gsize row_bytes;
    int bands;
    int write_errno;
} PixelOutput;

/** Converts a failed filesystem pixel write into the expected GIO error. */
static void set_pixel_file_error(GError **error, const char *operation, int saved_errno)
{
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Cannot %s decoded pixels: %s",
                operation, g_strerror(saved_errno));
}

/** Writes a bounded libvips pixel strip without retaining an entire decoded image. */
static int write_pixel_strip(VipsRegion *region, VipsRect *area, void *data)
{
    PixelOutput *output = data;
    float *row = output->row;
    int bands = output->bands;
    for (int y = area->top; y < VIPS_RECT_BOTTOM(area); y++) {
        const float *in = (const float *)VIPS_REGION_ADDR(region, area->left, y);
        for (int x = 0; x < area->width; x++, in += bands) {
            for (int c = 0; c < 3; c++) {
                row[4 * x + c] = isfinite(in[c]) ? in[c] : 0;
            }
            row[4 * x + 3] = bands == 4 && isfinite(in[3]) ? CLAMP(in[3], 0, 1) : 1;
        }
        if (fwrite(row, 1, output->row_bytes, output->file) != output->row_bytes) {
            output->write_errno = errno;
            return -1;
        }
    }
    return 0;
}

/** Writes a validated little-endian pixel transport buffer for the isolated worker. */
gboolean image_codec_write_pixels(VipsImage *image, const char *path, GError **error)
{
    int width = vips_image_get_width(image);
    int height = vips_image_get_height(image);
    int bands = vips_image_get_bands(image);
    gboolean ok = FALSE;

    if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
        (guint64)width * height > 16777216 || (bands != 3 && bands != 4) ||
        vips_image_get_format(image) != VIPS_FORMAT_FLOAT) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Invalid linear image geometry or layout");
        return FALSE;
    }

    // The width limit bounds the converted row to 256 KiB.
    gsize row_bytes = (gsize)width * 4 * sizeof(float);
    g_autofree float *row = g_try_malloc(row_bytes);
    if (!row) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Cannot allocate decoded pixel row");
        return FALSE;
    }
    FILE *file = g_fopen(path, "wb");
    if (!file) {
        set_pixel_file_error(error, "open output for", errno);
        return FALSE;
    }
    guint32 dimensions[] = {GUINT32_TO_BE(width), GUINT32_TO_BE(height)};
    PixelOutput output = {.file = file, .row = row, .row_bytes = row_bytes, .bands = bands};
    if (fwrite("MIF1", 1, 4, file) != 4 || fwrite(dimensions, sizeof dimensions, 1, file) != 1) {
        set_pixel_file_error(error, "write header for", errno);
        goto done;
    }
    // The sink evaluates on libvips workers and delivers full-width strips in order,
    // one callback at a time. The region belongs to libvips for the duration of the call.
    if (vips_sink_disc(image, write_pixel_strip, &output)) {
        if (output.write_errno) {
            set_pixel_file_error(error, "write", output.write_errno);
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Cannot prepare decoded pixel rows: %s", vips_error_buffer());
            vips_error_clear();
        }
        goto done;
    }
    ok = TRUE;
done:
    if (fclose(file) != 0 && ok) {
        set_pixel_file_error(error, "close output for", errno);
        ok = FALSE;
    }
    return ok;
}

/** Maps and validates pixel transport bytes before constructing an image. */
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
