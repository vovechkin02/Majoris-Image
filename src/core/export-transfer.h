#pragma once

#include <gio/gio.h>

typedef struct _ExportSource ExportSource;

G_GNUC_INTERNAL void image_export_prepare_async(GFile *source, GFile *destination,
                                               GCancellable *cancel,
                                               GAsyncReadyCallback callback, gpointer user_data);
/* Finish transfers ownership of the pin. Successful NULL means no native identity is available. */
G_GNUC_INTERNAL ExportSource *image_export_prepare_finish(GAsyncResult *result, GError **error);
G_GNUC_INTERNAL void image_export_source_free(ExportSource *source);
/* The caller owns the returned close-on-exec descriptor. */
G_GNUC_INTERNAL int image_export_source_dup(const ExportSource *source, GError **error);

/* Keep source alive until the callback. It may be NULL when local identity is unavailable. */
G_GNUC_INTERNAL void image_export_transfer_async(const char *path, GFile *destination,
                                                const ExportSource *source, gint64 limit,
                                                GCancellable *cancel,
                                                GAsyncReadyCallback callback, gpointer user_data);
/* Successful NULL means native publication completed. Otherwise, finish transfers ownership
 * of the validated bytes with the input already closed. The caller publishes them through GIO,
 * which retains its own reference for the asynchronous replacement. */
G_GNUC_INTERNAL GBytes *image_export_transfer_finish(GAsyncResult *result, GError **error);
