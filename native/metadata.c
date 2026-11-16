#include <json-glib/json-glib.h>
#include <lcms2.h>

#include "bridge.h"

char *image_codec_image_details(VipsImage *image)
{
    JsonObject *info = json_object_new();
    const void *profile = NULL;
    size_t size = 0;
    const char *field = vips_image_get_typeof(image, "majoris-image-source-icc")
                            ? "majoris-image-source-icc"
                            : VIPS_META_ICC_NAME;
    if (vips_image_get_typeof(image, field) &&
        !vips_image_get_blob(image, field, &profile, &size)) {
        cmsHPROFILE icc = cmsOpenProfileFromMem(profile, size);
        if (icc) {
            cmsUInt32Number length =
                cmsGetProfileInfoASCII(icc, cmsInfoDescription, "en", "US", NULL, 0);
            char *description = g_malloc0(MAX(1, length));
            cmsGetProfileInfoASCII(icc, cmsInfoDescription, "en", "US", description, length);
            json_object_set_string_member(info, "profile", description);
            g_free(description);
            char *version = g_strdup_printf("%.1f", cmsGetProfileVersion(icc));
            json_object_set_string_member(info, "icc-version", version);
            g_free(version);
            json_object_set_string_member(info, "colour-space",
                                          cmsGetColorSpace(icc) == cmsSigRgbData    ? "RGB"
                                          : cmsGetColorSpace(icc) == cmsSigCmykData ? "CMYK"
                                          : cmsGetColorSpace(icc) == cmsSigGrayData
                                              ? "Gray"
                                              : "ICC-defined");
            cmsCloseProfile(icc);
        } else {
            json_object_set_string_member(info, "profile-error", "Invalid ICC profile");
        }
    }
    int transfer = 0;
    int primaries = 0;
    if (vips_image_get_typeof(image, "majoris-image-transfer")) {
        vips_image_get_int(image, "majoris-image-transfer", &transfer);
    }
    if (vips_image_get_typeof(image, "majoris-image-primaries")) {
        vips_image_get_int(image, "majoris-image-primaries", &primaries);
    }
    json_object_set_string_member(
        info, "transfer",
        transfer == 16                                              ? "PQ / ST 2084"
        : transfer == 18                                            ? "HLG"
        : transfer == 8 || image->Type == VIPS_INTERPRETATION_scRGB ? "Linear"
        : vips_image_get_typeof(image, VIPS_META_ICC_NAME)          ? "ICC-defined"
                                                                    : "sRGB (assumed)");
    if (primaries) {
        json_object_set_string_member(info, "primaries",
                                      primaries == 9    ? "Rec.2020"
                                      : primaries == 12 ? "Display P3"
                                      : primaries == 1  ? "Rec.709 / sRGB"
                                                        : "CICP-defined");
    }
    if (transfer) {
        char *cicp = g_strdup_printf("%d / %d / 0 / full", primaries, transfer);
        json_object_set_string_member(info, "cicp", cicp);
        g_free(cicp);
    }
    JsonNode *root = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(root, info);
    char *result = json_to_string(root, FALSE);
    json_node_free(root);
    return result;
}

int image_codec_frame_duration(VipsImage *image, int page)
{
    int *delays = NULL;
    int count = 0;
    if (vips_image_get_typeof(image, "delay") &&
        !vips_image_get_array_int(image, "delay", &delays, &count) && page >= 0 && page < count) {
        return MAX(delays[page], 0);
    }
    return 0;
}
