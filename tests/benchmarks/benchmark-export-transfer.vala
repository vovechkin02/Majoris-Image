[CCode (cname = "image_export_transfer_async", finish_name = "image_export_transfer_finish",
        cheader_filename = "export-transfer.h")]
private extern async Bytes? transfer_output (string path, File destination, void* protected_source,
                                            int64 limit,
                                            Cancellable cancel) throws Error;

[CCode (cname = "image_export_prepare_async", finish_name = "image_export_prepare_finish",
        cheader_filename = "export-transfer.h")]
private extern async void* prepare_export (File source, File destination, Cancellable cancel)
    throws Error;

[CCode (cname = "image_export_source_free", cheader_filename = "export-transfer.h")]
private extern void release_export_source (void* protected_source);

private void record_phase (string stage, int64 started, int64 bytes) {
    stdout.printf ("ImageKit-profile %s %" + int64.FORMAT + " %" + int64.FORMAT + "\n",
        stage, get_monotonic_time () - started, bytes);
}

private async void probe_stream (string mode, File input, File target, Cancellable cancel,
                                 bool staging) throws Error {
    const int CHUNK_BYTES = 256 * 1024;
    var reader = yield input.read_async ();
    var stream = staging
        ? yield target.create_async (FileCreateFlags.PRIVATE, Priority.DEFAULT, cancel)
        : yield target.replace_async (null, false, FileCreateFlags.NONE, Priority.DEFAULT, cancel);
    var buffer = new uint8[CHUNK_BYTES];
    int64 written = 0;
    int64 uploading = get_monotonic_time ();
    while (true) {
        ssize_t count = yield reader.read_async (buffer);
        if (count == 0) {
            if (mode == "remote-cancel-before-close") {
                cancel.cancel ();
            }
            break;
        }
        size_t consumed;
        yield stream.write_all_async (buffer[:count], Priority.DEFAULT, cancel, out consumed);
        written += (int64) consumed;
        if (mode == "remote-cancel-write" ||
            (mode == "remote-cancel-midway" && written >= 8 * 1024 * 1024)) {
            cancel.cancel ();
            break;
        }
    }
    record_phase ("writes", uploading, written);
    record_phase ("payload-allocation", get_monotonic_time (), CHUNK_BYTES);
    int64 closing = get_monotonic_time ();
    yield stream.close_async (Priority.DEFAULT, cancel);
    record_phase ("close", closing, written);
}

private async void probe_remote (string mode, File input, File destination) throws Error {
    const int64 MAX_EXPORT_BYTES = 268435456;
    if (destination.is_native ()) {
        throw new IOError.INVALID_ARGUMENT ("The probe requires a non-native destination");
    }
    var cancel = new Cancellable ();
    int64 canceled_at = 0;
    ulong cancellation = cancel.cancelled.connect (() => {
        if (canceled_at == 0) {
            canceled_at = get_monotonic_time ();
        }
    });
    uint watch = Unix.signal_add (Posix.Signal.USR1, () => {
        cancel.cancel ();
        return Source.CONTINUE;
    });
    int64 started = get_monotonic_time ();
    try {
        if (mode.has_prefix ("remote-fallback")) {
            if (mode.has_suffix ("-precanceled")) {
                cancel.cancel ();
            }
            var payload = yield transfer_output (input.get_path (), destination, null,
                MAX_EXPORT_BYTES, cancel);
            assert (payload != null);
            int64 size = (int64) payload.get_size ();
            record_phase ("validation-and-bytes", started, size);
            record_phase ("payload-allocation", get_monotonic_time (), size);
            cancel.set_error_if_cancelled ();
            int64 publishing = get_monotonic_time ();
            yield destination.replace_contents_bytes_async (payload, null, false,
                FileCreateFlags.NONE, cancel, null);
            record_phase ("export-replacement", publishing, size);
            if (mode == "remote-fallback-cancel-after-success") {
                cancel.cancel ();
            }
        } else {
            // This probe deliberately exercises GIO streams without Core validation.
            // It must only be used with generated fixtures and disposable destinations.
            bool push = mode == "remote-push-stage";
            bool staging = push || mode == "remote-stage";
            var target = destination;
            if (staging) {
                target = destination.get_parent ().get_child (".probe-" + Uuid.string_random ());
            }
            try {
                if (push) {
                    int64 uploading = get_monotonic_time ();
                    yield input.copy_async (target, FileCopyFlags.NOFOLLOW_SYMLINKS,
                        Priority.DEFAULT, cancel, null);
                    record_phase ("copy", uploading, 1);
                } else {
                    yield probe_stream (mode, input, target, cancel, staging);
                }
                if (staging) {
                    int64 moving = get_monotonic_time ();
                    yield target.move_async (destination,
                        FileCopyFlags.OVERWRITE | FileCopyFlags.NO_FALLBACK_FOR_MOVE,
                        Priority.DEFAULT, cancel, null);
                    record_phase ("move", moving, 1);
                }
            } catch (Error error) {
                if (staging) {
                    try {
                        yield target.delete_async ();
                    } catch (Error cleanup) {
                        stderr.printf ("Staging cleanup: %s\n", cleanup.message);
                    }
                }
                throw error;
            }
        }
        record_phase ("workload", started, 1);
    } catch (IOError.CANCELLED error) {
        if (canceled_at != 0) {
            record_phase ("cancellation-cleanup", canceled_at, 0);
        }
        throw error;
    } finally {
        Source.remove (watch);
        cancel.disconnect (cancellation);
    }
}

private async void transfer (bool baseline, File input, File destination, Cancellable cancel,
                             bool change_source = false) throws Error {
    const int64 MAX_EXPORT_BYTES = 268435456;
    int64 started = get_monotonic_time ();
    if (baseline) {
        var info = yield input.query_info_async ("standard::type,standard::size",
            FileQueryInfoFlags.NOFOLLOW_SYMLINKS, Priority.DEFAULT, cancel);
        if (info.get_file_type () != FileType.REGULAR ||
            info.get_size () < 1 || info.get_size () > MAX_EXPORT_BYTES) {
            throw new IOError.INVALID_DATA ("Invalid benchmark fixture");
        }
        var bytes = yield input.load_bytes_async (cancel, null);
        int64 size = (int64) bytes.get_size ();
        record_phase ("output-read", started, size);
        int64 replacement_started = get_monotonic_time ();
        yield destination.replace_contents_bytes_async (bytes, null, false, FileCreateFlags.NONE,
            cancel, null);
        record_phase ("export-replacement", replacement_started, size);
        record_phase ("payload-allocation", get_monotonic_time (), size);
    } else {
        void* protected_source = yield prepare_export (input, destination, cancel);
        record_phase ("export-identity", started, 1);
        int64 transfer_started = get_monotonic_time ();
        try {
            if (change_source) {
                input.set_attribute_uint64 (FileAttribute.TIME_MODIFIED, 1,
                    FileQueryInfoFlags.NONE, cancel);
                transfer_started = get_monotonic_time ();
            }
            yield transfer_output (input.get_path (), destination, protected_source,
                MAX_EXPORT_BYTES, cancel);
            if (change_source) {
                throw new IOError.FAILED ("Published output after changing the source");
            }
        } catch (IOError.BUSY error) {
            if (!change_source || !error.message.has_prefix ("Image source changed")) {
                throw error;
            }
        } finally {
            record_phase ("export-native-transfer", transfer_started, 1);
            release_export_source (protected_source);
        }
        record_phase ("payload-allocation", get_monotonic_time (), int64.min (
            256 * 1024, input.query_info (FileAttribute.STANDARD_SIZE, 0).get_size ()));
    }
    record_phase ("workload", started, 1);
}

private async void measure_transfer (string mode, string path) throws Error {
    bool baseline = mode.has_prefix ("transfer-baseline");
    bool cancel_transfer = mode.has_suffix ("-cancel");
    var input = File.new_for_path (path);
    string directory = Environment.get_tmp_dir ();
    string destination_path = Path.build_filename (directory, "benchmark-export");
    var destination = File.new_for_path (destination_path);
    if (mode.has_suffix ("-symlink")) {
        var referent = File.new_for_path (Path.build_filename (directory, "benchmark-referent"));
        referent.replace_contents ("existing".data, null, false, FileCreateFlags.NONE, null);
        destination.make_symbolic_link ("benchmark-referent");
    } else if (!mode.has_suffix ("-new")) {
        destination.replace_contents ("existing".data, null, false, FileCreateFlags.NONE, null);
    }
    var cancel = new Cancellable ();
    if (!cancel_transfer) {
        yield transfer (baseline, input, destination, cancel, mode == "transfer-stream-changed");
        return;
    }

    int64 canceled_at = 0;
    uint timer = Timeout.add (5, () => {
        canceled_at = get_monotonic_time ();
        cancel.cancel ();
        return Source.REMOVE;
    });
    try {
        yield transfer (baseline, input, destination, cancel);
        throw new IOError.FAILED ("The benchmark transfer completed before cancellation");
    } catch (IOError.CANCELLED error) {
        assert (canceled_at > 0);
        record_phase ("cancellation-cleanup", canceled_at, 0);
    } finally {
        if (canceled_at == 0) {
            Source.remove (timer);
        }
    }
}

private async void run (string mode, string path, string? uri) throws Error {
    if (uri != null) {
        yield probe_remote (mode, File.new_for_path (path), File.new_for_uri (uri));
    } else {
        yield measure_transfer (mode, path);
    }
}

int main (string[] args) {
    bool remote = args.length == 4 && args[1] in new string[] {
        "remote-fallback", "remote-fallback-precanceled", "remote-fallback-cancel-after-success",
        "remote-stream", "remote-stage",
        "remote-push-stage", "remote-cancel-write", "remote-cancel-midway",
        "remote-cancel-before-close"
    };
    if (!remote && (args.length != 3 || !(args[1] in new string[] {
        "transfer-baseline", "transfer-stream",
        "transfer-baseline-cancel", "transfer-stream-cancel",
        "transfer-baseline-new", "transfer-stream-new",
        "transfer-baseline-symlink", "transfer-stream-symlink", "transfer-stream-changed"
    }))) {
        stderr.printf ("Usage: benchmark-export-transfer MODE INPUT [REMOTE_URI]\n");
        return 2;
    }
    var loop = new MainLoop ();
    int status = 0;
    run.begin (args[1], args[2], remote ? args[3] : null, (object, result) => {
        try {
            run.end (result);
        } catch (Error error) {
            stderr.printf ("%s:%d: %s\n", error.domain.to_string (), error.code, error.message);
            status = (remote && error is IOError.CANCELLED) ? 3 : 1;
        }
        loop.quit ();
    });
    loop.run ();
    return status;
}
