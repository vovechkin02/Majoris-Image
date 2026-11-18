using ImageKit;

async void check (File fixtures) throws Error {
    var directory = File.new_for_path (DirUtils.make_tmp ("imagekit-jpeg-XXXXXX"));
    var registry = new DecoderRegistry ();
    var encoder = new EncoderRegistry ();
    var source = directory.get_child ("source.jpg");
    var seed = directory.get_child ("asymmetric.svg");
    FileUtils.set_contents (
        seed.get_path (),
        "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='16'>" +
        "<rect width='32' height='16' fill='white'/>" +
        "<rect width='16' height='8' fill='red'/>" +
        "<rect x='16' width='16' height='8' fill='green'/>" +
        "<rect y='8' width='16' height='8' fill='blue'/>" +
        "</svg>"
    );
    var options = new ExportOptions ();
    options.format = "jpeg";
    options.bit_depth = 8;
    options.quality = 95;
    yield encoder.encode (new ImageSource (seed), source, options);
    var original = yield source.load_bytes_async (null, null);
    var before = yield registry.open (new ImageSource (source));

    options.lossless = true;
    var transform = new RenderOptions ();
    transform.turns = 1;
    var first = directory.get_child ("clockwise.jpg");
    yield encoder.encode (new ImageSource (source), first, options, transform);
    var rotated = yield registry.open (new ImageSource (first));
    assert (rotated.width == before.height && rotated.height == before.width);
    transform.turns = 3;
    var restored = directory.get_child ("restored.jpg");
    yield encoder.encode (new ImageSource (first), restored, options, transform);
    var after = yield registry.open (new ImageSource (restored));
    assert (after.preview.bytes.compare (before.preview.bytes) == 0);
    assert ((yield source.load_bytes_async (null, null)).compare (original) == 0);

    foreach (int orientation in new int[] {1, 2, 3, 4, 5, 6, 7, 8}) {
        var input = directory.get_child ("oriented-%d.jpg".printf (orientation));
        yield source.copy_async (input, 0, Priority.DEFAULT, null, null);
        var metadata = new GExiv2.Metadata ();
        metadata.open_path (input.get_path ());
        metadata.try_set_orientation ((GExiv2.Orientation) orientation);
        metadata.save_file (input.get_path ());
        var oriented = yield registry.open (new ImageSource (input));
        var output = directory.get_child ("normalized-%d.jpg".printf (orientation));
        transform.turns = 1;
        yield encoder.encode (new ImageSource (input), output, options, transform);
        var result = yield registry.open (new ImageSource (output));
        assert (result.width == oriented.height && result.height == oriented.width);
        var expected = yield registry.render (new ImageSource (input), transform, 512);
        assert (expected.width == result.preview.width && expected.height == result.preview.height);
        float* reference = (float*) expected.bytes.get_data ();
        float* actual = (float*) result.preview.bytes.get_data ();
        for (int sample = 0; sample < expected.width * expected.height * 4; sample++) {
            assert (Math.fabs (reference[sample] - actual[sample]) < 0.035);
        }
        metadata.open_path (output.get_path ());
        assert (metadata.try_get_orientation () == GExiv2.Orientation.NORMAL);
    }

    transform.exposure = 1;
    try {
        yield encoder.encode (new ImageSource (source), directory.get_child ("invalid.jpg"),
                              options, transform);
        assert_not_reached ();
    } catch (ImageError.UNSUPPORTED error) {
        // The unsupported edit must be rejected.
    }
    transform.exposure = 0;
    options.lossless = false;
    options.width = 239;
    options.height = 159;
    var odd = directory.get_child ("odd.jpg");
    yield encoder.encode (new ImageSource (fixtures.get_child ("vector.svg")), odd, options);
    options.width = options.height = 0;
    options.lossless = true;
    try {
        yield encoder.encode (new ImageSource (odd), directory.get_child ("must-not-exist.jpg"),
                              options, transform);
        assert_not_reached ();
    } catch (ImageError.UNSUPPORTED error) {
        // An imperfect coefficient transform must be rejected.
    }
    assert (!directory.get_child ("must-not-exist.jpg").query_exists ());
    var cancel = new Cancellable ();
    cancel.cancel ();
    try {
        yield encoder.encode (new ImageSource (source), directory.get_child ("cancelled.jpg"),
                              options, transform, cancel);
        assert_not_reached ();
    } catch (IOError.CANCELLED error) {
        // The canceled export must not publish an output.
    }
    assert (!directory.get_child ("cancelled.jpg").query_exists ());
    try {
        yield encoder.encode (new ImageSource (source), source, options, transform);
        assert_not_reached ();
    } catch (IOError.INVALID_ARGUMENT error) {
        // Source/destination aliasing must be rejected.
    }
}

int main (string[] args) {
    var loop = new MainLoop ();
    int status = 0;
    check.begin (File.new_for_path (args[1]), (obj, result) => {
        try {
            check.end (result);
        } catch (Error error) {
            stderr.printf ("%s\n", error.message);
            status = 1;
        }
        loop.quit ();
    });
    loop.run ();
    return status;
}
