using ImageKit;

private const int SNAPSHOT_SAMPLES = 7;
private const int SNAPSHOT_ITERATIONS = 200;

[CCode (cname = "gsk_container_node_get_n_children", cheader_filename = "gtk/gtk.h")]
private extern uint container_size (Gsk.RenderNode node);
[CCode (cname = "gsk_container_node_get_child", cheader_filename = "gtk/gtk.h")]
private extern unowned Gsk.RenderNode container_child (Gsk.RenderNode node, uint index);
[CCode (cname = "gsk_transform_node_get_child", cheader_filename = "gtk/gtk.h")]
private extern unowned Gsk.RenderNode transform_child (Gsk.RenderNode node);
[CCode (cname = "gsk_clip_node_get_child", cheader_filename = "gtk/gtk.h")]
private extern unowned Gsk.RenderNode clip_child (Gsk.RenderNode node);
[CCode (cname = "gsk_mask_node_get_source", cheader_filename = "gtk/gtk.h")]
private extern unowned Gsk.RenderNode mask_source (Gsk.RenderNode node);

private async void wait_frame () {
    Timeout.add (10, () => {
        wait_frame.callback ();
        return Source.REMOVE;
    });
    yield;
}

private Gsk.RenderNode? snapshot_view (Gtk.Box parent, ImageView view) {
    view.queue_draw ();
    var snapshot = new Gtk.Snapshot ();
    parent.snapshot_child (view, snapshot);
    return snapshot.to_node ();
}

private int count_textures (Gsk.RenderNode? node) {
    if (node == null) {
        return 0;
    }
    switch (node.get_node_type ()) {
        case Gsk.RenderNodeType.CONTAINER_NODE:
            int count = 0;
            uint children = container_size (node);
            for (uint i = 0; i < children; i++) {
                count += count_textures (container_child (node, i));
            }
            return count;
        case Gsk.RenderNodeType.TRANSFORM_NODE:
            return count_textures (transform_child (node));
        case Gsk.RenderNodeType.CLIP_NODE:
            return count_textures (clip_child (node));
        case Gsk.RenderNodeType.MASK_NODE:
            return count_textures (mask_source (node));
        case Gsk.RenderNodeType.TEXTURE_NODE:
        case Gsk.RenderNodeType.TEXTURE_SCALE_NODE:
            return 1;
        default:
            return 0;
    }
}

private int visible_tiles (ImageView view) {
    // The benchmark uses centered actual size at device scale 1.
    int left = view.image_width - view.get_width ();
    int top = view.image_height - view.get_height ();
    int columns = (left + 2 * view.get_width () + 1023) / 1024 - left / 1024;
    int rows = (top + 2 * view.get_height () + 1023) / 1024 - top / 1024;
    return columns * rows;
}

private async void benchmark_view (Gtk.Box parent, ImageView[] views, File file) throws Error {
    int64 deadline = get_monotonic_time () + 5 * TimeSpan.SECOND;
    foreach (var view in views) {
        while (!view.get_mapped () || view.get_width () <= 0 || view.get_height () <= 0) {
            if (get_monotonic_time () >= deadline) {
                throw new IOError.TIMED_OUT (
                    "ImageView benchmark window did not receive an allocation");
            }
            yield wait_frame ();
        }
        assert (view.get_scale_factor () == 1);
    }

    int64 started = get_monotonic_time ();
    foreach (var view in views) {
        yield view.load_file (file);
    }
    int64 preview = get_monotonic_time () - started;
    int expected = views.length;
    foreach (var view in views) {
        view.actual_size ();
        expected += visible_tiles (view);
    }
    started = get_monotonic_time ();
    int64 first = 0;
    int64 complete = 0;
    deadline = started + 120 * TimeSpan.SECOND;
    while (complete == 0 && get_monotonic_time () < deadline) {
        int textures = 0;
        foreach (var view in views) {
            textures += count_textures (snapshot_view (parent, view));
        }
        if (first == 0 && textures > views.length) {
            first = get_monotonic_time () - started;
        }
        if (textures == expected) {
            complete = get_monotonic_time () - started;
        } else {
            yield wait_frame ();
        }
    }
    if (complete == 0) {
        throw new IOError.TIMED_OUT ("ImageView benchmark viewport did not complete");
    }
    stdout.printf ("Viewers %d (%dx%d first allocation), preview %s us, first tile %s us, " +
        "complete %s us\n",
        views.length, views[0].get_width (), views[0].get_height (),
        preview.to_string (), first.to_string (), complete.to_string ());

    foreach (var moving in new bool[] { false, true }) {
        int64[] samples = new int64[SNAPSHOT_SAMPLES];
        for (int sample = 0; sample < samples.length; sample++) {
            var timer = new Timer ();
            for (int iteration = 0; iteration < SNAPSHOT_ITERATIONS; iteration++) {
                foreach (var view in views) {
                    if (moving) {
                        int direction = iteration % 2 == 0 ? 1 : -1;
                        view.zoom_to (direction == 1 ? 0.75 : 1);
                        view.pan_by (direction * 200, direction * 120);
                    }
                    assert (snapshot_view (parent, view) != null);
                }
            }
            samples[sample] = (int64) (timer.elapsed () * TimeSpan.SECOND);
        }
        // Seven samples are small enough to sort in place without boxing.
        for (int i = 1; i < samples.length; i++) {
            int64 value = samples[i];
            int j = i;
            while (j > 0 && samples[j - 1] > value) {
                samples[j] = samples[j - 1];
                j--;
            }
            samples[j] = value;
        }
        stdout.printf ("  %s snapshot: median %.2f us per iteration\n",
            moving ? "pan/zoom" : "unchanged",
            (double) samples[samples.length / 2] / SNAPSHOT_ITERATIONS);
    }
}

private async void run_benchmarks (File file) throws Error {
    for (int count = 1; count <= 2; count++) {
        var window = new Gtk.Window ();
        window.title = "GTK ImageView rendering benchmark";
        window.set_default_size (1920, 1080);
        var box = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 0);
        var views = new ImageView[count];
        for (int index = 0; index < count; index++) {
            views[index] = new ImageView ();
            box.append (views[index]);
        }
        window.child = box;
        window.present ();
        try {
            yield benchmark_view (box, views, file);
        } finally {
            window.destroy ();
        }
    }
    string memory;
    FileUtils.get_contents ("/proc/self/status", out memory);
    foreach (var line in memory.split ("\n")) {
        if (line.has_prefix ("VmHWM:")) {
            stdout.printf ("%s\n", line);
        }
    }
}

private async void benchmark_raw_preview (File file) throws Error {
    var window = new Gtk.Window ();
    window.title = "RAW preview benchmark";
    window.set_default_size (800, 600);
    var view = new ImageView ();
    window.child = view;
    window.present ();
    int64 first_preview = 0;
    int64 started = get_monotonic_time ();
    ulong handler = view.notify["state"].connect (() => {
        if (view.state == ViewState.PREVIEW_READY && first_preview == 0) {
            first_preview = get_monotonic_time () - started;
        }
    });
    try {
        yield view.load_file (file);
        if (first_preview == 0 || view.document == null ||
            view.document.raw_state != RawState.RAW_DEVELOPED) {
            throw new IOError.FAILED ("RAW preview or full development was not presented");
        }
        stdout.printf ("ImageKit-profile gtk-preview %" + int64.FORMAT + " 1\n", first_preview);
        stdout.printf ("ImageKit-profile workload %" + int64.FORMAT + " 1\n",
            get_monotonic_time () - started);
    } finally {
        view.disconnect (handler);
        window.destroy ();
    }
}

private async void run_mode (string[] args) throws Error {
    if (args.length == 3 && args[1] == "raw-first") {
        yield benchmark_raw_preview (File.new_for_path (args[2]));
    } else if (args.length == 2) {
        yield run_benchmarks (File.new_for_path (args[1]));
    } else {
        throw new IOError.INVALID_ARGUMENT ("Expected FILE or raw-first FILE");
    }
}

int main (string[] args) {
    if (!Gtk.init_check ()) {
        return 77;
    }
    var loop = new MainLoop ();
    int status = 0;
    run_mode.begin (args, (object, response) => {
        try {
            run_mode.end (response);
        } catch (Error error) {
            stderr.printf ("%s\n", error.message);
            status = 1;
        }
        loop.quit ();
    });
    loop.run ();
    return status;
}
