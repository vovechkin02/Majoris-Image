#pragma once

#include <gio/gio.h>

G_GNUC_INTERNAL void worker_output_read_async(const char *path, gint64 limit, GCancellable *cancel,
                                              GAsyncReadyCallback callback, gpointer user_data);
/* Finish transfers ownership of the validated bytes. */
G_GNUC_INTERNAL GBytes *worker_output_read_finish(GAsyncResult *result, GError **error);
