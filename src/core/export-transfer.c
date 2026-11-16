#define _GNU_SOURCE

#include "export-transfer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "majoris-image.h"

#define EXPORT_CHUNK_BYTES (256 * 1024)
#define MAX_DESTINATION_SYMLINKS 40

typedef struct {
    char *path;
    GFile *destination;
    gint64 limit;
} ExportTransfer;

static void record_phase(const char *phase, gint64 started, guint64 count)
{
    if (g_strcmp0(g_getenv("MAJORIS_IMAGE_PROFILE"), "1") == 0) {
        g_printerr("ImageKit-profile %s %" G_GINT64_FORMAT " %" G_GUINT64_FORMAT "\n",
                   phase, g_get_monotonic_time() - started, count);
    }
}

static void release_transfer(gpointer data)
{
    ExportTransfer *transfer = data;
    g_free(transfer->path);
    g_object_unref(transfer->destination);
    g_free(transfer);
}

static void set_io_error(GError **error, const char *operation, int saved_errno)
{
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno), "%s: %s",
                operation, g_strerror(saved_errno));
}

static void invalid_output(GError **error, const char *message)
{
    g_set_error_literal(error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE,
                        message);
}

static int open_output(const char *path, gint64 limit, struct stat *initial, GError **error)
{
    g_autofree char *directory = g_path_get_dirname(path);
    g_autofree char *name = g_path_get_basename(path);
    int parent = open(directory, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int handle = parent < 0 ? -1 : openat(parent, name, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    int saved_errno = errno;
    if (parent >= 0) {
        close(parent);
    }
    if (handle < 0) {
        if (saved_errno == ENOENT) {
            invalid_output(error, "Decoder did not produce an output file");
        } else if (saved_errno == ENOTDIR || saved_errno == ELOOP) {
            invalid_output(error, "Invalid decoder response path");
        } else {
            set_io_error(error, "Cannot inspect encoded output", saved_errno);
        }
        return -1;
    }

    int input = -1;
    if (fstat(handle, initial) < 0) {
        set_io_error(error, "Cannot inspect encoded output", errno);
    } else if (!S_ISREG(initial->st_mode) || initial->st_size < 1 || initial->st_size > limit) {
        invalid_output(error, "Invalid decoder response type or size");
    } else {
        /* O_PATH classifies even devices and FIFOs without opening them for I/O.
         * This procfs link reopens that inode regardless of pathname changes. */
        g_autofree char *fd_path = g_strdup_printf("/proc/self/fd/%d", handle);
        input = open(fd_path, O_RDONLY | O_CLOEXEC);
        if (input < 0) {
            set_io_error(error, "Cannot open encoded output", errno);
        }
    }
    close(handle);
    return input;
}

static gboolean write_chunk(int output, const guint8 *buffer, gsize length,
                            GCancellable *cancel, GError **error)
{
    while (length > 0) {
        if (g_cancellable_set_error_if_cancelled(cancel, error)) {
            return FALSE;
        }
        ssize_t written = write(output, buffer, length);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            set_io_error(error, "Cannot write export staging file", written < 0 ? errno : EIO);
            return FALSE;
        }
        buffer += written;
        length -= written;
    }
    return TRUE;
}

static gboolean transfer_output(int input, const struct stat *initial, int output,
                                guint8 *payload, GCancellable *cancel, GError **error)
{
    const gsize size = initial->st_size;
    g_autofree guint8 *buffer = payload == NULL ? g_malloc(MIN(size, EXPORT_CHUNK_BYTES)) : NULL;
    gsize total = 0;
    while (TRUE) {
        if (g_cancellable_set_error_if_cancelled(cancel, error)) {
            return FALSE;
        }
        guint8 extra;
        guint8 *chunk = total == size ? &extra : payload != NULL ? payload + total : buffer;
        gsize capacity = total == size ? 1 : MIN(size - total, EXPORT_CHUNK_BYTES);
        ssize_t length = read(input, chunk, capacity);
        if (length < 0 && errno == EINTR) {
            continue;
        }
        if (length < 0) {
            set_io_error(error, "Cannot read encoded output", errno);
            return FALSE;
        }
        if (length == 0) {
            break;
        }
        /* The validated initial size is at most the limit. Never write beyond it. */
        if ((gsize)length > size - total) {
            invalid_output(error, "Encoded output grew during transfer");
            return FALSE;
        }
        if (output >= 0 && !write_chunk(output, chunk, length, cancel, error)) {
            return FALSE;
        }
        total += length;
    }

    struct stat final;
    if (fstat(input, &final) < 0) {
        set_io_error(error, "Cannot verify encoded output", errno);
        return FALSE;
    }
    if (total != size || final.st_size != initial->st_size ||
        final.st_mtim.tv_sec != initial->st_mtim.tv_sec ||
        final.st_mtim.tv_nsec != initial->st_mtim.tv_nsec ||
        final.st_ctim.tv_sec != initial->st_ctim.tv_sec ||
        final.st_ctim.tv_nsec != initial->st_ctim.tv_nsec) {
        invalid_output(error, "Encoded output changed during transfer");
        return FALSE;
    }
    return TRUE;
}

static gboolean close_file(int *fd, const char *operation, GError **error)
{
    int closing = *fd;
    *fd = -1;
    if (!g_close(closing, NULL)) {
        set_io_error(error, operation, errno);
        return FALSE;
    }
    return TRUE;
}

/* FileCreateFlags.NONE follows a final destination symlink, including a dangling one. */
static gboolean resolve_destination(char **path, struct stat *info, gboolean *exists,
                                    GError **error)
{
    for (guint links = 0; links <= MAX_DESTINATION_SYMLINKS; links++) {
        if (lstat(*path, info) < 0) {
            if (errno == ENOENT) {
                *exists = FALSE;
                return TRUE;
            }
            set_io_error(error, "Cannot inspect export destination", errno);
            return FALSE;
        }
        if (!S_ISLNK(info->st_mode)) {
            if (!S_ISREG(info->st_mode)) {
                g_set_error_literal(error, G_IO_ERROR,
                                    S_ISDIR(info->st_mode) ? G_IO_ERROR_IS_DIRECTORY
                                                          : G_IO_ERROR_NOT_REGULAR_FILE,
                                    "Export destination must be a regular file");
                return FALSE;
            }
            if (faccessat(AT_FDCWD, *path, W_OK, AT_EACCESS) < 0) {
                set_io_error(error, "Cannot write export destination", errno);
                return FALSE;
            }
            *exists = TRUE;
            return TRUE;
        }
        if (links == MAX_DESTINATION_SYMLINKS) {
            break;
        }
        g_autoptr(GError) link_error = NULL;
        g_autofree char *target = g_file_read_link(*path, &link_error);
        if (target == NULL) {
            GIOErrorEnum code = G_IO_ERROR_FAILED;
            if (g_error_matches(link_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
                code = G_IO_ERROR_NOT_FOUND;
            } else if (g_error_matches(link_error, G_FILE_ERROR, G_FILE_ERROR_ACCES)) {
                code = G_IO_ERROR_PERMISSION_DENIED;
            }
            g_set_error_literal(error, G_IO_ERROR, code, "Cannot resolve export destination link");
            return FALSE;
        }
        g_autofree char *directory = g_path_get_dirname(*path);
        g_free(*path);
        *path = g_path_is_absolute(target) ? g_steal_pointer(&target)
                                          : g_build_filename(directory, target, NULL);
    }
    set_io_error(error, "Cannot resolve export destination", ELOOP);
    return FALSE;
}

static gboolean finalize_staging(int output, const struct stat *existing, GError **error)
{
    if (existing != NULL) {
        struct stat staging;
        if (fstat(output, &staging) < 0 ||
            ((staging.st_uid != existing->st_uid || staging.st_gid != existing->st_gid) &&
             fchown(output, existing->st_uid, existing->st_gid) < 0) ||
            fchmod(output, existing->st_mode & 0777) < 0) {
            set_io_error(error, "Cannot preserve export destination permissions", errno);
            return FALSE;
        }
    }
    if (fsync(output) < 0) {
        set_io_error(error, "Cannot sync export staging file", errno);
        return FALSE;
    }
    return TRUE;
}

static gboolean replace_native(int *input, const struct stat *initial, const char *path,
                               GCancellable *cancel, GError **error)
{
    struct stat existing;
    gboolean exists;
    g_autofree char *target = g_strdup(path);
    if (!resolve_destination(&target, &existing, &exists, error)) {
        return FALSE;
    }
    g_autofree char *parent = g_path_get_dirname(target);
    g_autofree char *name = g_path_get_basename(target);
    int directory = open(parent, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) {
        set_io_error(error, "Cannot open export destination directory", errno);
        return FALSE;
    }

    /* Anchor staging, commit and cleanup to the same directory. The private
     * directory protects the staged bytes even when the final mode is public. */
    g_autofree char *staging =
        g_strdup_printf("/proc/self/fd/%d/.majoris-export-XXXXXX", directory);
    if (g_mkdtemp(staging) == NULL) {
        set_io_error(error, "Cannot create export staging directory", errno);
        close(directory);
        return FALSE;
    }

    int staging_directory = open(staging, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat staging_info;
    if (staging_directory < 0 || fstat(staging_directory, &staging_info) < 0) {
        set_io_error(error, "Cannot open export staging directory", errno);
        if (staging_directory >= 0) {
            close(staging_directory);
        }
        g_rmdir(staging);
        close(directory);
        return FALSE;
    }
    if (staging_info.st_uid != geteuid() || (staging_info.st_mode & 0077) != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "Export staging directory is not private");
        close(staging_directory);
        close(directory);
        return FALSE;
    }

    int output = openat(staging_directory, "output", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    gboolean committed = FALSE;
    gint64 copy_started = g_get_monotonic_time();
    if (output < 0) {
        set_io_error(error, "Cannot create export staging file", errno);
    } else if (transfer_output(*input, initial, output, NULL, cancel, error) &&
               close_file(input, "Cannot close encoded output", error)) {
        record_phase("export-copy", copy_started, initial->st_size);
        gint64 commit_started = g_get_monotonic_time();
        if (!g_cancellable_set_error_if_cancelled(cancel, error) &&
            finalize_staging(output, exists ? &existing : NULL, error) &&
            close_file(&output, "Cannot close export staging file", error) &&
            !g_cancellable_set_error_if_cancelled(cancel, error)) {
            if (renameat(staging_directory, "output", directory, name) == 0) {
                committed = TRUE;
            } else {
                set_io_error(error, "Cannot replace export destination", errno);
            }
        }
        record_phase("export-commit", commit_started, committed ? initial->st_size : 0);
    }

    if (output >= 0) {
        close(output);
    }
    if (!committed && unlinkat(staging_directory, "output", 0) < 0 && errno != ENOENT) {
        g_debug("Cannot unlink export staging file: %s", g_strerror(errno));
    }
    struct stat current;
    if (lstat(staging, &current) == 0 && current.st_dev == staging_info.st_dev &&
        current.st_ino == staging_info.st_ino && g_rmdir(staging) < 0) {
        g_debug("Cannot remove export staging directory: %s", g_strerror(errno));
    }
    close(staging_directory);
    close(directory);
    return committed;
}

static void run_transfer(GTask *task, gpointer source, gpointer task_data, GCancellable *cancel)
{
    (void)source;
    ExportTransfer *transfer = task_data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) payload = NULL;
    if (g_cancellable_set_error_if_cancelled(cancel, &error)) {
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }

    struct stat initial;
    int input = open_output(transfer->path, transfer->limit, &initial, &error);
    if (input >= 0) {
        g_autofree char *path = g_file_is_native(transfer->destination)
            ? g_file_get_path(transfer->destination) : NULL;
        if (path != NULL) {
            replace_native(&input, &initial, path, cancel, &error);
        } else {
            g_autofree guint8 *bytes = g_malloc(initial.st_size);
            if (transfer_output(input, &initial, -1, bytes, cancel, &error) &&
                close_file(&input, "Cannot close encoded output", &error)) {
                payload = g_bytes_new_take(g_steal_pointer(&bytes), initial.st_size);
            }
        }
        if (input >= 0) {
            close(input);
        }
    }
    if (error != NULL) {
        g_task_return_error(task, g_steal_pointer(&error));
    } else {
        g_task_return_pointer(task, g_steal_pointer(&payload), (GDestroyNotify)g_bytes_unref);
    }
}

void image_export_transfer_async(const char *path, GFile *destination, gint64 limit,
                                 GCancellable *cancel, GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_autoptr(GTask) task = g_task_new(NULL, cancel, callback, user_data);
    ExportTransfer *transfer = g_new(ExportTransfer, 1);
    transfer->path = g_strdup(path);
    transfer->destination = g_object_ref(destination);
    transfer->limit = limit;
    g_task_set_task_data(task, transfer, release_transfer);
    /* A cancellation after the rename must not turn a successful commit into an error. */
    g_task_set_check_cancellable(task, FALSE);
    g_task_run_in_thread(task, run_transfer);
}

GBytes *image_export_transfer_finish(GAsyncResult *result, GError **error)
{
    return g_task_propagate_pointer(G_TASK(result), error);
}
