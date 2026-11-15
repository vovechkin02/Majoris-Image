using ImageKit;

private class FaultBackend : Object, DecoderBackend {
    public string id { get { return "test-fault"; } }
    public string executable { get { return path; } }
    public ImageCapabilities capabilities { get { return ImageCapabilities.METADATA; } }
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
    var source = new ImageSource (File.new_for_path (input));
    try {
        FileUtils.set_contents (input, "C");
        try { yield registry.inspect (source); assert_not_reached (); }
        catch (ImageError.DECODER_CRASH e) { }

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

        FileUtils.set_contents (input, "I");
        try { yield registry.inspect (source); assert_not_reached (); }
        catch (ImageError.INVALID_RESPONSE e) { }
    } finally {
        FileUtils.unlink (input);
        DirUtils.remove (dir);
    }
}

int main (string[] args) {
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
