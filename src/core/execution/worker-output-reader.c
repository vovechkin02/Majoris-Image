#define _GNU_SOURCE

#include "worker-output-reader.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "majoris-image.h"

#define OUTPUT_CHUNK_BYTES (256 * 1024)

typedef struct {
    gint64 limit;
    char path[];
} OutputRead;

static void set_io_error(GError **error, const char *operation, int saved_errno)
{
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno), "%s: %s", operation,
                g_strerror(saved_errno));
}

static void invalid_output(GError **error, const char *message)
{
    g_set_error_literal(error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE,
                        message);
}

static int open_output(const OutputRead *request, struct stat *initial, GCancellable *cancel,
                       GError **error)
{
    // Pin the final component without opening a potentially hostile device for I/O.
    int pin = open(request->path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (pin < 0) {
        int saved_errno = errno;
        if (saved_errno == ENOENT) {
            invalid_output(error, "Decoder did not produce an output file");
        } else {
            set_io_error(error, "Cannot open decoder output", saved_errno);
        }
        return -1;
    }

    int fd = -1;
    if (fstat(pin, initial) < 0) {
        set_io_error(error, "Cannot inspect decoder output", errno);
    } else if (!S_ISREG(initial->st_mode) || initial->st_size < 1 ||
               initial->st_size > request->limit) {
        invalid_output(error, "Invalid decoder response type or size");
    } else if (!g_cancellable_set_error_if_cancelled(cancel, error)) {
        char pinned_path[64];
        g_snprintf(pinned_path, sizeof pinned_path, "/proc/self/fd/%d", pin);
        // procfs opens the pinned inode even when the original name has changed.
        fd = open(pinned_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            set_io_error(error, "Cannot read decoder output", errno);
        }
    }
    close(pin);
    return fd;
}

static ssize_t read_chunk(int fd, void *data, gsize count, GCancellable *cancel, GError **error)
{
    while (!g_cancellable_set_error_if_cancelled(cancel, error)) {
        ssize_t received = read(fd, data, count);
        if (received >= 0) {
            return received;
        }
        if (errno != EINTR) {
            set_io_error(error, "Cannot read decoder output", errno);
            return -1;
        }
    }
    return -1;
}

static gboolean verify_output(int fd, const struct stat *initial, GError **error)
{
    struct stat final;
    if (fstat(fd, &final) < 0) {
        set_io_error(error, "Cannot recheck decoder output", errno);
        return FALSE;
    }
    if (!S_ISREG(final.st_mode) || final.st_dev != initial->st_dev ||
        final.st_ino != initial->st_ino || final.st_size != initial->st_size ||
        final.st_mtim.tv_sec != initial->st_mtim.tv_sec ||
        final.st_mtim.tv_nsec != initial->st_mtim.tv_nsec ||
        final.st_ctim.tv_sec != initial->st_ctim.tv_sec ||
        final.st_ctim.tv_nsec != initial->st_ctim.tv_nsec) {
        invalid_output(error, "Decoder output changed while reading");
        return FALSE;
    }
    return TRUE;
}

static GBytes *read_output(int fd, const struct stat *initial, gint64 limit, GCancellable *cancel,
                           GError **error)
{
    if (g_cancellable_set_error_if_cancelled(cancel, error)) {
        return NULL;
    }
    gsize size = (gsize)initial->st_size;
    g_autofree guint8 *data = g_try_malloc(size);
    if (data == NULL) {
        g_set_error_literal(error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_MEMORY,
                            "Cannot allocate decoder output buffer");
        return NULL;
    }

    for (gsize offset = 0; offset < size;) {
        ssize_t received =
            read_chunk(fd, data + offset, MIN(OUTPUT_CHUNK_BYTES, size - offset), cancel, error);
        if (received < 0) {
            return NULL;
        }
        if (received == 0) {
            invalid_output(error, "Decoder output ended before its expected size");
            return NULL;
        }
        offset += (gsize)received;
    }

    // At the limit, the descriptor size check detects extra bytes without reading past it.
    if (initial->st_size < limit) {
        guint8 extra;
        ssize_t received = read_chunk(fd, &extra, sizeof extra, cancel, error);
        if (received < 0) {
            return NULL;
        }
        if (received != 0) {
            invalid_output(error, "Decoder output grew beyond its expected size");
            return NULL;
        }
    }
    if (g_cancellable_set_error_if_cancelled(cancel, error) || !verify_output(fd, initial, error) ||
        g_cancellable_set_error_if_cancelled(cancel, error)) {
        return NULL;
    }
    return g_bytes_new_take(g_steal_pointer(&data), size);
}

static void read_output_thread(GTask *task, gpointer source, gpointer task_data,
                               GCancellable *cancel)
{
    (void)source;
    const OutputRead *request = task_data;
    g_autoptr(GError) error = NULL;
    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    if (request->limit <= 0 || (guint64)request->limit > G_MAXSIZE) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Invalid decoder output size limit");
        return;
    }

    struct stat initial;
    int fd = open_output(request, &initial, cancel, &error);
    GBytes *bytes = NULL;
    if (fd >= 0) {
        bytes = read_output(fd, &initial, request->limit, cancel, &error);
        close(fd);
    }
    if (bytes != NULL) {
        g_task_return_pointer(task, bytes, (GDestroyNotify)g_bytes_unref);
    } else {
        g_task_return_error(task, g_steal_pointer(&error));
    }
}

void worker_output_read_async(const char *path, gint64 limit, GCancellable *cancel,
                              GAsyncReadyCallback callback, gpointer user_data)
{
    g_autoptr(GTask) task = g_task_new(NULL, cancel, callback, user_data);
    gsize path_size = strlen(path) + 1;
    OutputRead *request = g_malloc(sizeof *request + path_size);
    request->limit = limit;
    memcpy(request->path, path, path_size);
    g_task_set_task_data(task, request, g_free);
    g_task_run_in_thread(task, read_output_thread);
}

GBytes *worker_output_read_finish(GAsyncResult *result, GError **error)
{
    return g_task_propagate_pointer(G_TASK(result), error);
}
