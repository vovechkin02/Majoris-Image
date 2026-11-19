#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include <gio/gio.h>

#include <zlib.h>

static guint32 read_be32(const guint8 *data)
{
    guint32 result;
    memcpy(&result, data, sizeof result);
    return GUINT32_FROM_BE(result);
}

static unsigned png_pixel_bits(const guint8 *header)
{
    unsigned depth = header[8], color = header[9];
    if (header[10] || header[11] || header[12] > 1) {
        return 0;
    }
    switch (color) {
    case 0:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16 ? depth : 0;
    case 3:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8 ? depth : 0;
    case 2:
        return depth == 8 || depth == 16 ? 3 * depth : 0;
    case 4:
        return depth == 8 || depth == 16 ? 2 * depth : 0;
    case 6:
        return depth == 8 || depth == 16 ? 4 * depth : 0;
    default:
        return 0;
    }
}

static gboolean png_text_valid(const guint8 *text, guint32 size)
{
    const guint8 *value = memchr(text, 0, size);
    if (!value || value == text || value - text > 79) {
        return FALSE;
    }
    gsize key_length = value++ - text;
    gsize value_length = text + size - value;
    if (memchr(value, 0, value_length)) {
        return FALSE;
    }
    if (key_length == 10 && !memcmp(text, "Thumb::URI", key_length)) {
        return value_length > 0;
    }
    if (key_length == 12 && !memcmp(text, "Thumb::MTime", key_length)) {
        if (!value_length || value_length > 20) {
            return FALSE;
        }
        for (gsize i = 0; i < value_length; i++) {
            if (!g_ascii_isdigit(value[i])) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

static gboolean png_pixels_valid(const GByteArray *compressed, guint32 width, guint32 height,
                                 unsigned bits, gboolean interlaced)
{
    static const unsigned adam7[][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                        {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    gsize row_sizes[7] = {0}, row_counts[7] = {0};
    uLongf expected = 0;
    unsigned passes = interlaced ? G_N_ELEMENTS(adam7) : 1;
    for (unsigned pass = 0; pass < passes; pass++) {
        unsigned x = interlaced ? adam7[pass][0] : 0;
        unsigned y = interlaced ? adam7[pass][1] : 0;
        unsigned dx = interlaced ? adam7[pass][2] : 1;
        unsigned dy = interlaced ? adam7[pass][3] : 1;
        if (width <= x || height <= y) {
            continue;
        }
        gsize columns = (width - x + dx - 1) / dx;
        row_counts[pass] = (height - y + dy - 1) / dy;
        row_sizes[pass] = 1 + (columns * bits + 7) / 8;
        expected += row_sizes[pass] * row_counts[pass];
    }

    // The thumbnail edge cap bounds this allocation even for RGBA16 and Adam7.
    g_autofree guint8 *decoded = g_malloc(expected);
    uLongf decoded_size = expected;
    uLong input_size = compressed->len;
    if (uncompress2(decoded, &decoded_size, compressed->data, &input_size) != Z_OK ||
        decoded_size != expected || input_size != compressed->len) {
        return FALSE;
    }
    gsize offset = 0;
    for (unsigned pass = 0; pass < passes; pass++) {
        for (gsize row = 0; row < row_counts[pass]; row++, offset += row_sizes[pass]) {
            if (decoded[offset] > 4) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

gboolean image_thumbnail_valid(GBytes *bytes, int edge)
{
    static const guint8 signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    gsize length;
    const guint8 *data = g_bytes_get_data(bytes, &length);
    if (edge < 1 || edge > 1024 || length < 45 || length > 8 * 1024 * 1024 ||
        memcmp(data, signature, 8) || read_be32(data + 8) != 13 || memcmp(data + 12, "IHDR", 4)) {
        return FALSE;
    }
    guint32 width = read_be32(data + 16), height = read_be32(data + 20);
    unsigned bits = png_pixel_bits(data + 16);
    gboolean indexed = data[25] == 3;
    if (!bits || !width || !height || width > (guint32)edge || height > (guint32)edge) {
        return FALSE;
    }

    g_autoptr(GByteArray) compressed = g_byte_array_new();
    gboolean palette = FALSE;
    gboolean has_pixels = FALSE;
    gboolean pixels_ended = FALSE;
    for (gsize offset = 8; length - offset >= 12;) {
        guint32 size = read_be32(data + offset);
        if (size > length - offset - 12) {
            return FALSE;
        }
        const guint8 *type = data + offset + 4;
        const guint8 *payload = type + 4;
        if (crc32(0, type, size + 4) != read_be32(payload + size)) {
            return FALSE;
        }
        if (offset == 8) {
            offset += size + 12;
            continue;
        }
        offset += size + 12;
        if (!memcmp(type, "IDAT", 4)) {
            if (pixels_ended || (indexed && !palette)) {
                return FALSE;
            }
            g_byte_array_append(compressed, payload, size);
            has_pixels = TRUE;
            continue;
        }
        pixels_ended = has_pixels;
        if (!memcmp(type, "IEND", 4)) {
            return compressed->len > 0 && size == 0 && offset == length &&
                   png_pixels_valid(compressed, width, height, bits, data[28] == 1);
        }
        if (!memcmp(type, "PLTE", 4)) {
            if (palette || has_pixels || !size || size % 3 || size > 768 ||
                (indexed && size / 3 > (1u << data[24])) || data[25] == 0 || data[25] == 4) {
                return FALSE;
            }
            palette = TRUE;
        } else if (g_ascii_isupper(type[0])) {
            return FALSE;
        } else if (!memcmp(type, "tEXt", 4) && !png_text_valid(payload, size)) {
            return FALSE;
        }
    }
    return FALSE;
}

/* Lock paths are never replaced or removed, so all callers lock the same inode. */
int image_cache_try_lock(const char *path)
{
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        return fd;
    }
    int result = errno == EWOULDBLOCK || errno == EINTR ? -2 : -1;
    close(fd);
    return result;
}

void image_cache_unlock(int fd)
{
    close(fd);
}

static void publish_thread(GTask *task, gpointer source, gpointer data, GCancellable *stop)
{
    (void)source;
    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    const char *path = g_task_get_name(task);
    g_autofree char *directory = g_path_get_dirname(path);
    g_autofree char *name = g_path_get_basename(path);
    g_autofree char *pending = g_strconcat(name, ".pending", NULL);
    gsize size;
    const char *content = g_bytes_get_data(data, &size);
    GError *error = NULL;
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int fd = -1;
    if (dir < 0) {
        goto failed;
    }

    // The caller owns the cache stripe through completion, including recovery
    // of an unpublished file left by a killed process.
    if (unlinkat(dir, pending, 0) < 0 && errno != ENOENT) {
        goto failed;
    }
    fd = openat(dir, pending, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        goto failed;
    }
    for (gsize offset = 0; offset < size;) {
        if (g_cancellable_set_error_if_cancelled(stop, &error)) {
            goto done;
        }
        ssize_t written = write(fd, content + offset, size - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            if (written == 0) {
                errno = EIO;
            }
            goto failed;
        }
        offset += written;
    }
    if (fsync(fd) < 0) {
        goto failed;
    }
    if (g_cancellable_set_error_if_cancelled(stop, &error)) {
        goto done;
    }
    if (renameat(dir, pending, dir, name) < 0 || fsync(dir) < 0) {
        goto failed;
    }
    goto done;
failed:
    g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot publish thumbnail: %s",
                g_strerror(errno));
done:
    if (fd >= 0) {
        close(fd);
    }
    if (dir >= 0) {
        unlinkat(dir, pending, 0);
        close(dir);
    }
    if (error) {
        g_task_return_error(task, error);
    } else {
        g_task_return_boolean(task, TRUE);
    }
}

void image_cache_publish_async(const char *path, GBytes *bytes, GCancellable *stop,
                               GAsyncReadyCallback callback, gpointer data)
{
    GTask *task = g_task_new(NULL, stop, callback, data);
    g_task_set_name(task, path);
    g_task_set_task_data(task, g_bytes_ref(bytes), (GDestroyNotify)g_bytes_unref);
    g_task_run_in_thread(task, publish_thread);
    g_object_unref(task);
}

void image_cache_publish_finish(GAsyncResult *result, GError **error)
{
    g_task_propagate_boolean(G_TASK(result), error);
}

static void png_chunk(GByteArray *png, const char *type, const void *data, guint32 size)
{
    guint32 length = GUINT32_TO_BE(size);
    g_byte_array_append(png, (guint8 *)&length, 4);
    guint start = png->len;
    g_byte_array_append(png, (const guint8 *)type, 4);
    if (size) {
        g_byte_array_append(png, data, size);
    }
    guint32 crc = GUINT32_TO_BE(crc32(0, png->data + start, size + 4));
    g_byte_array_append(png, (guint8 *)&crc, 4);
}

static void png_text(GByteArray *png, const char *key, const char *value)
{
    gsize length = strlen(key) + strlen(value) + 1;
    if (length > 65536) {
        return;
    }
    g_autofree char *text = g_malloc(length);
    memcpy(text, key, strlen(key) + 1);
    memcpy(text + strlen(key) + 1, value, strlen(value));
    png_chunk(png, "tEXt", text, length);
}

GBytes *image_glycin_png(GBytes *pixels, int w, int h, const char *uri, const char *mtime,
                         GCancellable *stop, GError **error)
{
    if (w < 1 || h < 1 || w > 1024 || h > 1024 || g_bytes_get_size(pixels) != (gsize)w * h * 16) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Invalid thumbnail pixel buffer");
        return NULL;
    }
    const float *input = g_bytes_get_data(pixels, NULL);
    gsize stride = (gsize)w * 4 + 1, size = stride * h;
    g_autofree guint8 *raw = g_malloc(size);
    for (int y = 0; y < h; y++) {
        if (g_cancellable_set_error_if_cancelled(stop, error)) {
            return NULL;
        }
        guint8 *row = raw + y * stride;
        *row++ = 0;
        for (int x = 0; x < w; x++, input += 4, row += 4) {
            for (int c = 0; c < 3; c++) {
                double v = CLAMP(input[c], 0, 1);
                row[c] = (guint8)lround(
                    255 * (v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055));
            }
            row[3] = (guint8)lround(255 * CLAMP(input[3], 0, 1));
        }
    }
    uLongf count = compressBound(size);
    g_autofree guint8 *compressed = g_malloc(count);
    if (compress2(compressed, &count, raw, size, Z_DEFAULT_COMPRESSION) != Z_OK) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "PNG compression failed");
        return NULL;
    }
    GByteArray *png = g_byte_array_new();
    const guint8 signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    g_byte_array_append(png, signature, sizeof signature);
    guint8 ihdr[13] = {0};
    guint32 width = GUINT32_TO_BE(w), height = GUINT32_TO_BE(h);
    memcpy(ihdr, &width, 4);
    memcpy(ihdr + 4, &height, 4);
    ihdr[8] = 8;
    ihdr[9] = 6;
    png_chunk(png, "IHDR", ihdr, sizeof ihdr);
    guint8 intent = 0;
    png_chunk(png, "sRGB", &intent, 1);
    if (*mtime) {
        png_text(png, "Thumb::URI", uri);
        png_text(png, "Thumb::MTime", mtime);
    }
    png_chunk(png, "IDAT", compressed, count);
    png_chunk(png, "IEND", NULL, 0);
    return g_byte_array_free_to_bytes(png);
}
