#pragma once

#include <gio/gio.h>

/**
 * worker_output_read_async:
 * @path: Filesystem path to the source, temporary output or target resource.
 * @limit: Maximum accepted size in bytes; oversized output is rejected.
 * @cancel: Optional cancellation token for pending asynchronous work.
 * @callback: Completion callback invoked when the asynchronous operation finishes.
 * @user_data: Opaque caller data passed back to the completion callback.
 *
 * Starts bounded asynchronous reading of a validated worker output file.
 *
 * Returns: No value; results are delivered to the provided GAsyncReadyCallback.
 */
G_GNUC_INTERNAL void worker_output_read_async(const char *path, gint64 limit, GCancellable *cancel,
                                              GAsyncReadyCallback callback, gpointer user_data);
/* Finish transfers ownership of the validated bytes. */
/**
 * worker_output_read_finish:
 * @result: Result passed to the matching asynchronous finish method.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Returns validated bytes owned by the caller or propagates the read failure.
 *
 * Returns: Validated owned GBytes or NULL on error; the caller releases the bytes.
 */
G_GNUC_INTERNAL GBytes *worker_output_read_finish(GAsyncResult *result, GError **error);
