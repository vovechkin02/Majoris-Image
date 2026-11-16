using ImageKit;

private class FaultBackend : Object, DecoderBackend {
    public string id {
        get {
            return "test-fault";
        }
    }
    public string executable {
        get {
            return path;
        }
    }
    public ImageCapabilities capabilities {
        get {
            return ImageCapabilities.METADATA | ImageCapabilities.STILL;
        }
    }
    public string[] mime_types {
        owned get {
            return { "text/plain" };
        }
    }

    private string path;

    public FaultBackend (string path) {
        this.path = path;
    }

    public bool accepts (string mime, Bytes signature) {
        return signature.get_size () > 0;
    }
}

private string configure_launcher_fixture () throws Error {
    string launcher = Environment.find_program_in_path ("bwrap");
    if (launcher == null) {
        throw new IOError.NOT_FOUND ("Bubblewrap is required for the worker security test");
    }
    string directory = DirUtils.make_tmp ("launcher-test-XXXXXX");
    string wrapper = Path.build_filename (directory, "bwrap");
    string script = """#!/bin/sh
if [ -f "$IMAGEKIT_LAUNCHER_TEST/arm" ]; then
    (
        count=0
        while [ ! -f "$IMAGEKIT_LAUNCHER_TEST/release" ] && [ "$count" -lt 200 ]; do
            count=$((count + 1))
            sleep 0.02
        done
        : > "$IMAGEKIT_LAUNCHER_TEST/done"
    ) &
    : > "$IMAGEKIT_LAUNCHER_TEST/ready"
    wait
else
    exec %s "$@"
fi
""".printf (Shell.quote (launcher));
    FileUtils.set_contents (wrapper, script);
    if (FileUtils.chmod (wrapper, 0700) != 0) {
        throw new IOError.FAILED ("Cannot make the launcher fixture executable");
    }
    Environment.set_variable ("IMAGEKIT_LAUNCHER_TEST", directory, true);
    Environment.set_variable ("PATH", directory + ":" + Environment.get_variable ("PATH"), true);
    return directory;
}

private async void wait_for_pipe_holder (string path) throws Error {
    int64 deadline = get_monotonic_time () + 5 * TimeSpan.SECOND;
    while (!FileUtils.test (path, FileTest.EXISTS)) {
        if (get_monotonic_time () >= deadline) {
            throw new IOError.TIMED_OUT ("The launcher fixture did not release its diagnostic pipe");
        }
        Timeout.add (5, () => {
            wait_for_pipe_holder.callback ();
            return Source.REMOVE;
        });
        yield;
    }
}

private async void verify_launcher_cancellation (DecoderRegistry registry, ImageSource source,
    string directory) throws Error {
    string arm = Path.build_filename (directory, "arm");
    string ready = Path.build_filename (directory, "ready");
    string released = Path.build_filename (directory, "release");
    string done = Path.build_filename (directory, "done");
    FileUtils.set_contents (arm, "");
    var cancel = new Cancellable ();
    int64 deadline = get_monotonic_time () + 2 * TimeSpan.SECOND;
    bool canceled = false;
    uint timer = Timeout.add (5, () => {
        if (!canceled &&
            (FileUtils.test (ready, FileTest.EXISTS) || get_monotonic_time () >= deadline)) {
            canceled = true;
            cancel.cancel ();
        }
        return Source.CONTINUE;
    });
    try {
        yield registry.inspect (source, cancel);
        assert_not_reached ();
    } catch (IOError.CANCELLED error) {
        assert (FileUtils.test (ready, FileTest.EXISTS));
        // The canceled request must complete while the helper still owns the pipe.
        assert (!FileUtils.test (done, FileTest.EXISTS));
    } finally {
        Source.remove (timer);
        FileUtils.unlink (arm);
        try {
            FileUtils.set_contents (released, "");
            yield wait_for_pipe_holder (done);
        } catch (Error cleanup_error) {
            GLib.error ("%s", cleanup_error.message);
        }
    }
}

private async void verify_replacement_during_decode (DecoderRegistry registry, ImageSource source)
    throws Error {
    string path = source.file.get_path ();
    FileUtils.set_contents (path, "H");
    bool replaced = false;
    Error? mutation_error = null;
    var deadline = get_monotonic_time () + 3 * TimeSpan.SECOND;
    var cancel = new Cancellable ();
    uint timer = Timeout.add (5, () => {
        if (replaced || mutation_error != null) {
            return Source.CONTINUE;
        }
        if (get_monotonic_time () >= deadline) {
            mutation_error = new IOError.TIMED_OUT ("The worker did not signal decode readiness");
            cancel.cancel ();
            return Source.CONTINUE;
        }
        try {
            var directory = Dir.open (Environment.get_tmp_dir ());
            string? name;
            while ((name = directory.read_name ()) != null) {
                if (!name.has_prefix ("majoris-image-")) {
                    continue;
                }
                string job = Path.build_filename (Environment.get_tmp_dir (), name);
                if (FileUtils.test (Path.build_filename (job, "ready"), FileTest.EXISTS)) {
                    FileUtils.set_contents (path, "Y");
                    FileUtils.set_contents (Path.build_filename (job, "release"), "");
                    replaced = true;
                    break;
                }
            }
        } catch (Error error) {
            mutation_error = error;
            cancel.cancel ();
        }
        return Source.CONTINUE;
    });
    try {
        yield registry.open (source, null, cancel);
        throw new IOError.FAILED ("The replacement was accepted as the decoded source");
    } catch (ImageError.SOURCE_CHANGED error) {
        assert (replaced);
    } catch (IOError.CANCELLED error) {
        throw mutation_error ?? error;
    } finally {
        Source.remove (timer);
    }
}

async void verify_worker_failures_and_isolation (string worker_path, string launcher_directory)
    throws Error {
    var registry = new DecoderRegistry ();
    registry.timeout_seconds = 1;
    registry.register_backend (new FaultBackend (worker_path));
    string dir = DirUtils.make_tmp ("majoris-worker-test-XXXXXX");
    string input = Path.build_filename (dir, "original");
    string secret = Path.build_filename (dir, "app-data");
    var source = new ImageSource (File.new_for_path (input));
    try {
        foreach (string mode in new string[] {
            "C", "C", "X"
        }) {
            FileUtils.set_contents (input, mode);
            try {
                yield registry.inspect (source);
                assert_not_reached ();
            } catch (ImageError.DECODER_CRASH e) {
                // The injected worker crash must surface as a decoder error.
            }
        }

        FileUtils.set_contents (input, "T");
        try {
            yield registry.inspect (source);
            assert_not_reached ();
        } catch (ImageError.TIMEOUT e) {
            // The deliberately stalled worker must time out.
        }

        var cancel = new Cancellable ();
        Timeout.add (50, () => {
            cancel.cancel ();
            return Source.REMOVE;
        });
        try {
            yield registry.inspect (source, cancel);
            assert_not_reached ();
        } catch (IOError.CANCELLED e) {
            // Cancellation is the expected result for this request.
        }

        FileUtils.set_contents (input, "R");
        try {
            yield registry.inspect (source);
            assert_not_reached ();
        } catch (ImageError.UNSUPPORTED e) {
            assert (e.message == "source is read-only");
        }
        string original;
        FileUtils.get_contents (input, out original);
        assert (original == "R");

        foreach (string mode in new string[] {
            "I", "J", "N", "Q", "Z", "E", "K", "W"
        }) {
            FileUtils.set_contents (input, mode);
            try {
                yield registry.inspect (source);
                assert_not_reached ();
            } catch (ImageError.INVALID_RESPONSE e) {
                // The malformed worker response must be rejected.
            }
        }
        FileUtils.set_contents (input, "U");
        try {
            yield registry.inspect (source);
            assert_not_reached ();
        } catch (ImageError.DECODE e) {
            assert (e.message == "unknown diagnostic");
        }

        FileUtils.set_contents (input, "M");
        try {
            yield registry.inspect (source);
            assert_not_reached ();
        } catch (ImageError.MEMORY e) {
            // The injected memory failure must retain its error code.
        }

        // The synthetic worker uses the actual Bubblewrap header and prlimit.
        FileUtils.set_contents (secret, "private test application data");
        string network = FileUtils.read_link ("/proc/self/ns/net");
        FileUtils.set_contents (input, "!%s %s".printf (network, secret));
        var recovered = yield registry.inspect (source);
        assert (recovered.width == 1 && recovered.loader == "fault-test");

        foreach (string mode in new string[] {
            "S", "L"
        }) {
            FileUtils.set_contents (input, mode);
            try {
                yield registry.open (source);
                assert_not_reached ();
            } catch (ImageError.INVALID_RESPONSE e) {
                // The malformed worker response must be rejected.
            }
        }
        FileUtils.set_contents (input, "Y");
        var document = yield registry.open (source);
        assert (document.preview.width == 1 && document.preview.stride == 16);
        registry.timeout_seconds = 5;
        yield verify_launcher_cancellation (registry, source, launcher_directory);
        yield verify_replacement_during_decode (registry, source);
        var unavailable = new DecoderRegistry ();
        unavailable.register_backend (new FaultBackend (Path.build_filename (dir, "nonexistent-worker")));
        try {
            yield unavailable.inspect (source);
            assert_not_reached ();
        } catch (ImageError.SANDBOX e) {
            assert (e.message.contains ("bwrap"));
        }

        cancel = new Cancellable ();
        cancel.cancel ();
        try {
            yield ImageSource.from_stream (new MemoryInputStream.from_bytes (new Bytes ("input".data)), cancel);
            assert_not_reached ();
        } catch (IOError.CANCELLED e) {
            // Cancellation is the expected result for this request.
        }
        var entries = Dir.open (Environment.get_tmp_dir ());
        string? entry;
        while ((entry = entries.read_name ()) != null) {
            assert (!entry.has_prefix ("majoris-image-"));
        }
        string children;
        FileUtils.get_contents ("/proc/thread-self/children", out children);
        assert (children.strip () == "");
    } finally {
        FileUtils.unlink (secret);
        FileUtils.unlink (input);
        DirUtils.remove (dir);
    }
}

int main (string[] args) {
    if (args.length != 2) {
        return 2;
    }
    var loop = new MainLoop ();
    int result = 0;
    string launcher_directory;
    try {
        launcher_directory = configure_launcher_fixture ();
    } catch (Error error) {
        stderr.printf ("%s\n", error.message);
        return 1;
    }
    verify_worker_failures_and_isolation.begin (args[1], launcher_directory, (obj, response) => {
        try {
            verify_worker_failures_and_isolation.end (response);
        } catch (Error e) {
            stderr.printf ("%s\n", e.message);
            result = 1;
        }
        loop.quit ();
    });
    loop.run ();
    return result;
}
