#include <gio/gio.h>

typedef struct {
    GFileOutputStream parent;
    char *fault;
    gsize remaining;
    guint writes;
} ProbeStream;

typedef GFileOutputStreamClass ProbeStreamClass;

static const char replace_tag;

G_DEFINE_TYPE(ProbeStream, probe_stream, G_TYPE_FILE_OUTPUT_STREAM)

static gssize probe_write(GOutputStream *output, const void *buffer, gsize count,
                          GCancellable *cancel, GError **error)
{
    ProbeStream *stream = (ProbeStream *)output;
    (void)buffer;
    if (stream->writes++ == 0) {
        stream->remaining = count;
    }
    g_assert_cmpuint(count, ==, stream->remaining);
    if (g_str_equal(stream->fault, "cancel-write") && stream->writes == 2) {
        g_cancellable_cancel(cancel);
        g_cancellable_set_error_if_cancelled(cancel, error);
        return -1;
    }
    if (g_str_equal(stream->fault, "first-write") ||
        (g_str_equal(stream->fault, "later-write") && stream->writes == 2)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Injected write failure");
        return -1;
    }
    gsize written = MIN(count, 256 * 1024);
    stream->remaining -= written;
    return (gssize)written;
}

static gboolean probe_close(GOutputStream *output, GCancellable *cancel, GError **error)
{
    ProbeStream *stream = (ProbeStream *)output;
    if (g_str_equal(stream->fault, "short-writes")) {
        g_assert_cmpuint(stream->writes, >, 1);
        g_assert_cmpuint(stream->remaining, ==, 0);
        return TRUE;
    }
    g_assert_true(g_cancellable_is_cancelled(cancel));
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Injected cleanup failure");
    return FALSE;
}

static void probe_stream_finalize(GObject *object)
{
    ProbeStream *stream = (ProbeStream *)object;
    g_free(stream->fault);
    G_OBJECT_CLASS(probe_stream_parent_class)->finalize(object);
}

static void probe_stream_class_init(ProbeStreamClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = probe_stream_finalize;
    G_OUTPUT_STREAM_CLASS(klass)->write_fn = probe_write;
    G_OUTPUT_STREAM_CLASS(klass)->close_fn = probe_close;
}

static void probe_stream_init(ProbeStream *stream)
{
    (void)stream;
}

void __real_g_file_replace_async(GFile *, const char *, gboolean, GFileCreateFlags, int,
                                 GCancellable *, GAsyncReadyCallback, gpointer);
GFileOutputStream *__real_g_file_replace_finish(GFile *, GAsyncResult *, GError **);

void __wrap_g_file_replace_async(GFile *file, const char *etag, gboolean backup,
                                 GFileCreateFlags flags, int priority, GCancellable *cancel,
                                 GAsyncReadyCallback callback, gpointer user_data)
{
    if (!g_file_has_uri_scheme(file, "imagekit-probe")) {
        __real_g_file_replace_async(file, etag, backup, flags, priority, cancel, callback,
                                    user_data);
        return;
    }

    g_assert_null(etag);
    g_assert_false(backup);
    g_assert_cmpint(flags, ==, G_FILE_CREATE_NONE);
    g_autoptr(GTask) task = g_task_new(file, cancel, callback, user_data);
    g_task_set_source_tag(task, (gpointer)&replace_tag);
    g_autofree char *fault = g_file_get_basename(file);
    if (g_str_equal(fault, "open-denied")) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                "Injected replacement open failure");
        return;
    }

    ProbeStream *stream = g_object_new(probe_stream_get_type(), NULL);
    stream->fault = g_steal_pointer(&fault);
    g_task_return_pointer(task, stream, g_object_unref);
}

GFileOutputStream *__wrap_g_file_replace_finish(GFile *file, GAsyncResult *result, GError **error)
{
    if (g_async_result_is_tagged(result, (gpointer)&replace_tag)) {
        return g_task_propagate_pointer(G_TASK(result), error);
    }
    return __real_g_file_replace_finish(file, result, error);
}

GBytes *__real_g_bytes_new_take(gpointer data, gsize size);
GBytes *__wrap_g_bytes_new_take(gpointer data, gsize size);

GBytes *__wrap_g_bytes_new_take(gpointer data, gsize size)
{
    gint64 started = g_get_monotonic_time();
    GBytes *bytes = __real_g_bytes_new_take(data, size);
    gint64 elapsed = g_get_monotonic_time() - started;

    /* The allocation and completed-output validation precede this ownership transfer. */
    g_print("ImageKit-profile gbytes-construction %" G_GINT64_FORMAT " %zu\n", elapsed, size);
    return bytes;
}
