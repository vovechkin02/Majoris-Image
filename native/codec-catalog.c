#include <json-glib/json-glib.h>
#include <libheif/heif.h>
#include <libraw/libraw.h>

#include "bridge.h"
#include "capability-flags.h"
#ifdef IMAGE_CODEC_AVIF_SEQUENCE
#include <avif/avif.h>
#endif

typedef struct {
    JsonBuilder *builder;
    gboolean encoder;
} CatalogContext;

static const struct NativeLoader {
    const char *operation;
    const char *mimes;
    unsigned capabilities;
    const char *desktop_baseline;
} loaders[] = {
    {"jpegload", "image/jpeg",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_THUMBNAIL | IMAGE_CAPABILITY_METADATA, "image/jpeg"},
    {"pngload", "image/png;image/apng",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_THUMBNAIL |
         IMAGE_CAPABILITY_METADATA,
     "image/png"},
    {"tiffload", "image/tiff",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_MULTI_IMAGE | IMAGE_CAPABILITY_HIGH_DEPTH |
         IMAGE_CAPABILITY_HDR | IMAGE_CAPABILITY_REGION | IMAGE_CAPABILITY_THUMBNAIL |
         IMAGE_CAPABILITY_METADATA,
     "image/tiff"},
    {"webpload", "image/webp",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_ANIMATION | IMAGE_CAPABILITY_THUMBNAIL |
         IMAGE_CAPABILITY_METADATA},
    {"gifload", "image/gif",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_ANIMATION | IMAGE_CAPABILITY_THUMBNAIL |
         IMAGE_CAPABILITY_METADATA},
    {"jp2kload", "image/jp2",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_THUMBNAIL |
         IMAGE_CAPABILITY_METADATA},
    {"jxlload", "image/jxl",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_HDR |
         IMAGE_CAPABILITY_THUMBNAIL | IMAGE_CAPABILITY_METADATA},
    {"svgload", "image/svg+xml;image/svg+xml-compressed",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_REGION | IMAGE_CAPABILITY_THUMBNAIL |
         IMAGE_CAPABILITY_VECTOR},
    {"radload", "image/vnd.radiance",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_HDR |
         IMAGE_CAPABILITY_THUMBNAIL},
    {"ppmload",
     "image/x-portable-anymap;image/x-portable-bitmap;image/x-portable-graymap;image/"
     "x-portable-pixmap",
     IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_THUMBNAIL},
};

// The package descriptor must be reproducible on hosts with different plugins.
char **image_codec_desktop_baseline(void)
{
    GPtrArray *types = g_ptr_array_new();
    for (guint i = 0; i < G_N_ELEMENTS(loaders); i++) {
        if (loaders[i].desktop_baseline) {
            g_ptr_array_add(types, g_strdup(loaders[i].desktop_baseline));
        }
    }
    g_ptr_array_add(types, g_strdup("image/x-exr"));
    g_ptr_array_add(types, NULL);
    return (char **)g_ptr_array_free(types, FALSE);
}

static void add_formats(JsonBuilder *builder, const char *mimes, unsigned capabilities)
{
    json_builder_set_member_name(builder, "mime-types");
    json_builder_begin_array(builder);
    g_auto(GStrv) list = g_strsplit(mimes, ";", -1);
    for (char **mime = list; *mime; mime++) {
        if (**mime) {
            json_builder_add_string_value(builder, *mime);
        }
    }
    json_builder_end_array(builder);
    json_builder_set_member_name(builder, "capability-flags");
    json_builder_add_int_value(builder, capabilities);
    json_builder_set_member_name(builder, "thumbnail");
    json_builder_add_boolean_value(builder, (capabilities & IMAGE_CAPABILITY_THUMBNAIL) != 0);
    // Flags are decoder potential. Inspect the individual file to negotiate.
    json_builder_set_member_name(builder, "conditional");
    json_builder_add_boolean_value(builder, TRUE);
}

static void add_backend(JsonBuilder *builder, const char *operation, const char *mimes,
                        unsigned capabilities)
{
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "operation");
    json_builder_add_string_value(builder, operation);
    json_builder_set_member_name(builder, "encoder");
    json_builder_add_boolean_value(builder, FALSE);
    add_formats(builder, mimes, capabilities);
    json_builder_end_object(builder);
}

static void *collect_operation(GType type, void *data)
{
    if (G_TYPE_IS_ABSTRACT(type)) {
        return NULL;
    }
    CatalogContext *ctx = data;
    VipsObjectClass *klass = g_type_class_ref(type);
    VipsForeignClass *foreign = VIPS_FOREIGN_CLASS(klass);
    if (klass->nickname) {
        JsonBuilder *builder = ctx->builder;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "operation");
        json_builder_add_string_value(builder, klass->nickname);
        json_builder_set_member_name(builder, "encoder");
        json_builder_add_boolean_value(builder, ctx->encoder);
        json_builder_set_member_name(builder, "suffixes");
        json_builder_begin_array(builder);
        if (foreign->suffs) {
            for (const char **suffix = foreign->suffs; *suffix; suffix++) {
                json_builder_add_string_value(builder, *suffix);
            }
        }
        json_builder_end_array(builder);
        if (!ctx->encoder) {
            for (guint i = 0; i < G_N_ELEMENTS(loaders); i++) {
                if (g_str_equal(klass->nickname, loaders[i].operation)) {
                    add_formats(builder, loaders[i].mimes, loaders[i].capabilities);
                    break;
                }
            }
        }
        json_builder_end_object(builder);
    }
    g_type_class_unref(klass);
    return NULL;
}

char *image_codec_catalog_json(void)
{
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_array(builder);
    CatalogContext ctx = {builder, FALSE};
    vips_type_map_all(VIPS_TYPE_FOREIGN_LOAD, collect_operation, &ctx);
    ctx.encoder = TRUE;
    vips_type_map_all(VIPS_TYPE_FOREIGN_SAVE, collect_operation, &ctx);

    add_backend(builder, "openexr-native", "image/x-exr",
                IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_MULTI_IMAGE |
                    IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_HDR | IMAGE_CAPABILITY_REGION |
                    IMAGE_CAPABILITY_THUMBNAIL | IMAGE_CAPABILITY_METADATA);
    if (libraw_cameraCount() > 0) {
        add_backend(builder, "libraw",
                    "image/x-adobe-dng;image/x-canon-cr2;image/x-canon-cr3;image/x-canon-crw;"
                    "image/x-nikon-nef;image/x-nikon-nrw;image/x-sony-arw;image/x-sony-sr2;"
                    "image/x-fuji-raf;image/x-panasonic-rw2;image/x-olympus-orf;"
                    "image/x-pentax-pef;image/x-samsung-srw;image/x-minolta-mrw",
                    IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_HIGH_DEPTH |
                        IMAGE_CAPABILITY_THUMBNAIL | IMAGE_CAPABILITY_EMBEDDED_PREVIEW |
                        IMAGE_CAPABILITY_METADATA);
    }
    if (vips_type_find("VipsOperation", "heifload")) {
        if (heif_have_decoder_for_format(heif_compression_AV1)) {
            add_backend(builder, "heifload-avif", "image/avif",
                        IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_MULTI_IMAGE |
                            IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_HDR |
                            IMAGE_CAPABILITY_THUMBNAIL | IMAGE_CAPABILITY_METADATA);
        }
        if (heif_have_decoder_for_format(heif_compression_HEVC)) {
            add_backend(builder, "heifload-hevc", "image/heif;image/heic",
                        IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_MULTI_IMAGE |
                            IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_HDR |
                            IMAGE_CAPABILITY_THUMBNAIL | IMAGE_CAPABILITY_METADATA);
        }
    }
#ifdef IMAGE_CODEC_AVIF_SEQUENCE
    if (avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_DECODE)) {
        add_backend(builder, "libavif-sequence", "image/avif-sequence",
                    IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_ANIMATION |
                        IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_HDR |
                        IMAGE_CAPABILITY_THUMBNAIL | IMAGE_CAPABILITY_METADATA);
    }
#endif
#ifdef IMAGE_CODEC_HEIF_SEQUENCE
    if (heif_have_decoder_for_format(heif_compression_HEVC)) {
        add_backend(builder, "libheif-sequence", "image/heif-sequence;image/heic-sequence",
                    IMAGE_CAPABILITY_STILL | IMAGE_CAPABILITY_ANIMATION |
                        IMAGE_CAPABILITY_HIGH_DEPTH | IMAGE_CAPABILITY_HDR |
                        IMAGE_CAPABILITY_THUMBNAIL | IMAGE_CAPABILITY_METADATA);
    }
#endif
    json_builder_end_array(builder);
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    return json_to_string(root, FALSE);
}
