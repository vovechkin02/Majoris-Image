using ImageKit;

private async void verify_photos_export_workflow (ImageView view, File large, File next, File destination, File? raw, string[] extra) throws Error {
    var registry = new DecoderRegistry ();
    var options = new DecodeOptions ();
    options.preview_size = 800;
    var source = new ImageSource (large);
    var next_source = new ImageSource (next);
    var thumbnails = new ThumbnailService ();
    var preload = yield thumbnails.generate (next_source, 128);
    assert (preload.get_size () > 45);

    var navigation = new PreloadManager.with_registry (registry);
    ImageSource[] neighbors = {source, next_source};
    foreach (var path in extra) {
        if (path.has_suffix (".jpg")) {
            var first = yield navigation.open (new ImageSource (File.new_for_path (path)), options);
            view.set_document (first);
        } else if (path.has_suffix (".qoi")) {
            var mimes = yield registry.thumbnail_mime_types ();
            if ("image/qoi" in mimes || "image/x-qoi" in mimes) {
                neighbors += new ImageSource (File.new_for_path (path));
            }
        }
    }
    yield navigation.preload (neighbors, options);
    if (neighbors.length > 2) {
        var qoi = yield navigation.open (neighbors[2], options);
        assert (qoi == (yield navigation.open (neighbors[2], options)));
        view.set_document (qoi);
    }

    var document = yield navigation.open (source, options);
    assert (document == (yield navigation.open (source, options)));
    var small = options.copy ();
    small.preview_size = 128;
    assert ((yield navigation.open (source, small)) != document);
    assert (document.supports_regions && document.width > 10000 && document.bit_depth >= 16);
    assert (view.scroll_zoom_mode == ScrollZoomMode.ALWAYS);
    view.set_document (document);
    view.accessible_description = "High-resolution image";
    view.zoom_to (1.0);
    view.pan_by (-512, -128);
    view.rotate_clockwise ();
    view.rotate_counterclockwise ();
    var region = yield document.render_region (10000, 5000, 128, 128, 128);
    assert (region.width == 128 && region.height == 128);
    view.fit_to_view ();

    yield view.load_source (next_source, options);
    assert (view.document.hdr && view.accessible_description == "");
    view.accessible_description = "High-dynamic-range image";
    view.presentation_policy = PresentationPolicy.SDR;
    var sdr = yield view.document.render_region (0, 0, view.document.width, view.document.height, 64, true);
    assert (sdr.width > 0);
    if (view.document.page_count > 1) {
        yield view.set_page (1);
    }

    foreach (var path in extra) {
        if (path.has_suffix (".jpg") || path.has_suffix (".qoi")) {
            continue;
        }
        yield view.load_file (File.new_for_path (path), options);
        if (view.animated) {
            yield view.seek_frame (1);
            view.play ();
            view.pause ();
            assert (view.current_frame == 1);
        } else {
            assert (view.document.color_managed && view.document.bit_depth >= 16);
        }
    }
    if (raw != null) {
        yield view.load_file (raw, options);
        assert (view.document.raw_state == RawState.RAW_DEVELOPED);
    }
    var export_options = new ExportOptions ();
    export_options.format = "png";
    yield new EncoderRegistry ().encode (next_source, destination, export_options);
    var exported = yield registry.open (new ImageSource (destination));
    var reference = yield registry.open (next_source);
    assert (exported.width == reference.width && exported.height == reference.height);
    assert (exported.mime_type == "image/png" && exported.bit_depth == 16);
    var expected = yield reference.render_region (0, 0, reference.width, reference.height,
        int.max (reference.width, reference.height), true);
    var actual = yield exported.render_region (0, 0, exported.width, exported.height,
        int.max (exported.width, exported.height));
    foreach (int y in new int[] { 0, exported.height / 2, exported.height - 1 }) {
        foreach (int x in new int[] { 0, exported.width / 2, exported.width - 1 }) {
            for (int band = 0; band < 4; band++) {
                // The PNG export quantizes the linear SDR result to 16-bit sRGB.
                double wanted = sample_float_channel (expected, x, y, band).clamp (0, 1);
                double observed = sample_float_channel (actual, x, y, band);
                if (!observed.is_finite () || !wanted.is_finite () || Math.fabs (observed - wanted) >= 0.002) {
                    throw new ImageError.EXPORT ("PNG pixel (%d,%d) channel %d: expected %.6f, got %.6f",
                        x, y, band, wanted, observed);
                }
            }
        }
    }
}

private float sample_float_channel (PixelBuffer buffer, int x, int y, int band) {
    unowned uint8[] bytes = buffer.bytes.get_data ();
    float value = 0;
    Memory.copy (&value, &bytes[y * buffer.stride + (x * 4 + band) * 4], sizeof (float));
    return value;
}

int main (string[] args) {
    if (args.length < 4) {
        return 2;
    }
    if (!Gtk.init_check ()) {
        return 77;
    }
    ImageView.register_type ();
    var builder = new Gtk.Builder.from_string (
        "<interface><object class='ImageKitView' id='image'/></interface>", -1);
    var view = (ImageView) builder.get_object ("image");
    var window = new Gtk.Window ();
    window.title = "Photos viewer";
    window.set_default_size (960, 720);
    window.child = view;
    window.present ();
    var loop = new MainLoop ();
    int status = 0;
    verify_photos_export_workflow.begin (view, File.new_for_path (args[1]), File.new_for_path (args[2]),
        File.new_for_path (args[3]), args.length > 4 && args[4] != "" ? File.new_for_path (args[4]) : null,
        args.length > 5 ? args[5:args.length] : new string[0], (obj, result) => {
        try {
            verify_photos_export_workflow.end (result);
        } catch (Error error) {
            stderr.printf ("%s\n", error.message);
            status = 1;
        }
        loop.quit ();
    });
    loop.run ();
    window.destroy ();
    return status;
}
