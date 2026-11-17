#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/core/worker-output-reader.h"
#include "majoris-image.h"

typedef ssize_t (*ReadFunction)(int fd, void *buffer, size_t size);

typedef struct {
    GMainLoop *loop;
    GCancellable *cancel;
    gint64 limit;
    gint64 started;
    gint64 finished;
    gint64 metadata_us;
    gint64 recheck_us;
    gint64 cancel_started;
    GBytes *bytes;
    GError *error;
} Measurement;

static const char *input_path;
static const char *workload;
static Measurement *measurement;
static gboolean baseline;
static int read_fd = -1;
static ReadFunction read_original;
static guint reads;
static guint64 consumed;
static guint allocations;
static gsize output_allocation;

#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS == 64
#define __real_open __real_open64
#define __wrap_open __wrap_open64
#define __real_fstat __real_fstat64
#define __wrap_fstat __wrap_fstat64
#endif

int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...);
int __real_fstat(int fd, struct stat *info);
int __wrap_fstat(int fd, struct stat *info);
gpointer __real_g_try_malloc(gsize size);
gpointer __wrap_g_try_malloc(gsize size);

int __wrap_open(const char *path, int flags, ...)
{
    g_assert_false((flags & O_CREAT) != 0);
    gint64 started = g_get_monotonic_time();
    int fd = __real_open(path, flags);
    measurement->metadata_us += g_get_monotonic_time() - started;
    if ((flags & O_PATH) == 0) {
        g_atomic_int_set(&read_fd, fd);
    }
    return fd;
}

int __wrap_fstat(int fd, struct stat *info)
{
    gint64 started = g_get_monotonic_time();
    int result = __real_fstat(fd, info);
    gint64 *phase =
        fd == g_atomic_int_get(&read_fd) ? &measurement->recheck_us : &measurement->metadata_us;
    *phase += g_get_monotonic_time() - started;
    return result;
}

gpointer __wrap_g_try_malloc(gsize size)
{
    allocations++;
    output_allocation += size;
    return __real_g_try_malloc(size);
}

static gboolean is_benchmark_read(int fd)
{
    int tracked = g_atomic_int_get(&read_fd);
    if (fd == tracked) {
        return TRUE;
    }
    if (!baseline || tracked >= 0 || input_path == NULL) {
        return FALSE;
    }
    char descriptor[64], path[4096];
    g_snprintf(descriptor, sizeof descriptor, "/proc/self/fd/%d", fd);
    ssize_t count = readlink(descriptor, path, sizeof path - 1);
    if (count <= 0) {
        return FALSE;
    }
    path[count] = '\0';
    if (!g_str_equal(path, input_path)) {
        return FALSE;
    }
    g_atomic_int_set(&read_fd, fd);
    return TRUE;
}

ssize_t read(int fd, void *buffer, size_t size)
{
    ReadFunction next_read = read_original;
    if (next_read == NULL) {
        next_read = G_GNUC_EXTENSION(ReadFunction) dlsym(RTLD_NEXT, "read");
    }
    ssize_t received = next_read(fd, buffer, size);
    int saved_errno = errno;
    if (!is_benchmark_read(fd)) {
        errno = saved_errno;
        return received;
    }
    reads++;
    if (received > 0) {
        consumed += received;
    }
    if (reads == 1 && received > 0) {
        if (g_str_equal(workload, "grow")) {
            g_assert_cmpint(truncate(input_path, measurement->limit + 1), ==, 0);
        } else if (g_str_equal(workload, "truncate")) {
            g_assert_cmpint(truncate(input_path, received), ==, 0);
        } else if (g_str_equal(workload, "cancel")) {
            measurement->cancel_started = g_get_monotonic_time();
            g_cancellable_cancel(measurement->cancel);
        }
    }
    errno = saved_errno;
    return received;
}

static void complete(Measurement *result)
{
    result->finished = g_get_monotonic_time();
    g_main_loop_quit(result->loop);
}

static void bounded_ready(GObject *source, GAsyncResult *response, gpointer data)
{
    (void)source;
    Measurement *result = data;
    result->bytes = worker_output_read_finish(response, &result->error);
    complete(result);
}

static void baseline_ready(GObject *source, GAsyncResult *response, gpointer data)
{
    Measurement *result = data;
    result->bytes = g_file_load_bytes_finish(G_FILE(source), response, NULL, &result->error);
    complete(result);
}

/* The previous WorkerClient sequence, using the same two asynchronous GIO operations. */
static void baseline_metadata_ready(GObject *source, GAsyncResult *response, gpointer data)
{
    Measurement *result = data;
    g_autoptr(GFileInfo) info = g_file_query_info_finish(G_FILE(source), response, &result->error);
    result->metadata_us = g_get_monotonic_time() - result->started;
    if (info == NULL) {
        if (g_error_matches(result->error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
            g_clear_error(&result->error);
            result->error =
                g_error_new_literal(IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE,
                                    "Decoder did not produce an output file");
        }
        complete(result);
        return;
    }
    goffset size = g_file_info_get_size(info);
    if (g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR || size < 1 ||
        size > result->limit) {
        result->error =
            g_error_new_literal(IMAGE_KIT_IMAGE_ERROR, IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE,
                                "Invalid decoder response type or size");
        complete(result);
        return;
    }
    g_file_load_bytes_async(G_FILE(source), result->cancel, baseline_ready, result);
}

static gint64 cpu_us(struct rusage usage)
{
    return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * G_USEC_PER_SEC +
           usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
}

static long peak_rss_kib(void)
{
    // Unlike getrusage(), this excludes the Python launcher's pre-exec high-water mark.
    FILE *status = fopen("/proc/self/status", "r");
    g_assert_nonnull(status);
    char line[256];
    long peak = 0;
    while (fgets(line, sizeof line, status) != NULL) {
        if (sscanf(line, "VmHWM: %ld kB", &peak) == 1) {
            break;
        }
    }
    fclose(status);
    g_assert_cmpint(peak, >, 0);
    return peak;
}

static void report(const Measurement *result, gint64 cpu, long peak_rss)
{
    const char *outcome = "ok";
    if (g_error_matches(result->error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        outcome = "canceled";
    } else if (g_error_matches(result->error, IMAGE_KIT_IMAGE_ERROR,
                               IMAGE_KIT_IMAGE_ERROR_INVALID_RESPONSE)) {
        outcome = "invalid-response";
    } else if (result->error != NULL) {
        g_printerr("%s\n", result->error->message);
        outcome = "io-error";
    }
    g_autofree char *digest = result->bytes == NULL
                                  ? NULL
                                  : g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, result->bytes);
    g_autofree char *allocation = baseline ? g_strdup("null") : g_strdup_printf("%u", allocations);
    g_autofree char *allocation_bytes =
        baseline ? g_strdup("null") : g_strdup_printf("%" G_GSIZE_FORMAT, output_allocation);
    g_print(
        "{\"read_us\":%" G_GINT64_FORMAT ",\"cpu_us\":%" G_GINT64_FORMAT
        ",\"peak_rss_kib\":%ld,\"bytes_consumed\":%" G_GUINT64_FORMAT
        ",\"read_calls\":%u,\"open_metadata_us\":%" G_GINT64_FORMAT
        ",\"final_verification_us\":%" G_GINT64_FORMAT ",\"cancel_latency_us\":%" G_GINT64_FORMAT
        ",\"output_allocations\":%s,\"output_allocation_bytes\":%s"
        ",\"outcome\":\"%s\",\"returned_bytes\":%" G_GSIZE_FORMAT ",\"sha256\":\"%s\"}\n",
        result->finished - result->started, cpu, peak_rss, consumed, reads, result->metadata_us,
        result->recheck_us, result->cancel_started ? result->finished - result->cancel_started : 0,
        allocation, allocation_bytes, outcome,
        result->bytes == NULL ? 0 : g_bytes_get_size(result->bytes), digest ? digest : "");
}

static void measure(const char *path, gint64 limit)
{
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    Measurement result = {.loop = loop, .cancel = cancel, .limit = limit};
    measurement = &result;
    struct rusage before, after;
    g_assert_cmpint(getrusage(RUSAGE_SELF, &before), ==, 0);
    result.started = g_get_monotonic_time();
    if (baseline) {
        g_file_query_info_async(file, "standard::type,standard::size",
                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, G_PRIORITY_DEFAULT, cancel,
                                baseline_metadata_ready, &result);
    } else {
        worker_output_read_async(path, limit, cancel, bounded_ready, &result);
    }
    g_main_loop_run(loop);
    g_assert_cmpint(getrusage(RUSAGE_SELF, &after), ==, 0);
    report(&result, cpu_us(after) - cpu_us(before), peak_rss_kib());
    g_clear_pointer(&result.bytes, g_bytes_unref);
    g_clear_error(&result.error);
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        g_printerr("Usage: %s baseline|bounded normal|grow|truncate|cancel PATH LIMIT\n", argv[0]);
        return 2;
    }
    baseline = g_str_equal(argv[1], "baseline");
    read_original = G_GNUC_EXTENSION(ReadFunction) dlsym(RTLD_NEXT, "read");
    workload = argv[2];
    input_path = argv[3];
    measure(input_path, g_ascii_strtoll(argv[4], NULL, 10));
    return 0;
}
