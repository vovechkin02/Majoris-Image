#include <json-glib/json-glib.h>
#include <libheif/heif.h>

#include "bridge.h"
#include "glycin-discovery.h"

static void add_mime(GString *mimes, GHashTable *seen, const char *mime)
{
    if (g_hash_table_contains(seen, mime)) {
        return;
    }
    g_hash_table_add(seen, g_strdup(mime));
    g_string_append_printf(mimes, "%s;", mime);
}

int main(int argc, char **argv)
{
    if (argc != 5 || VIPS_INIT(argv[0])) {
        return 1;
    }
    if (!g_str_equal(argv[4], "baseline") && !g_str_equal(argv[4], "native") &&
        !g_str_equal(argv[4], "glycin") && !g_str_equal(argv[4], "system")) {
        return 1;
    }
    g_autoptr(GString) mimes = g_string_new(NULL);
    g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (g_str_equal(argv[4], "baseline")) {
        g_auto(GStrv) formats = image_codec_desktop_baseline();
        for (char **mime = formats; *mime; mime++) {
            add_mime(mimes, seen, *mime);
        }
    } else {
        g_autoptr(JsonParser) parser = json_parser_new();
        g_autofree char *catalog = image_codec_catalog_json();
        if (!json_parser_load_from_data(parser, catalog, -1, NULL)) {
            return 1;
        }
        JsonArray *entries = json_node_get_array(json_parser_get_root(parser));
        for (guint i = 0; i < json_array_get_length(entries); i++) {
            JsonObject *entry = json_array_get_object_element(entries, i);
            if (!json_object_get_boolean_member_with_default(entry, "thumbnail", FALSE)) {
                continue;
            }
            JsonArray *formats = json_object_get_array_member(entry, "mime-types");
            for (guint j = 0; j < json_array_get_length(formats); j++) {
                add_mime(mimes, seen, json_array_get_string_element(formats, j));
            }
        }
        gboolean discover_glycin = g_str_equal(argv[4], "glycin");
#ifdef IMAGE_CODEC_GLYCIN_SYSTEM
        discover_glycin = discover_glycin || g_str_equal(argv[4], "system");
#endif
        if (discover_glycin) {
            g_auto(GStrv) formats = image_discover_glycin_loaders();
            for (char **mime = formats; *mime; mime++) {
                if (image_mime_matches(*mime, "image/avif") &&
                    !heif_have_decoder_for_format(heif_compression_AV1)) {
                    continue;
                }
                if ((image_mime_matches(*mime, "image/heif") ||
                     image_mime_matches(*mime, "image/heic")) &&
                    !heif_have_decoder_for_format(heif_compression_HEVC)) {
                    continue;
                }
                add_mime(mimes, seen, *mime);
            }
        }
    }

    g_autoptr(GKeyFile) output = g_key_file_new();
    if (!g_key_file_load_from_file(output, argv[1], G_KEY_FILE_NONE, NULL)) {
        return 1;
    }
    g_autofree char *exec = g_strconcat(argv[3], " %u %o %s", NULL);
    g_key_file_set_string(output, "Thumbnailer Entry", "TryExec", argv[3]);
    g_key_file_set_string(output, "Thumbnailer Entry", "Exec", exec);
    g_key_file_set_string(output, "Thumbnailer Entry", "MimeType", mimes->str);
    return g_key_file_save_to_file(output, argv[2], NULL) ? 0 : 1;
}
