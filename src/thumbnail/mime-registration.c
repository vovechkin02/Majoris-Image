#include <vips/vips.h>
#include <libheif/heif.h>
#include <stdio.h>

static gboolean installed(const char *nickname)
{
    return vips_type_find("VipsOperation", nickname) != 0;
}

int main(int argc, char **argv)
{
    if (argc != 5 || VIPS_INIT(argv[0])) return 1;
    g_autoptr(GString) mimes = g_string_new(
        "image/x-exr;image/x-adobe-dng;image/x-canon-cr2;image/x-canon-cr3;"
        "image/x-canon-crw;image/x-nikon-nef;image/x-nikon-nrw;image/x-sony-arw;"
        "image/x-sony-sr2;image/x-fuji-raf;image/x-panasonic-rw2;image/x-olympus-orf;"
        "image/x-pentax-pef;image/x-samsung-srw;image/x-minolta-mrw;");
    const struct { const char *operation, *mimes; } formats[] = {
        {"jpegload", "image/jpeg;"},
        {"pngload", "image/png;image/apng;"},
        {"tiffload", "image/tiff;"},
        {"webpload", "image/webp;"},
        {"gifload", "image/gif;"},
        {"jp2kload", "image/jp2;"},
        {"jxlload", "image/jxl;"},
        {"svgload", "image/svg+xml;image/svg+xml-compressed;"},
        {"radload", "image/vnd.radiance;"},
        {"ppmload", "image/x-portable-anymap;image/x-portable-bitmap;image/x-portable-graymap;image/x-portable-pixmap;"}
    };
    for (guint i = 0; i < G_N_ELEMENTS(formats); i++)
        if (installed(formats[i].operation)) g_string_append(mimes, formats[i].mimes);
    if (installed("heifload")) {
        if (heif_have_decoder_for_format(heif_compression_AV1))
            g_string_append(mimes, "image/avif;");
        if (heif_have_decoder_for_format(heif_compression_HEVC))
            g_string_append(mimes, "image/heif;image/heic;");
    }

    if (g_str_equal(argv[4], "glycin")) {
        /* Match the default installed loader config used by the fallback. */
        g_autoptr(GKeyFile) loaders = g_key_file_new();
        if (g_key_file_load_from_file(loaders,
            "/usr/share/glycin-loaders/1+/conf.d/glycin-image-rs.conf", G_KEY_FILE_NONE, NULL)) {
            const char *legacy[] = { "image/bmp", "image/vnd.microsoft.icon", "image/qoi",
                "image/x-qoi", "image/x-tga", "image/vnd-ms.dds", "image/x-dds" };
            for (guint i = 0; i < G_N_ELEMENTS(legacy); i++) {
                g_autofree char *group = g_strconcat("loader:", legacy[i], NULL);
                g_autofree char *exec = g_key_file_get_string(loaders, group, "Exec", NULL);
                if (exec && g_file_test(exec, G_FILE_TEST_IS_EXECUTABLE))
                    g_string_append_printf(mimes, "%s;", legacy[i]);
            }
        }
    }

    g_autoptr(GKeyFile) output = g_key_file_new();
    if (!g_key_file_load_from_file(output, argv[1], G_KEY_FILE_NONE, NULL)) return 1;
    g_autofree char *exec = g_strconcat(argv[3], " %u %o %s", NULL);
    g_key_file_set_string(output, "Thumbnailer Entry", "TryExec", argv[3]);
    g_key_file_set_string(output, "Thumbnailer Entry", "Exec", exec);
    g_key_file_set_string(output, "Thumbnailer Entry", "MimeType", mimes->str);
    return g_key_file_save_to_file(output, argv[2], NULL) ? 0 : 1;
}
