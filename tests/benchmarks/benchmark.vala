using ImageKit;

private class BenchmarkBackend : Object, DecoderBackend {
    private string worker;

    public BenchmarkBackend (string worker) {
        this.worker = worker;
    }

    public string id {
        get {
            return "benchmark-png";
        }
    }

    public string executable {
        get {
            return worker;
        }
    }

    public ImageCapabilities capabilities {
        get {
            return ImageCapabilities.STILL | ImageCapabilities.METADATA |
                ImageCapabilities.COLOR_MANAGED;
        }
    }

    public string[] mime_types {
        owned get {
            return { "image/png" };
        }
    }

    public bool accepts (string mime, Bytes signature) {
        return mime == "image/png" && signature.get_size () >= 8;
    }
}

private void record_phase (string stage, int64 started) {
    stdout.printf ("ImageKit-profile %s %" + int64.FORMAT + " 1\n",
        stage, get_monotonic_time () - started);
}

private async void measure_export (string workload, ImageSource source, File? remote)
    throws Error {
    var fields = workload.split ("-", 2);
    var format = fields[0];
    var action = fields.length == 2 ? fields[1] : "";
    var options = new ExportOptions ();
    options.format = format;
    options.lossless = true;
    options.hdr = format == "uhdr";
    var transform = new RenderOptions ();
    // The shared large TIFF fixture contributes a 2500 × 1250 region.
    transform.crop_width = 0.125;
    transform.crop_height = 0.125;
    var destination = remote ?? File.new_for_path (
        Path.build_filename (Environment.get_tmp_dir (), "benchmark-export"));
    if (remote == null) {
        destination.replace_contents ("existing".data, null, false, FileCreateFlags.NONE, null);
    }
    int64 started = get_monotonic_time ();
    var cancel = new Cancellable ();
    uint watch = Unix.signal_add (Posix.Signal.USR1, () => {
        cancel.cancel ();
        return Source.CONTINUE;
    });
    try {
        if (action == "precanceled") {
            cancel.cancel ();
        }
        yield new EncoderRegistry ().encode (source, destination, options, transform, cancel);
        if (action == "cancel-after-success") {
            cancel.cancel ();
        }
    } finally {
        stdout.printf ("Caller canceled: %s\n", cancel.is_cancelled ().to_string ());
        Source.remove (watch);
    }
    record_phase ("workload", started);
}

private bool job_file_exists (string filename, int minimum_size) throws Error {
    var temporary_path = Environment.get_tmp_dir ();
    var temporary = Dir.open (temporary_path);
    string? name;
    while ((name = temporary.read_name ()) != null) {
        if (!name.has_prefix ("majoris-image-") || name.has_prefix ("majoris-image-source-")) {
            continue;
        }
        var file = File.new_for_path (Path.build_filename (temporary_path, name, filename));
        try {
            if (file.query_info (FileAttribute.STANDARD_SIZE, 0).get_size () >= minimum_size) {
                return true;
            }
        } catch (IOError.NOT_FOUND error) {
            // This job has yet to reach the requested stage.
        }
    }
    return false;
}

private async void measure_cancellation (ImageSource source, DecoderRegistry registry,
    string readiness_file, int minimum_size) throws Error {
    var cancel = new Cancellable ();
    int64 canceled_at = 0;
    var ready = false;
    Error? probe_error = null;
    var deadline = get_monotonic_time () + 5 * TimeSpan.SECOND;
    uint timer = Timeout.add (5, () => {
        if (canceled_at != 0) {
            return Source.CONTINUE;
        }
        try {
            ready = job_file_exists (readiness_file, minimum_size);
            if (!ready && get_monotonic_time () < deadline) {
                return Source.CONTINUE;
            }
        } catch (Error error) {
            probe_error = error;
        }
        canceled_at = get_monotonic_time ();
        cancel.cancel ();
        return Source.CONTINUE;
    });
    try {
        yield registry.open (source, null, cancel);
        throw new IOError.FAILED ("The request completed before cancellation");
    } catch (IOError.CANCELLED error) {
        if (probe_error != null) {
            throw probe_error;
        }
        if (!ready) {
            throw new IOError.TIMED_OUT ("The job did not reach the cancellation stage");
        }
        record_phase ("cancellation-cleanup", canceled_at);
    } finally {
        Source.remove (timer);
    }
}

private async void verify_remote_staging_failure (string mode, ImageSource source,
    DecoderRegistry registry) throws Error {
    try {
        yield registry.open (source);
    } catch (Error error) {
        if ((mode == "gvfs-limit" && error is ImageError.MEMORY) ||
            (mode == "gvfs-write-error" && error is IOError.FAILED)) {
            return;
        }
        throw error;
    }
    throw new IOError.FAILED ("The remote staging failure was not reported");
}

private async ImageSource stage_stream (File file) throws Error {
    var input = yield file.read_async ();
    ImageSource staged = null;
    try {
        staged = yield ImageSource.from_stream (input);
    } finally {
        try {
            yield input.close_async ();
        } catch (Error error) {
            stderr.printf ("%s\n", error.message);
        }
    }
    return staged;
}

private async void measure_preloading (string mode, ImageSource source,
    DecoderRegistry registry, DecodeOptions options) throws Error {
    var preload = new PreloadManager.with_registry (registry);
    var neighbor = new ImageSource (source.file.get_parent ().get_child ("multipart.exr"));
    var neighbors = mode == "warm" ? new ImageSource[] { source }
        : new ImageSource[] { source, neighbor };
    yield preload.preload (neighbors, options);
    var warmed = yield preload.open (source, options);
    if (mode == "warm") {
        int64 started = get_monotonic_time ();
        for (int request = 0; request < 20; request++) {
            assert (warmed == (yield preload.open (source, options)));
        }
        stdout.printf ("Warm-open-seconds %.9f\n",
            (get_monotonic_time () - started) / 20000000.0);
    } else {
        assert (warmed == (yield preload.open (source, options)));
    }
    if (mode == "navigate") {
        for (int i = 0; i < 12; i++) {
            yield preload.open (i % 2 == 0 ? source : neighbor, options);
        }
    }
}

private async void measure (string mode, ImageSource source, File? remote) throws Error {
    if (mode.has_prefix ("export-")) {
        yield measure_export (mode.substring ("export-".length), source, remote);
        return;
    }
    int64 began = get_monotonic_time ();
    var registry = new DecoderRegistry ();
    var options = new DecodeOptions ();
    if (mode == "render") {
        var transform = new RenderOptions ();
        transform.turns = 1;
        var pixels = yield registry.render (source, transform, 1024);
        stdout.printf ("Rendered %d × %d, %zu bytes\n", pixels.width, pixels.height,
            pixels.bytes.get_size ());
    } else if (mode == "thumbnail") {
        var png = yield new ThumbnailService ().generate (source, 256);
        stdout.printf ("PNG bytes %zu\n", png.get_size ());
    } else if (mode == "cancel") {
        source = yield ImageSource.from_bytes (new Bytes ("T".data));
        yield measure_cancellation (source, registry, "ready", 0);
    } else if (mode == "gvfs-cancel") {
        assert (!source.file.is_native ());
        yield measure_cancellation (source, registry, "input", 1);
    } else if (mode == "gvfs-write-error" || mode == "gvfs-limit") {
        assert (!source.file.is_native ());
        yield verify_remote_staging_failure (mode, source, registry);
    } else {
        if (mode == "stream") {
            source = yield stage_stream (source.file);
        }
        if (mode == "custom") {
            registry.register_backend (new BenchmarkBackend (
                Environment.get_variable ("MAJORIS_IMAGE_WORKER") ?? ""));
        }
        if (mode == "fallback16") {
            string? fault_worker = Environment.get_variable ("IMAGEKIT_BENCHMARK_FAULT_WORKER");
            if (fault_worker == null) {
                throw new IOError.INVALID_ARGUMENT ("An RGB16 fallback benchmark needs a fault worker");
            }
            registry.register_backend (new BenchmarkBackend (fault_worker));
            options.required_capabilities |= ImageCapabilities.COLOR_MANAGED;
        }
        if (mode == "gvfs") {
            assert (!source.file.is_native ());
        }
        if (mode == "custom" || mode == "gvfs") {
            yield registry.inspect (source);
        }
        options.raw_preview_only = mode == "raw-preview";
        options.sdr_presentation = mode == "hdr-sdr";
        if (mode == "full") {
            options.preview_size = 4096;
        }
        int64 opening = get_monotonic_time ();
        var document = yield registry.open (source, options);
        if (mode == "fallback16") {
            record_phase ("fallback-open", opening);
        }
        stdout.printf ("%s %d × %d\n", document.decoder, document.width, document.height);
        switch (mode) {
            case "warm":
            case "preload":
            case "navigate":
                yield measure_preloading (mode, source, registry, options);
                break;
            case "animation":
                assert (document.frame_count > 1);
                for (int frame = 1; frame < document.frame_count; frame++) {
                    yield document.open_frame (frame);
                }
                break;
            case "animation-apng-10":
            case "animation-apng-50":
            case "animation-apng-100":
                assert (document.animated && document.mime_type == "image/apng");
                // Preserve the initial document as the owning cursor. Each forward
                // request advances it once, and the final sample identifies output.
                int count = int.parse (mode.substring ("animation-apng-".length));
                assert (document.frame_count == count);
                ImageDocument latest = document;
                for (int frame = 1; frame < count; frame++) {
                    latest = yield document.open_frame (frame);
                }
                stdout.printf ("Animation-last-sha256 %s\n",
                    Checksum.compute_for_bytes (ChecksumType.SHA256, latest.preview.bytes));
                break;
            case "animation-apng":
                assert (document.animated && document.mime_type == "image/apng");
                // Keep the same root document to exercise the forward cursor.
                // Rewinds and repeated passes must still yield matching pixels.
                for (int pass = 0; pass < 3; pass++) {
                    for (int frame = 1; frame < document.frame_count; frame++) {
                        yield document.open_frame (frame);
                    }
                    yield document.open_frame (0);
                }
                break;
            case "pages":
                assert (document.page_count > 1);
                for (int index = 1; index < document.page_count; index++) {
                    int64 started = get_monotonic_time ();
                    yield document.open_page (index);
                    record_phase ("additional-page", started);
                }
                break;
            case "tiles":
                for (int tile = 0; tile < 8; tile++) {
                    int64 started = get_monotonic_time ();
                    yield document.render_region (tile * 512, 512, 512, 512, 512);
                    record_phase ("region", started);
                }
                break;
            case "tile":
                int width = int.min (512, document.width);
                int height = int.min (512, document.height);
                int64 started = get_monotonic_time ();
                var tile = yield document.render_region (document.width - width,
                    document.height - height, width, height, 512);
                record_phase ("region", started);
                stdout.printf ("Region bytes %zu\n", tile.bytes.get_size ());
                break;
            case "fallback":
                assert (document.decoder == "Glycin");
                break;
            case "fallback16":
                assert (document.decoder == "Glycin" && document.bit_depth == 16 &&
                    document.color_managed);
                stdout.printf ("Fallback-pixels %s\nFallback-color %s\n",
                    Checksum.compute_for_bytes (ChecksumType.SHA256, document.preview.bytes),
                    document.color_diagnostic);
                break;
            case "stream":
            case "custom":
            case "gvfs":
            case "icc":
            case "full":
            case "open":
            case "jpeg":
            case "large-tiff":
            case "hdr-sdr":
            case "raw-preview":
            case "raw-develop":
                break;
            default:
                throw new IOError.INVALID_ARGUMENT ("Unknown benchmark mode");
        }
    }
    record_phase ("workload", began);
}

int main (string[] args) {
    if (args.length != 3 && !(args.length == 4 && args[1].has_prefix ("export-"))) {
        stderr.printf ("Usage: majoris-image-benchmark MODE FILE [EXPORT_URI]\n");
        return 2;
    }
    var source = new ImageSource (File.new_for_commandline_arg (args[2]));
    var remote = args.length == 4 ? File.new_for_uri (args[3]) : null;
    var loop = new MainLoop ();
    int result = 0;
    measure.begin (args[1], source, remote, (obj, response) => {
        try {
            measure.end (response);
        } catch (Error e) {
            stderr.printf ("%s:%d: %s\n", e.domain.to_string (), e.code, e.message);
            result = (remote != null && e is IOError.CANCELLED) ? 3 : 1;
        }
        loop.quit ();
    });
    loop.run ();
    return result;
}
