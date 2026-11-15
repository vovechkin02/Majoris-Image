using ImageKit;

private async void exercise (ImageView view, File image, File hdr, File? raw, string[] extra) throws Error {
    var registry = new DecoderRegistry ();
    var thumbnails = new ThumbnailService ();
    var options = new DecodeOptions ();
    options.preview_size = 256;
    options.allow_regions = false;
    options.raw_preview_only = true;

    var stream = yield image.read_async ();
    var staged = yield ImageSource.from_stream (stream);
    yield stream.close_async ();
    yield view.load_source (staged, options);
    assert (view.document.preview.width <= 256);
    assert (view.scroll_zoom_mode == ScrollZoomMode.CONTROL);
    view.accessible_description = "Selected image preview";
    var details = yield registry.inspect (staged);
    assert (details.width == view.document.width);

    int iterations = 24;
    var stress = Environment.get_variable ("MAJORIS_FILES_SWITCH_COUNT");
    if (stress != null) {
        int parsed;
        if (int.try_parse (stress, out parsed)) iterations = parsed.clamp (24, 512);
    }
    for (int i = 0; i < iterations; i++) {
        var stop = new Cancellable ();
        var next = i % 2 == 0 ? hdr : image;
        view.load_file.begin (image, options, stop, (obj, result) => {
            try { view.load_file.end (result); }
            catch (Error error) { assert (error is IOError.CANCELLED); }
        });
        stop.cancel ();
        bool finished = false;
        Error? thumbnail_error = null;
        thumbnails.generate.begin (staged, 128, null, (obj, result) => {
            try { assert (thumbnails.generate.end (result).get_size () > 45); }
            catch (Error error) { thumbnail_error = error; }
            finished = true;
        });
        yield view.load_file (next, options);
        while (!finished) {
            Idle.add (exercise.callback);
            yield;
        }
        if (thumbnail_error != null) throw thumbnail_error;
        assert (view.document.hdr == (i % 2 == 0));
        assert (view.accessible_description == "");
        view.accessible_description = "Selected image preview";
        view.visible = false;
        view.visible = true;
    }
    foreach (var path in extra) {
        yield view.load_file (File.new_for_path (path), options);
        assert (!view.document.supports_regions && view.document.preview.width <= 256);
        if (view.animated) {
            yield view.seek_frame (1);
            view.play ();
            view.visible = false;
            view.visible = true;
            view.pause ();
            assert (view.current_frame == 1);
        }
    }
    if (raw != null) {
        yield view.load_file (raw, options);
        assert (view.document.raw_state == RawState.RAW_PREVIEW_AVAILABLE);
    }

    var swatch = new Bytes (new uint8[] { 32, 64, 128, 255 });
    view.set_texture (new Gdk.MemoryTexture (1, 1, Gdk.MemoryFormat.R8G8B8A8, swatch, 4));
    assert (view.document == null && view.image_width == 1 && view.presentation_status == PresentationStatus.SDR);

    var non_image = yield ImageSource.from_bytes (new Bytes ("plain text".data));
    try { yield view.load_source (non_image, options); assert_not_reached (); }
    catch (ImageError.UNSUPPORTED error) { }
    view.clear ();
    assert (view.state == ViewState.EMPTY);
}

int main (string[] args) {
    if (args.length < 3) return 2;
    if (!Gtk.init_check ()) return 77;
    ImageView.register_type ();
    var builder = new Gtk.Builder.from_string (
        "<interface><object class='GtkScrolledWindow' id='layout'>" +
        "<property name='hscrollbar-policy'>never</property><property name='child'>" +
        "<object class='ImageKitView' id='preview'>" +
        "<property name='scroll-zoom-mode'>control</property><property name='height-request'>420</property>" +
        "<accessibility><property name='label'>Image preview</property></accessibility>" +
        "</object></property></object></interface>", -1);
    var view = (ImageView) builder.get_object ("preview");
    var window = new Gtk.Window ();
    window.set_default_size (520, 360);
    window.child = (Gtk.ScrolledWindow) builder.get_object ("layout");
    window.present ();
    var loop = new MainLoop ();
    int status = 0;
    exercise.begin (view, File.new_for_path (args[1]), File.new_for_path (args[2]),
        args.length > 3 && args[3] != "" ? File.new_for_path (args[3]) : null,
        args.length > 4 ? args[4:args.length] : new string[0], (obj, result) => {
        try { exercise.end (result); }
        catch (Error error) { stderr.printf ("%s\n", error.message); status = 1; }
        loop.quit ();
    });
    loop.run ();
    window.destroy ();
    return status;
}
