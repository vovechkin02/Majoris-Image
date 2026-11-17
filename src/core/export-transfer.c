#define _GNU_SOURCE

#include "export-transfer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "majoris-image.h"

#define EXPORT_CHUNK_BYTES (256 * 1024)
#define MAX_DESTINATION_SYMLINKS 40
#define ACCESS_ACL "system.posix_acl_access"

struct _ExportSource {
    char *path;
    int fd;
    struct stat identity;
};

typedef struct {
    char *path;
    GFile *destination;
    const ExportSource *source;
    gint64 limit;
} ExportTransfer;

typedef struct {
    struct stat directory;
    struct stat entry;
} DestinationLink;

typedef struct {
    int directory;
    int file;
    char *name;
    struct stat parent;
    struct stat existing;
    GArray *links;
} Destination;

typedef struct {
    char *source;
    char *destination;
} ExportPreparation;

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

static gboolean same_inode(const struct stat *first, const struct stat *second)
{
    return first->st_dev == second->st_dev && first->st_ino == second->st_ino;
}

static gboolean source_metadata_matches(const struct stat *initial, const struct stat *current)
{
    return S_ISREG(current->st_mode) && same_inode(initial, current) &&
        initial->st_size == current->st_size &&
        initial->st_mtim.tv_sec == current->st_mtim.tv_sec &&
        initial->st_mtim.tv_nsec == current->st_mtim.tv_nsec &&
        initial->st_ctim.tv_sec == current->st_ctim.tv_sec &&
        initial->st_ctim.tv_nsec == current->st_ctim.tv_nsec;
}

static gboolean validate_source(const ExportSource *source, GError **error)
{
    if (source == NULL) {
        return TRUE;
    }

    struct stat pinned;
    gint64 started = g_get_monotonic_time();
    if (fstat(source->fd, &pinned) < 0) {
        set_io_error(error, "Cannot recheck pinned image source", errno);
        return FALSE;
    }
    gboolean pinned_matches = source_metadata_matches(&source->identity, &pinned);
    record_phase("export-source-descriptor", started, 1);

    struct stat named;
    started = g_get_monotonic_time();
    if (stat(source->path, &named) < 0) {
        int saved_errno = errno;
        if (saved_errno == ENOENT || saved_errno == ENOTDIR || saved_errno == ELOOP) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY,
                        "Image source path changed during export: %s", g_strerror(saved_errno));
        } else {
            set_io_error(error, "Cannot recheck image source path", saved_errno);
        }
        return FALSE;
    }
    gboolean named_matches = source_metadata_matches(&source->identity, &named);
    record_phase("export-source-path", started, 1);

    if (!pinned_matches || !named_matches) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "Image source changed during export");
        return FALSE;
    }
    return TRUE;
}

static gboolean same_entry(const struct stat *first, const struct stat *second)
{
    return same_inode(first, second) &&
        first->st_size == second->st_size && first->st_mode == second->st_mode &&
        first->st_uid == second->st_uid && first->st_gid == second->st_gid &&
        first->st_nlink == second->st_nlink &&
        first->st_mtim.tv_sec == second->st_mtim.tv_sec &&
        first->st_mtim.tv_nsec == second->st_mtim.tv_nsec &&
        first->st_ctim.tv_sec == second->st_ctim.tv_sec &&
        first->st_ctim.tv_nsec == second->st_ctim.tv_nsec;
}

static void destination_changed(GError **error)
{
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY,
                        "Export destination changed during transfer");
}

static gboolean check_source_alias(const ExportSource *source, const struct stat *entry,
                                   GError **error)
{
    if (source != NULL && same_inode(&source->identity, entry)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Choose a separate destination for image export");
        return FALSE;
    }
    return TRUE;
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

static void clear_destination(Destination *destination)
{
    if (destination->file >= 0) {
        close(destination->file);
    }
    if (destination->directory >= 0) {
        close(destination->directory);
    }
    g_free(destination->name);
    g_clear_pointer(&destination->links, g_array_unref);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(Destination, clear_destination)

/* Follow final links, including a dangling link, as GIO does with FileCreateFlags.NONE.
 * Read each link via its descriptor so its identity and contents describe the same inode. */
static gboolean resolve_destination(const char *path, Destination *destination, GError **error)
{
    g_autofree char *target = g_strdup(path);
    for (guint links = 0; links <= MAX_DESTINATION_SYMLINKS; links++) {
        g_autofree char *parent = g_path_get_dirname(target);
        g_autofree char *name = g_path_get_basename(target);
        if (g_str_has_suffix(target, "/") || g_str_equal(name, ".") || g_str_equal(name, "..")) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY,
                                "Export destination must be a regular file");
            return FALSE;
        }

        destination->directory = open(parent, O_PATH | O_DIRECTORY | O_CLOEXEC);
        if (destination->directory < 0 || fstat(destination->directory, &destination->parent) < 0) {
            set_io_error(error, "Cannot inspect export destination directory", errno);
            return FALSE;
        }
        destination->file = openat(destination->directory, name, O_PATH | O_NOFOLLOW | O_CLOEXEC);
        if (destination->file < 0) {
            if (errno == ENOENT) {
                destination->name = g_steal_pointer(&name);
                return TRUE;
            }
            set_io_error(error, "Cannot open export destination", errno);
            return FALSE;
        }
        struct stat info;
        if (fstat(destination->file, &info) < 0) {
            set_io_error(error, "Cannot inspect export destination", errno);
            return FALSE;
        }
        if (S_ISREG(info.st_mode)) {
            destination->existing = info;
            destination->name = g_steal_pointer(&name);
            return TRUE;
        }
        if (!S_ISLNK(info.st_mode)) {
            g_set_error_literal(error, G_IO_ERROR,
                                S_ISDIR(info.st_mode) ? G_IO_ERROR_IS_DIRECTORY
                                                      : G_IO_ERROR_NOT_REGULAR_FILE,
                                "Export destination must be a regular file");
            return FALSE;
        }
        if (links == MAX_DESTINATION_SYMLINKS) {
            break;
        }

        char link[PATH_MAX + 1];
        ssize_t length = readlinkat(destination->file, "", link, sizeof link);
        if (length < 0 || (gsize)length == sizeof link) {
            set_io_error(error, "Cannot resolve export destination link",
                         length < 0 ? errno : ENAMETOOLONG);
            return FALSE;
        }
        link[length] = '\0';
        if (destination->links == NULL) {
            destination->links = g_array_new(FALSE, FALSE, sizeof(DestinationLink));
        }
        DestinationLink entry = {destination->parent, info};
        g_array_append_val(destination->links, entry);
        close(destination->file);
        close(destination->directory);
        destination->file = -1;
        destination->directory = -1;
        g_free(target);
        target = g_path_is_absolute(link) ? g_strdup(link) : g_strconcat(parent, "/", link, NULL);
    }
    set_io_error(error, "Cannot resolve export destination", ELOOP);
    return FALSE;
}

static gboolean same_links(const GArray *first, const GArray *second)
{
    if (first == NULL || second == NULL) {
        return first == second;
    }
    if (first->len != second->len) {
        return FALSE;
    }
    for (guint index = 0; index < first->len; index++) {
        const DestinationLink *before = &g_array_index(first, DestinationLink, index);
        const DestinationLink *after = &g_array_index(second, DestinationLink, index);
        if (!same_inode(&before->directory, &after->directory) ||
            !same_entry(&before->entry, &after->entry)) {
            return FALSE;
        }
    }
    return TRUE;
}

static gboolean validate_destination(const char *path, const Destination *initial,
                                     const ExportSource *source, GError **error)
{
    g_auto(Destination) current = {.directory = -1, .file = -1};
    if (!resolve_destination(path, &current, NULL)) {
        destination_changed(error);
        return FALSE;
    }
    if (current.file >= 0 && !check_source_alias(source, &current.existing, error)) {
        return FALSE;
    }
    if (!same_inode(&initial->parent, &current.parent) ||
        !g_str_equal(initial->name, current.name) || !same_links(initial->links, current.links) ||
        (initial->file < 0) != (current.file < 0)) {
        destination_changed(error);
        return FALSE;
    }

    /* Check the actual rename parent and name after rewalking the caller's path. */
    struct stat entry;
    if (fstatat(initial->directory, initial->name, &entry, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!check_source_alias(source, &entry, error)) {
            return FALSE;
        }
        if (initial->file >= 0 && same_entry(&initial->existing, &current.existing) &&
            same_entry(&initial->existing, &entry)) {
            return validate_source(source, error);
        }
    } else if (errno == ENOENT && initial->file < 0) {
        return validate_source(source, error);
    }
    destination_changed(error);
    return FALSE;
}

static gboolean preserve_permissions(int output, const Destination *destination, GError **error)
{
    const struct stat *existing = &destination->existing;
    struct stat staging;
    if (fstat(output, &staging) < 0) {
        set_io_error(error, "Cannot inspect export staging file", errno);
        return FALSE;
    }
    uid_t owner = staging.st_uid == existing->st_uid ? (uid_t)-1 : existing->st_uid;
    gid_t group = staging.st_gid == existing->st_gid ? (gid_t)-1 : existing->st_gid;
    if ((owner != (uid_t)-1 || group != (gid_t)-1) && fchown(output, owner, group) < 0) {
        set_io_error(error, "Cannot preserve export destination ownership", errno);
        return FALSE;
    }

    /* The access ACL and ordinary mode bits describe the same access policy.
     * Read only this attribute through the pinned inode. Linux bounds xattr sizes. */
    g_autofree char *handle = g_strdup_printf("/proc/self/fd/%d", destination->file);
    ssize_t size = getxattr(handle, ACCESS_ACL, NULL, 0);
    if (size < 0 && errno != ENODATA && errno != ENOTSUP) {
        set_io_error(error, "Cannot inspect export destination ACL", errno);
        return FALSE;
    }
    if (size > 0) {
        g_autofree guint8 *acl = g_malloc(size);
        ssize_t length = getxattr(handle, ACCESS_ACL, acl, size);
        if (length < 0) {
            set_io_error(error, "Cannot read export destination ACL", errno);
            return FALSE;
        }
        if (length != size) {
            destination_changed(error);
            return FALSE;
        }
        if (fsetxattr(output, ACCESS_ACL, acl, length, 0) < 0) {
            set_io_error(error, "Cannot preserve export destination ACL", errno);
            return FALSE;
        }
    } else if (fremovexattr(output, ACCESS_ACL) < 0 && errno != ENODATA && errno != ENOTSUP) {
        set_io_error(error, "Cannot remove inherited export staging ACL", errno);
        return FALSE;
    }
    if (fchmod(output, existing->st_mode & 0777) < 0) {
        set_io_error(error, "Cannot preserve export destination mode", errno);
        return FALSE;
    }
    return TRUE;
}

static gboolean finalize_staging(int output, const Destination *destination, GError **error)
{
    if (destination->file >= 0 && !preserve_permissions(output, destination, error)) {
        return FALSE;
    }
    if (fsync(output) < 0) {
        set_io_error(error, "Cannot sync export staging file", errno);
        return FALSE;
    }
    return TRUE;
}

static void remove_staging_directory(int parent, const char *name, const struct stat *created)
{
    struct stat current;
    if (fstatat(parent, name, &current, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISDIR(current.st_mode) && same_inode(created, &current) &&
        unlinkat(parent, name, AT_REMOVEDIR) < 0) {
        g_debug("Cannot remove export staging directory: %s", g_strerror(errno));
    }
}

static gboolean copy_and_publish(int *input, const struct stat *initial, const char *path,
                                 const Destination *destination, const ExportSource *source,
                                 int staging_directory, GCancellable *cancel, GError **error)
{
    int output = openat(staging_directory, "output", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (output < 0) {
        set_io_error(error, "Cannot create export staging file", errno);
        return FALSE;
    }

    gboolean committed = FALSE;
    gint64 copy_started = g_get_monotonic_time();
    if (transfer_output(*input, initial, output, NULL, cancel, error) &&
        close_file(input, "Cannot close encoded output", error)) {
        record_phase("export-copy", copy_started, initial->st_size);
        gint64 commit_started = g_get_monotonic_time();
        if (!g_cancellable_set_error_if_cancelled(cancel, error) &&
            finalize_staging(output, destination, error) &&
            close_file(&output, "Cannot close export staging file", error) &&
            !g_cancellable_set_error_if_cancelled(cancel, error) &&
            validate_destination(path, destination, source, error) &&
            !g_cancellable_set_error_if_cancelled(cancel, error)) {
            /* NOREPLACE checks absence atomically. Linux has no equivalent
             * rename that requires an existing destination to match an inode. */
            int result = destination->file < 0
                ? renameat2(staging_directory, "output", destination->directory, destination->name,
                            RENAME_NOREPLACE)
                : renameat(staging_directory, "output", destination->directory, destination->name);
            if (result == 0) {
                committed = TRUE;
            } else {
                set_io_error(error, "Cannot publish export destination", errno);
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
    return committed;
}

static gboolean replace_native(int *input, const struct stat *initial, const char *path,
                               const ExportSource *source, GCancellable *cancel, GError **error)
{
    g_auto(Destination) destination = {.directory = -1, .file = -1};
    if (!resolve_destination(path, &destination, error)) {
        return FALSE;
    }
    if (destination.file >= 0) {
        if (!check_source_alias(source, &destination.existing, error)) {
            return FALSE;
        }
        if (faccessat(destination.file, "", W_OK, AT_EMPTY_PATH | AT_EACCESS) < 0) {
            set_io_error(error, "Cannot write export destination", errno);
            return FALSE;
        }
    }

    /* Keep staging, publication and cleanup anchored to the validated directory. */
    int directory = destination.directory;
    g_autofree char *staging =
        g_strdup_printf("/proc/self/fd/%d/.majoris-export-XXXXXX", directory);
    if (g_mkdtemp(staging) == NULL) {
        set_io_error(error, "Cannot create export staging directory", errno);
        return FALSE;
    }

    const char *name = strrchr(staging, '/') + 1;
    struct stat created;
    if (fstatat(directory, name, &created, AT_SYMLINK_NOFOLLOW) < 0) {
        /* Without the created identity, deleting by name could remove a substitute. */
        set_io_error(error, "Cannot identify newly created export staging directory", errno);
        return FALSE;
    }
    int staging_directory = openat(directory, name, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat opened;
    gboolean committed = FALSE;
    if (staging_directory < 0 || fstat(staging_directory, &opened) < 0) {
        set_io_error(error, "Cannot inspect export staging directory", errno);
    } else if (!same_inode(&created, &opened) || opened.st_uid != geteuid() ||
               (opened.st_mode & 0077) != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "Export staging directory is not private");
    } else {
        committed = copy_and_publish(input, initial, path, &destination, source,
                                     staging_directory, cancel, error);
    }

    remove_staging_directory(directory, name, &created);
    if (staging_directory >= 0) {
        close(staging_directory);
    }
    return committed;
}

void image_export_source_free(ExportSource *source)
{
    if (source == NULL) {
        return;
    }
    if (source->fd >= 0) {
        close(source->fd);
    }
    g_free(source->path);
    g_free(source);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(ExportSource, image_export_source_free)

int image_export_source_dup(const ExportSource *source, GError **error)
{
    gint64 started = g_get_monotonic_time();
    int fd = fcntl(source->fd, F_DUPFD_CLOEXEC, 3);
    if (fd < 0) {
        set_io_error(error, "Cannot duplicate image source descriptor", errno);
    }
    record_phase("export-source-duplicate", started, 1);
    return fd;
}

static void release_preparation(gpointer data)
{
    ExportPreparation *preparation = data;
    g_free(preparation->source);
    g_free(preparation->destination);
    g_free(preparation);
}

static void run_preparation(GTask *task, gpointer object, gpointer data, GCancellable *cancel)
{
    (void)object;
    (void)cancel;
    const ExportPreparation *preparation = data;
    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    if (preparation->source == NULL) {
        g_task_return_pointer(task, NULL, NULL);
        return;
    }

    g_autoptr(ExportSource) source = g_new0(ExportSource, 1);
    source->path = g_strdup(preparation->source);
    g_autoptr(GError) error = NULL;
    gint64 started = g_get_monotonic_time();
    source->fd = open(source->path, O_PATH | O_CLOEXEC);
    if (source->fd < 0 || fstat(source->fd, &source->identity) < 0) {
        set_io_error(&error, "Cannot inspect image source", errno);
        g_task_return_error(task, g_steal_pointer(&error));
        return;
    }
    record_phase("export-source-capture", started, 1);
    if (!S_ISREG(source->identity.st_mode)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "The image source must be a regular file");
        return;
    }
    if (preparation->destination != NULL) {
        g_auto(Destination) destination = {.directory = -1, .file = -1};
        if (!resolve_destination(preparation->destination, &destination, &error) ||
            (destination.file >= 0 &&
             !check_source_alias(source, &destination.existing, &error))) {
            g_task_return_error(task, g_steal_pointer(&error));
            return;
        }
    }
    g_task_return_pointer(task, g_steal_pointer(&source),
                          (GDestroyNotify)image_export_source_free);
}

void image_export_prepare_async(GFile *source, GFile *destination, GCancellable *cancel,
                                GAsyncReadyCallback callback, gpointer user_data)
{
    g_autoptr(GTask) task = g_task_new(NULL, cancel, callback, user_data);
    ExportPreparation *preparation = g_new(ExportPreparation, 1);
    gboolean native_source = g_file_is_native(source);
    gboolean native_destination = g_file_is_native(destination);
    preparation->source = native_source ? g_file_get_path(source) : NULL;
    preparation->destination = native_destination ? g_file_get_path(destination) : NULL;
    g_task_set_task_data(task, preparation, release_preparation);
    if (native_source && preparation->source == NULL) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Cannot obtain the native image source path");
        return;
    }
    if (native_source && native_destination && preparation->destination == NULL) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Cannot obtain the native export destination path");
        return;
    }
    g_task_run_in_thread(task, run_preparation);
}

ExportSource *image_export_prepare_finish(GAsyncResult *result, GError **error)
{
    return g_task_propagate_pointer(G_TASK(result), error);
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
            replace_native(&input, &initial, path, transfer->source, cancel, &error);
        } else {
            g_autofree guint8 *bytes = g_malloc(initial.st_size);
            if (transfer_output(input, &initial, -1, bytes, cancel, &error) &&
                close_file(&input, "Cannot close encoded output", &error) &&
                !g_cancellable_set_error_if_cancelled(cancel, &error) &&
                validate_source(transfer->source, &error)) {
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

void image_export_transfer_async(const char *path, GFile *destination,
                                 const ExportSource *source, gint64 limit,
                                 GCancellable *cancel, GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    g_autoptr(GTask) task = g_task_new(NULL, cancel, callback, user_data);
    ExportTransfer *transfer = g_new(ExportTransfer, 1);
    transfer->path = g_strdup(path);
    transfer->destination = g_object_ref(destination);
    transfer->source = source;
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
