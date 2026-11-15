#include "bridge.h"

gboolean image_codec_ultra_probe(const char *path)
{
    (void)path;
    return FALSE;
}

VipsImage *image_codec_ultra_decode(const char *path, gboolean sdr, GError **error)
{
    (void)path; (void)sdr;
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Ultra HDR codec unavailable");
    return NULL;
}

gboolean image_codec_ultra_encode(VipsImage *image, const char *path, int format, int quality, GError **error)
{
    (void)image; (void)path; (void)format; (void)quality;
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Ultra HDR codec unavailable");
    return FALSE;
}
