#include "glycin-discovery.h"

gboolean image_mime_matches(const char *first, const char *second)
{
    if (g_strcmp0(first, second) == 0) {
        return TRUE;
    }
    if (!first || !second) {
        return FALSE;
    }
    g_autofree char *a = g_content_type_from_mime_type(first);
    g_autofree char *b = g_content_type_from_mime_type(second);
    return a && b && g_content_type_equals(a, b);
}

static void load_config_root(GHashTable *entries, const char *root)
{
    // Glycin 1's Rust loader protocol reports compatibility version 1.
    g_autofree char *path = g_build_filename(root, "glycin-loaders", "1+", "conf.d", NULL);
    g_autoptr(GDir) directory = g_dir_open(path, 0, NULL);
    if (!directory) {
        return;
    }
    const char *name;
    while ((name = g_dir_read_name(directory))) {
        if (!g_str_has_suffix(name, ".conf")) {
            continue;
        }
        g_autofree char *filename = g_build_filename(path, name, NULL);
        g_autoptr(GKeyFile) config = g_key_file_new();
        if (!g_key_file_load_from_file(config, filename, G_KEY_FILE_NONE, NULL)) {
            continue;
        }
        g_auto(GStrv) groups = g_key_file_get_groups(config, NULL);
        for (char **group = groups; *group; group++) {
            if (!g_str_has_prefix(*group, "loader:image/")) {
                continue;
            }
            const char *mime = *group + strlen("loader:");
            if (strpbrk(mime, ":; \t\r\n")) {
                continue;
            }
            char *executable = g_key_file_get_string(config, *group, "Exec", NULL);
            if (executable) {
                g_hash_table_replace(entries, g_strdup(mime), executable);
            }
        }
    }
}

static int compare_mimes(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(*(const char *const *)a, *(const char *const *)b);
}

char **image_discover_glycin_loaders(void)
{
    g_autoptr(GHashTable) entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const char *const *roots = g_get_system_data_dirs();
    for (int i = g_strv_length((char **)roots) - 1; i >= 0; i--) {
        load_config_root(entries, roots[i]);
    }
    load_config_root(entries, g_get_user_data_dir());
    GPtrArray *mimes = g_ptr_array_new();
    GHashTableIter iterator;
    gpointer key, value;
    g_hash_table_iter_init(&iterator, entries);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        const char *exec = value;
        if (g_path_is_absolute(exec) && g_file_test(exec, G_FILE_TEST_IS_REGULAR) &&
            g_file_test(exec, G_FILE_TEST_IS_EXECUTABLE)) {
            g_ptr_array_add(mimes, g_strdup(key));
        }
    }
    g_ptr_array_sort(mimes, compare_mimes);
    g_ptr_array_add(mimes, NULL);
    return (char **)g_ptr_array_free(mimes, FALSE);
}
