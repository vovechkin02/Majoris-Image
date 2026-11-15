#include "bridge.h"

int main(int argc, char **argv)
{
    if (argc != 7 || VIPS_INIT(argv[0])) return 1;
    VipsImage *black = NULL, *mid = NULL, *tiff = NULL, *raster = NULL;
    VipsImage *working = NULL, *profiled = NULL;
    g_autoptr(GError) error = NULL;
    int rc = 1;
    if (vips_black(&black, 20000, 10000, "bands", 3, NULL) ||
        vips_linear1(black, &mid, 0, 32768, NULL) ||
        vips_cast(mid, &tiff, VIPS_FORMAT_USHORT, NULL) ||
        vips_tiffsave(tiff, argv[1], "tile", TRUE, "compression", VIPS_FOREIGN_TIFF_COMPRESSION_DEFLATE, NULL))
        goto done;

    guint8 pixels[8 * 24 * 3];
    for (int i = 0; i < 8 * 24; i++) {
        pixels[i * 3] = 255 * (i / 64 == 0);
        pixels[i * 3 + 1] = 255 * (i / 64 == 1);
        pixels[i * 3 + 2] = 255 * (i / 64 == 2);
    }
    raster = vips_image_new_from_memory(pixels, sizeof pixels, 8, 24, 3, VIPS_FORMAT_UCHAR);
    int delays[] = {100, 100, 100};
    vips_image_set_int(raster, "page-height", 8);
    vips_image_set_int(raster, "loop", 1);
    vips_image_set_array_int(raster, "delay", delays, 3);
    if (vips_gifsave(raster, argv[2], NULL)) goto done;
    vips_image_set_int(raster, "loop", 0);
    int alternate[] = {40, 170, 250};
    vips_image_set_array_int(raster, "delay", alternate, 3);
    if (vips_webpsave(raster, argv[3], "lossless", TRUE, NULL)) goto done;
    if (vips_type_find("VipsOperation", "jp2ksave")) {
        VipsImage *first = NULL;
        if (vips_crop(raster, &first, 0, 0, 8, 8, NULL)) goto done;
        int result = vips_jp2ksave(first, argv[4], "lossless", TRUE, NULL);
        g_object_unref(first);
        if (result) goto done;
    } else if (!g_file_set_contents(argv[4], "", 0, NULL)) goto done;
    const float linear[] = {.18f, .32f, .12f, .5f, .4f, .2f};
    working = vips_image_new_from_memory_copy(linear, sizeof linear, 2, 1, 3, VIPS_FORMAT_FLOAT);
    working->Type = VIPS_INTERPRETATION_scRGB;
    profiled = majoris_codec_convert_profile(working, "display-p3", 16, &error);
    if (!profiled) {
        g_printerr("%s\n", error->message);
        goto done;
    }
    if (vips_pngsave(profiled, argv[5], "bitdepth", 16, NULL) ||
        vips_tiffsave(profiled, argv[6], NULL)) goto done;
    rc = 0;
done:
    if (rc) g_printerr("%s\n", vips_error_buffer());
    g_clear_object(&profiled);
    g_clear_object(&working);
    g_clear_object(&raster);
    g_clear_object(&tiff);
    g_clear_object(&mid);
    g_clear_object(&black);
    vips_shutdown();
    return rc;
}
