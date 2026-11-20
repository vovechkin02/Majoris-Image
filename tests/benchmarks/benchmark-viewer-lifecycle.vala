using ImageKit;

/** Returns the number of children in a GSK container node. */
[CCode (cname = "gsk_container_node_get_n_children", cheader_filename = "gtk/gtk.h")]
private extern uint container_size (Gsk.RenderNode node);
/** Retrieves a borrowed child node from a GSK container. */
[CCode (cname = "gsk_container_node_get_child", cheader_filename = "gtk/gtk.h")]
private extern unowned Gsk.RenderNode container_child (Gsk.RenderNode node, uint index);
/** Retrieves a borrowed child node from a transform node. */
[CCode (cname = "gsk_transform_node_get_child", cheader_filename = "gtk/gtk.h")]
private extern unowned Gsk.RenderNode transform_child (Gsk.RenderNode node);
/** Retrieves a borrowed child node from a clip node. */
[CCode (cname = "gsk_clip_node_get_child", cheader_filename = "gtk/gtk.h")]
private extern unowned Gsk.RenderNode clip_child (Gsk.RenderNode node);
/** Retrieves the borrowed texture-mask source for a mask node. */
[CCode (cname = "gsk_mask_node_get_source", cheader_filename = "gtk/gtk.h")]
private extern unowned Gsk.RenderNode mask_source (Gsk.RenderNode node);

/** Counts visible preview and decoded tile textures in a benchmark snapshot. */
private int count_textures (Gsk.RenderNode? node) {
    if (node == null) {
        return 0;
    }
    switch (node.get_node_type ()) {
        case Gsk.RenderNodeType.CONTAINER_NODE:
            int total = 0;
            for (uint index = 0; index < container_size (node); index++) {
                total += count_textures (container_child (node, index));
            }
            return total;
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

/** Waits for preview-plus-tile snapshots on all eight large-image viewers. */
private async void await_tiled_views (Gtk.Grid parent, Gee.ArrayList<ImageView> views)
    throws Error {
    int64 deadline = get_monotonic_time () + 90 * TimeSpan.SECOND;
    while (get_monotonic_time () < deadline) {
        int tiled = 0;
        foreach (var view in views) {
            if (!view.get_mapped () || view.get_width () <= 0 || view.get_height () <= 0) {
                continue;
            }
            view.queue_draw ();
            var snapshot = new Gtk.Snapshot ();
            parent.snapshot_child (view, snapshot);
            if (count_textures (snapshot.to_node ()) >= 2) {
                tiled++;
            }
        }
        if (tiled == views.size) {
            return;
        }
        yield quiesce ();
    }
    throw new IOError.TIMED_OUT ("Not all large-image viewers presented a decoded tile");
}

/** Tracks finalization of a viewer without extending the lifetime of its GObject. */
private class ViewerLifetimeProbe : Object {
    private WeakRef observed = WeakRef (null);

    /** Tracks an ImageView without retaining a strong reference to it. */
    public ViewerLifetimeProbe (ImageView view) {
        observed.set (view);
    }

    /** Reports whether the original viewer remains alive after normal release. */
    public bool is_alive () {
        return observed.get () != null;
    }
}

/** Loads one image view and returns only container-owned references. */
private async void attach_benchmark_view (File file, Gtk.Grid parent,
                                          Gee.ArrayList<ImageView> views,
                                          Gee.ArrayList<ViewerLifetimeProbe> probes,
                                          int viewer, bool tiled) throws Error {
    var view = new ImageView ();
    parent.attach (view, viewer % 4, viewer / 4, 1, 1);
    views.add (view);
    probes.add (new ViewerLifetimeProbe (view));
    yield view.load_file (file);
    var loaded = view.document;
    if (loaded == null) {
        throw new IOError.FAILED ("Viewer did not load the benchmark image");
    }
    if (tiled) {
        if (!loaded.supports_regions || view.image_width < 4096 ||
            view.image_height < 2048) {
            throw new IOError.INVALID_DATA (
                "Large-image benchmark requires a high-resolution tiled source");
        }
        view.actual_size ();
    }
}

/** Waits for weak viewer probes to clear after references and GTK parents are released. */
private async void assert_viewers_released (Gee.ArrayList<ViewerLifetimeProbe> probes,
                                            int cycle) throws Error {
    for (int attempt = 0; attempt < 15; attempt++) {
        int retained = 0;
        foreach (var probe in probes) {
            if (probe.is_alive ()) {
                retained++;
            }
        }
        if (retained == 0) {
            stdout.printf ("Viewer-lifetime cycle=%d released_viewers=%d\n",
                cycle, probes.size);
            return;
        }
        yield quiesce ();
    }
    throw new IOError.FAILED ("GTK retained one or more ImageView objects after release");
}

/** Extracts a Linux memory counter from one procfs snapshot in KiB. */
private int64 memory_field (string contents, string field) throws Error {
    foreach (var line in contents.split ("\n")) {
        if (line.has_prefix (field + ":")) {
            var fields = line.substring (field.length + 1).strip ().split (" ");
            return int64.parse (fields[0]);
        }
    }
    throw new IOError.NOT_FOUND ("Missing Linux memory counter %s", field);
}

/** Reads resident and high-water RSS from the same Linux status snapshot. */
private int64[] memory_pair () throws Error {
    string contents;
    FileUtils.get_contents ("/proc/self/status", out contents);
    int64[] counters = {
        memory_field (contents, "VmRSS"),
        memory_field (contents, "VmHWM"),
    };
    return counters;
}

/** Samples independently reported Linux memory categories for diagnostics only.
 *
 * These measurements are deliberately not part of the matched six-cycle
 * acceptance workload. They distinguish anonymous, file-backed and private
 * residency but cannot by themselves prove that allocations are live leaks.
 */
private void report_memory_diagnostics (string stage, int cycle, int textures) {
    try {
        string status;
        string rollup;
        FileUtils.get_contents ("/proc/self/status", out status);
        FileUtils.get_contents ("/proc/self/smaps_rollup", out rollup);
        stdout.printf (
            "Viewer-diagnostic stage=%s cycle=%d textures=%d " +
            "rss_kib=%" + int64.FORMAT + " hwm_kib=%" + int64.FORMAT +
            " anon_kib=%" + int64.FORMAT + " file_kib=%" + int64.FORMAT +
            " smaps_rss_kib=%" + int64.FORMAT +
            " smaps_pss_kib=%" + int64.FORMAT +
            " private_dirty_kib=%" + int64.FORMAT +
            " swap_kib=%" + int64.FORMAT + "\n",
            stage, cycle, textures,
            memory_field (status, "VmRSS"),
            memory_field (status, "VmHWM"),
            memory_field (status, "RssAnon"),
            memory_field (status, "RssFile"),
            memory_field (rollup, "Rss"),
            memory_field (rollup, "Pss"),
            memory_field (rollup, "Private_Dirty"),
            memory_field (rollup, "Swap"));
    } catch (Error error) {
        // WSL and other Linux variants may omit the optional procfs fields.
        // Report the limitation without making diagnostic mode look qualified.
        stdout.printf ("Viewer-diagnostic stage=%s cycle=%d unavailable=1\n",
            stage, cycle);
    }
}

/** Counts snapshot texture nodes in live viewers without retaining the nodes. */
private int count_view_textures (Gtk.Grid parent, Gee.ArrayList<ImageView> views) {
    int total = 0;
    foreach (var view in views) {
        if (!view.get_mapped ()) {
            continue;
        }
        var snapshot = new Gtk.Snapshot ();
        parent.snapshot_child (view, snapshot);
        total += count_textures (snapshot.to_node ());
    }
    return total;
}

/** Yields to GTK so asynchronous finalization and idle cleanup can complete. */
private async void quiesce () {
    Timeout.add (200, () => {
        quiesce.callback ();
        return Source.REMOVE;
    });
    yield;
}

/** Measures simultaneous-viewer memory and post-release residency across cycles. */
private async void measure_lifecycle (File file, int cycles, int viewers_per_cycle,
                                      bool tiled)
    throws Error {
    bool diagnostics = Environment.get_variable ("IMAGEKIT_MEMORY_DIAGNOSTICS") == "1";
    int64[] initial_counters = memory_pair ();
    int64 initial = initial_counters[0];
    stdout.printf ("Viewer-memory initial_rss_kib=%" + int64.FORMAT + "\n", initial);
    if (diagnostics) {
        report_memory_diagnostics ("initial", 0, 0);
    }
    for (int cycle = 0; cycle < cycles; cycle++) {
        var window = new Gtk.Window ();
        window.set_default_size (1600, 900);
        var parent = new Gtk.Grid ();
        parent.column_homogeneous = true;
        parent.row_homogeneous = true;
        window.child = parent;
        window.present ();
        var views = new Gee.ArrayList<ImageView> ();
        var probes = new Gee.ArrayList<ViewerLifetimeProbe> ();
        for (int viewer = 0; viewer < viewers_per_cycle; viewer++) {
            yield attach_benchmark_view (file, parent, views, probes, viewer, tiled);
        }
        // Keep every viewer alive simultaneously before sampling host memory.
        // This exercises per-view caches rather than sequentially disposing them.
        yield quiesce ();
        if (tiled) {
            yield await_tiled_views (parent, views);
            stdout.printf ("Viewer-tiles cycle=%d ready_viewers=%d\n",
                cycle + 1, views.size);
        }
        int64[] live_counters = memory_pair ();
        int64 live = live_counters[0];
        if (diagnostics) {
            report_memory_diagnostics (
                "live", cycle + 1, count_view_textures (parent, views));
        }
        foreach (var view in views) {
            parent.remove (view);
        }
        // Normal reference release is the measured path; do not force dispose.
        views.clear ();
        window.destroy ();
        // Weak references prove that the viewer objects were released; stable
        // RSS alone cannot distinguish allocator arenas from retained objects.
        yield assert_viewers_released (probes, cycle + 1);
        yield quiesce ();
        int64[] settled_counters = memory_pair ();
        int64 current = settled_counters[0];
        int64 peak = live_counters[1] > settled_counters[1]
            ? live_counters[1] : settled_counters[1];
        if (diagnostics) {
            report_memory_diagnostics ("settled", cycle + 1, 0);
            if (peak < live || peak < current) {
                stdout.printf ("Viewer-high-water-anomaly cycle=%d\n", cycle + 1);
            }
        }
        stdout.printf (
            "Viewer-memory cycle=%d live_rss_kib=%" + int64.FORMAT +
            " settled_rss_kib=%" + int64.FORMAT +
            " peak_rss_kib=%" + int64.FORMAT + "\n",
            cycle + 1, live, current, peak);
    }
    int64[] final_counters = memory_pair ();
    int64 settled = final_counters[0];
    if (diagnostics) {
        report_memory_diagnostics ("final", cycles, 0);
    }
    stdout.printf ("Viewer-memory final_rss_kib=%" + int64.FORMAT +
        " retained_delta_kib=%" + int64.FORMAT + "\n", settled, settled - initial);
}

/** Runs the GTK viewer-lifecycle benchmark; physical GPU memory is measured separately. */
int main (string[] args) {
    if (args.length != 5 || !Gtk.init_check ()) {
        stderr.printf ("Usage: benchmark-viewer-lifecycle FILE CYCLES VIEWERS preview|tiled\n");
        return 2;
    }
    int cycles = int.parse (args[2]);
    int viewers = int.parse (args[3]);
    if (cycles < 1 || cycles > 50 || viewers < 1 || viewers > 16) {
        return 2;
    }
    if (args[4] != "preview" && args[4] != "tiled") {
        return 2;
    }
    var loop = new MainLoop ();
    int status = 0;
    measure_lifecycle.begin (File.new_for_commandline_arg (args[1]), cycles, viewers,
        args[4] == "tiled",
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
