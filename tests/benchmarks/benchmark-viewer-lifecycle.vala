using ImageKit;

/** Forces GObject disposal to validate release of image-view-owned resources. */
[CCode (cname = "g_object_run_dispose", cheader_filename = "glib-object.h")]
private extern void force_dispose (Object object);

/** Reads one Linux process-residency counter, in KiB, without changing allocations. */
private int64 memory_kib (string field) throws Error {
    string contents;
    FileUtils.get_contents ("/proc/self/status", out contents);
    foreach (var line in contents.split ("\n")) {
        if (line.has_prefix (field + ":")) {
            var parts = line.substring (field.length + 1).strip ().split (" ");
            return int64.parse (parts[0]);
        }
    }
    throw new IOError.NOT_FOUND ("Missing Linux resident-memory counter %s", field);
}

/** Yields to GTK so asynchronous finalization and idle cleanup can complete. */
private async void quiesce () {
    Timeout.add (200, () => {
        quiesce.callback ();
        return Source.REMOVE;
    });
    yield;
}

/** Measures host-memory high-water and post-disposal residency across viewer cycles. */
private async void measure_lifecycle (File file, int cycles, int viewers_per_cycle)
    throws Error {
    int64 initial = memory_kib ("VmRSS");
    stdout.printf ("Viewer-memory initial_rss_kib=%" + int64.FORMAT + "\n", initial);
    for (int cycle = 0; cycle < cycles; cycle++) {
        var window = new Gtk.Window ();
        window.set_default_size (800, 600);
        var parent = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
        window.child = parent;
        window.present ();
        for (int viewer = 0; viewer < viewers_per_cycle; viewer++) {
            var view = new ImageView ();
            parent.append (view);
            yield view.load_file (file);
            if (view.document == null) {
                throw new IOError.FAILED ("Viewer did not load the benchmark image");
            }
            parent.remove (view);
            force_dispose (view);
            if (view.document != null || view.metadata != null) {
                throw new IOError.FAILED ("Disposed viewer retained its public document state");
            }
        }
        window.destroy ();
        yield quiesce ();
        int64 current = memory_kib ("VmRSS");
        int64 peak = memory_kib ("VmHWM");
        stdout.printf (
            "Viewer-memory cycle=%d rss_kib=%" + int64.FORMAT +
            " peak_rss_kib=%" + int64.FORMAT + "\n",
            cycle + 1, current, peak);
    }
    int64 settled = memory_kib ("VmRSS");
    stdout.printf ("Viewer-memory final_rss_kib=%" + int64.FORMAT +
        " retained_delta_kib=%" + int64.FORMAT + "\n", settled, settled - initial);
}

/** Runs the GTK viewer-lifecycle benchmark; physical GPU memory is measured separately. */
int main (string[] args) {
    if (args.length != 4 || !Gtk.init_check ()) {
        stderr.printf ("Usage: benchmark-viewer-lifecycle FILE CYCLES VIEWERS\n");
        return 2;
    }
    int cycles = int.parse (args[2]);
    int viewers = int.parse (args[3]);
    if (cycles < 1 || cycles > 50 || viewers < 1 || viewers > 16) {
        return 2;
    }
    var loop = new MainLoop ();
    int status = 0;
    measure_lifecycle.begin (File.new_for_commandline_arg (args[1]), cycles, viewers,
        (source, result) => {
        try {
            measure_lifecycle.end (result);
        } catch (Error error) {
            stderr.printf ("Viewer lifecycle benchmark failed: %s\n", error.message);
            status = 1;
        }
        loop.quit ();
    });
    loop.run ();
    return status;
}
