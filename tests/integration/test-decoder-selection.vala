using ImageKit;

private float sample_float_channel (PixelBuffer data, int x, int channel) {
    unowned uint8[] bytes = data.bytes.get_data ();
    float value = 0;
    Memory.copy (&value, &bytes[16 * x + 4 * channel], 4);
    return value;
}

private class MissingFixtureCodec : Object, DecoderBackend {
    private string path;
    public MissingFixtureCodec (string path) {
        this.path = path;
    }
    public string id {
        get {
            return "test-missing-fixture";
        }
    }
    public string executable {
        get {
            return path;
        }
    }
    public ImageCapabilities capabilities {
        get {
            return ImageCapabilities.STILL | ImageCapabilities.METADATA | ImageCapabilities.COLOR_MANAGED;
        }
    }
    public string[] mime_types {
        owned get {
            return { "image/qoi", "image/bmp", "image/vnd.microsoft.icon", "image/x-tga",
                     "image/vnd-ms.dds", "image/x-portable-graymap" };
        }
    }
    public bool accepts (string mime, Bytes signature) {
        unowned uint8[] data = signature.get_data ();
        if (data.length < 4) {
            return false;
        }
        if (data[0] == 'q' && data[1] == 'o' && data[2] == 'i' && data[3] == 'f') {
            return true;
        }
        if (mime in mime_types || (data[0] == 'B' && data[1] == 'M') ||
            (data[0] == 'D' && data[1] == 'D' && data[2] == 'S') ||
            (data[0] == 'P' && data[1] == '5')) {
            return true;
        }
        if (mime == "image/png" && data[0] == 137) {
            return true;
        }
        return data[0] == 0 && data[1] == 0 && (data[2] == 1 || data[2] == 2);
    }
}

async void verify_decoder_selection_and_frames (string[] args) throws Error {
    var registry = new DecoderRegistry ();
    var registration = new KeyFile ();
    registration.load_from_file (args[9], KeyFileFlags.NONE);
    string[] registered = registration.get_string_list ("Thumbnailer Entry", "MimeType");
    var advertised = yield registry.thumbnail_mime_types ();
    foreach (var mime in registered) {
        assert (mime in advertised);
    }

    int[] corner = { 4, 1, 5, 6, 4, 6, 5, 1 };
    for (int i = 0; i < 8; i++) {
        var path = Path.build_filename (Path.get_dirname (args[2]), "orientation-%d.tiff".printf (i + 1));
        var oriented = yield registry.open (new ImageSource (File.new_for_path (path)));
        assert (oriented.width == (i < 4 ? 3 : 2) && oriented.height == (i < 4 ? 2 : 3));
        for (int channel = 0; channel < 3; channel++) {
            assert ((sample_float_channel (oriented.preview, 0, channel) > 0.9f) == ((corner[i] & (4 >> channel)) != 0));
        }
    }
    string generated_directory = Path.get_dirname (args[1]);
    foreach (string name in new string[] { "navigation.jpg", "still.webp", "jpeg2000.jp2", "animated.gif", "animated.webp" }) {
        var file = File.new_for_path (Path.build_filename (generated_directory, name));
        var info = yield file.query_info_async (FileAttribute.STANDARD_SIZE, 0);
        if (name == "jpeg2000.jp2" && info.get_size () == 0) {
            stdout.printf ("Optional JPEG 2000 encoder unavailable for generated fixture\n");
            continue;
        }
        var decoded = yield registry.open (new ImageSource (file));
        assert (decoded.width == 8 && decoded.height == 8);
        // JPEG permits quantization error. The other fixtures are lossless.
        double tolerance = name.has_suffix (".jpg") ? 0.035 : 0.0005;
        assert (Math.fabs (sample_float_channel (decoded.preview, 0, 0) - 1) < tolerance);
        assert (Math.fabs (sample_float_channel (decoded.preview, 0, 1)) < tolerance);
        assert (Math.fabs (sample_float_channel (decoded.preview, 0, 2)) < tolerance);
        assert (sample_float_channel (decoded.preview, 0, 3) == 1);
        if (name.has_prefix ("animated")) {
            assert (decoded.animated && decoded.frame_count == 3);
            var green = yield decoded.open_frame (1);
            assert (sample_float_channel (green.preview, 0, 1) > 0.99f && sample_float_channel (green.preview, 0, 0) < 0.01f);
        }
    }
    var sequence = new ImageSource (File.new_for_path (args[5]));
    try {
        var still = yield registry.open (sequence);
        if (still.animated) {
            assert (still.frame_count == 2 && still.page_count == 1 && still.loop_count == 0);
            assert (still.timed_sequence && still.frame_timestamp_us == 0 && still.frame_duration_us == 70000);
            assert (sample_float_channel (still.preview, 0, 0) > 0.9f);
            var next = yield still.open_frame (1);
            assert (next.frame == 1 && next.frame_timestamp_us == 70000 && next.frame_duration_us == 160000);
            assert (sample_float_channel (next.preview, 0, 1) > 0.9f && sample_float_channel (next.preview, 0, 0) < 0.05f);
        } else {
            assert (args[6] != "avif");
            assert (still.timed_sequence && still.page_count == 1);
            assert (still.warning.contains ("still preview"));
            stdout.printf ("AVIF sequence: installed decoder supplies a still preview\n");
        }
    } catch (Error error) {
        if (args[6] == "avif") {
            throw error;
        }
        if (!(error is ImageError.CODEC_MISSING) && !(error is ImageError.UNSUPPORTED)) {
            throw error;
        }
        stdout.printf ("AVIF sequence: no compatible decoder installed\n");
    }
    if (args[6] == "avif") {
        var options = new DecodeOptions ();
        options.required_capabilities |= ImageCapabilities.HDR | ImageCapabilities.HIGH_DEPTH |
            ImageCapabilities.COLOR_MANAGED | ImageCapabilities.ANIMATION;
        var hdr = yield registry.open (new ImageSource (File.new_for_path (args[8])), options);
        assert (hdr.hdr && hdr.bit_depth == 10 && hdr.frame_count == 2);
        assert (sample_float_channel (hdr.preview, 0, 0) > 1.0f);
        var second_hdr = yield hdr.open_frame (1);
        assert (second_hdr.bit_depth == 10 && sample_float_channel (second_hdr.preview, 0, 1) > 1.0f);
        var sdr = yield second_hdr.render_region (0, 0, second_hdr.width, second_hdr.height, 8, true);
        assert (sample_float_channel (sdr, 0, 1) <= 1.001f);
    }
    if (args[7] != "") {
        var heic = yield registry.open (new ImageSource (File.new_for_path (args[7])));
        assert (heic.decoder == "libheif-sequence" && heic.frame_count == 2 && heic.loop_count == 2);
        assert (heic.width == 64 && heic.height == 64 && heic.frame_duration_us == 70000);
        assert (sample_float_channel (heic.preview, 0, 0) > 0.9f);
        var second_heic = yield heic.open_frame (1);
        assert (second_heic.frame_timestamp_us == 70000 && second_heic.frame_duration_us == 160000);
        assert (sample_float_channel (second_heic.preview, 0, 1) > 0.9f);
        var multi = yield registry.open (new ImageSource (File.new_for_path (
            Path.build_filename (Path.get_dirname (args[7]), "heic-multitrack.heic"))));
        assert (multi.frame_count == 2 && sample_float_channel (multi.preview, 0, 0) > 0.9f);
        var generated = Path.get_dirname (args[1]);
        var infinite = yield registry.open (new ImageSource (File.new_for_path (
            Path.build_filename (generated, "infinite.heic"))));
        assert (infinite.loop_count == 0 && infinite.frame_count == 2);
        assert ((yield infinite.open_frame (1)).frame_timestamp_us == 70000);
        var trimmed = yield registry.open (new ImageSource (File.new_for_path (
            Path.build_filename (generated, "trimmed.heic"))));
        assert (trimmed.frame_count == 3 && trimmed.loop_count == 1);
        assert (trimmed.frame_timestamp_us == 0 && trimmed.frame_duration_us == 20000);
        assert (sample_float_channel (trimmed.preview, 0, 3) == 0);
        var trim_red = yield trimmed.open_frame (1);
        assert (trim_red.frame_timestamp_us == 20000 && trim_red.frame_duration_us == 35000);
        assert (sample_float_channel (trim_red.preview, 0, 0) > 0.9f);
        var trim_green = yield trimmed.open_frame (2);
        assert (trim_green.frame_timestamp_us == 55000 && trim_green.frame_duration_us == 60000);
        assert (sample_float_channel (trim_green.preview, 0, 1) > 0.9f);
        var fractional = yield registry.open (new ImageSource (File.new_for_path (
            Path.build_filename (generated, "partial-repeat.heic"))));
        assert (fractional.frame_count == 3 && fractional.loop_count == 1);
        var tail = yield fractional.open_frame (2);
        assert (tail.frame_timestamp_us == 230000 && tail.frame_duration_us == 40000);
        assert (sample_float_channel (tail.preview, 0, 0) > 0.9f);
        foreach (var invalid in new string[] { "offset.heic", "invalid-timing.heic", "rate.heic", "composition.heic" }) {
            var source = new ImageSource (File.new_for_path (Path.build_filename (generated, invalid)));
            try {
                yield registry.open (source);
                assert_not_reached ();
            } catch (ImageError.DECODE error) {
                assert (error.message.contains ("edit list"));
            } catch (ImageError.CORRUPT error) {
                // Corrupt input is an accepted rejection for this fixture.
            }
        }
    }
    var apng = new ImageSource (File.new_for_path (args[2]));
    var animation = yield registry.open (apng);
    assert (animation.animated && animation.frame_count == 3 && animation.page_count == 1);
    assert (animation.frame_duration_ms == 40 && animation.loop_count == 1);
    assert (sample_float_channel (animation.preview, 0, 0) > 0.99f);

    var qoi = new ImageSource (File.new_for_path (args[1]));
    string catalog = yield registry.installed_codecs ();
    if (!catalog.contains ("\"operation\":\"glycin\"")) {
        assert (args[3] != "enabled");
        stdout.printf ("Glycin loader unavailable; native APNG metadata passed\n");
        return;
    }
    var directory = Path.get_dirname (args[1]);
    var color_options = new DecodeOptions ();
    color_options.required_capabilities |= ImageCapabilities.COLOR_MANAGED | ImageCapabilities.HIGH_DEPTH;
    var png_p3 = new ImageSource (File.new_for_path (Path.build_filename (directory, "profiled.png")));
    var tiff_p3 = new ImageSource (File.new_for_path (Path.build_filename (directory, "profiled.tiff")));
    var native_p3 = yield registry.open (png_p3, color_options);
    var native_tiff = yield registry.open (tiff_p3, color_options);
    assert (native_p3.color_managed && native_p3.source_bit_depth == 16);
    float[] reference = { 0.18f, 0.32f, 0.12f };
    for (int channel = 0; channel < 3; channel++) {
        assert (Math.fabs (sample_float_channel (native_p3.preview, 0, channel) - reference[channel]) < 0.005);
        assert (Math.fabs (sample_float_channel (native_tiff.preview, 0, channel) - reference[channel]) < 0.005);
    }
    registry.register_backend (new MissingFixtureCodec (args[4]));
    if (catalog.contains ("image/png")) {
        var fallback_p3 = yield registry.open (png_p3);
        assert (fallback_p3.decoder == "Glycin" && fallback_p3.bit_depth == 16);
        assert (!fallback_p3.color_managed && fallback_p3.color_diagnostic.contains ("ICC"));
        for (int channel = 0; channel < 3; channel++) {
            assert (Math.fabs (sample_float_channel (fallback_p3.preview, 0, channel) - reference[channel]) < 0.01);
        }
        var adobe = new ImageSource (File.new_for_path (Path.build_filename (directory, "adobe.png")));
        var fallback_adobe = yield registry.open (adobe);
        assert (fallback_adobe.decoder == "Glycin" && fallback_adobe.bit_depth == 16);
        for (int channel = 0; channel < 3; channel++) {
            assert (Math.fabs (sample_float_channel (fallback_adobe.preview, 0, channel) - reference[channel]) < 0.01);
        }
        var gray = new ImageSource (File.new_for_path (Path.build_filename (directory, "gray-profile.png")));
        var fallback_gray = yield registry.open (gray);
        assert (fallback_gray.decoder == "Glycin" && fallback_gray.bit_depth == 16);
        for (int channel = 0; channel < 3; channel++) {
            assert (Math.fabs (sample_float_channel (fallback_gray.preview, 0, channel) - 0.106) < 0.01);
        }
        assert (Math.fabs (sample_float_channel (fallback_gray.preview, 0, 3) - 0.5) < 0.002);
        assert (sample_float_channel (fallback_gray.preview, 1, 3) == 0 && sample_float_channel (fallback_gray.preview, 1, 0) == 0);
        var srgb = new ImageSource (File.new_for_path (Path.build_filename (directory, "srgb.png")));
        var require_color = new DecodeOptions ();
        require_color.required_capabilities |= ImageCapabilities.COLOR_MANAGED;
        var verified = yield registry.open (srgb, require_color);
        assert (verified.decoder == "Glycin" && verified.color_managed);
        assert (Math.fabs (sample_float_channel (verified.preview, 0, 0) - 0.21586) < 0.002);
        assert (Math.fabs (sample_float_channel (verified.preview, 0, 3) - 128.0 / 255) < 0.002);
        var cicp = new ImageSource (File.new_for_path (Path.build_filename (directory, "cicp.png")));
        try {
            yield registry.open (cicp);
            assert_not_reached ();
        } catch (ImageError.COLOR_PROFILE error) {
            // Strict color handling must reject this profile.
        }
        var broken = new ImageSource (File.new_for_path (Path.build_filename (directory, "invalid-profile.png")));
        try {
            yield registry.open (broken);
            assert_not_reached ();
        } catch (ImageError.COLOR_PROFILE error) {
            // Strict color handling must reject this profile.
        }
    }
    string[] names = { "bmp", "ico", "tga", "dds", "pgm" };
    string[] mimes = { "image/bmp", "image/vnd.microsoft.icon", "image/x-tga",
                       "image/vnd-ms.dds", "image/x-portable-graymap" };
    for (int i = 0; i < names.length; i++) {
        if (!catalog.contains (mimes[i])) {
            continue;
        }
        var path = Path.build_filename (Path.get_dirname (args[1]), "fallback." + names[i]);
        var fixture = new ImageSource (File.new_for_path (path));
        var bounded = new DecodeOptions ();
        bounded.raw_preview_only = true; // Options also used for ordinary files selected in a Files preview.
        var legacy = yield registry.open (fixture, bounded);
        stdout.printf ("%s: %s %dx%d\n", names[i], legacy.decoder, legacy.width, legacy.height);
        assert (legacy.decoder == "Glycin" && legacy.width == (i == 3 ? 4 : 2));
        if (i < 4) {
            assert (sample_float_channel (legacy.preview, 0, 0) > 0.95f && sample_float_channel (legacy.preview, 1, 1) > 0.95f);
        } else {
            assert (Math.fabs (sample_float_channel (legacy.preview, 0, 0) - 0.0513f) < 0.002f);
            assert (sample_float_channel (legacy.preview, 0, 0) == sample_float_channel (legacy.preview, 0, 1));
        }
    }
    var first = yield registry.open (qoi);
    assert (first.decoder == "Glycin" && first.width == 2 && first.height == 1);
    assert (first.bit_depth == 8 && first.has_alpha && first.pixel_buffer_bit_depth == 32);
    assert (sample_float_channel (first.preview, 0, 0) > 0.99f);
    assert (Math.fabs (sample_float_channel (first.preview, 0, 3) - 128.0 / 255) < 0.001);
    var downscaled = yield first.render_region (0, 0, 2, 1, 1);
    assert (downscaled.width == 1 && downscaled.height == 1);
    assert (Math.fabs (sample_float_channel (downscaled, 0, 3) - (128.0 / 255 + 1) / 2) < 0.002);
    assert (Math.fabs (sample_float_channel (downscaled, 0, 0) - 128.0 / 383) < 0.002);
    assert (Math.fabs (sample_float_channel (downscaled, 0, 1) - 255.0 / 383) < 0.002);
    var crop = yield first.render_region (1, 0, 1, 1, 4);
    assert (crop.width == 1 && sample_float_channel (crop, 0, 1) > 0.99f);
    var recipe = new RenderOptions ();
    recipe.nearest_neighbor = true;
    var nearest = yield registry.render (qoi, recipe, 1);
    assert (sample_float_channel (nearest, 0, 0) < 0.01f || sample_float_channel (nearest, 0, 1) < 0.01f);
    recipe.turns = 1;
    var rotated = yield registry.render (qoi, recipe, 8);
    assert (rotated.width == 1 && rotated.height == 2);
    // The registered test worker forces the native-missing path. Cache
    // identity distinguishes a successful preload from a foreground retry.
    var preloader = new PreloadManager.with_registry (registry);
    yield preloader.preload ({ qoi });
    var preloaded_qoi = yield preloader.open (qoi);
    assert (preloaded_qoi.decoder == "Glycin");
    assert (preloaded_qoi == (yield preloader.open (qoi)));
    var strict = new DecodeOptions ();
    strict.required_capabilities |= ImageCapabilities.COLOR_MANAGED;
    assert ((yield registry.open (qoi, strict)).color_managed);
    var linear_qoi = new ImageSource (File.new_for_path (Path.build_filename (directory, "linear.qoi")));
    var linear = yield registry.open (linear_qoi);
    assert (Math.fabs (sample_float_channel (linear.preview, 0, 0) - 128.0 / 255) < 0.002);
    assert (Math.fabs (sample_float_channel (linear.preview, 0, 3) - 128.0 / 255) < 0.002);
    assert (sample_float_channel (linear.preview, 1, 0) == 0);
    try {
        yield registry.open (linear_qoi, strict);
        assert_not_reached ();
    } catch (ImageError.COLOR_PROFILE error) {
        // Strict color handling must reject this profile.
    }
    var probe = yield registry.inspect (qoi);
    assert (probe.loader == first.decoder && probe.depth == first.bit_depth);
    yield preloader.preload ({ linear_qoi, qoi }, strict);
    try {
        yield preloader.open (linear_qoi, strict);
        assert_not_reached ();
    } catch (ImageError.COLOR_PROFILE error) {
        // Strict color handling must reject this profile.
    }
    var verified_qoi = yield preloader.open (qoi, strict);
    assert (verified_qoi.color_managed && verified_qoi == (yield preloader.open (qoi, strict)));
    var options = new DecodeOptions ();
    options.required_capabilities = ImageCapabilities.HDR;
    try {
        yield registry.open (qoi, options);
        assert_not_reached ();
    } catch (ImageError.UNSUPPORTED error) {
        // The fixture or requested operation may be unsupported.
    } catch (ImageError.CODEC_MISSING error) {
        // An optional codec may be unavailable.
    }
    var stop = new Cancellable ();
    stop.cancel ();
    try {
        yield registry.open (qoi, null, stop);
        assert_not_reached ();
    } catch (IOError.CANCELLED error) {
        // Cancellation is the expected result for this request.
    }
    var thumbnail = yield new ThumbnailService ().generate (qoi, 16);
    assert (thumbnail.get_size () > 45);

    var second = yield animation.open_frame (1);
    assert (second.frame == 1 && second.frame_duration_ms == 170);
    assert (sample_float_channel (second.preview, 0, 0) > 0.1f && sample_float_channel (second.preview, 0, 1) > 0.1f);
    assert (sample_float_channel (second.preview, 1, 0) > 0.99f);
    var frame_options = new DecodeOptions ();
    frame_options.page = 2;
    var third = yield registry.open (apng, frame_options);
    assert (third.frame == 2 && third.frame_duration_ms == 250);
    assert (sample_float_channel (third.preview, 0, 0) > 0.99f && sample_float_channel (third.preview, 0, 1) < 0.01f);
    assert (sample_float_channel (third.preview, 1, 3) < 0.01f);
}

int main (string[] args) {
    var loop = new MainLoop ();
    int status = 0;
    verify_decoder_selection_and_frames.begin (args, (obj, result) => {
        try {
            verify_decoder_selection_and_frames.end (result);
        } catch (Error error) {
            stderr.printf ("%s\n", error.message);
            status = 1;
        }
        loop.quit ();
    });
    loop.run ();
    return status;
}
