using MajorisImage;

private async void exercise (ImageView view, File image, File hdr, File? raw) throws Error {
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
    var details = yield registry.inspect (staged);
    assert (details.width == view.document.width);

    for (int i = 0; i < 24; i++) {
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
        view.visible = false;
        view.visible = true;
    }
    if (raw != null) yield view.load_file (raw, options);

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
        "<interface><object class='MajorisImageView' id='preview'/></interface>", -1);
    var view = (ImageView) builder.get_object ("preview");
    var window = new Gtk.Window ();
    window.child = view;
    window.present ();
    var loop = new MainLoop ();
    int status = 0;
    exercise.begin (view, File.new_for_path (args[1]), File.new_for_path (args[2]),
        args.length > 3 ? File.new_for_path (args[3]) : null, (obj, result) => {
        try { exercise.end (result); }
        catch (Error error) { stderr.printf ("%s\n", error.message); status = 1; }
        loop.quit ();
    });
    loop.run ();
    window.destroy ();
    return status;
}
