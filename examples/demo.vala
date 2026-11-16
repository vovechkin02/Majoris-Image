using ImageKit;

int main (string[] args) {
    var app = new Gtk.Application ("org.majoris.Image.Demo", ApplicationFlags.DEFAULT_FLAGS);
    app.activate.connect (() => {
        if (app.active_window != null) {
            app.active_window.present ();
            return;
        }
        ImageView.register_type ();
        var ui = new Gtk.Builder.from_resource ("/org/majoris/Image/demo.ui");
        var window = (Gtk.ApplicationWindow) ui.get_object ("window");
        window.application = app;
        var view = (ImageView) ui.get_object ("viewer");
        var status = (Gtk.Label) ui.get_object ("status");
        var play = (Gtk.Button) ui.get_object ("play");
        var prev = (Gtk.Button) ui.get_object ("prev");
        var next = (Gtk.Button) ui.get_object ("next");
        var metadata = (Gtk.Button) ui.get_object ("metadata");
        var policy = (Gtk.DropDown) ui.get_object ("policy");

        ((Gtk.Button) ui.get_object ("open")).clicked.connect (() => choose_image.begin (window, view));
        ((Gtk.Button) ui.get_object ("fit")).clicked.connect (() => view.fit_to_view ());
        ((Gtk.Button) ui.get_object ("actual")).clicked.connect (() => view.actual_size ());
        ((Gtk.Button) ui.get_object ("in")).clicked.connect (() => view.zoom_in ());
        ((Gtk.Button) ui.get_object ("out")).clicked.connect (() => view.zoom_out ());
        ((Gtk.Button) ui.get_object ("rotate")).clicked.connect (() => view.rotate_clockwise ());
        play.clicked.connect (() => {
            if (view.playing) {
                view.pause ();
            } else {
                view.play ();
            }
        });
        prev.clicked.connect (() => change_page.begin (view, view.current_page - 1));
        next.clicked.connect (() => change_page.begin (view, view.current_page + 1));
        metadata.clicked.connect (() => show_metadata (window, view));
        policy.model = new Gtk.StringList ({"Automatic", "SDR", "HDR preferred"});
        policy.notify["selected"].connect (() => view.presentation_policy = (PresentationPolicy) policy.selected);

        SourceFunc refresh = () => {
            play.sensitive = view.animated;
            play.label = view.playing ? "Pause" : "Play";
            prev.sensitive = view.current_page > 0 && view.state == ViewState.READY;
            next.sensitive = view.current_page + 1 < view.page_count && view.state == ViewState.READY;
            metadata.sensitive = view.metadata != null;
            if (view.error_message != "") {
                status.label = view.error_message;
            } else if (view.state == ViewState.LOADING) {
                status.label = "Loading image…";
            } else if (view.state == ViewState.EMPTY) {
                status.label = "Open an image to get started.";
            } else {
                status.label = "%d × %d · %s · Page %d of %d%s%s".printf (
                    view.image_width, view.image_height, view.decoder, view.current_page + 1, view.page_count,
                    view.hdr_source ? " · HDR source, SDR presentation" : "",
                    view.warning != "" ? " · " + view.warning : "");
            }
            return Source.REMOVE;
        };
        view.notify["state"].connect (() => refresh ());
        view.notify["animated"].connect (() => refresh ());
        view.notify["playing"].connect (() => refresh ());
        view.notify["warning"].connect (() => refresh ());
        refresh ();
        window.present ();
    });
    return app.run (args);
}

async void choose_image (Gtk.Window window, ImageView view) {
    try {
        var chooser = new Gtk.FileDialog ();
        var file = yield chooser.open (window, null);
        yield view.load_file (file);
    } catch (Error e) {
        debug ("%s", e.message);
    }
}

async void change_page (ImageView view, int page) {
    try {
        yield view.set_page (page);
    } catch (Error e) {
        debug ("%s", e.message);
    }
}

void show_metadata (Gtk.Window parent, ImageView view) {
    if (view.metadata == null) {
        return;
    }
    var text = new Gtk.TextView ();
    text.wrap_mode = Gtk.WrapMode.WORD_CHAR;
    text.editable = false;
    text.buffer.text = view.metadata.json;
    var scroll = new Gtk.ScrolledWindow ();
    scroll.child = text;
    var window = new Gtk.Window ();
    window.title = "Image metadata";
    window.transient_for = parent;
    window.destroy_with_parent = true;
    window.set_default_size (620, 420);
    window.child = scroll;
    window.present ();
}
