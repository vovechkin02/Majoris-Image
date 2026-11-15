#include "image-texture.h"

int main(void)
{
    const float hdr[] = {2.5f, 0.125f, 0.25f, 0.5f};
    g_autoptr(GBytes) bytes = g_bytes_new_static(hdr, sizeof hdr);
    g_autoptr(GdkTexture) texture = majoris_image_texture_new(bytes, 1, 1, sizeof hdr);
    GdkColorState *linear = gdk_color_state_get_srgb_linear();
    g_assert_true(gdk_color_state_equal(gdk_texture_get_color_state(texture), linear));

    GdkTextureDownloader *downloader = gdk_texture_downloader_new(texture);
    gdk_texture_downloader_set_format(downloader, GDK_MEMORY_R32G32B32A32_FLOAT);
    gdk_texture_downloader_set_color_state(downloader, linear);
    float pixel[4] = {0};
    gdk_texture_downloader_download_into(downloader, (guchar *)pixel, sizeof pixel);
    gdk_texture_downloader_free(downloader);
    for (int i = 0; i < 4; i++)
        g_assert_cmpfloat_with_epsilon(pixel[i], hdr[i], 1e-6);
    return 0;
}
