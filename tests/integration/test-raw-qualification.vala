using ImageKit;

async string compute_fixture_sha256 (File file) throws Error {
    var stream = yield file.read_async (Priority.DEFAULT, null);
    var hash = new Checksum (ChecksumType.SHA256);
    while (true) {
        var bytes = yield stream.read_bytes_async (65536, Priority.DEFAULT, null);
        if (bytes.get_size () == 0) {
            break;
        }
        hash.update (bytes.get_data (), bytes.get_size ());
    }
    yield stream.close_async (Priority.DEFAULT, null);
    return hash.get_string ();
}

async void verify_raw_manifest_samples (string directory, string manifest) throws Error {
    string contents;
    FileUtils.get_contents (manifest, out contents);
    int count = 0, previews = 0;
    foreach (var line in contents.split ("\n")) {
        if (line.strip () == "" || line.has_prefix ("#")) {
            continue;
        }
        var fields = line.split ("\t");
        if (fields.length != 11) {
            throw new IOError.INVALID_DATA ("Invalid RAW manifest entry: %s", line);
        }
        var name = fields[0];
        var file = File.new_for_path (Path.build_filename (directory, name));
        if (file.query_file_type (FileQueryInfoFlags.NOFOLLOW_SYMLINKS) != FileType.REGULAR) {
            throw new IOError.NOT_FOUND ("Missing required RAW fixture: %s", file.get_path ());
        }
        var source = new ImageSource (file);
        var registry = new DecoderRegistry ();
        var before = yield compute_fixture_sha256 (file);
        if (before != fields[8]) {
            throw new IOError.INVALID_DATA ("RAW checksum mismatch for %s: expected %s, got %s",
                name, fields[8], before);
        }
        var probe = yield registry.inspect (source);
        assert (probe.raw && probe.width > 0 && probe.height > 0);
        assert (probe.embedded_preview == (fields[6] == "true"));
        assert (probe.source_bit_depth == int.parse (fields[7]));
        var parser = new Json.Parser ();
        parser.load_from_data (probe.metadata);
        var metadata = parser.get_root ().get_object ();
        assert (metadata.get_string_member ("camera-model").strip () == fields[1]);
        assert (metadata.get_string_member ("orientation") == fields[5]);
        var options = new DecodeOptions ();
        options.preview_size = 256;
        options.raw_preview_only = true;
        try {
            var preview = yield registry.open (source, options);
            assert (preview.raw_state == RawState.RAW_PREVIEW_AVAILABLE && preview.bit_depth <= 16);
            assert (preview.bit_depth == 8 || preview.bit_depth == 16);
            assert (preview.pixel_buffer_bit_depth == 32);
            assert (preview.metadata.json.contains ("\"embedded-preview\":true"));
            if (previews == 0) {
                var preloader = new PreloadManager.with_registry (registry);
                yield preloader.preload ({ source }, options);
                var cached = yield preloader.open (source, options);
                assert (cached.raw_state == RawState.RAW_PREVIEW_AVAILABLE &&
                    cached.decoder == preview.decoder && cached == (yield preloader.open (source, options)));
            }
            previews++;
        } catch (Error error) {
            if (probe.embedded_preview) {
                throw error;
            }
            stdout.printf ("%s reports no embedded preview: %s\n", name, error.message);
        }
        options.raw_preview_only = false;
        var rgb = yield registry.open (source, options);
        assert (rgb.raw_state == RawState.RAW_DEVELOPED && rgb.bit_depth == 16);
        assert (rgb.source_bit_depth == probe.source_bit_depth && rgb.pixel_buffer_bit_depth == 32);
        assert (rgb.width == int.parse (fields[3]) && rgb.height == int.parse (fields[4]));
        assert (rgb.preview.bytes.get_size () == rgb.preview.stride * rgb.preview.height);
        options.camera_white_balance = false;
        options.raw_quality = 0;
        var neutral = yield registry.open (source, options);
        assert (neutral.width == rgb.width && neutral.height == rgb.height);
        assert (neutral.metadata.json.contains ("\"camera-white-balance\":false"));
        var thumbnail = yield new ThumbnailService ().generate (source, 128);
        assert (thumbnail.get_size () > 45);
        assert (before == (yield compute_fixture_sha256 (file)));
        stdout.printf ("%s developed %dx%d, source precision %d\n", name, rgb.width, rgb.height,
            rgb.source_bit_depth);
        count++;
    }
    assert (count == 9 && previews == 9);
    stdout.printf ("%d real RAW samples, %d embedded previews\n", count, previews);
}

int main (string[] args) {
    if (args.length != 3) {
        return 2;
    }
    var loop = new MainLoop ();
    int status = 0;
    verify_raw_manifest_samples.begin (args[1], args[2], (obj, result) => {
        try {
            verify_raw_manifest_samples.end (result);
        } catch (Error error) {
            stderr.printf ("%s\n", error.message);
            status = 1;
        }
        loop.quit ();
    });
    loop.run ();
    return status;
}
