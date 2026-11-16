using ImageKit;

async void measure (string mode, ImageSource source) throws Error {
    int64 began = get_monotonic_time ();
    var registry = new DecoderRegistry ();
    var options = new DecodeOptions ();
    switch (mode) {
    case "thumbnail":
        var png = yield new ThumbnailService ().generate (source, 256);
        stdout.printf ("PNG bytes %zu\n", png.get_size ());
        break;
    case "warm":
    case "icc":
    case "animation":
    case "preload":
    case "navigate":
    case "tiles":
    case "full":
    case "open":
    case "tile":
    case "hdr-sdr":
    case "raw-preview":
    case "raw-develop":
        options.raw_preview_only = mode == "raw-preview";
        options.sdr_presentation = mode == "hdr-sdr";
        if (mode == "full") {
            options.preview_size = 4096;
        }
        var document = yield registry.open (source, options);
        stdout.printf ("%s %d × %d\n", document.decoder, document.width, document.height);
        if (mode == "warm" || mode == "preload" || mode == "navigate") {
            var preload = new PreloadManager.with_registry (registry);
            var neighbor = new ImageSource (source.file.get_parent ().get_child ("multipart.exr"));
            yield preload.preload (mode == "warm" ? new ImageSource[] { source } : new ImageSource[] { source, neighbor }, options);
            var warmed = yield preload.open (source, options);
            if (mode == "warm") {
                int64 warm_start = get_monotonic_time ();
                for (int request = 0; request < 20; request++) {
                    assert (warmed == (yield preload.open (source, options)));
                }
                stdout.printf ("Warm-open-seconds %.9f\n",
                    (get_monotonic_time () - warm_start) / 20000000.0);
            } else {
                assert (warmed == (yield preload.open (source, options)));
            }
            if (mode == "navigate") {
                for (int i = 0; i < 12; i++) {
                    yield preload.open (i % 2 == 0 ? source : neighbor, options);
                }
            }
        }
        if (mode == "animation") {
            for (int frame = 1; frame < document.frame_count; frame++) {
                yield document.open_frame (frame);
            }
            assert (document.frame_count > 1);
        }
        if (mode == "tiles") {
            for (int tile = 0; tile < 8; tile++) {
                yield document.render_region (tile * 512, 512, 512, 512, 512);
            }
        }
        if (mode == "tile") {
            int width = int.min (512, document.width), height = int.min (512, document.height);
            var tile = yield document.render_region (document.width - width, document.height - height, width, height, 512);
            stdout.printf ("Region bytes %zu\n", tile.bytes.get_size ());
        }
        break;
    default:
        throw new IOError.INVALID_ARGUMENT ("Unknown benchmark mode");
    }
    stdout.printf ("%s %.3f s\n", mode, (get_monotonic_time () - began) / 1000000.0);
}

int main (string[] args) {
    if (args.length != 3) {
        stderr.printf ("Usage: majoris-image-benchmark MODE FILE\n");
        return 2;
    }
    var source = new ImageSource (File.new_for_commandline_arg (args[2]));
    var loop = new MainLoop ();
    int result = 0;
    measure.begin (args[1], source, (obj, response) => {
        try {
            measure.end (response);
        } catch (Error e) {
            stderr.printf ("%s\n", e.message);
            result = 1;
        }
        loop.quit ();
    });
    loop.run ();
    return result;
}
