#include "bridge.h"

/** Checks whether a source has a supported Ultra HDR gain-map representation. */
gboolean image_codec_ultra_probe(const char *path)
{
    (void)path;
    return FALSE;
}

/** Decodes Ultra HDR content with HDR or SDR presentation as requested. */
VipsImage *image_codec_ultra_decode(const char *path, gboolean sdr, GError **error)
{
    (void)path;
    (void)sdr;
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Ultra HDR codec unavailable");
    return NULL;
}

/** Encodes an image into the requested Ultra HDR output representation. */
gboolean image_codec_ultra_encode(VipsImage *image, const char *path, int format, int quality,
                                  GError **error)
{
    (void)image;
    (void)path;
    (void)format;
    (void)quality;
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Ultra HDR codec unavailable");
    return FALSE;
}
