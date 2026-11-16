#include "image-texture.h"

static void assert_float_rgba_and_color_state(GdkTexture *texture, GdkColorState *state,
                                              const float expected[4])
{
    g_assert_nonnull(texture);
    g_assert_true(gdk_color_state_equal(gdk_texture_get_color_state(texture), state));

    GdkTextureDownloader *downloader = gdk_texture_downloader_new(texture);
    gdk_texture_downloader_set_format(downloader, GDK_MEMORY_R32G32B32A32_FLOAT);
    gdk_texture_downloader_set_color_state(downloader, state);
    float pixel[4] = {0};
    gdk_texture_downloader_download_into(downloader, (guchar *)pixel, sizeof pixel);
    gdk_texture_downloader_free(downloader);
    for (int i = 0; i < 4; i++) {
        g_assert_cmpfloat_with_epsilon(pixel[i], expected[i], 1e-6);
    }
}

static void test_linear_srgb_float_texture(void)
{
    const float hdr[] = {2.5f, 0.125f, 0.25f, 0.5f};
    g_autoptr(GBytes) bytes = g_bytes_new_static(hdr, sizeof hdr);
    g_autoptr(GdkTexture) texture = image_texture_new(bytes, 1, 1, sizeof hdr);
    assert_float_rgba_and_color_state(texture, gdk_color_state_get_srgb_linear(), hdr);
}

static void test_pq_float_texture(void)
{
    /* PQ samples are prepared independently, not retagged linear values. */
    const float pq_samples[] = {0.508078f, 0.392871f, 0.299699f, 0.5f};
    g_autoptr(GBytes) pq_bytes = g_bytes_new_static(pq_samples, sizeof pq_samples);
    GdkColorState *pq_state = gdk_color_state_get_rec2100_pq();
    g_autoptr(GdkTexture) pq =
        image_texture_new_float_rgba(pq_bytes, 1, 1, sizeof pq_samples, pq_state);
    assert_float_rgba_and_color_state(pq, pq_state, pq_samples);
    gboolean hdr_texture;
    gboolean sdr_texture;
    g_assert_nonnull(
        g_strstr_len(image_texture_external_description(pq, &sdr_texture, &hdr_texture), -1, "PQ"));
    g_assert_true(hdr_texture);
    g_assert_false(sdr_texture);
}

static void test_linear_rec2100_float_texture(void)
{
    gboolean hdr_texture;
    gboolean sdr_texture;
    const float linear_2020[] = {1.5f, 0.75f, 0.35f, 1.0f};
    g_autoptr(GBytes) linear_bytes = g_bytes_new_static(linear_2020, sizeof linear_2020);
    GdkColorState *linear_state = gdk_color_state_get_rec2100_linear();
    g_autoptr(GdkTexture) linear =
        image_texture_new_float_rgba(linear_bytes, 1, 1, sizeof linear_2020, linear_state);
    assert_float_rgba_and_color_state(linear, linear_state, linear_2020);
    g_assert_nonnull(g_strstr_len(
        image_texture_external_description(linear, &sdr_texture, &hdr_texture), -1, "linear Rec."));
    g_assert_true(hdr_texture);
    g_assert_false(sdr_texture);
}

static void test_p3_float_texture(void)
{
    gboolean hdr_texture;
    gboolean sdr_texture;
    g_autoptr(GdkCicpParams) params = gdk_cicp_params_new();
    gdk_cicp_params_set_color_primaries(params, 12);
    gdk_cicp_params_set_transfer_function(params, 13);
    gdk_cicp_params_set_matrix_coefficients(params, 0);
    gdk_cicp_params_set_range(params, GDK_CICP_RANGE_FULL);
    g_autoptr(GError) error = NULL;
    GdkColorState *p3_state = gdk_cicp_params_build_color_state(params, &error);
    g_assert_no_error(error);
    const float p3_samples[] = {0.25f, 0.5f, 0.75f, 1.0f};
    g_autoptr(GBytes) p3_bytes = g_bytes_new_static(p3_samples, sizeof p3_samples);
    g_autoptr(GdkTexture) p3 =
        image_texture_new_float_rgba(p3_bytes, 1, 1, sizeof p3_samples, p3_state);
    assert_float_rgba_and_color_state(p3, p3_state, p3_samples);
    g_assert_nonnull(g_strstr_len(
        image_texture_external_description(p3, &sdr_texture, &hdr_texture), -1, "another"));
    g_assert_false(hdr_texture);
    g_assert_false(sdr_texture);
    gdk_color_state_unref(p3_state);
}

static void test_invalid_float_texture_layout(void)
{
    const float hdr[] = {2.5f, 0.125f, 0.25f, 0.5f};
    g_autoptr(GBytes) bytes = g_bytes_new_static(hdr, sizeof hdr);
    GdkColorState *pq_state = gdk_color_state_get_rec2100_pq();
    g_assert_null(image_texture_new_float_rgba(bytes, 2, 1, sizeof hdr, pq_state));
    g_assert_null(image_texture_new_float_rgba(bytes, 1, 2, sizeof hdr, pq_state));
    g_assert_null(image_texture_new_float_rgba(bytes, 1, 1, sizeof hdr, NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/texture/float-rgba/linear-srgb", test_linear_srgb_float_texture);
    g_test_add_func("/texture/float-rgba/rec2100-pq", test_pq_float_texture);
    g_test_add_func("/texture/float-rgba/rec2100-linear", test_linear_rec2100_float_texture);
    g_test_add_func("/texture/float-rgba/display-p3", test_p3_float_texture);
    g_test_add_func("/texture/float-rgba/invalid-layout", test_invalid_float_texture_layout);
    return g_test_run();
}
