using MajorisImage;

private async void exercise (ImageView view, File large, File next, File destination, File? raw, string[] extra) throws Error {
    var registry = new DecoderRegistry ();
    var options = new DecodeOptions ();
    options.preview_size = 800;
    var source = new ImageSource (large);
    var next_source = new ImageSource (next);
    var thumbnails = new ThumbnailService ();
    var preload = yield thumbnails.generate (next_source, 128);
    assert (preload.get_size () > 45);

    var document = yield registry.open (source, options);
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
    if (view.document.page_count > 1) yield view.set_page (1);

    foreach (var path in extra) {
        yield view.load_file (File.new_for_path (path), options);
        if (view.animated) {
            yield view.seek_frame (1);
            view.play ();
            view.pause ();
            assert (view.current_frame == 1);
        } else {
            assert (view.document.colour_managed && view.document.bit_depth >= 16);
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
    assert (exported.width > 0 && exported.mime_type == "image/png");
}

int main (string[] args) {
    if (args.length < 4) return 2;
    if (!Gtk.init_check ()) return 77;
    ImageView.register_type ();
    var builder = new Gtk.Builder.from_string (
        "<interface><object class='MajorisImageView' id='image'/></interface>", -1);
    var view = (ImageView) builder.get_object ("image");
    var window = new Gtk.Window ();
    window.child = view;
    window.present ();
    var loop = new MainLoop ();
    int status = 0;
    exercise.begin (view, File.new_for_path (args[1]), File.new_for_path (args[2]),
        File.new_for_path (args[3]), args.length > 4 && args[4] != "" ? File.new_for_path (args[4]) : null,
        args.length > 5 ? args[5:args.length] : new string[0], (obj, result) => {
        try { exercise.end (result); }
        catch (Error error) { stderr.printf ("%s\n", error.message); status = 1; }
        loop.quit ();
    });
    loop.run ();
    window.destroy ();
    return status;
}
