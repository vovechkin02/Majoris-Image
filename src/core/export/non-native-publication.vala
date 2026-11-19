namespace ImageKit {
    /** Writes validated bytes to a non-native destination and checks close-time errors. */
    internal async void publish_non_native_async (File destination, Bytes payload,
                                                  Cancellable stop) throws Error {
        stop.set_error_if_cancelled ();
        int64 opening = OperationProfile.begin ();
        var stream = yield destination.replace_async (null, false, FileCreateFlags.NONE,
            Priority.DEFAULT, stop);
        OperationProfile.record ("export-remote-open", opening);

        // The coroutine owns payload; the write borrows its backing storage.
        unowned uint8[] data = payload.get_data ();
        int64 writing = OperationProfile.begin ();
        try {
            try {
                yield stream.write_all_async (data, Priority.DEFAULT, stop, null);
            } finally {
                OperationProfile.record ("export-remote-write", writing);
            }
        } catch (Error write_error) {
            // GIO has no generic abort. Request canceled cleanup even if the
            // caller is still active. The backend may still publish buffered data.
            var cleanup = new Cancellable ();
            cleanup.cancel ();
            try {
                yield stream.close_async (Priority.DEFAULT, cleanup);
            } catch (Error close_error) {
                debug ("Replacement stream cleanup: %s", close_error.message);
            }
            throw write_error;
        }

        // Some backends submit the remote write in close.
        int64 closing = OperationProfile.begin ();
        try {
            yield stream.close_async (Priority.DEFAULT, stop);
        } finally {
            OperationProfile.record ("export-remote-close", closing);
        }
    }
}
