#include <gio/gio.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <sys/file.h>
#include <unistd.h>
#include <string.h>
#include <zlib.h>

static guint32 read_be32(const guint8 *data)
{
    guint32 result;
    memcpy(&result, data, sizeof result);
    return GUINT32_FROM_BE(result);
}

gboolean image_thumbnail_valid(GBytes *bytes, int edge)
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

/* Lock paths are never replaced or removed, so all callers lock the same inode. */
int image_cache_try_lock(const char *path)
{
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) return fd;
    int result = errno == EWOULDBLOCK || errno == EINTR ? -2 : -1;
    close(fd);
    return result;
}

void image_cache_unlock(int fd) { close(fd); }

static void publish_thread(GTask *task, gpointer source, gpointer data, GCancellable *stop)
{
    if (g_task_return_error_if_cancelled(task)) return;
    const char *path = g_task_get_name(task);
    g_autofree char *directory = g_path_get_dirname(path);
    g_autofree char *name = g_path_get_basename(path);
    g_autofree char *pending = g_strconcat(name, ".pending", NULL);
    gsize size;
    const char *content = g_bytes_get_data(data, &size);
    GError *error = NULL;
    int dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int fd = -1;
    if (dir < 0) goto failed;

    // The caller owns the cache stripe through completion, including recovery
    // of an unpublished file left by a killed process.
    if (unlinkat(dir, pending, 0) < 0 && errno != ENOENT) goto failed;
    fd = openat(dir, pending, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) goto failed;
    for (gsize offset = 0; offset < size;) {
        if (g_cancellable_set_error_if_cancelled(stop, &error)) goto done;
        ssize_t written = write(fd, content + offset, size - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { if (written == 0) errno = EIO; goto failed; }
        offset += written;
    }
    if (fsync(fd) < 0) goto failed;
    if (g_cancellable_set_error_if_cancelled(stop, &error)) goto done;
    if (renameat(dir, pending, dir, name) < 0 || fsync(dir) < 0) goto failed;
    goto done;
failed:
    g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot publish thumbnail: %s", g_strerror(errno));
done:
    if (fd >= 0) close(fd);
    if (dir >= 0) {
        unlinkat(dir, pending, 0);
        close(dir);
    }
    if (error) g_task_return_error(task, error);
    else g_task_return_boolean(task, TRUE);
}

void image_cache_publish_async(const char *path, GBytes *bytes,
                                      GCancellable *stop, GAsyncReadyCallback callback, gpointer data)
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
    if (size) g_byte_array_append(png, data, size);
    guint32 crc = GUINT32_TO_BE(crc32(0, png->data + start, size + 4));
    g_byte_array_append(png, (guint8 *)&crc, 4);
}

static void png_text(GByteArray *png, const char *key, const char *value)
{
    gsize length = strlen(key) + strlen(value) + 1;
    if (length > 65536) return;
    g_autofree char *text = g_malloc(length);
    memcpy(text, key, strlen(key) + 1);
    memcpy(text + strlen(key) + 1, value, strlen(value));
    png_chunk(png, "tEXt", text, length);
}

GBytes *image_glycin_png(GBytes *pixels, int w, int h, const char *uri,
                                 const char *mtime, GCancellable *stop, GError **error)
{
    if (w < 1 || h < 1 || w > 1024 || h > 1024 || g_bytes_get_size(pixels) != (gsize)w * h * 16) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid thumbnail pixel buffer");
        return NULL;
    }
    const float *input = g_bytes_get_data(pixels, NULL);
    gsize stride = (gsize)w * 4 + 1, size = stride * h;
    g_autofree guint8 *raw = g_malloc(size);
    for (int y = 0; y < h; y++) {
        if (g_cancellable_set_error_if_cancelled(stop, error)) return NULL;
        guint8 *row = raw + y * stride;
        *row++ = 0;
        for (int x = 0; x < w; x++, input += 4, row += 4) {
            for (int c = 0; c < 3; c++) {
                double v = CLAMP(input[c], 0, 1);
                row[c] = (guint8)lround(255 * (v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0/2.4) - 0.055));
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
    ihdr[8] = 8; ihdr[9] = 6;
    png_chunk(png, "IHDR", ihdr, sizeof ihdr);
    guint8 intent = 0;
    png_chunk(png, "sRGB", &intent, 1);
    if (*mtime) { png_text(png, "Thumb::URI", uri); png_text(png, "Thumb::MTime", mtime); }
    png_chunk(png, "IDAT", compressed, count);
    png_chunk(png, "IEND", NULL, 0);
    return g_byte_array_free_to_bytes(png);
}
