#pragma once

#include <gio/gio.h>

/* A NULL result with no error means the native destination was committed. */
G_GNUC_INTERNAL void image_export_transfer_async(const char *path, GFile *destination,
                                                gint64 limit, GCancellable *cancel,
                                                GAsyncReadyCallback callback, gpointer user_data);
G_GNUC_INTERNAL GBytes *image_export_transfer_finish(GAsyncResult *result, GError **error);
