#include "bridge.h"
#include <json-glib/json-glib.h>

typedef struct { JsonBuilder *builder; gboolean encoder; } CatalogueContext;
static void *collect_operation(GType type, void *data) {
    if (G_TYPE_IS_ABSTRACT(type)) return NULL;
    CatalogueContext *ctx = data;
    VipsObjectClass *klass = g_type_class_ref(type);
    VipsForeignClass *foreign = VIPS_FOREIGN_CLASS(klass);
    if (klass->nickname) {
        json_builder_begin_object(ctx->builder);
        json_builder_set_member_name(ctx->builder,"operation"); json_builder_add_string_value(ctx->builder,klass->nickname);
        json_builder_set_member_name(ctx->builder,"encoder"); json_builder_add_boolean_value(ctx->builder,ctx->encoder);
        json_builder_set_member_name(ctx->builder,"suffixes"); json_builder_begin_array(ctx->builder);
        if (foreign->suffs) for (const char **s = foreign->suffs; *s; s++) json_builder_add_string_value(ctx->builder,*s);
        json_builder_end_array(ctx->builder); json_builder_end_object(ctx->builder);
    }
    g_type_class_unref(klass); return NULL;
}
char *majoris_codec_codec_json(void) {
    JsonBuilder *builder = json_builder_new(); json_builder_begin_array(builder);
    CatalogueContext ctx = {builder,FALSE}; vips_type_map_all(VIPS_TYPE_FOREIGN_LOAD,collect_operation,&ctx);
    ctx.encoder = TRUE; vips_type_map_all(VIPS_TYPE_FOREIGN_SAVE,collect_operation,&ctx);
    json_builder_end_array(builder); JsonNode *root=json_builder_get_root(builder); char *result=json_to_string(root,FALSE);
    json_node_free(root); g_object_unref(builder); return result;
}
char **majoris_codec_image_mime_types(void) {
    gchar **suffixes = vips_foreign_get_suffixes(); GHashTable *seen = g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
    GPtrArray *types = g_ptr_array_new();
    for (gchar **suffix=suffixes; suffix && *suffix; suffix++) {
        gchar *filename = g_strconcat("image",*suffix,NULL); gboolean uncertain;
        gchar *type = g_content_type_guess(filename,NULL,0,&uncertain); g_free(filename);
        if (type && g_str_has_prefix(type,"image/") && !g_hash_table_contains(seen,type)) {
            g_hash_table_add(seen,g_strdup(type)); g_ptr_array_add(types,type);
        } else g_free(type);
    }
    g_hash_table_unref(seen); g_strfreev(suffixes); g_ptr_array_add(types,NULL);
    return (char **)g_ptr_array_free(types,FALSE);
}
