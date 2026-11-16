#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
    SWAP_INPUT,
    CANCEL_TRANSFER,
    SYNC_ERROR,
    CLOSE_ERROR,
    COMMIT_ERROR,
    CANCEL_BEFORE_COMMIT,
    CANCEL_AFTER_COMMIT,
} TransferFault;

typedef struct {
    char *directory;
    char *input;
    char *output;
    GCancellable *cancel;
    GMainLoop *loop;
    GError *error;
    GBytes *payload;
} TransferFixture;

static TransferFixture *active_fixture;
static TransferFault fault;
static guint reads;
static guint closes;

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
        case SWAP_INPUT: {
            g_autofree char *moved = g_strconcat(active_fixture->input, ".moved", NULL);
            g_assert_cmpint(g_rename(active_fixture->input, moved), ==, 0);
            g_assert_cmpint(symlink("/dev/zero", active_fixture->input), ==, 0);
            break;
        }
        case CANCEL_TRANSFER:
            g_cancellable_cancel(active_fixture->cancel);
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
    }
    return result;
}

gboolean __wrap_g_close(gint fd, GError **error)
{
    gboolean closed = __real_g_close(fd, error);
    if (fault == CLOSE_ERROR) {
        if (++closes == 2) {
            errno = EIO;
            return FALSE;
        }
    }
    return closed;
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
    fixture->loop = g_main_loop_new(NULL, FALSE);
    fault = NO_FAULT;
    reads = 0;
    closes = 0;
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

static void run_transfer(TransferFixture *fixture)
{
    g_autoptr(GFile) target = g_file_new_for_path(fixture->output);
    image_export_transfer_async(fixture->input, target, LIMIT_BYTES, fixture->cancel,
                                transfer_done, fixture);
    g_main_loop_run(fixture->loop);
    g_assert_null(fixture->payload);
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

static void test_invalid_input(TransferFixture *fixture, gconstpointer data)
{
    const char *kind = data;
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
    run_transfer(fixture);
    g_assert_error(fixture->error, IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE);
    g_assert_false(g_file_test(fixture->output, G_FILE_TEST_EXISTS));
}

static void test_non_native_payload(TransferFixture *fixture, gconstpointer data)
{
    (void)data;
    gsize size = 3 * CHUNK_BYTES + 17;
    g_autofree char *expected = make_pattern(fixture->input, size);
    g_autoptr(GFile) target = g_file_new_for_uri("resource:///export-transfer");
    g_assert_false(g_file_is_native(target));
    image_export_transfer_async(fixture->input, target, LIMIT_BYTES, fixture->cancel,
                                transfer_done, fixture);
    g_main_loop_run(fixture->loop);
    g_assert_no_error(fixture->error);
    g_assert_nonnull(fixture->payload);
    g_assert_cmpuint(g_bytes_get_size(fixture->payload), ==, size);
    g_autofree char *actual = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, fixture->payload);
    g_assert_cmpstr(actual, ==, expected);
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
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_FAILED);
        break;
    case WRITE_ERROR:
        g_assert_error(fixture->error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
        break;
    case COMMIT_ERROR:
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
    g_test_add("/export-transfer/non-native-validated-payload", TransferFixture, NULL,
               setup_fixture, test_non_native_payload, teardown_fixture);

    const char *invalid[] = {"empty", "oversized", "missing", "symlink", "fifo"};
    for (guint index = 0; index < G_N_ELEMENTS(invalid); index++) {
        g_autofree char *name = g_strconcat("/export-transfer/invalid-", invalid[index], NULL);
        g_test_add(name, TransferFixture, invalid[index], setup_fixture, test_invalid_input,
                   teardown_fixture);
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
    };
    for (guint index = 0; index < G_N_ELEMENTS(failures); index++) {
        g_test_add(failures[index].name, TransferFixture, GINT_TO_POINTER(failures[index].fault),
                   setup_fixture, test_failed_transfer, teardown_fixture);
    }
    return g_test_run();
}
