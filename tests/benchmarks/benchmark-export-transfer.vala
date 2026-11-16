[CCode (cname = "image_export_transfer_async", finish_name = "image_export_transfer_finish",
        cheader_filename = "export-transfer.h")]
private extern async Bytes? transfer_output (string path, File destination, int64 limit,
                                            Cancellable cancel) throws Error;

private void record_phase (string stage, int64 started, int64 bytes) {
    stdout.printf ("ImageKit-profile %s %" + int64.FORMAT + " %" + int64.FORMAT + "\n",
        stage, get_monotonic_time () - started, bytes);
}

private async void transfer (bool baseline, File input, File destination, Cancellable cancel)
    throws Error {
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
        yield transfer_output (input.get_path (), destination, MAX_EXPORT_BYTES, cancel);
        record_phase ("payload-allocation", get_monotonic_time (), int64.min (
            256 * 1024, input.query_info (FileAttribute.STANDARD_SIZE, 0).get_size ()));
    }
    record_phase ("workload", started, 1);
}

private async void measure_transfer (string mode, string path) throws Error {
    bool baseline = mode.has_prefix ("transfer-baseline");
    bool cancel_transfer = mode.has_suffix ("-cancel");
    var input = File.new_for_path (path);
    var destination = File.new_for_path (
        Path.build_filename (Environment.get_tmp_dir (), "benchmark-export"));
    destination.replace_contents ("existing".data, null, false, FileCreateFlags.NONE, null);
    var cancel = new Cancellable ();
    if (!cancel_transfer) {
        yield transfer (baseline, input, destination, cancel);
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

int main (string[] args) {
    if (args.length != 3 || !(args[1] in new string[] {
        "transfer-baseline", "transfer-stream", "transfer-baseline-cancel", "transfer-stream-cancel"
    })) {
        stderr.printf ("Usage: benchmark-export-transfer MODE INPUT\n");
        return 2;
    }
    var loop = new MainLoop ();
    int status = 0;
    measure_transfer.begin (args[1], args[2], (object, result) => {
        try {
            measure_transfer.end (result);
        } catch (Error error) {
            stderr.printf ("%s\n", error.message);
            status = 1;
        }
        loop.quit ();
    });
    loop.run ();
    return status;
}
