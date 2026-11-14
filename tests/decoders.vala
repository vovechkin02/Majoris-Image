using MajorisImage;

private float sample (PixelBuffer data, int x, int channel) {
    unowned uint8[] bytes = data.bytes.get_data ();
    float value = 0;
    Memory.copy (&value, &bytes[16 * x + 4 * channel], 4);
    return value;
}

private class MissingQoiCodec : Object, DecoderBackend {
    private string path;
    public MissingQoiCodec (string path) { this.path = path; }
    public string id { get { return "test-missing-qoi"; } }
    public string executable { get { return path; } }
    public ImageCapabilities capabilities { get { return ImageCapabilities.STILL | ImageCapabilities.METADATA; } }
    public string[] mime_types { owned get { return { "image/qoi" }; } }
    public bool accepts (string mime, Bytes signature) {
        unowned uint8[] data = signature.get_data ();
        return data.length > 4 && data[0] == 'q' && data[1] == 'o' && data[2] == 'i' && data[3] == 'f';
    }
}

async void exercise (string[] args) throws Error {
    var registry = new DecoderRegistry ();
    int[] corner = { 4, 1, 5, 6, 4, 6, 5, 1 };
    for (int i = 0; i < 8; i++) {
        var path = Path.build_filename (Path.get_dirname (args[2]), "orientation-%d.tiff".printf (i + 1));
        var oriented = yield registry.open (new ImageSource (File.new_for_path (path)));
        assert (oriented.width == (i < 4 ? 3 : 2) && oriented.height == (i < 4 ? 2 : 3));
        for (int channel = 0; channel < 3; channel++)
            assert ((sample (oriented.preview, 0, channel) > 0.9f) == ((corner[i] & (4 >> channel)) != 0));
    }
    var sequence = new ImageSource (File.new_for_path (args[5]));
    try {
        var still = yield registry.open (sequence);
        if (!still.animated) {
            assert (still.timed_sequence && still.page_count == 1);
            assert (still.warning.contains ("still preview"));
            stdout.printf ("AVIF sequence: installed decoder supplies a still preview\n");
        }
    } catch (Error error) {
        if (!(error is ImageError.CODEC_MISSING) && !(error is ImageError.UNSUPPORTED)) throw error;
        stdout.printf ("AVIF sequence: no compatible decoder installed\n");
    }
    var apng = new ImageSource (File.new_for_path (args[2]));
    var animation = yield registry.open (apng);
    assert (animation.animated && animation.frame_count == 3 && animation.page_count == 1);
    assert (animation.frame_duration_ms == 40 && animation.loop_count == 1);
    assert (sample (animation.preview, 0, 0) > 0.99f);

    var qoi = new ImageSource (File.new_for_path (args[1]));
    string catalogue = yield registry.installed_codecs ();
    if (!catalogue.contains ("\"operation\":\"glycin\"")) {
        assert (args[3] != "enabled");
        stdout.printf ("Glycin loader unavailable; native APNG metadata passed\n");
        return;
    }
    registry.register_backend (new MissingQoiCodec (args[4]));
    var first = yield registry.open (qoi);
    assert (first.decoder == "Glycin" && first.width == 2 && first.height == 1);
    assert (first.bit_depth == 8 && first.has_alpha && first.pixel_buffer_bit_depth == 32);
    assert (sample (first.preview, 0, 0) > 0.99f);
    assert (Math.fabs (sample (first.preview, 0, 3) - 128.0 / 255) < 0.001);
    var probe = yield registry.inspect (qoi);
    assert (probe.loader == first.decoder && probe.depth == first.bit_depth);
    var options = new DecodeOptions ();
    options.required_capabilities = ImageCapabilities.HDR;
    try { yield registry.open (qoi, options); assert_not_reached (); }
    catch (ImageError.UNSUPPORTED error) { }
    catch (ImageError.CODEC_MISSING error) { }
    var stop = new Cancellable ();
    stop.cancel ();
    try { yield registry.open (qoi, null, stop); assert_not_reached (); }
    catch (IOError.CANCELLED error) { }
    var thumbnail = yield new ThumbnailService ().generate (qoi, 16);
    assert (thumbnail.get_size () > 45);

    var second = yield animation.open_frame (1);
    assert (second.frame == 1 && second.frame_duration_ms == 170);
    assert (sample (second.preview, 0, 0) > 0.1f && sample (second.preview, 0, 1) > 0.1f);
    assert (sample (second.preview, 1, 0) > 0.99f);
    var frame_options = new DecodeOptions ();
    frame_options.page = 2;
    var third = yield registry.open (apng, frame_options);
    assert (third.frame == 2 && third.frame_duration_ms == 250);
    assert (sample (third.preview, 0, 0) > 0.99f && sample (third.preview, 0, 1) < 0.01f);
    assert (sample (third.preview, 1, 3) < 0.01f);
}

int main (string[] args) {
    var loop = new MainLoop ();
    int status = 0;
    exercise.begin (args, (obj, result) => {
        try { exercise.end (result); }
        catch (Error error) { stderr.printf ("%s\n", error.message); status = 1; }
        loop.quit ();
    });
    loop.run ();
    return status;
}
