using ImageKit;

private class FaultBackend : Object, DecoderBackend {
    public string id { get { return "test-fault"; } }
    public string executable { get { return path; } }
    public ImageCapabilities capabilities { get { return ImageCapabilities.METADATA | ImageCapabilities.STILL; } }
    public string[] mime_types { owned get { return { "text/plain" }; } }
    private string path;
    public FaultBackend (string path) { this.path = path; }
    public bool accepts (string mime, Bytes signature) { return signature.get_size () > 0; }
}

async void exercise (string worker_path) throws Error {
    var registry = new DecoderRegistry ();
    registry.timeout_seconds = 1;
    registry.register_backend (new FaultBackend (worker_path));
    string dir = DirUtils.make_tmp ("majoris-worker-test-XXXXXX");
    string input = Path.build_filename (dir, "original");
    string secret = Path.build_filename (dir, "app-data");
    var source = new ImageSource (File.new_for_path (input));
    try {
        foreach (string mode in new string[] { "C", "C", "X" }) {
            FileUtils.set_contents (input, mode);
            try { yield registry.inspect (source); assert_not_reached (); }
            catch (ImageError.DECODER_CRASH e) { }
        }

        FileUtils.set_contents (input, "T");
        try { yield registry.inspect (source); assert_not_reached (); }
        catch (ImageError.TIMEOUT e) { }

        var cancel = new Cancellable ();
        Timeout.add (50, () => { cancel.cancel (); return Source.REMOVE; });
        try { yield registry.inspect (source, cancel); assert_not_reached (); }
        catch (IOError.CANCELLED e) { }

        FileUtils.set_contents (input, "R");
        try { yield registry.inspect (source); assert_not_reached (); }
        catch (ImageError.UNSUPPORTED e) { assert (e.message == "source is read-only"); }
        string original;
        FileUtils.get_contents (input, out original);
        assert (original == "R");

        foreach (string mode in new string[] { "I", "J", "N", "Q", "Z", "E", "K", "W" }) {
            FileUtils.set_contents (input, mode);
            try { yield registry.inspect (source); assert_not_reached (); }
            catch (ImageError.INVALID_RESPONSE e) { }
        }
        FileUtils.set_contents (input, "U");
        try { yield registry.inspect (source); assert_not_reached (); }
        catch (ImageError.DECODE e) { assert (e.message == "unknown diagnostic"); }

        FileUtils.set_contents (input, "M");
        try { yield registry.inspect (source); assert_not_reached (); }
        catch (ImageError.MEMORY e) { }

        // The synthetic worker uses the actual Bubblewrap header and prlimit.
        FileUtils.set_contents (secret, "private test application data");
        string network = FileUtils.read_link ("/proc/self/ns/net");
        FileUtils.set_contents (input, "!%s %s".printf (network, secret));
        var recovered = yield registry.inspect (source);
        assert (recovered.width == 1 && recovered.loader == "fault-test");

        foreach (string mode in new string[] { "S", "L" }) {
            FileUtils.set_contents (input, mode);
            try { yield registry.open (source); assert_not_reached (); }
            catch (ImageError.INVALID_RESPONSE e) { }
        }
        FileUtils.set_contents (input, "Y");
        var document = yield registry.open (source);
        assert (document.preview.width == 1 && document.preview.stride == 16);
        var unavailable = new DecoderRegistry ();
        unavailable.register_backend (new FaultBackend (Path.build_filename (dir, "nonexistent-worker")));
        try { yield unavailable.inspect (source); assert_not_reached (); }
        catch (ImageError.SANDBOX e) { assert (e.message.contains ("bwrap")); }

        cancel = new Cancellable ();
        cancel.cancel ();
        try {
            yield ImageSource.from_stream (new MemoryInputStream.from_bytes (new Bytes ("input".data)), cancel);
            assert_not_reached ();
        } catch (IOError.CANCELLED e) { }
        var entries = Dir.open (Environment.get_tmp_dir ());
        string? entry;
        while ((entry = entries.read_name ()) != null)
            assert (!entry.has_prefix ("majoris-image-"));
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
    if (args.length != 2) return 2;
    var loop = new MainLoop ();
    int result = 0;
    exercise.begin (args[1], (obj, response) => {
        try { exercise.end (response); }
        catch (Error e) { stderr.printf ("%s\n", e.message); result = 1; }
        loop.quit ();
    });
    loop.run ();
    return result;
}
