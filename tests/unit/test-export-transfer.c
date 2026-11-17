#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "export-transfer.h"
#include "majoris-image.h"

#define CHUNK_BYTES (256 * 1024)
#define LIMIT_BYTES (256 * 1024 * 1024)

typedef enum {
    NO_FAULT,
    SHORT_IO,
    READ_ERROR,
    WRITE_ERROR,
    TRUNCATE_INPUT,
    GROW_INPUT,
    MODIFY_INPUT,
    SWAP_INPUT,
    CANCEL_TRANSFER,
    SYNC_ERROR,
    CLOSE_ERROR,
    COMMIT_ERROR,
    CANCEL_BEFORE_COMMIT,
    CANCEL_AFTER_COMMIT,
    STAGING_OPEN_ERROR,
    STAGING_STAT_ERROR,
    STAGING_OWNER_ERROR,
    STAGING_MODE_ERROR,
    STAGING_CREATE_ERROR,
    CHOWN_ERROR,
    CHMOD_ERROR,
    REPLACE_DURING_COPY,
    DELETE_AFTER_SYNC,
    CHANGE_MODE_AFTER_SYNC,
    SYMLINK_AFTER_SYNC,
    RETARGET_BEFORE_VALIDATION,
    CREATE_AFTER_RESOLUTION,
    CREATE_AT_PUBLICATION,
    SOURCE_ALIAS_DURING_COPY,
    SUBSTITUTE_PARENT,
    SUBSTITUTE_STAGING,
    SOURCE_WRITE_DURING_COPY,
    SOURCE_WRITE_AFTER_SYNC,
    SOURCE_CAPTURE_ERROR,
    CANCEL_SOURCE_CAPTURE,
    SOURCE_DESCRIPTOR_ERROR,
    SOURCE_PATH_ERROR,
} TransferFault;

typedef struct {
    char *directory;
    char *input;
    char *output;
    char *source_path;
    ExportSource *source;
    struct stat source_before;
    int source_fd;
    GCancellable *cancel;
    GMainLoop *loop;
    GError *error;
    GBytes *payload;
    gboolean remote;
} TransferFixture;

static TransferFixture *active_fixture;
static TransferFault fault;
static guint reads;
static guint closes;
static int staging_fd;
static int output_fd;

#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS == 64
#define __real_openat __real_openat64
#define __wrap_openat __wrap_openat64
#define __real_fstat __real_fstat64
#define __wrap_fstat __wrap_fstat64
#define __real_stat __real_stat64
#define __wrap_stat __wrap_stat64
#endif

int __real_openat(int fd, const char *path, int flags, ...);
int __wrap_openat(int fd, const char *path, int flags, ...);
int __real_fstat(int fd, struct stat *info);
int __wrap_fstat(int fd, struct stat *info);
int __real_stat(const char *path, struct stat *info);
int __wrap_stat(const char *path, struct stat *info);
int __real_fchown(int fd, uid_t owner, gid_t group);
int __wrap_fchown(int fd, uid_t owner, gid_t group);
int __real_fchmod(int fd, mode_t mode);
int __wrap_fchmod(int fd, mode_t mode);

ssize_t __real_read(int fd, void *buffer, size_t size);
ssize_t __real_write(int fd, const void *buffer, size_t size);
int __real_fsync(int fd);
gboolean __real_g_close(gint fd, GError **error);
int __real_renameat(int old_dir, const char *old_path, int new_dir, const char *new_path);
ssize_t __wrap_read(int fd, void *buffer, size_t size);
ssize_t __wrap_write(int fd, const void *buffer, size_t size);
int __wrap_fsync(int fd);
gboolean __wrap_g_close(gint fd, GError **error);
int __wrap_renameat(int old_dir, const char *old_path, int new_dir, const char *new_path);
int __real_renameat2(int old_dir, const char *old_path, int new_dir, const char *new_path,
                    unsigned int flags);
int __wrap_renameat2(int old_dir, const char *old_path, int new_dir, const char *new_path,
                    unsigned int flags);

static void create_competing_file(void)
{
    g_assert_true(g_file_set_contents(active_fixture->output, "competing export", -1, NULL));
}

static void set_source_mtime(TransferFixture *fixture, time_t seconds_delta)
{
    struct timespec times[] = {fixture->source_before.st_atim, fixture->source_before.st_mtim};
    times[1].tv_sec += seconds_delta;
    g_assert_cmpint(utimensat(AT_FDCWD, fixture->source_path, times, 0), ==, 0);
}

static void overwrite_source(TransferFixture *fixture)
{
    int fd = open(fixture->source_path, O_WRONLY | O_CLOEXEC);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(pwrite(fd, "freshness", 9, 0), ==, 9);
    g_assert_cmpint(close(fd), ==, 0);
}

int __wrap_openat(int fd, const char *path, int flags, ...)
{
    if (g_str_has_prefix(path, ".majoris-export-") && (flags & O_DIRECTORY) != 0) {
        if (fault == CREATE_AFTER_RESOLUTION) {
            create_competing_file();
        } else if (fault == SUBSTITUTE_STAGING) {
            g_assert_cmpint(renameat(fd, path, fd, "moved-staging"), ==, 0);
            g_assert_cmpint(mkdirat(fd, path, 0700), ==, 0);
        } else if (fault == STAGING_OPEN_ERROR) {
            errno = EACCES;
            return -1;
        }
        staging_fd = __real_openat(fd, path, flags);
        return staging_fd;
    }
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        int mode = va_arg(arguments, int);
        va_end(arguments);
        if (fault == STAGING_CREATE_ERROR) {
            errno = ENOSPC;
            return -1;
        }
        output_fd = __real_openat(fd, path, flags, mode);
        return output_fd;
    }
    return __real_openat(fd, path, flags);
}

int __wrap_fstat(int fd, struct stat *info)
{
    if (active_fixture != NULL && fd == active_fixture->source_fd &&
        fault == SOURCE_DESCRIPTOR_ERROR) {
        errno = EIO;
        return -1;
    }
    if (fd == staging_fd && fault == STAGING_STAT_ERROR) {
        errno = EIO;
        return -1;
    }
    int result = __real_fstat(fd, info);
    if (result == 0 && active_fixture != NULL && active_fixture->source_fd < 0 &&
        info->st_dev == active_fixture->source_before.st_dev &&
        info->st_ino == active_fixture->source_before.st_ino) {
        active_fixture->source_fd = fd;
        if (fault == SOURCE_CAPTURE_ERROR) {
            errno = EIO;
            return -1;
        }
        if (fault == CANCEL_SOURCE_CAPTURE) {
            g_cancellable_cancel(active_fixture->cancel);
        }
    }
    if (result == 0 && fd == staging_fd && fault == STAGING_OWNER_ERROR) {
        info->st_uid ^= 1;
    } else if (result == 0 && fd == staging_fd && fault == STAGING_MODE_ERROR) {
        info->st_mode |= 0040;
    } else if (result == 0 && fd == output_fd && fault == CHOWN_ERROR) {
        info->st_gid ^= 1;
    }
    return result;
}

int __wrap_stat(const char *path, struct stat *info)
{
    if (fault == SOURCE_PATH_ERROR && g_strcmp0(path, active_fixture->source_path) == 0) {
        errno = EACCES;
        return -1;
    }
    return __real_stat(path, info);
}

int __wrap_fchown(int fd, uid_t owner, gid_t group)
{
    if (fault == CHOWN_ERROR) {
        errno = EPERM;
        return -1;
    }
    return __real_fchown(fd, owner, group);
}

int __wrap_fchmod(int fd, mode_t mode)
{
    if (fault == CHMOD_ERROR) {
        errno = EPERM;
        return -1;
    }
    return __real_fchmod(fd, mode);
}

ssize_t __wrap_read(int fd, void *buffer, size_t size)
{
    if (++reads == 2) {
        switch (fault) {
        case READ_ERROR:
            errno = EIO;
            return -1;
        case TRUNCATE_INPUT:
            g_assert_cmpint(truncate(active_fixture->input, CHUNK_BYTES), ==, 0);
            break;
        case GROW_INPUT:
            g_assert_cmpint(truncate(active_fixture->input, LIMIT_BYTES + 1), ==, 0);
            break;
        case MODIFY_INPUT: {
            int writable = open(active_fixture->input, O_WRONLY | O_CLOEXEC);
            g_assert_cmpint(writable, >=, 0);
            g_assert_cmpint(pwrite(writable, "changed", 7, 0), ==, 7);
            /* Force a timestamp difference even on filesystems with coarse timestamps. */
            const struct timespec times[] = {{.tv_nsec = UTIME_OMIT}, {.tv_sec = 1}};
            g_assert_cmpint(futimens(writable, times), ==, 0);
            g_assert_cmpint(close(writable), ==, 0);
            break;
        }
        case SWAP_INPUT: {
            g_autofree char *moved = g_strconcat(active_fixture->input, ".moved", NULL);
            g_assert_cmpint(g_rename(active_fixture->input, moved), ==, 0);
            g_assert_cmpint(symlink("/dev/zero", active_fixture->input), ==, 0);
            break;
        }
        case CANCEL_TRANSFER:
            g_cancellable_cancel(active_fixture->cancel);
            break;
        case REPLACE_DURING_COPY:
            create_competing_file();
            break;
        case SOURCE_ALIAS_DURING_COPY:
            g_assert_cmpint(g_remove(active_fixture->output), ==, 0);
            g_assert_cmpint(link(active_fixture->source_path, active_fixture->output), ==, 0);
            break;
        case SOURCE_WRITE_DURING_COPY:
            overwrite_source(active_fixture);
            set_source_mtime(active_fixture, 1);
            break;
        default:
            break;
        }
    }
    return __real_read(fd, buffer, fault == SHORT_IO ? MIN(size, 8191) : size);
}

ssize_t __wrap_write(int fd, const void *buffer, size_t size)
{
    if (fault == WRITE_ERROR && reads > 1) {
        errno = ENOSPC;
        return -1;
    }
    return __real_write(fd, buffer, fault == SHORT_IO ? MIN(size, 4093) : size);
}

int __wrap_fsync(int fd)
{
    if (fault == SYNC_ERROR) {
        errno = EIO;
        return -1;
    }
    int result = __real_fsync(fd);
    if (fault == CANCEL_BEFORE_COMMIT) {
        g_cancellable_cancel(active_fixture->cancel);
    } else if (fault == DELETE_AFTER_SYNC || fault == SYMLINK_AFTER_SYNC) {
        g_assert_cmpint(g_remove(active_fixture->output), ==, 0);
        if (fault == SYMLINK_AFTER_SYNC) {
            g_assert_cmpint(symlink("encoded", active_fixture->output), ==, 0);
        }
    } else if (fault == CHANGE_MODE_AFTER_SYNC) {
        g_assert_cmpint(g_chmod(active_fixture->output, 0641), ==, 0);
    } else if (fault == SOURCE_WRITE_AFTER_SYNC) {
        overwrite_source(active_fixture);
        set_source_mtime(active_fixture, 1);
    }
    return result;
}

gboolean __wrap_g_close(gint fd, GError **error)
{
    gboolean closed = __real_g_close(fd, error);
    if (++closes == 2) {
        if (fault == CLOSE_ERROR) {
            errno = EIO;
            return FALSE;
        }
        if (fault == RETARGET_BEFORE_VALIDATION) {
            g_autofree char *hop = g_build_filename(active_fixture->directory, "hop", NULL);
            g_assert_cmpint(g_remove(hop), ==, 0);
            g_assert_cmpint(symlink("second", hop), ==, 0);
        } else if (fault == SUBSTITUTE_PARENT) {
            g_autofree char *parent = g_path_get_dirname(active_fixture->output);
            g_autofree char *moved = g_build_filename(active_fixture->directory, "moved", NULL);
            g_assert_cmpint(g_rename(parent, moved), ==, 0);
            g_assert_cmpint(g_mkdir(parent, 0700), ==, 0);
            create_competing_file();
        }
    }
    return closed;
}

int __wrap_renameat2(int old_dir, const char *old_path, int new_dir, const char *new_path,
                    unsigned int flags)
{
    if (fault == CREATE_AT_PUBLICATION) {
        create_competing_file();
    }
    int result = __real_renameat2(old_dir, old_path, new_dir, new_path, flags);
    if (fault == CANCEL_AFTER_COMMIT && result == 0) {
        g_cancellable_cancel(active_fixture->cancel);
    }
    return result;
}

int __wrap_renameat(int old_dir, const char *old_path, int new_dir, const char *new_path)
{
    if (fault == COMMIT_ERROR) {
        errno = EACCES;
        return -1;
    }
    int result = __real_renameat(old_dir, old_path, new_dir, new_path);
    if (fault == CANCEL_AFTER_COMMIT && result == 0) {
        g_cancellable_cancel(active_fixture->cancel);
    }
    return result;
}

static void setup_fixture(TransferFixture *fixture, gconstpointer data)
{
    (void)data;
    g_autoptr(GError) error = NULL;
    fixture->directory = g_dir_make_tmp("export-transfer-test-XXXXXX", &error);
    g_assert_no_error(error);
    fixture->input = g_build_filename(fixture->directory, "encoded", NULL);
    fixture->output = g_build_filename(fixture->directory, "destination", NULL);
    fixture->cancel = g_cancellable_new();
    fixture->source_fd = -1;
    fixture->loop = g_main_loop_new(NULL, FALSE);
    fault = NO_FAULT;
    reads = 0;
    closes = 0;
    staging_fd = -1;
    output_fd = -1;
    active_fixture = fixture;
}

static void teardown_fixture(TransferFixture *fixture, gconstpointer data)
{
    (void)data;
    g_autoptr(GDir) directory = g_dir_open(fixture->directory, 0, NULL);
    const char *entry;
    while ((entry = g_dir_read_name(directory)) != NULL) {
        g_assert_false(g_str_has_prefix(entry, ".majoris-export-"));
        g_autofree char *path = g_build_filename(fixture->directory, entry, NULL);
        g_assert_cmpint(g_remove(path), ==, 0);
    }
    g_assert_cmpint(g_rmdir(fixture->directory), ==, 0);
    g_free(fixture->directory);
    g_free(fixture->input);
    g_free(fixture->output);
    g_free(fixture->source_path);
    g_clear_pointer(&fixture->source, image_export_source_free);
    g_clear_error(&fixture->error);
    g_clear_pointer(&fixture->payload, g_bytes_unref);
    g_object_unref(fixture->cancel);
    g_main_loop_unref(fixture->loop);
    active_fixture = NULL;
}

static void transfer_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    TransferFixture *fixture = data;
    fixture->payload = image_export_transfer_finish(result, &fixture->error);
    g_main_loop_quit(fixture->loop);
}

static void preparation_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    TransferFixture *fixture = data;
    fixture->source = image_export_prepare_finish(result, &fixture->error);
    g_main_loop_quit(fixture->loop);
}

static GFile *transfer_destination(TransferFixture *fixture)
{
    return fixture->remote ? g_file_new_for_uri("resource:///export-transfer")
                           : g_file_new_for_path(fixture->output);
}

static void prepare_file(TransferFixture *fixture, GFile *source)
{
    g_autoptr(GFile) destination = transfer_destination(fixture);
    image_export_prepare_async(source, destination, fixture->cancel, preparation_done, fixture);
    g_main_loop_run(fixture->loop);
}

static void prepare_source_at(TransferFixture *fixture, const char *path)
{
    g_autoptr(GFile) source = g_file_new_for_path(path);
    prepare_file(fixture, source);
}

static void prepare_source(TransferFixture *fixture)
{
    prepare_source_at(fixture, fixture->source_path);
}

static void run_transfer(TransferFixture *fixture)
{
    g_autoptr(GFile) target = transfer_destination(fixture);
    image_export_transfer_async(fixture->input, target, fixture->source, LIMIT_BYTES,
                                fixture->cancel, transfer_done, fixture);
    g_main_loop_run(fixture->loop);
    if (!fixture->remote) {
        g_assert_null(fixture->payload);
    }
}

static char *make_pattern(const char *path, gsize size)
{
    g_autofree guint8 *buffer = g_malloc(CHUNK_BYTES);
    g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_SHA256);
    GRand *random = g_rand_new_with_seed(12345);
    FILE *file = g_fopen(path, "wb");
    g_assert_nonnull(file);
    for (gsize remaining = size; remaining > 0;) {
        gsize length = MIN(remaining, CHUNK_BYTES);
        for (gsize index = 0; index < length; index += sizeof(guint32)) {
            guint32 value = g_rand_int(random);
            memcpy(buffer + index, &value, MIN(sizeof value, length - index));
        }
        g_assert_cmpuint(fwrite(buffer, 1, length, file), ==, length);
        g_checksum_update(checksum, buffer, length);
        remaining -= length;
    }
    g_assert_cmpint(fclose(file), ==, 0);
    g_rand_free(random);
    return g_strdup(g_checksum_get_string(checksum));
}

static void assert_checksum(const char *path, const char *expected)
{
    g_autofree guint8 *buffer = g_malloc(CHUNK_BYTES);
    g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_SHA256);
    FILE *file = g_fopen(path, "rb");
    g_assert_nonnull(file);
    gsize length;
    while ((length = fread(buffer, 1, CHUNK_BYTES, file)) > 0) {
        g_checksum_update(checksum, buffer, length);
    }
    g_assert_true(feof(file));
    g_assert_cmpint(fclose(file), ==, 0);
    g_assert_cmpstr(g_checksum_get_string(checksum), ==, expected);
}

static void test_copy(TransferFixture *fixture, gconstpointer data)
{
    gsize size = GPOINTER_TO_UINT(data);
    g_autofree char *expected = make_pattern(fixture->input, size);
    if (size == CHUNK_BYTES) {
        g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
        g_assert_cmpint(g_chmod(fixture->output, 0640), ==, 0);
        fault = SHORT_IO;
    } else if (size == 3 * CHUNK_BYTES + 17) {
        fault = CANCEL_AFTER_COMMIT;
    }
    run_transfer(fixture);
    g_assert_no_error(fixture->error);
    if (fault == CANCEL_AFTER_COMMIT) {
        g_assert_true(g_cancellable_is_cancelled(fixture->cancel));
    }
    assert_checksum(fixture->output, expected);
    if (size == CHUNK_BYTES) {
        GStatBuf info;
        g_assert_cmpint(g_stat(fixture->output, &info), ==, 0);
        g_assert_cmpuint(info.st_mode & 0777, ==, 0640);
    }
}

static void test_destination_link(TransferFixture *fixture, gconstpointer data)
{
    gboolean has_target = GPOINTER_TO_INT(data);
    g_autofree char *target = g_build_filename(fixture->directory, "referent", NULL);
    g_autofree char *expected = make_pattern(fixture->input, 1051);
    if (has_target) {
        g_assert_true(g_file_set_contents(target, "previous export", -1, NULL));
    }
    g_assert_cmpint(symlink("referent", fixture->output), ==, 0);
    run_transfer(fixture);
    g_assert_no_error(fixture->error);
    g_assert_true(g_file_test(fixture->output, G_FILE_TEST_IS_SYMLINK));
    assert_checksum(target, expected);
}

static void test_destination_link_limit(TransferFixture *fixture, gconstpointer data)
{
    guint links = GPOINTER_TO_UINT(data);
    g_autofree char *target = g_build_filename(fixture->directory, "referent", NULL);
    g_autofree char *expected = make_pattern(fixture->input, 1051);
    g_autofree char *name = g_strdup(fixture->output);
    g_assert_true(g_file_set_contents(target, "previous export", -1, NULL));
    for (guint link = 1; link <= links; link++) {
        g_autofree char *next = link == links ? g_strdup(target)
            : g_strdup_printf("%s/link-%u", fixture->directory, link);
        g_assert_cmpint(symlink(next, name), ==, 0);
        g_free(name);
        name = g_steal_pointer(&next);
    }
    run_transfer(fixture);
    if (links == 40) {
        g_assert_no_error(fixture->error);
        assert_checksum(target, expected);
    } else {
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_TOO_MANY_LINKS);
        g_autofree char *contents = NULL;
        g_assert_true(g_file_get_contents(target, &contents, NULL, NULL));
        g_assert_cmpstr(contents, ==, "previous export");
    }
}

static void test_destination_cycle(TransferFixture *fixture, gconstpointer data)
{
    (void)data;
    g_autofree char *expected = make_pattern(fixture->input, 1051);
    g_assert_cmpint(symlink("destination", fixture->output), ==, 0);
    run_transfer(fixture);
    g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_TOO_MANY_LINKS);
    g_assert_true(g_file_test(fixture->output, G_FILE_TEST_IS_SYMLINK));
    assert_checksum(fixture->input, expected);
}

static void assert_same_inode(const struct stat *before, const struct stat *after)
{
    g_assert_cmpuint(after->st_dev, ==, before->st_dev);
    g_assert_cmpuint(after->st_ino, ==, before->st_ino);
}

static void test_source_alias(TransferFixture *fixture, gconstpointer data)
{
    const char *kind = data;
    fixture->source_path = g_build_filename(fixture->directory, "source", NULL);
    g_autofree char *expected = make_pattern(fixture->source_path, 113);
    g_autofree char *parent_link = NULL;
    if (g_str_equal(kind, "hard-link")) {
        g_assert_cmpint(link(fixture->source_path, fixture->output), ==, 0);
    } else if (g_str_equal(kind, "symlink")) {
        g_assert_cmpint(symlink("source", fixture->output), ==, 0);
    } else if (g_str_equal(kind, "chain")) {
        g_autofree char *middle = g_build_filename(fixture->directory, "middle", NULL);
        g_assert_cmpint(symlink("source", middle), ==, 0);
        g_assert_cmpint(symlink("middle", fixture->output), ==, 0);
    } else if (g_str_equal(kind, "parent-link")) {
        parent_link = g_build_filename(fixture->directory, "parent-link", NULL);
        g_assert_cmpint(symlink(".", parent_link), ==, 0);
        g_free(fixture->output);
        fixture->output = g_build_filename(parent_link, "source", NULL);
    } else {
        g_free(fixture->output);
        fixture->output = g_build_filename(fixture->directory, ".", "source", NULL);
    }
    struct stat source_before;
    struct stat destination_before;
    struct stat after;
    g_assert_cmpint(stat(fixture->source_path, &source_before), ==, 0);
    g_assert_cmpint(lstat(fixture->output, &destination_before), ==, 0);
    if (g_str_equal(kind, "relative")) {
        g_autofree char *working_directory = g_get_current_dir();
        g_assert_cmpint(g_chdir(fixture->directory), ==, 0);
        prepare_source_at(fixture, "source");
        g_assert_cmpint(g_chdir(working_directory), ==, 0);
    } else {
        prepare_source(fixture);
    }
    g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_null(fixture->source);
    g_assert_cmpint(stat(fixture->source_path, &after), ==, 0);
    assert_same_inode(&source_before, &after);
    g_assert_cmpint(lstat(fixture->output, &after), ==, 0);
    assert_same_inode(&destination_before, &after);
    assert_checksum(fixture->source_path, expected);
    g_assert_cmpint(stat(fixture->output, &after), ==, 0);
    assert_same_inode(&source_before, &after);
}

static void assert_contents(const char *path, const char *expected)
{
    g_autofree char *contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, expected);
}

static void test_destination_mutation(TransferFixture *fixture, gconstpointer data)
{
    fault = GPOINTER_TO_INT(data);
    g_autofree char *expected = make_pattern(fixture->input, 3 * CHUNK_BYTES);
    g_autofree char *first = g_build_filename(fixture->directory, "first", NULL);
    g_autofree char *second = g_build_filename(fixture->directory, "second", NULL);
    g_autofree char *parent = g_build_filename(fixture->directory, "parent", NULL);
    if (fault == RETARGET_BEFORE_VALIDATION) {
        g_autofree char *hop = g_build_filename(fixture->directory, "hop", NULL);
        g_assert_true(g_file_set_contents(first, "first export", -1, NULL));
        g_assert_true(g_file_set_contents(second, "second export", -1, NULL));
        g_assert_cmpint(symlink("first", hop), ==, 0);
        g_assert_cmpint(symlink("hop", fixture->output), ==, 0);
    } else {
        if (fault == SUBSTITUTE_PARENT) {
            g_assert_cmpint(g_mkdir(parent, 0700), ==, 0);
            g_free(fixture->output);
            fixture->output = g_build_filename(parent, "destination", NULL);
        }
        if (fault != CREATE_AFTER_RESOLUTION && fault != CREATE_AT_PUBLICATION) {
            g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
        }
    }

    struct stat source_before;
    g_autofree char *source_hash = NULL;
    if (fault == SOURCE_ALIAS_DURING_COPY) {
        fixture->source_path = g_build_filename(fixture->directory, "source", NULL);
        source_hash = make_pattern(fixture->source_path, 113);
        g_assert_cmpint(stat(fixture->source_path, &source_before), ==, 0);
        prepare_source(fixture);
        g_assert_no_error(fixture->error);
        g_assert_nonnull(fixture->source);
    }

    run_transfer(fixture);
    switch (fault) {
    case CREATE_AT_PUBLICATION:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_EXISTS);
        assert_contents(fixture->output, "competing export");
        break;
    case REPLACE_DURING_COPY:
    case CREATE_AFTER_RESOLUTION:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_BUSY);
        assert_contents(fixture->output, "competing export");
        break;
    case DELETE_AFTER_SYNC:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_assert_false(g_file_test(fixture->output, G_FILE_TEST_EXISTS));
        break;
    case CHANGE_MODE_AFTER_SYNC: {
        struct stat info;
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_assert_cmpint(stat(fixture->output, &info), ==, 0);
        g_assert_cmpuint(info.st_mode & 0777, ==, 0641);
        assert_contents(fixture->output, "previous export");
        break;
    }
    case SYMLINK_AFTER_SYNC:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_assert_true(g_file_test(fixture->output, G_FILE_TEST_IS_SYMLINK));
        assert_checksum(fixture->output, expected);
        break;
    case RETARGET_BEFORE_VALIDATION:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_BUSY);
        g_assert_true(g_file_test(fixture->output, G_FILE_TEST_IS_SYMLINK));
        assert_contents(fixture->output, "second export");
        assert_contents(first, "first export");
        assert_contents(second, "second export");
        break;
    case SOURCE_ALIAS_DURING_COPY: {
        struct stat after;
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
        g_assert_cmpint(stat(fixture->source_path, &after), ==, 0);
        assert_same_inode(&source_before, &after);
        g_assert_cmpint(stat(fixture->output, &after), ==, 0);
        assert_same_inode(&source_before, &after);
        assert_checksum(fixture->output, source_hash);
        break;
    }
    case SUBSTITUTE_PARENT: {
        g_autofree char *moved = g_build_filename(fixture->directory, "moved", NULL);
        g_autofree char *old = g_build_filename(moved, "destination", NULL);
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_BUSY);
        assert_contents(fixture->output, "competing export");
        assert_contents(old, "previous export");
        g_assert_cmpint(g_remove(old), ==, 0);
        g_assert_cmpint(g_rmdir(moved), ==, 0);
        g_assert_cmpint(g_remove(fixture->output), ==, 0);
        g_assert_cmpint(g_rmdir(parent), ==, 0);
        break;
    }
    default:
        g_assert_not_reached();
    }
}

static void test_replacement_metadata(TransferFixture *fixture, gconstpointer data)
{
    (void)data;
    g_autofree char *other_link = g_build_filename(fixture->directory, "other-link", NULL);
    g_autofree char *expected = make_pattern(fixture->input, 1051);
    g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
    g_assert_cmpint(g_chmod(fixture->output, 07750), ==, 0);
    g_assert_cmpint(link(fixture->output, other_link), ==, 0);
    struct stat before;
    struct stat after;
    g_assert_cmpint(stat(fixture->output, &before), ==, 0);
    run_transfer(fixture);
    g_assert_no_error(fixture->error);
    g_assert_cmpint(stat(fixture->output, &after), ==, 0);
    g_assert_cmpuint(after.st_uid, ==, before.st_uid);
    g_assert_cmpuint(after.st_gid, ==, before.st_gid);
    g_assert_cmpuint(after.st_mode & 07777, ==, 0750);
    g_assert_cmpuint(after.st_ino, !=, before.st_ino);
    g_assert_cmpint(stat(other_link, &after), ==, 0);
    assert_same_inode(&before, &after);
    assert_contents(other_link, "previous export");
    assert_checksum(fixture->output, expected);
}

static void test_preserve_different_group(TransferFixture *fixture, gconstpointer data)
{
    (void)data;
    int count = getgroups(0, NULL);
    g_assert_cmpint(count, >=, 0);
    g_autofree gid_t *groups = g_new(gid_t, count);
    g_assert_cmpint(getgroups(count, groups), ==, count);
    gid_t group = (gid_t)-1;
    for (int index = 0; index < count; index++) {
        if (groups[index] != getegid()) {
            group = groups[index];
            break;
        }
    }
    if (group == (gid_t)-1) {
        g_test_skip("A supplementary group is required to test changing the staged group");
        return;
    }

    g_autofree char *expected = make_pattern(fixture->input, 1051);
    g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
    int result = chown(fixture->output, (uid_t)-1, group);
    if (result < 0 && (errno == EPERM || errno == ENOTSUP)) {
        g_test_skip("The test filesystem does not permit changing to a supplementary group");
        return;
    }
    g_assert_cmpint(result, ==, 0);
    run_transfer(fixture);
    g_assert_no_error(fixture->error);
    struct stat published;
    g_assert_cmpint(stat(fixture->output, &published), ==, 0);
    g_assert_cmpuint(published.st_gid, ==, group);
    assert_checksum(fixture->output, expected);
}

static void test_pinned_source(TransferFixture *fixture, gconstpointer data)
{
    fixture->remote = GPOINTER_TO_INT(data);
    fixture->source_path = g_build_filename(fixture->directory, "source", NULL);
    g_autofree char *original = g_build_filename(fixture->directory, "original", NULL);
    g_autofree char *expected = make_pattern(fixture->source_path, 1051);
    g_autofree char *encoded = make_pattern(fixture->input, 1067);
    g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
    struct stat before;
    g_assert_cmpint(stat(fixture->source_path, &before), ==, 0);
    fixture->source_before = before;
    prepare_source(fixture);
    g_assert_no_error(fixture->error);
    g_assert_cmpint(g_rename(fixture->source_path, original), ==, 0);
    g_assert_true(g_file_set_contents(fixture->source_path, "substitute", -1, NULL));

    int fd = image_export_source_dup(fixture->source, &fixture->error);
    g_assert_no_error(fixture->error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(fcntl(fd, F_GETFD), ==, FD_CLOEXEC);
    g_assert_cmpint(fcntl(fixture->source_fd, F_GETFD), ==, FD_CLOEXEC);
    struct stat pinned;
    g_assert_cmpint(fstat(fd, &pinned), ==, 0);
    assert_same_inode(&before, &pinned);
    g_assert_cmpint(close(fd), ==, 0);
    run_transfer(fixture);
    g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_assert_null(fixture->payload);
    assert_contents(fixture->output, "previous export");
    assert_contents(fixture->source_path, "substitute");
    assert_checksum(original, expected);
    assert_checksum(fixture->input, encoded);
    g_clear_pointer(&fixture->source, image_export_source_free);
    g_assert_cmpint(fcntl(fixture->source_fd, F_GETFD), ==, -1);
    g_assert_cmpint(errno, ==, EBADF);
}

static void test_skip_non_native_source(TransferFixture *fixture, gconstpointer data)
{
    fixture->remote = GPOINTER_TO_INT(data);
    g_autoptr(GFile) source = g_file_new_for_uri("resource:///export-source");
    g_assert_false(g_file_is_native(source));
    prepare_file(fixture, source);
    g_assert_no_error(fixture->error);
    g_assert_null(fixture->source);
}

static void test_invalid_source(TransferFixture *fixture, gconstpointer data)
{
    const char *scenario = data;
    fixture->remote = TRUE;
    gboolean directory = g_str_equal(scenario, "directory");
    fixture->source_path = directory ? g_strdup(fixture->directory)
                                     : g_build_filename(fixture->directory, "source", NULL);
    int code = G_IO_ERROR_INVALID_ARGUMENT;
    if (g_str_equal(scenario, "missing")) {
        code = G_IO_ERROR_NOT_FOUND;
    } else if (g_str_equal(scenario, "fifo")) {
        g_assert_cmpint(mkfifo(fixture->source_path, 0600), ==, 0);
    } else if (!directory) {
        g_assert_true(g_file_set_contents(fixture->source_path, "source", -1, NULL));
        g_assert_cmpint(stat(fixture->source_path, &fixture->source_before), ==, 0);
        gboolean canceled = g_str_equal(scenario, "canceled");
        fault = canceled ? CANCEL_SOURCE_CAPTURE : SOURCE_CAPTURE_ERROR;
        code = canceled ? G_IO_ERROR_CANCELLED : g_io_error_from_errno(EIO);
    }
    prepare_source(fixture);
    g_assert_error(fixture->error, G_IO_ERROR, code);
    g_assert_null(fixture->source);
    if (fixture->source_fd >= 0) {
        g_assert_cmpint(fcntl(fixture->source_fd, F_GETFD), ==, -1);
        g_assert_cmpint(errno, ==, EBADF);
    }
}

static void test_source_freshness(TransferFixture *fixture, gconstpointer data)
{
    const char *scenario = data;
    fixture->remote = g_str_has_prefix(scenario, "remote-");
    if (fixture->remote) {
        scenario += strlen("remote-");
    }
    fixture->source_path = g_build_filename(fixture->directory, "source", NULL);
    g_autofree char *source_hash = make_pattern(fixture->source_path, 113);
    g_autofree char *output_hash = make_pattern(fixture->input, CHUNK_BYTES * 2);
    gboolean new_destination = g_str_equal(scenario, "during-copy");
    struct stat destination_before;
    if (!new_destination) {
        g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
        g_assert_cmpint(stat(fixture->output, &destination_before), ==, 0);
    }

    /* Make a subsequent read eligible to update atime even on a relatime mount. */
    const struct timespec times[] = {{1, 0}, {1000000000, 0}};
    g_assert_cmpint(utimensat(AT_FDCWD, fixture->source_path, times, 0), ==, 0);
    g_assert_cmpint(stat(fixture->source_path, &fixture->source_before), ==, 0);
    prepare_source(fixture);
    g_assert_no_error(fixture->error);
    g_assert_cmpint(fixture->source_fd, >=, 0);

    gboolean atime_only = g_str_equal(scenario, "read");
    gboolean deleted = g_str_equal(scenario, "deleted");
    gboolean renamed = g_str_equal(scenario, "renamed");
    gboolean canceled = g_str_equal(scenario, "canceled");
    gboolean invalid_path = g_str_equal(scenario, "invalid-path");
    gboolean restored_mtime = g_str_equal(scenario, "restored-mtime");
    gboolean changed_bytes = FALSE;
    if (g_str_equal(scenario, "grow") || g_str_equal(scenario, "shrink")) {
        off_t size = g_str_equal(scenario, "grow") ? 114 : 112;
        g_assert_cmpint(truncate(fixture->source_path, size), ==, 0);
        changed_bytes = TRUE;
    } else if (g_str_equal(scenario, "same-size") || restored_mtime) {
        overwrite_source(fixture);
        set_source_mtime(fixture, restored_mtime ? 0 : 1);
        changed_bytes = TRUE;
    } else if (g_str_equal(scenario, "mtime-nanoseconds")) {
        struct timespec modified[] = {times[0], times[1]};
        modified[1].tv_nsec = 1000;
        g_assert_cmpint(utimensat(AT_FDCWD, fixture->source_path, modified, 0), ==, 0);
    } else if (atime_only) {
        assert_checksum(fixture->source_path, source_hash);
    } else if (deleted || invalid_path) {
        g_assert_cmpint(g_remove(fixture->source_path), ==, 0);
        if (invalid_path) {
            g_assert_cmpint(symlink("source", fixture->source_path), ==, 0);
        }
    } else if (renamed) {
        g_autofree char *original = g_build_filename(fixture->directory, "original", NULL);
        g_assert_cmpint(g_rename(fixture->source_path, original), ==, 0);
    } else if (canceled) {
        fault = CANCEL_TRANSFER;
    } else if (g_str_equal(scenario, "during-copy")) {
        fault = SOURCE_WRITE_DURING_COPY;
        changed_bytes = TRUE;
    } else if (g_str_equal(scenario, "after-sync")) {
        fault = SOURCE_WRITE_AFTER_SYNC;
        changed_bytes = TRUE;
    } else if (g_str_equal(scenario, "descriptor-error")) {
        fault = SOURCE_DESCRIPTOR_ERROR;
    } else if (g_str_equal(scenario, "path-error")) {
        fault = SOURCE_PATH_ERROR;
    }

    const struct stat *before = &fixture->source_before;
    struct stat after;
    g_assert_cmpint(__real_fstat(fixture->source_fd, &after), ==, 0);
    if (restored_mtime) {
        g_assert_cmpint(after.st_mtim.tv_sec, ==, before->st_mtim.tv_sec);
        g_assert_cmpint(after.st_mtim.tv_nsec, ==, before->st_mtim.tv_nsec);
        if (after.st_ctim.tv_sec == before->st_ctim.tv_sec &&
            after.st_ctim.tv_nsec == before->st_ctim.tv_nsec) {
            g_test_skip("Filesystem did not report a ctime change after restoring mtime");
            return;
        }
    } else if (g_str_equal(scenario, "mtime-nanoseconds")) {
        g_assert_cmpint(after.st_mtim.tv_sec, ==, before->st_mtim.tv_sec);
        if (after.st_mtim.tv_nsec == before->st_mtim.tv_nsec) {
            g_test_skip("Filesystem did not retain the subsecond mtime change");
            return;
        }
    }

    run_transfer(fixture);
    if (atime_only || g_str_equal(scenario, "unchanged")) {
        g_assert_no_error(fixture->error);
        if (fixture->remote) {
            g_assert_nonnull(fixture->payload);
            g_autofree char *hash = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256,
                                                                 fixture->payload);
            g_assert_cmpstr(hash, ==, output_hash);
            assert_contents(fixture->output, "previous export");
        } else {
            assert_checksum(fixture->output, output_hash);
        }
    } else {
        int code = G_IO_ERROR_BUSY;
        const char *diagnostic = "Image source changed";
        if (fault == SOURCE_DESCRIPTOR_ERROR) {
            code = g_io_error_from_errno(EIO);
            diagnostic = "Cannot recheck pinned image source";
        } else if (fault == SOURCE_PATH_ERROR) {
            code = G_IO_ERROR_PERMISSION_DENIED;
            diagnostic = "Cannot recheck image source path";
        } else if (deleted || invalid_path || renamed) {
            diagnostic = "Image source path changed";
        }
        if (canceled) {
            code = G_IO_ERROR_CANCELLED;
        }
        g_assert_error(fixture->error, G_IO_ERROR, code);
        g_assert_null(fixture->payload);
        if (!canceled) {
            g_assert_true(g_str_has_prefix(fixture->error->message, diagnostic));
        }
        if (new_destination) {
            g_assert_false(g_file_test(fixture->output, G_FILE_TEST_EXISTS));
        } else {
            struct stat destination_after;
            g_assert_cmpint(stat(fixture->output, &destination_after), ==, 0);
            assert_same_inode(&destination_before, &destination_after);
            assert_contents(fixture->output, "previous export");
        }
    }

    g_assert_cmpint(__real_fstat(fixture->source_fd, &after), ==, 0);
    assert_same_inode(before, &after);
    if (changed_bytes) {
        off_t expected_size = before->st_size;
        if (g_str_equal(scenario, "grow")) {
            expected_size++;
        } else if (g_str_equal(scenario, "shrink")) {
            expected_size--;
        }
        g_assert_cmpint(after.st_size, ==, expected_size);
        g_assert_true(after.st_size != before->st_size ||
                      after.st_mtim.tv_sec != before->st_mtim.tv_sec ||
                      after.st_mtim.tv_nsec != before->st_mtim.tv_nsec ||
                      after.st_ctim.tv_sec != before->st_ctim.tv_sec ||
                      after.st_ctim.tv_nsec != before->st_ctim.tv_nsec);
    } else if (atime_only) {
        assert_checksum(fixture->source_path, source_hash);
        if (after.st_atim.tv_sec == before->st_atim.tv_sec &&
            after.st_atim.tv_nsec == before->st_atim.tv_nsec) {
            g_test_message("Source reads did not advance atime on this mount");
        }
    }

    g_clear_pointer(&fixture->source, image_export_source_free);
    g_assert_cmpint(fcntl(fixture->source_fd, F_GETFD), ==, -1);
    g_assert_cmpint(errno, ==, EBADF);
}

static void test_permissions(TransferFixture *fixture, gconstpointer data)
{
    const char *kind = data;
    g_autofree char *expected = make_pattern(fixture->input, 1051);
    if (g_str_equal(kind, "umask")) {
        mode_t original_mode = umask(0027);
        run_transfer(fixture);
        umask(original_mode);
        g_assert_no_error(fixture->error);
        struct stat info;
        g_assert_cmpint(stat(fixture->output, &info), ==, 0);
        g_assert_cmpuint(info.st_mode & 0777, ==, 0640);
        assert_checksum(fixture->output, expected);
        return;
    }

    if (geteuid() == 0) {
        g_test_skip("Permission-denial checks require an unprivileged user");
        return;
    }
    g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
    if (g_str_equal(kind, "read-only-file")) {
        g_assert_cmpint(g_chmod(fixture->output, 0440), ==, 0);
    } else {
        g_assert_cmpint(g_chmod(fixture->directory, 0500), ==, 0);
    }
    run_transfer(fixture);
    g_assert_cmpint(g_chmod(fixture->directory, 0700), ==, 0);
    g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    assert_contents(fixture->output, "previous export");
}

static void test_access_acl(TransferFixture *fixture, gconstpointer data)
{
    (void)data;
    /* Linux POSIX ACL xattr encoding, with a named user and a group mask. */
    const struct {
        guint32 version;
        struct {
            guint16 tag;
            guint16 permissions;
            guint32 id;
        } entries[5];
    } acl = {
        GUINT32_TO_LE(2),
        {
            {GUINT16_TO_LE(1), GUINT16_TO_LE(6), GUINT32_TO_LE(G_MAXUINT32)},
            {GUINT16_TO_LE(2), GUINT16_TO_LE(4), GUINT32_TO_LE(geteuid() + 1)},
            {GUINT16_TO_LE(4), GUINT16_TO_LE(0), GUINT32_TO_LE(G_MAXUINT32)},
            {GUINT16_TO_LE(16), GUINT16_TO_LE(4), GUINT32_TO_LE(G_MAXUINT32)},
            {GUINT16_TO_LE(32), GUINT16_TO_LE(0), GUINT32_TO_LE(G_MAXUINT32)},
        },
    };
    g_autofree char *expected = make_pattern(fixture->input, 1051);
    g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
    int result = setxattr(fixture->output, "system.posix_acl_access", &acl, sizeof acl, 0);
    if (result < 0 && (errno == ENOTSUP || errno == EINVAL)) {
        g_test_skip("The test filesystem does not support the POSIX access ACL fixture");
        return;
    }
    g_assert_cmpint(result, ==, 0);
    run_transfer(fixture);
    g_assert_no_error(fixture->error);
    guint8 actual[sizeof acl];
    g_assert_cmpint(getxattr(fixture->output, "system.posix_acl_access", actual, sizeof actual),
                    ==, sizeof acl);
    g_assert_cmpmem(actual, sizeof actual, &acl, sizeof acl);
    assert_checksum(fixture->output, expected);
}

static void test_user_attribute(TransferFixture *fixture, gconstpointer data)
{
    (void)data;
    g_autofree char *expected = make_pattern(fixture->input, 1051);
    g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
    int result = setxattr(fixture->output, "user.export-test", "old", 3, 0);
    if (result < 0 && errno == ENOTSUP) {
        g_test_skip("The test filesystem does not support user extended attributes");
        return;
    }
    g_assert_cmpint(result, ==, 0);
    run_transfer(fixture);
    g_assert_no_error(fixture->error);
    g_assert_cmpint(getxattr(fixture->output, "user.export-test", NULL, 0), ==, -1);
    g_assert_cmpint(errno, ==, ENODATA);
    assert_checksum(fixture->output, expected);
}

static void test_substituted_staging(TransferFixture *fixture, gconstpointer data)
{
    (void)data;
    g_autofree char *expected = make_pattern(fixture->input, 1051);
    g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
    fault = SUBSTITUTE_STAGING;
    run_transfer(fixture);
    g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    assert_contents(fixture->output, "previous export");
    assert_checksum(fixture->input, expected);

    g_autoptr(GDir) directory = g_dir_open(fixture->directory, 0, NULL);
    const char *entry;
    guint replacements = 0;
    while ((entry = g_dir_read_name(directory)) != NULL) {
        if (g_str_has_prefix(entry, ".majoris-export-")) {
            g_autofree char *path = g_build_filename(fixture->directory, entry, NULL);
            g_assert_cmpint(g_rmdir(path), ==, 0);
            replacements++;
        }
    }
    g_assert_cmpuint(replacements, ==, 1);
    g_autofree char *moved = g_build_filename(fixture->directory, "moved-staging", NULL);
    g_assert_cmpint(g_rmdir(moved), ==, 0);
}

static void make_invalid_input(TransferFixture *fixture, const char *kind)
{
    if (g_str_equal(kind, "empty")) {
        g_assert_true(g_file_set_contents(fixture->input, "", 0, NULL));
    } else if (g_str_equal(kind, "oversized")) {
        g_assert_true(g_file_set_contents(fixture->input, "", 0, NULL));
        g_assert_cmpint(truncate(fixture->input, LIMIT_BYTES + 1), ==, 0);
    } else if (g_str_equal(kind, "symlink")) {
        g_assert_cmpint(symlink("/dev/zero", fixture->input), ==, 0);
    } else if (g_str_equal(kind, "fifo")) {
        g_assert_cmpint(mkfifo(fixture->input, 0600), ==, 0);
    }
}

static void test_invalid_input(TransferFixture *fixture, gconstpointer data)
{
    make_invalid_input(fixture, data);
    run_transfer(fixture);
    g_assert_error(fixture->error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE);
    g_assert_false(g_file_test(fixture->output, G_FILE_TEST_EXISTS));
}

static void read_non_native_payload(TransferFixture *fixture)
{
    g_autoptr(GFile) target = g_file_new_for_uri("resource:///export-transfer");
    g_assert_false(g_file_is_native(target));
    image_export_transfer_async(fixture->input, target, NULL, LIMIT_BYTES, fixture->cancel,
                                transfer_done, fixture);
    g_main_loop_run(fixture->loop);
}

static void test_non_native_invalid_input(TransferFixture *fixture, gconstpointer data)
{
    make_invalid_input(fixture, data);
    read_non_native_payload(fixture);
    g_assert_error(fixture->error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE);
    g_assert_null(fixture->payload);
}

static void test_non_native_payload(TransferFixture *fixture, gconstpointer data)
{
    gsize size = GPOINTER_TO_UINT(data);
    g_autofree char *expected = make_pattern(fixture->input, size);
    fault = SHORT_IO;
    read_non_native_payload(fixture);
    g_assert_no_error(fixture->error);
    g_assert_nonnull(fixture->payload);
    g_assert_cmpuint(g_bytes_get_size(fixture->payload), ==, size);
    g_autofree char *actual = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, fixture->payload);
    g_assert_cmpstr(actual, ==, expected);
}

static void test_non_native_rejection(TransferFixture *fixture, gconstpointer data)
{
    fault = GPOINTER_TO_INT(data);
    g_autofree char *expected = make_pattern(fixture->input, 3 * CHUNK_BYTES);
    read_non_native_payload(fixture);
    g_assert_null(fixture->payload);
    if (fault == READ_ERROR) {
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_FAILED);
    } else if (fault == CANCEL_TRANSFER) {
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    } else {
        g_assert_error(fixture->error, IMAGE_KIT_IMAGE_ERROR,
                       IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE);
    }
}

static void test_failed_transfer(TransferFixture *fixture, gconstpointer data)
{
    fault = GPOINTER_TO_INT(data);
    g_autofree char *expected = make_pattern(fixture->input, 3 * CHUNK_BYTES);
    gboolean has_destination = fault != TRUNCATE_INPUT && fault != CANCEL_TRANSFER;
    if (has_destination) {
        g_assert_true(g_file_set_contents(fixture->output, "previous export", -1, NULL));
    }
    run_transfer(fixture);
    switch (fault) {
    case READ_ERROR:
    case SYNC_ERROR:
    case CLOSE_ERROR:
    case STAGING_STAT_ERROR:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_FAILED);
        break;
    case WRITE_ERROR:
    case STAGING_CREATE_ERROR:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
        break;
    case COMMIT_ERROR:
    case STAGING_OPEN_ERROR:
    case STAGING_OWNER_ERROR:
    case STAGING_MODE_ERROR:
    case CHOWN_ERROR:
    case CHMOD_ERROR:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
        break;
    case CANCEL_TRANSFER:
    case CANCEL_BEFORE_COMMIT:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        break;
    default:
        g_assert_error(fixture->error, IMAGE_KIT_IMAGE_ERROR,
                       IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE);
        break;
    }
    if (has_destination) {
        g_autofree char *contents = NULL;
        g_assert_true(g_file_get_contents(fixture->output, &contents, NULL, NULL));
        g_assert_cmpstr(contents, ==, "previous export");
    } else {
        g_assert_false(g_file_test(fixture->output, G_FILE_TEST_EXISTS));
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    const struct {
        const char *name;
        guint size;
    } copies[] = {
        {"/export-transfer/small", 1024},
        {"/export-transfer/exact-chunk-and-partial-io", CHUNK_BYTES},
        {"/export-transfer/multiple-chunks-and-late-cancellation", 3 * CHUNK_BYTES + 17},
        {"/export-transfer/maximum-size", LIMIT_BYTES},
    };
    for (guint index = 0; index < G_N_ELEMENTS(copies); index++) {
        g_test_add(copies[index].name, TransferFixture, GUINT_TO_POINTER(copies[index].size),
                   setup_fixture, test_copy, teardown_fixture);
    }
    g_test_add("/export-transfer/destination-link", TransferFixture, GINT_TO_POINTER(TRUE),
               setup_fixture, test_destination_link, teardown_fixture);
    g_test_add("/export-transfer/dangling-destination-link", TransferFixture,
               GINT_TO_POINTER(FALSE), setup_fixture, test_destination_link, teardown_fixture);
    g_test_add("/export-transfer/maximum-destination-links", TransferFixture, GUINT_TO_POINTER(40),
               setup_fixture, test_destination_link_limit, teardown_fixture);
    g_test_add("/export-transfer/excess-destination-links", TransferFixture, GUINT_TO_POINTER(41),
               setup_fixture, test_destination_link_limit, teardown_fixture);
    g_test_add("/export-transfer/destination-link-cycle", TransferFixture, NULL,
               setup_fixture, test_destination_cycle, teardown_fixture);
    g_test_add("/export-transfer/non-native-validated-payload", TransferFixture,
               GUINT_TO_POINTER(3 * CHUNK_BYTES + 17),
               setup_fixture, test_non_native_payload, teardown_fixture);
    g_test_add("/export-transfer/non-native-exact-chunk", TransferFixture,
               GUINT_TO_POINTER(CHUNK_BYTES), setup_fixture, test_non_native_payload,
               teardown_fixture);
    g_test_add("/export-transfer/non-native-small", TransferFixture, GUINT_TO_POINTER(1024),
               setup_fixture, test_non_native_payload, teardown_fixture);

    const struct {
        const char *name;
        TransferFault fault;
    } non_native_failures[] = {
        {"/export-transfer/non-native-read-error", READ_ERROR},
        {"/export-transfer/non-native-unexpected-eof", TRUNCATE_INPUT},
        {"/export-transfer/non-native-growing-output", GROW_INPUT},
        {"/export-transfer/non-native-modified-output", MODIFY_INPUT},
        {"/export-transfer/non-native-cancel-during-validation", CANCEL_TRANSFER},
    };
    for (guint index = 0; index < G_N_ELEMENTS(non_native_failures); index++) {
        g_test_add(non_native_failures[index].name, TransferFixture,
                   GINT_TO_POINTER(non_native_failures[index].fault), setup_fixture,
                   test_non_native_rejection, teardown_fixture);
    }

    const char *invalid[] = {"empty", "oversized", "missing", "symlink", "fifo"};
    for (guint index = 0; index < G_N_ELEMENTS(invalid); index++) {
        g_autofree char *name = g_strconcat("/export-transfer/invalid-", invalid[index], NULL);
        g_test_add(name, TransferFixture, invalid[index], setup_fixture, test_invalid_input,
                   teardown_fixture);
        g_autofree char *non_native_name =
            g_strconcat("/export-transfer/non-native-invalid-", invalid[index], NULL);
        g_test_add(non_native_name, TransferFixture, invalid[index], setup_fixture,
                   test_non_native_invalid_input, teardown_fixture);
    }
    const struct {
        const char *name;
        TransferFault fault;
    } failures[] = {
        {"/export-transfer/read-error", READ_ERROR},
        {"/export-transfer/write-error", WRITE_ERROR},
        {"/export-transfer/unexpected-eof", TRUNCATE_INPUT},
        {"/export-transfer/growing-output", GROW_INPUT},
        {"/export-transfer/substituted-path", SWAP_INPUT},
        {"/export-transfer/cancel-during-copy", CANCEL_TRANSFER},
        {"/export-transfer/sync-error", SYNC_ERROR},
        {"/export-transfer/close-error", CLOSE_ERROR},
        {"/export-transfer/commit-error", COMMIT_ERROR},
        {"/export-transfer/cancel-before-commit", CANCEL_BEFORE_COMMIT},
        {"/export-transfer/staging-open-error", STAGING_OPEN_ERROR},
        {"/export-transfer/staging-stat-error", STAGING_STAT_ERROR},
        {"/export-transfer/staging-owner-error", STAGING_OWNER_ERROR},
        {"/export-transfer/staging-mode-error", STAGING_MODE_ERROR},
        {"/export-transfer/staging-create-error", STAGING_CREATE_ERROR},
        {"/export-transfer/preserve-ownership-error", CHOWN_ERROR},
        {"/export-transfer/preserve-mode-error", CHMOD_ERROR},
    };
    for (guint index = 0; index < G_N_ELEMENTS(failures); index++) {
        g_test_add(failures[index].name, TransferFixture, GINT_TO_POINTER(failures[index].fault),
                   setup_fixture, test_failed_transfer, teardown_fixture);
    }

    const char *aliases[] = {"hard-link", "symlink", "chain", "parent-link", "relative"};
    for (guint index = 0; index < G_N_ELEMENTS(aliases); index++) {
        g_autofree char *name = g_strconcat("/export-transfer/source-alias-", aliases[index], NULL);
        g_test_add(name, TransferFixture, aliases[index], setup_fixture, test_source_alias,
                   teardown_fixture);
    }
    const struct {
        const char *name;
        TransferFault fault;
    } mutations[] = {
        {"/export-transfer/create-after-resolution", CREATE_AFTER_RESOLUTION},
        {"/export-transfer/replace-during-copy", REPLACE_DURING_COPY},
        {"/export-transfer/delete-after-sync", DELETE_AFTER_SYNC},
        {"/export-transfer/change-mode-after-sync", CHANGE_MODE_AFTER_SYNC},
        {"/export-transfer/substitute-symlink-after-sync", SYMLINK_AFTER_SYNC},
        {"/export-transfer/retarget-before-validation", RETARGET_BEFORE_VALIDATION},
        {"/export-transfer/substitute-parent", SUBSTITUTE_PARENT},
        {"/export-transfer/source-alias-during-copy", SOURCE_ALIAS_DURING_COPY},
        {"/export-transfer/competing-create-at-publication", CREATE_AT_PUBLICATION},
    };
    for (guint index = 0; index < G_N_ELEMENTS(mutations); index++) {
        g_test_add(mutations[index].name, TransferFixture, GINT_TO_POINTER(mutations[index].fault),
                   setup_fixture, test_destination_mutation, teardown_fixture);
    }
    const char *permissions[] = {"umask", "read-only-file", "read-only-directory"};
    for (guint index = 0; index < G_N_ELEMENTS(permissions); index++) {
        g_autofree char *name = g_strconcat("/export-transfer/", permissions[index], NULL);
        g_test_add(name, TransferFixture, permissions[index], setup_fixture, test_permissions,
                   teardown_fixture);
    }
    g_test_add("/export-transfer/replacement-ownership-mode-and-hard-links", TransferFixture, NULL,
               setup_fixture, test_replacement_metadata, teardown_fixture);
    g_test_add("/export-transfer/preserve-supplementary-group", TransferFixture, NULL,
               setup_fixture, test_preserve_different_group, teardown_fixture);
    g_test_add("/export-transfer/source-descriptor-survives-path-replacement", TransferFixture,
               NULL, setup_fixture, test_pinned_source, teardown_fixture);
    g_test_add("/export-transfer/remote-source-descriptor-survives-path-replacement",
               TransferFixture, GINT_TO_POINTER(TRUE), setup_fixture, test_pinned_source,
               teardown_fixture);
    g_test_add("/export-transfer/native-destination-skips-non-native-source",
               TransferFixture, NULL, setup_fixture, test_skip_non_native_source,
               teardown_fixture);
    g_test_add("/export-transfer/remote-destination-skips-non-native-source",
               TransferFixture, GINT_TO_POINTER(TRUE), setup_fixture, test_skip_non_native_source,
               teardown_fixture);
    const char *invalid_sources[] = {"missing", "directory", "fifo", "capture-error", "canceled"};
    for (guint index = 0; index < G_N_ELEMENTS(invalid_sources); index++) {
        g_autofree char *name = g_strconcat("/export-transfer/remote-preflight-",
                                            invalid_sources[index], NULL);
        g_test_add(name, TransferFixture, invalid_sources[index], setup_fixture,
                   test_invalid_source, teardown_fixture);
    }
    const char *freshness[] = {
        "read", "grow", "shrink", "same-size", "mtime-nanoseconds", "restored-mtime",
        "during-copy", "after-sync", "deleted", "invalid-path", "descriptor-error", "path-error",
    };
    for (guint index = 0; index < G_N_ELEMENTS(freshness); index++) {
        g_autofree char *name = g_strconcat("/export-transfer/source-", freshness[index], NULL);
        g_test_add(name, TransferFixture, freshness[index], setup_fixture, test_source_freshness,
                   teardown_fixture);
    }
    const char *remote_freshness[] = {
        "remote-unchanged", "remote-read", "remote-grow", "remote-shrink",
        "remote-same-size", "remote-mtime-nanoseconds", "remote-restored-mtime",
        "remote-during-copy", "remote-renamed", "remote-deleted", "remote-invalid-path",
        "remote-descriptor-error", "remote-path-error", "remote-canceled",
    };
    for (guint index = 0; index < G_N_ELEMENTS(remote_freshness); index++) {
        g_autofree char *name = g_strconcat("/export-transfer/", remote_freshness[index], NULL);
        g_test_add(name, TransferFixture, remote_freshness[index], setup_fixture,
                   test_source_freshness, teardown_fixture);
    }
    g_test_add("/export-transfer/preserve-access-acl", TransferFixture, NULL,
               setup_fixture, test_access_acl, teardown_fixture);
    g_test_add("/export-transfer/drop-user-attribute", TransferFixture, NULL,
               setup_fixture, test_user_attribute, teardown_fixture);
    g_test_add("/export-transfer/leave-substituted-staging-alone", TransferFixture, NULL,
               setup_fixture, test_substituted_staging, teardown_fixture);
    return g_test_run();
}
