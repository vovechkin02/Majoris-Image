#include <gio/gio.h>

#include <vips/vips.h>
#include <zlib.h>

gboolean image_thumbnail_valid (GBytes *bytes, int edge);

static const guint8 signature[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
static const guint8 rgba[] = { 0, 255, 0, 17, 128, 51, 204, 255, 0 };

static void
append_chunk (GByteArray *png, const char *type, const guint8 *data, guint size)
{
    guint32 length = GUINT32_TO_BE (size);
    guint32 crc = crc32 (0, (const guint8 *)type, 4);
    if (size)
        crc = crc32 (crc, data, size);
    crc = GUINT32_TO_BE (crc);
    g_byte_array_append (png, (guint8 *)&length, 4);
    g_byte_array_append (png, (const guint8 *)type, 4);
    if (size)
        g_byte_array_append (png, data, size);
    g_byte_array_append (png, (guint8 *)&crc, 4);
}

static GBytes *
fixture (const char *mode)
{
    g_autoptr (GByteArray) png = g_byte_array_new ();
    guint8 header[] = { 0, 0, 0, 2, 0, 0, 0, 1, 8, 6, 0, 0, 0 };
    guint8 raw[sizeof rgba + 4] = { 0 };
    guint8 deflated[64];
    uLongf size = sizeof deflated;
    memcpy (raw, rgba, sizeof rgba);
    if (!strcmp (mode, "zero-width"))
        header[3] = 0;
    if (!strcmp (mode, "color-type"))
        header[9] = 1;
    if (!strcmp (mode, "filter"))
        raw[0] = 5;
    if (!strcmp (mode, "adam7")) {
        header[12] = 1;
        memmove (raw + 6, raw + 5, 4);
        raw[5] = 0;
    }
    guint raw_size = sizeof rgba;
    if (!strcmp (mode, "excess"))
        raw_size = sizeof raw;
    if (!strcmp (mode, "adam7"))
        raw_size++;
    g_assert_cmpint (compress (deflated, &size, raw, raw_size), ==, Z_OK);

    g_byte_array_append (png, signature, sizeof signature);
    append_chunk (png, "IHDR", header, sizeof header);
    const char uri[] = "Thumb::URI\0file:///fixture.png";
    const char time[] = "Thumb::MTime\0"
                        "1234567";
    const char invalid_time[] = "Thumb::MTime\0today";
    append_chunk (png, "tEXt", (const guint8 *)uri,
                  !strcmp (mode, "malformed-text") ? 4 : sizeof uri - 1);
    if (!strcmp (mode, "mtime"))
        append_chunk (png, "tEXt", (const guint8 *)invalid_time, sizeof invalid_time - 1);
    else
        append_chunk (png, "tEXt", (const guint8 *)time, sizeof time - 1);
    if (!strcmp (mode, "compression"))
        deflated[0] = 0;
    if (!strcmp (mode, "truncated-zlib"))
        size--;
    if (!strcmp (mode, "split-idat")) {
        append_chunk (png, "IDAT", deflated, 1);
        append_chunk (png, "IDAT", deflated + 1, size - 1);
    } else if (strcmp (mode, "missing-idat")) {
        append_chunk (png, "IDAT", deflated, size);
    }
    if (strcmp (mode, "missing-iend"))
        append_chunk (png, "IEND", NULL, 0);
    if (!strcmp (mode, "crc"))
        png->data[png->len - 1] ^= 1;
    if (!strcmp (mode, "signature"))
        png->data[0] ^= 1;
    if (!strcmp (mode, "truncated-chunk"))
        g_byte_array_set_size (png, png->len - 14);
    return g_byte_array_free_to_bytes (g_steal_pointer (&png));
}

static void
verify_valid (gconstpointer mode)
{
    g_autoptr (GBytes) bytes = fixture (mode);
    gsize size;
    const void *data = g_bytes_get_data (bytes, &size);
    g_assert_true (image_thumbnail_valid (bytes, 2));
    g_assert_false (image_thumbnail_valid (bytes, 1));
    VipsImage *image = NULL;
    g_assert_cmpint (vips_pngload_buffer ((void *)data, size, &image, NULL), ==, 0);
    g_assert_cmpint (image->Xsize, ==, 2);
    g_assert_cmpint (image->Ysize, ==, 1);
    g_assert_cmpint (image->Bands, ==, 4);
    g_autofree void *pixels = vips_image_write_to_memory (image, &size);
    g_assert_cmpmem (pixels, size, rgba + 1, sizeof rgba - 1);
    g_object_unref (image);
}

static void
verify_invalid (gconstpointer mode)
{
    g_autoptr (GBytes) bytes = fixture (mode);
    g_assert_false (image_thumbnail_valid (bytes, 2));
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    if (VIPS_INIT (argv[0]))
        return 1;
    vips_cache_set_max (0);
    const char *good[] = { "valid", "split-idat", "adam7" };
    const char *bad[]
        = { "signature",       "zero-width",  "missing-idat",   "missing-iend", "crc",
            "truncated-chunk", "compression", "truncated-zlib", "filter",       "excess",
            "malformed-text",  "color-type",  "mtime" };
    for (gsize i = 0; i < G_N_ELEMENTS (good); i++) {
        g_autofree char *name = g_strdup_printf ("/thumbnail/valid/%s", good[i]);
        g_test_add_data_func (name, good[i], verify_valid);
    }
    for (gsize i = 0; i < G_N_ELEMENTS (bad); i++) {
        g_autofree char *name = g_strdup_printf ("/thumbnail/invalid/%s", bad[i]);
        g_test_add_data_func (name, bad[i], verify_invalid);
    }
    int status = g_test_run ();
    vips_shutdown ();
    return status;
}
