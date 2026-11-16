using ImageKit;

private async void verify_installed_viewer (ImageView view, File fixture, File corrupt) throws Error {
    yield view.load_file (fixture);
    assert (view.state == ViewState.READY && view.document.source.file.equal (fixture));
    assert (view.image_width == 24 && view.image_height == 16);
    assert (view.presentation_status == PresentationStatus.SDR);
    var region = yield view.document.render_region (2, 3, 5, 4, 5);
    assert (region.width == 5 && region.height == 4 && region.bytes.get_size () == 80u * 4u);
    try {
        yield view.load_file (corrupt);
        assert_not_reached ();
    } catch (ImageError.UNSUPPORTED error) {
        assert (view.state == ViewState.UNSUPPORTED && view.document == null);
    }
}

int main (string[] args) {
    if (args.length != 3 || !Gtk.init_check ()) {
        return 2;
    }
    var window = new Gtk.Window ();
    window.title = "Installed GTK viewer test";
    window.set_default_size (640, 480);
    var view = new ImageView ();
    window.child = view;
    window.present ();
    var loop = new MainLoop ();
    int status = 1;
    uint watchdog = Timeout.add_seconds (20, () => {
        stderr.printf ("Installed viewer did not complete the ready/error checks\n");
        loop.quit ();
        return Source.REMOVE;
    });
    verify_installed_viewer.begin (view, File.new_for_path (args[1]), File.new_for_path (args[2]), (obj, result) => {
        Source.remove (watchdog);
        try {
            verify_installed_viewer.end (result);
            status = 0;
        } catch (Error error) {
            stderr.printf ("%s\n", error.message);
        }
        loop.quit ();
    });
    loop.run ();
    window.destroy ();
    return status;
}
