#include "bridge.h"
#include <lcms2.h>
#include <unistd.h>
#include <libheif/heif.h>

void image_codec_tag_linear(VipsImage *image) {
    cmsCIExyY white = { .3127, .3290, 1 };
    cmsCIExyYTRIPLE primaries = {{.64,.33,1},{.30,.60,1},{.15,.06,1}};
    cmsToneCurve *curve = cmsBuildGamma(NULL, 1.0), *curves[3] = {curve,curve,curve};
    cmsHPROFILE profile = cmsCreateRGBProfile(&white, &primaries, curves);
    cmsFreeToneCurve(curve);
    if (!profile) return;
    cmsUInt32Number size = 0;
    if (cmsSaveProfileToMem(profile, NULL, &size)) {
        void *data = g_malloc(size);
        if (cmsSaveProfileToMem(profile, data, &size)) vips_image_set_blob_copy(image, VIPS_META_ICC_NAME, data, size);
        g_free(data);
    }
    cmsCloseProfile(profile);
}

static cmsHPROFILE make_profile(const char *name) {
    if (!strcmp(name, "srgb") || !strcmp(name, "source")) return cmsCreate_sRGBProfile();
    cmsCIExyY white = { .3127, .3290, 1.0 };
    cmsCIExyYTRIPLE primaries;
    cmsToneCurve *curve;
    if (!strcmp(name, "display-p3")) {
        primaries = (cmsCIExyYTRIPLE) {{.68,.32,1}, {.265,.69,1}, {.15,.06,1}};
        cmsHPROFILE srgb = cmsCreate_sRGBProfile(); curve = cmsDupToneCurve(cmsReadTag(srgb, cmsSigRedTRCTag)); cmsCloseProfile(srgb);
    } else if (!strcmp(name, "adobe-rgb")) {
        primaries = (cmsCIExyYTRIPLE) {{.64,.33,1}, {.21,.71,1}, {.15,.06,1}}; curve = cmsBuildGamma(NULL, 2.19921875);
    } else if (!strcmp(name, "prophoto")) {
        white = *cmsD50_xyY(); primaries = (cmsCIExyYTRIPLE) {{.7347,.2653,1}, {.1596,.8404,1}, {.0366,.0001,1}}; curve = cmsBuildGamma(NULL, 1.8);
    } else return NULL;
    cmsToneCurve *curves[3] = {curve,curve,curve}; cmsHPROFILE result = cmsCreateRGBProfile(&white, &primaries, curves); cmsFreeToneCurve(curve);
    if (result) { cmsMLU *label = cmsMLUalloc(NULL, 1); cmsMLUsetASCII(label,"en","US",name); cmsWriteTag(result,cmsSigProfileDescriptionTag,label); cmsMLUfree(label); }
    return result;
}
static VipsImage *export_profile(VipsImage *linear, const char *path, int depth)
{
    g_autoptr(VipsImage) color = NULL;
    g_autoptr(VipsImage) pcs = NULL;
    g_autoptr(VipsImage) encoded = NULL;
    gboolean has_alpha = vips_image_hasalpha(linear);
    if (has_alpha && vips_extract_band(linear, &color, 0, "n", linear->Bands - 1, NULL))
        return NULL;
    if (vips_colourspace(color ? color : linear, &pcs, VIPS_INTERPRETATION_XYZ, NULL) ||
        vips_icc_export(pcs, &encoded, "output_profile", path, "depth", depth,
                        "pcs", VIPS_PCS_XYZ, "black_point_compensation", TRUE, NULL))
        return NULL;
    if (!has_alpha) return g_steal_pointer(&encoded);

    // Keep straight alpha out of the ICC transform and quantize it in the
    // destination sample range. The input alpha is normalized float data.
    g_autoptr(VipsImage) alpha = NULL;
    g_autoptr(VipsImage) scaled = NULL;
    g_autoptr(VipsImage) quantized = NULL;
    VipsImage *output = NULL;
    if (vips_extract_band(linear, &alpha, linear->Bands - 1, NULL) ||
        vips_linear1(alpha, &scaled, depth == 16 ? 65535.0 : 255.0, 0.5, NULL) ||
        vips_cast(scaled, &quantized, encoded->BandFmt, NULL) ||
        vips_bandjoin2(encoded, quantized, &output, NULL))
        return NULL;
    return output;
}

VipsImage *image_codec_convert_profile(VipsImage *linear, const char *profile_name, int depth, GError **error) {
    const void *source_profile = NULL; size_t source_size = 0;
    cmsHPROFILE profile = NULL; void *owned = NULL; VipsImage *output = NULL;
    if (!strcmp(profile_name, "source") && vips_image_get_typeof(linear, "majoris-image-source-icc")) {
        if (vips_image_get_blob(linear, "majoris-image-source-icc", &source_profile, &source_size)) goto fail;
        profile = cmsOpenProfileFromMem(source_profile, source_size);
    } else profile = make_profile(profile_name);
    if (!profile) { vips_error("ImageKit", "%s", "Cannot open the requested color profile"); goto fail; }
    cmsUInt32Number size = 0; cmsSaveProfileToMem(profile, NULL, &size); owned = g_malloc(size);
    if (!cmsSaveProfileToMem(profile, owned, &size)) goto fail;
    char *path = NULL; int fd = g_file_open_tmp("majoris-image-profile-XXXXXX", &path, error);
    if (fd < 0) goto fail;
    close(fd);
    gboolean written = g_file_set_contents(path, owned, size, error);
    if (written) output = export_profile(linear, path, depth);
    unlink(path); g_free(path); g_free(owned); cmsCloseProfile(profile);
    if (output) return output;
    if (!error || !*error) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,vips_error_buffer());
    vips_error_clear(); return NULL;
fail:
    if (profile) cmsCloseProfile(profile);
    g_free(owned);
    if (!error || !*error) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,vips_error_buffer());
    vips_error_clear(); return NULL;
}
gboolean image_codec_encoder_available(const char *extension) {
    if (!strcmp(extension,"exr") || !strcmp(extension,"uhdr")) return TRUE;
    if (!strcmp(extension,"avif") && !heif_have_encoder_for_format(heif_compression_AV1)) return FALSE;
    if ((!strcmp(extension,"heif") || !strcmp(extension,"heic")) && !heif_have_encoder_for_format(heif_compression_HEVC)) return FALSE;
    char *filename = g_strconcat("output.",extension,NULL); const char *operation = vips_foreign_find_save(filename); g_free(filename);
    gboolean result = operation != NULL; vips_error_clear(); return result;
}
