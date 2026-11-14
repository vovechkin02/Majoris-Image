using MajorisImage;

async string checksum (File file) throws Error {
    var stream = yield file.read_async (Priority.DEFAULT, null);
    var hash = new Checksum (ChecksumType.SHA256);
    while (true) {
        var bytes = yield stream.read_bytes_async (65536, Priority.DEFAULT, null);
        if (bytes.get_size () == 0) break;
        hash.update (bytes.get_data (), bytes.get_size ());
    }
    yield stream.close_async (Priority.DEFAULT, null);
    return hash.get_string ();
}

async void exercise (string directory) throws Error {
    var entries = Dir.open (directory);
    int count = 0, previews = 0;
    string? name;
    while ((name = entries.read_name ()) != null) {
        var file = File.new_for_path (Path.build_filename (directory, name));
        if (file.query_file_type (FileQueryInfoFlags.NOFOLLOW_SYMLINKS) != FileType.REGULAR) continue;
        var source = new ImageSource (file);
        var registry = new DecoderRegistry ();
        string before = yield checksum (file);
        var probe = yield registry.inspect (source);
        assert (probe.raw && probe.width > 0 && probe.height > 0);
        var options = new DecodeOptions ();
        options.preview_size = 256;
        options.raw_preview_only = true;
        try {
            var preview = yield registry.open (source, options);
            assert (preview.raw_state == RawState.RAW_PREVIEW_AVAILABLE && preview.bit_depth <= 16);
            assert (preview.bit_depth == 8 || preview.bit_depth == 16);
            assert (preview.pixel_buffer_bit_depth == 32);
            assert (preview.metadata.json.contains ("\"embedded-preview\":true"));
            previews++;
        } catch (Error error) {
            if (probe.embedded_preview) throw error;
            stdout.printf ("%s reports no embedded preview: %s\n", name, error.message);
        }
        options.raw_preview_only = false;
        var rgb = yield registry.open (source, options);
        assert (rgb.raw_state == RawState.RAW_DEVELOPED && rgb.bit_depth == 16);
        assert (rgb.source_bit_depth == probe.source_bit_depth);
        options.camera_white_balance = false;
        options.raw_quality = 0;
        var neutral = yield registry.open (source, options);
        assert (neutral.width == rgb.width && neutral.height == rgb.height);
        assert (neutral.metadata.json.contains ("\"camera-white-balance\":false"));
        var thumbnail = yield new ThumbnailService ().generate (source, 128);
        assert (thumbnail.get_size () > 45);
        assert (before == (yield checksum (file)));
        stdout.printf ("%s developed %dx%d, source precision %d\n", name, rgb.width, rgb.height, rgb.source_bit_depth);
        count++;
    }
    assert (count > 0 && previews > 0);
    stdout.printf ("%d real RAW samples, %d embedded previews\n", count, previews);
}

int main (string[] args) {
    var loop = new MainLoop ();
    int status = 0;
    exercise.begin (args[1], (obj, result) => {
        try { exercise.end (result); }
        catch (Error error) { stderr.printf ("%s\n", error.message); status = 1; }
        loop.quit ();
    });
    loop.run ();
    return status;
}
