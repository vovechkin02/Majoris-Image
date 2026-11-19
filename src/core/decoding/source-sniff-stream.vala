namespace ImageKit {
    // Owns the close after a sniffing read, including failed and canceled reads.
    /** Reads a bounded prefix of the source stream for format signature recognition. */
    internal async Bytes? read_sniff_stream (InputStream stream, size_t count, int priority,
        Cancellable stop, bool from_end = false) throws Error {
        Bytes? bytes = null;
        Error? close_error = null;
        try {
            if (from_end) {
                var seekable = stream as Seekable;
                if (seekable == null || !seekable.can_seek () ||
                    !seekable.seek (-(int64) count, SeekType.END, stop)) {
                    return null;
                }
            }
            bytes = yield stream.read_bytes_async (count, priority, stop);
        } finally {
            try {
                yield stream.close_async (priority, null);
            } catch (Error error) {
                if (bytes == null) {
                    debug ("Cannot close source sniffing stream: %s", error.message);
                } else {
                    close_error = error;
                }
            }
        }
        stop.set_error_if_cancelled ();
        if (close_error != null) {
            throw close_error;
        }
        return bytes;
    }
}
