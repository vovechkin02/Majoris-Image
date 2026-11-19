#pragma once

#include <gio/gio.h>

/** Opaque source pin retained to prevent export-time path substitution. */
typedef struct _ExportSource ExportSource;

/**
 * image_export_prepare_async:
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 * @destination: GIO destination to validate and publish the output into.
 * @cancel: Optional cancellation token for pending asynchronous work.
 * @callback: Completion callback invoked when the asynchronous operation finishes.
 * @user_data: Opaque caller data passed back to the completion callback.
 *
 * Begins asynchronous export-source validation and descriptor pinning.
 *
 * Returns: No value; complete the operation using image_export_prepare_finish().
 */
G_GNUC_INTERNAL void image_export_prepare_async(GFile *source, GFile *destination,
                                               GCancellable *cancel,
                                               GAsyncReadyCallback callback, gpointer user_data);
/* Finish transfers ownership of the pin. Successful NULL means the source is non-native. */
/**
 * image_export_prepare_finish:
 * @result: Result passed to the matching asynchronous finish method.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Returns the prepared source pin or NULL for a non-native source.
 *
 * Returns: An owned source pin, or NULL on successful non-native preparation; free the pin.
 */
G_GNUC_INTERNAL ExportSource *image_export_prepare_finish(GAsyncResult *result, GError **error);
/**
 * image_export_source_free:
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 *
 * Releases the native source pin and any owned file descriptors.
 */
G_GNUC_INTERNAL void image_export_source_free(ExportSource *source);
/* The caller owns the returned close-on-exec descriptor. */
/**
 * image_export_source_dup:
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Duplicates the pinned source descriptor; caller owns the returned descriptor.
 *
 * Returns: A new close-on-exec file descriptor owned by the caller, or -1 on failure.
 */
G_GNUC_INTERNAL int image_export_source_dup(const ExportSource *source, GError **error);

/* Keep source alive until the callback. It may be NULL when local identity is unavailable. */
/**
 * image_export_transfer_async:
 * @path: Filesystem path to the source, temporary output or target resource.
 * @destination: GIO destination to validate and publish the output into.
 * @source: Source filename, GIO file or source pin as specified by the declaration.
 * @limit: Maximum accepted size in bytes; oversized output is rejected.
 * @cancel: Optional cancellation token for pending asynchronous work.
 * @callback: Completion callback invoked when the asynchronous operation finishes.
 * @user_data: Opaque caller data passed back to the completion callback.
 *
 * Begins a guarded transfer to the requested GIO destination.
 *
 * Returns: No value; complete the operation using image_export_transfer_finish().
 */
G_GNUC_INTERNAL void image_export_transfer_async(const char *path, GFile *destination,
                                                const ExportSource *source, gint64 limit,
                                                GCancellable *cancel,
                                                GAsyncReadyCallback callback, gpointer user_data);
/* Successful NULL means native publication completed. Otherwise, finish transfers ownership
 * of the validated bytes after closing the input and checking the source pin, if supplied.
 * The caller keeps the bytes alive through asynchronous GIO publication. */
/**
 * image_export_transfer_finish:
 * @result: Result passed to the matching asynchronous finish method.
 * @error: Receives failure details when the operation cannot complete.
 *
 * Returns owned bytes for remote publication or NULL after native publication.
 *
 * Returns: Owned GBytes for remote publication, or NULL after successful native publication.
 */
G_GNUC_INTERNAL GBytes *image_export_transfer_finish(GAsyncResult *result, GError **error);
