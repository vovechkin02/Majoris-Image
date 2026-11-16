#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include <glib/gstdio.h>

#include "bridge.h"

static VipsImage *create_fixture(int width, int height, int bands)
{
    g_autoptr(VipsImage) xy = NULL;
    g_autoptr(VipsImage) x = NULL;
    g_autoptr(VipsImage) y = NULL;
    g_autoptr(VipsImage) red = NULL;
    g_autoptr(VipsImage) green = NULL;
    g_autoptr(VipsImage) blue = NULL;
    g_autoptr(VipsImage) alpha = NULL;
    VipsImage *image = NULL;

    if (vips_xyz(&xy, width, height, NULL) || vips_extract_band(xy, &x, 0, NULL) ||
        vips_extract_band(xy, &y, 1, NULL) || vips_linear1(x, &red, 1.0 / 1024, -0.25, NULL) ||
        vips_linear1(y, &green, 1.0 / 2048, 0.5, NULL) ||
        vips_linear1(y, &blue, -1.0 / 4096, 1.5, NULL) ||
        vips_linear1(x, &alpha, 1.0 / 8192, 0.5, NULL)) {
        return NULL;
    }
    VipsImage *channels[] = {red, green, blue, alpha};
    if (vips_bandjoin(channels, &image, bands, NULL)) {
        return NULL;
    }
    image->Type = VIPS_INTERPRETATION_scRGB;
    vips_image_set_int(image, "majoris-image-working-linear", 1);
    return image;
}

static int run_workload(const char *mode, int width, int height, int bands, int depth,
                        gboolean lossless, gboolean lazy, gboolean warm, const char *path)
{
    g_autoptr(GError) error = NULL;
    gint64 began = g_get_monotonic_time();
    g_autoptr(VipsImage) image = create_fixture(width, height, bands);
    if (image && !lazy) {
        VipsImage *resident = vips_image_copy_memory(image);
        g_object_unref(image);
        image = resident;
    }
    if (!image) {
        g_printerr("Fixture creation failed: %s", vips_error_buffer());
        return 1;
    }
    gint64 fixture_us = g_get_monotonic_time() - began;
    for (int run = 0; run <= warm; run++) {
        size_t vips_bytes_before = vips_tracked_get_mem();
        clock_t cpu_start = clock();
        began = g_get_monotonic_time();
        gboolean ok = strcmp(mode, "mif1") == 0
                          ? image_codec_write_pixels(image, path, &error)
                          : image_codec_jxl_encode(image, path, depth, 90, lossless, &error);
        gint64 operation_us = g_get_monotonic_time() - began;
        clock_t cpu_ticks = clock() - cpu_start;
        if (!ok) {
            g_printerr("%s\n", error->message);
            return 1;
        }
        if (run == warm) {
            struct rusage usage;
            GStatBuf output;
            if (getrusage(RUSAGE_SELF, &usage) || g_stat(path, &output)) {
                return 1;
            }
            g_print("{\"fixture_us\":%" G_GINT64_FORMAT ",\"operation_us\":%" G_GINT64_FORMAT
                    ",\"operation_cpu_ticks\":%ld,\"cpu_ticks_per_second\":%ld,"
                    "\"peak_rss_kib\":%ld,\"output_bytes\":%" G_GINT64_FORMAT
                    ",\"vips_bytes_before\":%zu,\"vips_highwater_bytes\":%zu}\n",
                    fixture_us, operation_us, (long)cpu_ticks, (long)CLOCKS_PER_SEC,
                    usage.ru_maxrss, (gint64)output.st_size, vips_bytes_before,
                    vips_tracked_get_mem_highwater());
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 10 || VIPS_INIT(argv[0])) {
        return 2;
    }
    int result = run_workload(argv[1], atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                              atoi(argv[6]), atoi(argv[7]), atoi(argv[8]), argv[9]);
    vips_shutdown();
    return result;
}
