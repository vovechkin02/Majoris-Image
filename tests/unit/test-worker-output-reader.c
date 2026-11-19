#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "../../src/core/execution/worker-output-reader.h"
#include "majoris-image.h"

#define CHUNK_BYTES (256 * 1024)
#define PAYLOAD_BYTES (3 * CHUNK_BYTES + 17)

typedef enum {
    NONE,
    DENY_OPEN,
    DENY_READ_OPEN,
    FAIL_INITIAL_STAT,
    FAIL_FINAL_STAT,
    FAIL_READ,
    FAIL_ALLOCATION,
    SHORT_AND_INTERRUPTED_READ,
    TRUNCATE,
    GROW,
    GROW_AT_LIMIT,
    REWRITE,
    MODIFY_TIME,
    CHANGE_MODE,
    SUBSTITUTE_BEFORE_OPEN,
    SUBSTITUTE_AFTER_OPEN,
    REPLACE_INODE,
    CANCEL_AFTER_OPEN,
    CANCEL_FIRST_READ,
    CANCEL_MIDDLE_READ,
    CANCEL_LAST_READ,
    CANCEL_FINAL_STAT,
    CANCEL_BEFORE_CALLBACK,
} Fault;

typedef struct {
    char *directory;
    char *path;
    char *replacement;
    char *saved;
    guint8 *data;
    GCancellable *cancel;
    GMainLoop *loop;
    GThread *caller;
    Fault fault;
    int pinned;
    int readable;
    guint reads;
    guint callbacks;
    guint open_attempts;
    gsize consumed;
    gsize limit;
    GBytes *bytes;
    GError *error;
} Fixture;

static Fixture *active;

#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS == 64
#define __real_open __real_open64
#define __wrap_open __wrap_open64
#define __real_fstat __real_fstat64
#define __wrap_fstat __wrap_fstat64
#endif

int __real_open(const char *path, int flags, ...);
int __real_fstat(int fd, struct stat *info);
ssize_t __real_read(int fd, void *data, size_t size);
int __real_close(int fd);
int __wrap_open(const char *path, int flags, ...);
int __wrap_fstat(int fd, struct stat *info);
ssize_t __wrap_read(int fd, void *data, size_t size);
int __wrap_close(int fd);
gpointer __real_g_try_malloc(gsize size);
gpointer __wrap_g_try_malloc(gsize size);

static void substitute_link(Fixture *fixture)
{
    g_assert_cmpint(g_rename(fixture->path, fixture->saved), ==, 0);
    g_assert_cmpint(symlink(fixture->replacement, fixture->path), ==, 0);
}

int __wrap_open(const char *path, int flags, ...)
{
    Fixture *fixture = active;
    gboolean pin = fixture != NULL && g_str_equal(path, fixture->path);
    gboolean reading =
        fixture != NULL && fixture->pinned >= 0 && g_str_has_prefix(path, "/proc/self/fd/");
    g_assert_false((flags & O_CREAT) != 0);
    if (pin) {
        fixture->open_attempts++;
        g_assert_true(fixture->caller != g_thread_self());
        g_assert_true((flags & (O_PATH | O_NOFOLLOW | O_CLOEXEC)) ==
                      (O_PATH | O_NOFOLLOW | O_CLOEXEC));
        if (fixture->fault == DENY_OPEN) {
            errno = EACCES;
            return -1;
        }
        if (fixture->fault == SUBSTITUTE_BEFORE_OPEN) {
            substitute_link(fixture);
        }
    }
    if (reading && fixture->fault == DENY_READ_OPEN) {
        errno = EACCES;
        return -1;
    }
    int fd = __real_open(path, flags);
    if (pin) {
        fixture->pinned = fd;
    }
    if (reading) {
        fixture->readable = fd;
        if (fixture->fault == CANCEL_AFTER_OPEN) {
            g_cancellable_cancel(fixture->cancel);
        }
    }
    return fd;
}

int __wrap_fstat(int fd, struct stat *info)
{
    Fixture *fixture = active;
    if (fixture != NULL) {
        if ((fd == fixture->pinned && fixture->fault == FAIL_INITIAL_STAT) ||
            (fd == fixture->readable && fixture->fault == FAIL_FINAL_STAT)) {
            errno = EIO;
            return -1;
        }
        if (fd == fixture->readable && fixture->fault == CANCEL_FINAL_STAT) {
            g_cancellable_cancel(fixture->cancel);
        }
    }
    return __real_fstat(fd, info);
}

static void mutate_output(Fixture *fixture)
{
    switch (fixture->fault) {
    case TRUNCATE:
        g_assert_cmpint(truncate(fixture->path, CHUNK_BYTES + 1), ==, 0);
        break;
    case GROW:
    case GROW_AT_LIMIT:
        g_assert_cmpint(truncate(fixture->path, PAYLOAD_BYTES + 1), ==, 0);
        break;
    case REWRITE: {
        int fd = __real_open(fixture->path, O_WRONLY | O_CLOEXEC);
        g_assert_cmpint(fd, >=, 0);
        g_assert_cmpint(pwrite(fd, "changed", 7, 2 * CHUNK_BYTES), ==, 7);
        g_assert_cmpint(__real_close(fd), ==, 0);
        break;
    }
    case MODIFY_TIME: {
        const struct timespec times[2] = {{0, UTIME_OMIT}, {1, 123}};
        g_assert_cmpint(utimensat(AT_FDCWD, fixture->path, times, 0), ==, 0);
        break;
    }
    case CHANGE_MODE:
        g_assert_cmpint(g_chmod(fixture->path, 0400), ==, 0);
        break;
    case SUBSTITUTE_AFTER_OPEN:
        substitute_link(fixture);
        break;
    case REPLACE_INODE:
        g_assert_cmpint(g_rename(fixture->replacement, fixture->path), ==, 0);
        break;
    default:
        break;
    }
}

ssize_t __wrap_read(int fd, void *data, size_t size)
{
    Fixture *fixture = active;
    if (fixture == NULL || fd != fixture->readable) {
        return __real_read(fd, data, size);
    }
    g_assert_cmpuint(size, <=, CHUNK_BYTES);
    g_assert_cmpuint(size, <=, fixture->limit - fixture->consumed);
    fixture->reads++;
    if (fixture->fault == SHORT_AND_INTERRUPTED_READ) {
        if (fixture->reads == 1) {
            errno = EINTR;
            return -1;
        }
        if (fixture->reads == 2) {
            size = MIN(size, 17);
        }
    }
    if (fixture->reads == 3 && fixture->fault == FAIL_READ) {
        errno = EIO;
        return -1;
    }
    if (fixture->reads == 2) {
        mutate_output(fixture);
    }
    ssize_t received = __real_read(fd, data, size);
    if (received > 0) {
        // The complete original payload must come from the pinned inode.
        if (fixture->fault == SUBSTITUTE_AFTER_OPEN || fixture->fault == REPLACE_INODE) {
            g_assert_cmpmem(data, received, fixture->data + fixture->consumed, received);
        }
        fixture->consumed += (gsize)received;
    }
    if ((fixture->reads == 1 && fixture->fault == CANCEL_FIRST_READ) ||
        (fixture->reads == 3 && fixture->fault == CANCEL_MIDDLE_READ) ||
        (fixture->consumed == PAYLOAD_BYTES && fixture->fault == CANCEL_LAST_READ)) {
        g_cancellable_cancel(fixture->cancel);
    }
    return received;
}

int __wrap_close(int fd)
{
    Fixture *fixture = active;
    int result = __real_close(fd);
    if (fixture != NULL && fd == fixture->pinned) {
        g_assert_cmpint(result, ==, 0);
        fixture->pinned = -1;
    }
    if (fixture != NULL && fd == fixture->readable) {
        g_assert_cmpint(result, ==, 0);
        fixture->readable = -1;
        if (fixture->fault == CANCEL_BEFORE_CALLBACK) {
            g_cancellable_cancel(fixture->cancel);
        }
    }
    return result;
}

gpointer __wrap_g_try_malloc(gsize size)
{
    if (active != NULL && active->fault == FAIL_ALLOCATION) {
        return NULL;
    }
    return __real_g_try_malloc(size);
}

static void output_ready(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    Fixture *fixture = data;
    g_assert_true(fixture->caller == g_thread_self());
    fixture->callbacks++;
    fixture->bytes = worker_output_read_finish(result, &fixture->error);
    g_main_loop_quit(fixture->loop);
}

static void consume_output(Fixture *fixture, gint64 limit)
{
    fixture->limit = limit > 0 ? (gsize)limit : 0;
    active = fixture;
    worker_output_read_async(fixture->path, limit, fixture->cancel, output_ready, fixture);
    g_main_loop_run(fixture->loop);
    active = NULL;
    g_assert_cmpuint(fixture->callbacks, ==, 1);
    g_assert_cmpint(fixture->pinned, ==, -1);
    g_assert_cmpint(fixture->readable, ==, -1);
}

static void setup(Fixture *fixture, gconstpointer data)
{
    (void)data;
    g_autoptr(GError) error = NULL;
    fixture->directory = g_dir_make_tmp("worker-output-test-XXXXXX", &error);
    g_assert_no_error(error);
    fixture->path = g_build_filename(fixture->directory, "output", NULL);
    fixture->replacement = g_build_filename(fixture->directory, "replacement", NULL);
    fixture->saved = g_build_filename(fixture->directory, "saved", NULL);
    fixture->data = g_malloc(PAYLOAD_BYTES);
    for (gsize i = 0; i < PAYLOAD_BYTES; i++) {
        fixture->data[i] = (guint8)(i ^ (i >> 11));
    }
    fixture->cancel = g_cancellable_new();
    fixture->loop = g_main_loop_new(NULL, FALSE);
    fixture->caller = g_thread_self();
    fixture->pinned = -1;
    fixture->readable = -1;
}

static void teardown(Fixture *fixture, gconstpointer data)
{
    (void)data;
    g_clear_pointer(&fixture->bytes, g_bytes_unref);
    g_clear_error(&fixture->error);
    g_object_unref(fixture->cancel);
    g_main_loop_unref(fixture->loop);
    g_remove(fixture->path);
    g_remove(fixture->replacement);
    g_remove(fixture->saved);
    g_assert_cmpint(g_rmdir(fixture->directory), ==, 0);
    g_free(fixture->data);
    g_free(fixture->path);
    g_free(fixture->replacement);
    g_free(fixture->saved);
    g_free(fixture->directory);
}

static void write_payload(Fixture *fixture, gsize size)
{
    g_autoptr(GError) error = NULL;
    g_assert_true(g_file_set_contents(fixture->path, (const char *)fixture->data, size, &error));
    g_assert_no_error(error);
}

static void assert_bytes(Fixture *fixture, gsize size)
{
    g_assert_no_error(fixture->error);
    g_assert_nonnull(fixture->bytes);
    gsize actual_size;
    const guint8 *actual = g_bytes_get_data(fixture->bytes, &actual_size);
    g_assert_cmpmem(actual, actual_size, fixture->data, size);
}

static void assert_invalid(Fixture *fixture)
{
    g_assert_error(fixture->error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE);
    g_assert_null(fixture->bytes);
}

static void verify_size(Fixture *fixture, gconstpointer data)
{
    gsize size = GPOINTER_TO_UINT(data);
    write_payload(fixture, size);
    consume_output(fixture, size);
    assert_bytes(fixture, size);
}

static void verify_invalid_size(Fixture *fixture, gconstpointer data)
{
    gint size = GPOINTER_TO_INT(data);
    if (size >= 0) {
        write_payload(fixture, size);
    }
    consume_output(fixture, CHUNK_BYTES);
    assert_invalid(fixture);
    g_assert_cmpuint(fixture->reads, ==, 0);
}

static void verify_invalid_limit(Fixture *fixture, gconstpointer data)
{
    write_payload(fixture, 1);
    consume_output(fixture, GPOINTER_TO_INT(data));
    g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_null(fixture->bytes);
}

static void verify_special(Fixture *fixture, gconstpointer data)
{
    const char *kind = data;
    int socket_fd = -1;
    if (g_str_equal(kind, "directory")) {
        g_assert_cmpint(g_mkdir(fixture->path, 0700), ==, 0);
    } else if (g_str_equal(kind, "fifo")) {
        g_assert_cmpint(mkfifo(fixture->path, 0600), ==, 0);
    } else if (g_str_equal(kind, "socket")) {
        struct sockaddr_un address = {.sun_family = AF_UNIX};
        g_assert_cmpuint(g_strlcpy(address.sun_path, fixture->path, sizeof address.sun_path), <,
                         sizeof address.sun_path);
        socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        g_assert_cmpint(socket_fd, >=, 0);
        g_assert_cmpint(bind(socket_fd, (struct sockaddr *)&address, sizeof address), ==, 0);
    } else if (g_str_equal(kind, "device-link")) {
        g_assert_cmpint(symlink("/dev/zero", fixture->path), ==, 0);
    } else {
        if (!g_str_equal(kind, "dangling-link")) {
            gsize size = g_str_equal(kind, "oversized-link") ? PAYLOAD_BYTES : 1;
            g_assert_true(
                g_file_set_contents(fixture->replacement, (const char *)fixture->data, size, NULL));
        }
        g_assert_cmpint(symlink(fixture->replacement, fixture->path), ==, 0);
    }
    consume_output(fixture, CHUNK_BYTES);
    assert_invalid(fixture);
    g_assert_cmpuint(fixture->reads, ==, 0);
    if (socket_fd >= 0) {
        __real_close(socket_fd);
    }
}

static void verify_device(Fixture *fixture, gconstpointer data)
{
    (void)data;
    char *original = fixture->path;
    fixture->path = (char *)"/dev/null";
    consume_output(fixture, CHUNK_BYTES);
    fixture->path = original;
    assert_invalid(fixture);
    g_assert_cmpuint(fixture->reads, ==, 0);
}

static void verify_fault(Fixture *fixture, gconstpointer data)
{
    fixture->fault = GPOINTER_TO_INT(data);
    write_payload(fixture, PAYLOAD_BYTES);
    if (fixture->fault == SUBSTITUTE_BEFORE_OPEN || fixture->fault == SUBSTITUTE_AFTER_OPEN ||
        fixture->fault == REPLACE_INODE) {
        g_assert_true(g_file_set_contents(fixture->replacement, "different inode", -1, NULL));
        const struct timespec times[2] = {{0, UTIME_OMIT}, {1, 123}};
        g_assert_cmpint(utimensat(AT_FDCWD, fixture->path, times, 0), ==, 0);
    }
    gint64 limit = fixture->fault == GROW_AT_LIMIT ? PAYLOAD_BYTES : PAYLOAD_BYTES + 1;
    consume_output(fixture, limit);
    switch (fixture->fault) {
    case SHORT_AND_INTERRUPTED_READ:
        assert_bytes(fixture, PAYLOAD_BYTES);
        g_assert_cmpuint(fixture->reads, ==, 6);
        return;
    case DENY_OPEN:
    case DENY_READ_OPEN:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
        break;
    case FAIL_INITIAL_STAT:
    case FAIL_FINAL_STAT:
    case FAIL_READ:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_FAILED);
        g_assert_nonnull(strstr(fixture->error->message, g_strerror(EIO)));
        break;
    case FAIL_ALLOCATION:
        g_assert_error(fixture->error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_MEMORY);
        g_assert_cmpuint(fixture->reads, ==, 0);
        break;
    case CANCEL_AFTER_OPEN:
    case CANCEL_FIRST_READ:
    case CANCEL_MIDDLE_READ:
    case CANCEL_LAST_READ:
    case CANCEL_FINAL_STAT:
    case CANCEL_BEFORE_CALLBACK:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        break;
    default:
        assert_invalid(fixture);
        break;
    }
    g_assert_null(fixture->bytes);
}

static void verify_pre_cancellation(Fixture *fixture, gconstpointer data)
{
    (void)data;
    fixture->fault = DENY_OPEN;
    g_cancellable_cancel(fixture->cancel);
    consume_output(fixture, PAYLOAD_BYTES);
    g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_null(fixture->bytes);
    g_assert_cmpuint(fixture->open_attempts, ==, 0);
}

static void register_cases(void)
{
    const struct {
        const char *name;
        guint size;
    } sizes[] = {{"one-byte", 1},
                 {"small", 1024},
                 {"chunk-boundary", CHUNK_BYTES},
                 {"partial-last-chunk", PAYLOAD_BYTES}};
    for (gsize i = 0; i < G_N_ELEMENTS(sizes); i++) {
        g_autofree char *name = g_strconcat("/worker-output/", sizes[i].name, NULL);
        g_test_add(name, Fixture, GUINT_TO_POINTER(sizes[i].size), setup, verify_size, teardown);
    }
    g_test_add("/worker-output/missing", Fixture, GINT_TO_POINTER(-1), setup, verify_invalid_size,
               teardown);
    g_test_add("/worker-output/empty", Fixture, GINT_TO_POINTER(0), setup, verify_invalid_size,
               teardown);
    g_test_add("/worker-output/one-byte-over-limit", Fixture, GINT_TO_POINTER(CHUNK_BYTES + 1),
               setup, verify_invalid_size, teardown);
    g_test_add("/worker-output/zero-limit", Fixture, GINT_TO_POINTER(0), setup,
               verify_invalid_limit, teardown);
    g_test_add("/worker-output/negative-limit", Fixture, GINT_TO_POINTER(-1), setup,
               verify_invalid_limit, teardown);
    const char *specials[] = {"directory",    "fifo",           "socket",       "device-link",
                              "regular-link", "oversized-link", "dangling-link"};
    for (gsize i = 0; i < G_N_ELEMENTS(specials); i++) {
        g_autofree char *name = g_strconcat("/worker-output/", specials[i], NULL);
        g_test_add(name, Fixture, specials[i], setup, verify_special, teardown);
    }
    g_test_add("/worker-output/device", Fixture, NULL, setup, verify_device, teardown);
    const struct {
        const char *name;
        Fault fault;
    } faults[] = {
        {"open-denied", DENY_OPEN},
        {"read-open-denied", DENY_READ_OPEN},
        {"initial-metadata-failure", FAIL_INITIAL_STAT},
        {"final-metadata-failure", FAIL_FINAL_STAT},
        {"read-error-after-chunks", FAIL_READ},
        {"allocation-failure", FAIL_ALLOCATION},
        {"short-and-interrupted-reads", SHORT_AND_INTERRUPTED_READ},
        {"unexpected-eof", TRUNCATE},
        {"growth-below-limit", GROW},
        {"growth-at-limit", GROW_AT_LIMIT},
        {"same-size-rewrite", REWRITE},
        {"modification-time", MODIFY_TIME},
        {"change-time", CHANGE_MODE},
        {"link-before-open", SUBSTITUTE_BEFORE_OPEN},
        {"link-after-open", SUBSTITUTE_AFTER_OPEN},
        {"inode-replacement", REPLACE_INODE},
        {"cancel-after-open", CANCEL_AFTER_OPEN},
        {"cancel-at-first-read", CANCEL_FIRST_READ},
        {"cancel-at-middle-read", CANCEL_MIDDLE_READ},
        {"cancel-at-last-read", CANCEL_LAST_READ},
        {"cancel-at-final-verification", CANCEL_FINAL_STAT},
        {"cancel-before-delivery", CANCEL_BEFORE_CALLBACK},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(faults); i++) {
        g_autofree char *name = g_strconcat("/worker-output/", faults[i].name, NULL);
        g_test_add(name, Fixture, GINT_TO_POINTER(faults[i].fault), setup, verify_fault, teardown);
    }
    g_test_add("/worker-output/cancel-before-open", Fixture, NULL, setup, verify_pre_cancellation,
               teardown);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    register_cases();
    return g_test_run();
}
