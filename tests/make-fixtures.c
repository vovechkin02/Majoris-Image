#include <vips/vips.h>

int main(int argc, char **argv)
{
    if (argc != 3 || VIPS_INIT(argv[0])) return 1;
    VipsImage *black = NULL, *mid = NULL, *tiff = NULL, *raster = NULL;
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
    rc = 0;
done:
    if (rc) g_printerr("%s\n", vips_error_buffer());
    g_clear_object(&raster);
    g_clear_object(&tiff);
    g_clear_object(&mid);
    g_clear_object(&black);
    vips_shutdown();
    return rc;
}
