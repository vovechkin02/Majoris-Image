#include <math.h>

#include <lcms2.h>

#include "bridge.h"

typedef struct {
    int transfer, primaries;
    double divisor, alpha, luminance_scale;
    cmsHTRANSFORM transform;
} LinearContext;
static void free_context(void *data)
{
    LinearContext *ctx = data;
    if (ctx->transform) {
        cmsDeleteTransform(ctx->transform);
    }
    g_free(ctx);
}
static cmsHTRANSFORM float_profile_transform(VipsImage *image)
{
    const void *data;
    size_t size;
    if (vips_image_get_blob(image, VIPS_META_ICC_NAME, &data, &size)) {
        return NULL;
    }
    cmsHPROFILE source = cmsOpenProfileFromMem(data, size);
    if (!source) {
        return NULL;
    }
    cmsCIExyY white = {.3127, .3290, 1};
    cmsCIExyYTRIPLE primaries = {{.64, .33, 1}, {.30, .60, 1}, {.15, .06, 1}};
    cmsToneCurve *curve = cmsBuildGamma(NULL, 1), *curves[] = {curve, curve, curve};
    cmsHPROFILE destination = cmsCreateRGBProfile(&white, &primaries, curves);
    cmsFreeToneCurve(curve);
    cmsUInt32Number format = image->Bands == 4 ? TYPE_RGBA_FLT : TYPE_RGB_FLT;
    cmsHTRANSFORM transform =
        destination
            ? cmsCreateTransform(source, format, destination, format, INTENT_RELATIVE_COLORIMETRIC,
                                 cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE | cmsFLAGS_COPY_ALPHA)
            : NULL;
    cmsCloseProfile(source);
    if (destination) {
        cmsCloseProfile(destination);
    }
    return transform;
}
static int linear_generate(VipsRegion *out, void *sequence, void *a, void *b, gboolean *stop)
{
    (void)a;
    (void)stop;
    VipsRegion *in = sequence;
    LinearContext *ctx = b;
    VipsRect area = out->valid;
    if (vips_region_prepare(in, &area)) {
        return -1;
    }
    int bands = out->im->Bands;
    for (int y = area.top; y < VIPS_RECT_BOTTOM(&area); y++) {
        const float *src = (const float *)VIPS_REGION_ADDR(in, area.left, y);
        float *dst = (float *)VIPS_REGION_ADDR(out, area.left, y);
        if (ctx->transform) {
            cmsDoTransform(ctx->transform, src, dst, area.width);
            if (ctx->luminance_scale != 1) {
                for (int x = 0; x < area.width; x++) {
                    for (int c = 0; c < 3; c++) {
                        dst[x * bands + c] *= ctx->luminance_scale;
                    }
                }
            }
            continue;
        }
        for (int x = 0; x < area.width; x++, src += bands, dst += bands) {
            double rgb[3];
            for (int c = 0; c < 3; c++) {
                double value = src[c] / ctx->divisor;
                rgb[c] = ctx->transfer == 16   ? image_codec_pq_to_linear(value) / 203.0
                         : ctx->transfer == 18 ? image_codec_hlg_to_linear(CLAMP(value, 0, 1))
                         : ctx->transfer == 13
                             ? (value <= .04045 ? value / 12.92 : pow((value + .055) / 1.055, 2.4))
                         : ctx->transfer == 1 || ctx->transfer == 6 || ctx->transfer == 14 ||
                                 ctx->transfer == 15
                             ? (value < .081 ? value / 4.5 : pow((value + .099) / 1.099, 1.0 / .45))
                             : value;
            }
            if (ctx->transfer == 18) {
                double luminance = MAX(0, .2627 * rgb[0] + .6780 * rgb[1] + .0593 * rgb[2]);
                double ootf = pow(luminance, .2) * 1000.0 / 203.0;
                for (int c = 0; c < 3; c++) {
                    rgb[c] *= ootf;
                }
            }
            if (ctx->primaries == 9) {
                dst[0] = 1.660491 * rgb[0] - .587641 * rgb[1] - .072850 * rgb[2];
                dst[1] = -.124550 * rgb[0] + 1.132900 * rgb[1] - .008349 * rgb[2];
                dst[2] = -.018151 * rgb[0] - .100579 * rgb[1] + 1.118730 * rgb[2];
            } else if (ctx->primaries == 12) {
                dst[0] = 1.224745 * rgb[0] - .224904 * rgb[1];
                dst[1] = -.042058 * rgb[0] + 1.042081 * rgb[1];
                dst[2] = -.019642 * rgb[0] - .078655 * rgb[1] + 1.098537 * rgb[2];
            } else {
                for (int c = 0; c < 3; c++) {
                    dst[c] = rgb[c];
                }
            }
            for (int c = 0; c < 3; c++) {
                dst[c] *= ctx->luminance_scale;
            }
            if (bands == 4) {
                dst[3] = src[3] / ctx->alpha;
            }
        }
    }
    return 0;
}
VipsImage *image_codec_to_linear(VipsImage *image, GError **error)
{
    if (vips_image_get_typeof(image, "majoris-image-working-linear")) {
        return g_object_ref(image);
    }
    VipsImage *rgb = NULL, *floating = NULL, *out = NULL;
    cmsHTRANSFORM transform = NULL;
    int transfer = 0, primaries = 1;
    gboolean encoded = vips_image_get_typeof(image, "majoris-image-transfer") != 0;
    double max = image->BandFmt == VIPS_FORMAT_USHORT  ? 65535.0
                 : image->BandFmt == VIPS_FORMAT_UCHAR ? 255.0
                                                       : 1.0;
    if (encoded) {
        if (vips_image_get_int(image, "majoris-image-transfer", &transfer) ||
            vips_image_get_int(image, "majoris-image-primaries", &primaries)) {
            goto fail;
        }
        rgb = g_object_ref(image);
    } else if (vips_image_get_typeof(image, VIPS_META_ICC_NAME) &&
               (image->BandFmt == VIPS_FORMAT_FLOAT || image->BandFmt == VIPS_FORMAT_DOUBLE) &&
               (image->Bands == 3 || image->Bands == 4)) {
        transform = float_profile_transform(image);
        if (!transform) {
            vips_error("ImageKit", "%s", "Invalid floating-point RGB profile");
            goto fail;
        }
        rgb = g_object_ref(image);
    } else {
        g_autoptr(VipsImage) color = NULL;
        g_autoptr(VipsImage) alpha = NULL;
        g_autoptr(VipsImage) pcs = NULL;
        g_autoptr(VipsImage) linear = NULL;
        if (vips_image_hasalpha(image) &&
            (vips_extract_band(image, &color, 0, "n", image->Bands - 1, NULL) ||
             vips_extract_band(image, &alpha, image->Bands - 1, NULL))) {
            goto fail;
        }
        VipsImage *input = color ? color : image;
        if (vips_image_get_typeof(input, VIPS_META_ICC_NAME)) {
            if (vips_icc_import(input, &pcs, "embedded", TRUE, "pcs", VIPS_PCS_XYZ,
                                "black_point_compensation", TRUE, NULL)) {
                goto fail;
            }
            input = pcs;
        }
        if (vips_colourspace(input, &linear, VIPS_INTERPRETATION_scRGB, NULL)) {
            goto fail;
        }
        // The color conversion can normalize or clamp extra bands. Preserve
        // the original straight alpha so the generator normalizes it once.
        if (alpha) {
            if (vips_bandjoin2(linear, alpha, &rgb, NULL)) {
                goto fail;
            }
        } else {
            rgb = g_steal_pointer(&linear);
        }
    }
    if (vips_cast(rgb, &floating, VIPS_FORMAT_FLOAT, NULL)) {
        goto fail;
    }
    if (floating->Bands != 3 && floating->Bands != 4) {
        vips_error("ImageKit", "%s", "Expected RGB or RGBA color data");
        goto fail;
    }
    out = vips_image_new();
    if (vips_image_pipelinev(out, VIPS_DEMAND_STYLE_THINSTRIP, floating, NULL)) {
        goto fail;
    }
    out->BandFmt = VIPS_FORMAT_FLOAT;
    out->Type = VIPS_INTERPRETATION_scRGB;
    LinearContext *ctx = g_new0(LinearContext, 1);
    ctx->transfer = transfer;
    ctx->primaries = primaries;
    ctx->divisor = encoded ? max : 1.0;
    ctx->alpha = max;
    ctx->luminance_scale = 1;
    double white_nits = 203;
    if (vips_image_get_typeof(image, "majoris-image-linear-white-nits") &&
        !vips_image_get_double(image, "majoris-image-linear-white-nits", &white_nits)) {
        ctx->luminance_scale = white_nits / 203.0;
    }
    int code_max = 0;
    if (encoded && vips_image_get_typeof(image, "majoris-image-code-max") &&
        !vips_image_get_int(image, "majoris-image-code-max", &code_max) && code_max > 0) {
        ctx->divisor = code_max;
    }
    ctx->transform = transform;
    transform = NULL;
    g_object_set_data_full(G_OBJECT(out), "majoris-image-linear-context", ctx, free_context);
    // The generator's input must outlive every lazy region request, including
    // when the libvips operation cache is disabled or evicts the cast operation.
    g_object_set_data_full(G_OBJECT(out), "majoris-image-linear-input", g_object_ref(floating),
                           g_object_unref);
    if (vips_image_get_typeof(image, VIPS_META_ICC_NAME)) {
        const void *profile;
        size_t size;
        if (!vips_image_get_blob(image, VIPS_META_ICC_NAME, &profile, &size)) {
            vips_image_set_blob_copy(out, "majoris-image-source-icc", profile, size);
        }
    }
    vips_image_remove(out, VIPS_META_ICC_NAME);
    vips_image_remove(out, "majoris-image-transfer");
    vips_image_remove(out, "majoris-image-primaries");
    vips_image_set_int(out, "majoris-image-working-linear", 1);
    if (encoded) {
        vips_image_set_int(out, "majoris-image-source-transfer", transfer);
        vips_image_set_int(out, "majoris-image-source-primaries", primaries);
    }
    if (vips_image_generate(out, vips_start_one, linear_generate, vips_stop_one, floating, ctx)) {
        goto fail;
    }
    g_object_unref(rgb);
    g_object_unref(floating);
    return out;
fail:
    if (transform) {
        cmsDeleteTransform(transform);
    }
    g_clear_object(&rgb);
    g_clear_object(&floating);
    g_clear_object(&out);
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, vips_error_buffer());
    vips_error_clear();
    return NULL;
}
