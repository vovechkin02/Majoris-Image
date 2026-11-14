using MajorisImage;

[CCode (cname = "g_object_run_dispose", cheader_filename = "glib-object.h")]
private extern void force_dispose (Object object);

async void exercise_view (ImageView view, ImageView other, string fixture_dir, string[] animations) throws Error {
    var png = File.new_for_path (Path.build_filename (fixture_dir, "rgb16.png"));
    var tiff = File.new_for_path (Path.build_filename (fixture_dir, "multipage.tiff"));
    var cancelled = new Cancellable ();
    cancelled.cancel ();
    try { yield view.load_file (png, null, cancelled); assert_not_reached (); }
    catch (IOError.CANCELLED e) { assert (view.state == ViewState.CANCELLED); }

    view.visible = false;
    yield view.load_file (png);
    assert (view.state == ViewState.READY && view.image_width == 24);
    view.visible = true;

    bool obsolete_finished = false;
    view.load_file.begin (png, null, null, (obj, result) => {
        try { view.load_file.end (result); }
        catch (Error e) { assert (e is IOError.CANCELLED); obsolete_finished = true; }
    });
    other.load_file.begin (png, null, null, (obj, result) => {
        try { other.load_file.end (result); }
        catch (Error error) { assert_not_reached (); }
    });
    view.load_file.begin (tiff, null, null, (obj, result) => {
        try { view.load_file.end (result); }
        catch (Error error) { assert (error is IOError.CANCELLED); }
    });
    view.visible = false;
    yield view.load_file (tiff);
    view.visible = true;
    while (other.state == ViewState.LOADING) yield delay (10);
    assert (obsolete_finished);
    assert (view.page_count == 2 && other.page_count == 1);
    assert (view.current_page == 0 && other.image_width == 24);
    yield view.set_page (1);
    assert (view.current_page == 1 && view.state == ViewState.READY);
    view.rotate_clockwise ();
    view.flip_horizontal ();
    view.actual_size ();
    double initial = view.zoom;
    view.zoom_in ();
    assert (view.zoom > initial);
    view.pan_by (10000, -10000);
    view.fit_to_view ();
    assert (view.fit_mode == FitMode.CONTAIN);

    bool replacement_done = false;
    ulong followup = view.notify["document"].connect (() => {
        if (view.document == null || view.document.mime_type != "image/tiff") return;
        view.load_file.begin (png, null, null, (obj, result) => {
            try { view.load_file.end (result); }
            catch (Error error) { assert_not_reached (); }
            replacement_done = true;
        });
    });
    try { yield view.load_file (tiff); assert_not_reached (); }
    catch (IOError.CANCELLED error) { }
    for (int i = 0; !replacement_done && i < 100; i++) yield delay (10);
    view.disconnect (followup);
    assert (replacement_done && view.document.mime_type == "image/png" && view.image_width == 24);

    var exr = File.new_for_path (Path.build_filename (fixture_dir, "multipart.exr"));
    yield view.load_file (exr);
    var hdr = view.document;
    assert (hdr.hdr && !hdr.tone_mapped);
    assert (view.presentation_status == PresentationStatus.SDR_TONE_MAPPED);
    view.presentation_policy = PresentationPolicy.HDR_PREFERRED;
    assert (view.presentation_status == PresentationStatus.HDR_UNAVAILABLE);
    assert (view.presentation_diagnostic.contains ("cannot verify"));
    view.presentation_policy = PresentationPolicy.SDR;
    assert (view.presentation_status == PresentationStatus.SDR_TONE_MAPPED);
    view.presentation_policy = PresentationPolicy.AUTOMATIC;
    assert (view.document == hdr);
    view.zoom_to (3);
    view.rotate_clockwise ();
    view.visible = false;
    view.visible = true;
    assert (view.fit_mode == FitMode.CUSTOM && view.zoom == 3);

    var dying = new ImageView ();
    bool disposed_callback = false;
    dying.load_file.begin (tiff, null, null, (obj, result) => {
        try { dying.load_file.end (result); assert_not_reached (); }
        catch (Error error) { assert (error is IOError.CANCELLED); }
        disposed_callback = true;
    });
    force_dispose (dying);
    for (int i = 0; !disposed_callback && i < 100; i++) yield delay (10);
    assert (disposed_callback && dying.document == null);

    foreach (string animation in animations) {
        var file = File.new_for_path (animation);
        yield view.load_file (file);
        assert (view.animated && view.frame_duration_ms > 0);
        assert (view.presentation_status == PresentationStatus.SDR);
        view.play ();
        assert (view.playing);
        view.visible = false;
        assert (!view.playing);
        yield delay (60);
        view.visible = true;
        assert (view.playing);
        yield delay (130);
        assert (view.current_frame > 0);
        view.pause ();
        view.visible = false;
        view.visible = true;
        assert (!view.playing);
        view.play ();
        yield delay (700);
        assert (view.playing == (view.document.loop_count == 0));
        view.play ();
        yield view.load_file (png);
        assert (!view.animated && !view.playing && view.document.mime_type == "image/png");
    }
    view.clear ();
    assert (view.state == ViewState.EMPTY && view.document == null && other.document != null);
}

private async void delay (uint milliseconds) {
    Timeout.add (milliseconds, () => { delay.callback (); return Source.REMOVE; });
    yield;
}

int main (string[] args) {
    if (!Gtk.init_check ()) return 77;
    ImageView.register_type ();
    var builder = new Gtk.Builder.from_string (
        "<interface><object class='MajorisImageView' id='test'/></interface>", -1);
    var view = (ImageView) builder.get_object ("test");
    var other = new ImageView ();
    var box = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
    box.append (view);
    box.append (other);
    var window = new Gtk.Window ();
    window.set_default_size (640, 480);
    window.child = box;
    window.present ();
    var loop = new MainLoop ();
    int code = 0;
    exercise_view.begin (view, other, args[1], args[2:args.length], (obj, result) => {
        try { exercise_view.end (result); }
        catch (Error e) { stderr.printf ("%s\n", e.message); code = 1; }
        loop.quit ();
    });
    loop.run ();
    window.destroy ();
    return code;
}
