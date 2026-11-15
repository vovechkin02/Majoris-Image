#include "config.h"
#include "bridge.h"
#include <libraw/libraw.h>
#include <vips/vips.h>
#include <lcms2.h>
#include <math.h>
#include <libheif/heif.h>
const char *image_codec_worker_path(void) { return WORKER_PATH; }
void image_codec_tag_srgb(VipsImage *image) {
    cmsHPROFILE profile = cmsCreate_sRGBProfile();
    cmsUInt32Number size = 0;
    if (profile && cmsSaveProfileToMem(profile, NULL, &size)) {
        void *data = g_malloc(size);
        if (cmsSaveProfileToMem(profile, data, &size)) vips_image_set_blob_copy(image, VIPS_META_ICC_NAME, data, size);
        g_free(data);
    }
    if (profile) cmsCloseProfile(profile);
}

/* Called only inside the isolated worker. Store CICP with the native vips
 * intermediate so encoded HDR samples retain their interpretation. */
void image_codec_annotate_heif(VipsImage *image, const char *source) {
    struct heif_context *context = heif_context_alloc();
    if (!context) return;
    struct heif_image_handle *handle = NULL;
    struct heif_color_profile_nclx *profile = NULL;
    struct heif_error error = heif_context_read_from_file(context, source, NULL);
    if (error.code == heif_error_Ok) error = heif_context_get_primary_image_handle(context, &handle);
    if (error.code == heif_error_Ok) {
        int depth=heif_image_handle_get_luma_bits_per_pixel(handle);
        if (depth >= 8 && depth <= 16) {
            vips_image_set_int(image,"majoris-image-bit-depth",depth);
            if (image->BandFmt == VIPS_FORMAT_USHORT) vips_image_set_int(image,"majoris-image-code-max",((1<<depth)-1)<<(16-depth));
        }
    }
    if (error.code == heif_error_Ok) error = heif_image_handle_get_nclx_color_profile(handle, &profile);
    if (error.code == heif_error_Ok && profile &&
            (profile->transfer_characteristics == 16 || profile->transfer_characteristics == 18)) {
        vips_image_set_int(image, "majoris-image-primaries", profile->color_primaries);
        vips_image_set_int(image, "majoris-image-transfer", profile->transfer_characteristics);
    }
    if (profile) heif_nclx_color_profile_free(profile);
    if (handle) heif_image_handle_release(handle);
    heif_context_free(context);
}
gboolean image_codec_develop_raw_options(const char *source, const char *target, int quality, gboolean camera_wb, GError **error) {
    libraw_data_t *raw = libraw_init(0);
    if (!raw) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "RAW allocation failed"); return FALSE; }
    raw->params.output_bps = 16;
    raw->params.use_camera_wb = camera_wb;
    raw->params.output_color = 1;
    raw->params.user_qual = quality == 0 ? 0 : 3;
    raw->params.highlight = 2;
    raw->params.no_auto_bright = 1;
    raw->params.gamm[0] = 1.0;
    raw->params.gamm[1] = 1.0;
    int result = libraw_open_file(raw, source);
    if (!result) result = libraw_unpack(raw);
    if (!result) result = libraw_dcraw_process(raw);
    raw->params.output_tiff = 1;
    if (!result) result = libraw_dcraw_ppm_tiff_writer(raw, target);
    libraw_close(raw);
    if (result) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, libraw_strerror(result));
    return result == 0;
}
gboolean image_codec_develop_raw(const char *source, const char *target, GError **error) {
    return image_codec_develop_raw_options(source, target, 1, TRUE, error);
}
double image_codec_pq_to_linear(double v) {
    const double m1 = 2610.0/16384, m2 = 2523.0/32, c1 = 3424.0/4096, c2 = 2413.0/128, c3 = 2392.0/128;
    double p = pow(CLAMP(v, 0, 1), 1/m2);
    return pow(MAX(p-c1,0)/(c2-c3*p),1/m1)*10000;
}
double image_codec_hlg_to_linear(double v) {
    return v <= 0.5 ? v*v/3 : (exp((v-0.55991073)/0.17883277)+0.28466892)/12;
}
