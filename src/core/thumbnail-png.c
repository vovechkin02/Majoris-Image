#include <glib.h>
#include <string.h>
#include <zlib.h>

static guint32 read_be32(const guint8 *data)
{
    guint32 result;
    memcpy(&result, data, sizeof result);
    return GUINT32_FROM_BE(result);
}

gboolean majoris_image_thumbnail_valid(GBytes *bytes, int edge)
{
    static const guint8 signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    gsize length;
    const guint8 *data = g_bytes_get_data(bytes, &length);
    if (length < 45 || memcmp(data, signature, 8) ||
        read_be32(data + 8) != 13 || memcmp(data + 12, "IHDR", 4))
        return FALSE;
    guint32 width = read_be32(data + 16), height = read_be32(data + 20);
    if (!width || !height || width > (guint32) edge || height > (guint32) edge)
        return FALSE;
    gboolean has_pixels = FALSE;
    for (gsize offset = 8; length - offset >= 12;) {
        guint32 size = read_be32(data + offset);
        if (size > length - offset - 12) return FALSE;
        const guint8 *type = data + offset + 4;
        if (crc32(0, type, size + 4) != read_be32(type + 4 + size))
            return FALSE;
        offset += size + 12;
        if (!memcmp(type, "IDAT", 4)) has_pixels = TRUE;
        if (!memcmp(type, "IEND", 4)) return has_pixels && size == 0 && offset == length;
    }
    return FALSE;
}
