using MajorisImage;

async void exercise_view (ImageView view, ImageView other, string fixture_dir, string animation) throws Error {
    var png = File.new_for_path (Path.build_filename (fixture_dir, "rgb16.png"));
    var tiff = File.new_for_path (Path.build_filename (fixture_dir, "multipage.tiff"));
    var cancelled = new Cancellable ();
    cancelled.cancel ();
    try { yield view.load_file (png, null, cancelled); assert_not_reached (); }
    catch (IOError.CANCELLED e) { assert (view.state == ViewState.CANCELLED); }

    bool obsolete_finished = false;
    view.load_file.begin (png, null, null, (obj, result) => {
        try { view.load_file.end (result); }
        catch (Error e) { assert (e is IOError.CANCELLED); obsolete_finished = true; }
    });
    yield view.load_file (tiff);
    yield other.load_file (png);
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

    if (animation != "") {
        yield view.load_file (File.new_for_path (animation));
        assert (view.animated);
        view.play ();
        assert (view.playing);
        Timeout.add (150, () => { exercise_view.callback (); return Source.REMOVE; });
        yield;
        view.pause ();
        assert (!view.playing);
    }
    view.clear ();
    assert (view.state == ViewState.EMPTY && view.document == null && other.document != null);
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
    exercise_view.begin (view, other, args[1], args.length > 2 ? args[2] : "", (obj, result) => {
        try { exercise_view.end (result); }
        catch (Error e) { stderr.printf ("%s\n", e.message); code = 1; }
        loop.quit ();
    });
    loop.run ();
    window.destroy ();
    return code;
}
