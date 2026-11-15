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
    case "full":
    case "open":
    case "tile":
    case "hdr-sdr":
    case "raw-preview":
    case "raw-develop":
        options.raw_preview_only = mode == "raw-preview";
        options.sdr_presentation = mode == "hdr-sdr";
        if (mode == "full") options.preview_size = 4096;
        var document = yield registry.open (source, options);
        stdout.printf ("%s %d × %d\n", document.decoder, document.width, document.height);
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
        try { measure.end (response); }
        catch (Error e) { stderr.printf ("%s\n", e.message); result = 1; }
        loop.quit ();
    });
    loop.run ();
    return result;
}
