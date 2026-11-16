using ImageKit;

async void verify (string directory, string large_path) throws Error {
    var recipe = RenderOptions.deserialize ("{\"version\":2,\"nearest-neighbour\":true}");
    assert (recipe.nearest_neighbor);
    string saved = recipe.serialize ();
    assert (saved.contains ("\"nearest-neighbour\"") && !saved.contains ("\"nearest-neighbor\""));
    assert (RenderOptions.deserialize (saved).nearest_neighbor);
    var source = new ImageSource (File.new_for_path (Path.build_filename (directory, "rgb16.png")));
    var registry = new DecoderRegistry ();
    var document = yield registry.open (source);
    assert (document.width == 24 && document.height == 16);
    assert (document.bit_depth == 16);
    assert ((document.capabilities & ImageCapabilities.HIGH_DEPTH) != 0);
    assert (document.preview.bytes.get_size () == (size_t) document.preview.stride * document.preview.height);
    var rgb8 = yield registry.open (new ImageSource (File.new_for_path (Path.build_filename (directory, "rgb8.png"))));
    assert (rgb8.bit_depth == 8);
    var pixels = yield document.render_region (0, 0, 1, 1, 1);
    assert (pixels.width == 1 && pixels.height == 1 && pixels.bytes.get_size () == 16);
    // Hold the expired entry open while the actual cache maintenance runs.
    string cache_path = Path.build_filename (Environment.get_user_cache_dir (), "majoris-image", "thumbnails-v2");
    assert (DirUtils.create_with_parents (cache_path, 0700) == 0);
    var expired = File.new_for_path (Path.build_filename (cache_path, string.nfill (64, '0') + ".png"));
    var expired_pixels = yield source.file.load_bytes_async (null, null);
    unowned uint8[] original_png = expired_pixels.get_data ();
    expired.replace_contents (original_png, null, false, FileCreateFlags.PRIVATE, null, null);
    uint64 old_time = (uint64) (new DateTime.now_utc ().to_unix () - 31 * 86400);
    expired.set_attribute_uint64 (FileAttribute.TIME_MODIFIED, old_time, FileQueryInfoFlags.NONE, null);
    var active_reader = yield expired.read_async ();
    var output = yield new ThumbnailService ().generate (source, 64);
    int64 eviction_deadline = get_monotonic_time () + 5 * TimeSpan.SECOND;
    while (expired.query_exists () && get_monotonic_time () < eviction_deadline)
        yield wait_for_eviction ();
    if (expired.query_exists ()) throw new IOError.TIMED_OUT ("Expired thumbnail was not evicted");
    uint8[] saved_pixels = new uint8[original_png.length];
    size_t read;
    assert (active_reader.read_all (saved_pixels, out read, null) && read == original_png.length);
    assert (expired_pixels.compare (new Bytes (saved_pixels)) == 0);
    yield active_reader.close_async ();
    var cached = yield new ThumbnailService ().generate (source, 64);
    assert (cached.compare (output) == 0);
    unowned uint8[] png = output.get_data ();
    assert (png[0] == 137 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');

    var vector = new ImageSource (File.new_for_path (Path.build_filename (directory, "vector.svg")));
    var svg = yield registry.open (vector);
    assert (svg.vector && svg.supports_regions);

    var multipart = new ImageSource (File.new_for_path (Path.build_filename (directory, "multipart.exr")));
    var exr = yield registry.open (multipart);
    assert (exr.hdr && exr.bit_depth == 32 && exr.page_count >= 2);
    var exr_stream = yield multipart.file.read_async ();
    var staged = yield ImageSource.from_stream (exr_stream);
    yield exr_stream.close_async ();
    var retained = yield registry.open (staged);
    assert (staged.file.delete ());
    var retained_sdr = yield retained.render_preview (true);
    var retained_hdr = yield retained.render_preview ();
    assert (retained_hdr.bytes.compare (exr.preview.bytes) == 0);
    assert (luminance (retained_sdr, 16, 8) < 1.0);
    assert (Math.fabs (sample (retained_sdr, 16, 8, 3) - 0.75) < 0.0001);
    var part = yield exr.open_page (1);
    assert (part.page == 1 && part.hdr && part.supports_regions);

    var options = new DecodeOptions ();
    options.sdr_presentation = true;
    var sdr = yield registry.open (multipart, options);
    assert (sdr.tone_mapped);
    double original_y = luminance (exr.preview, 16, 8);
    double mapped_y = luminance (sdr.preview, 16, 8);
    assert (Math.fabs (luminance (retained_sdr, 16, 8) - mapped_y) < 0.0001);
    assert (Math.fabs (mapped_y - 1.6 * original_y / (1 + 1.6 * original_y)) < 0.0001);
    assert (Math.fabs (sample (sdr.preview, 16, 8, 3) - 0.75) < 0.0001);
    var sdr_tile = yield sdr.render_region (16, 8, 1, 1, 1, true);
    assert (Math.fabs (luminance (sdr_tile, 0, 0) - mapped_y) < 0.0001);

    int64 began = get_monotonic_time ();
    var large_source = new ImageSource (File.new_for_path (large_path));
    var large = yield registry.open (large_source);
    assert (large.width == 20000 && large.height == 10000 && large.supports_regions);
    assert (large.preview.bytes.get_size () < 21 * 1024 * 1024);
    var tile = yield large.render_region (19900, 9900, 100, 100, 100);
    assert (tile.width == 100 && tile.height == 100);
    stdout.printf ("200 MP TIFF preview and region %.3f s\n", (get_monotonic_time () - began) / 1000000.0);

    var cancel = new Cancellable ();
    cancel.cancel ();
    try {
        yield registry.open (source, null, cancel);
        assert_not_reached ();
    } catch (IOError.CANCELLED e) { }
    var corrupt = new ImageSource (File.new_for_path (Path.build_filename (directory, "corrupt.bin")));
    try {
        yield registry.open (corrupt);
        assert_not_reached ();
    } catch (ImageError.UNSUPPORTED e) { }

    var preloader = new PreloadManager.with_registry (registry);
    var smaller = new DecodeOptions ();
    smaller.preview_size = 8;
    yield preloader.preload ({ corrupt, source, multipart, vector, rgb8.source });
    var prefetched = yield preloader.open (source);
    assert (prefetched.decoder == "pngload" && prefetched == (yield preloader.open (source)));
    var prefetched_exr = yield preloader.open (multipart);
    assert (prefetched_exr.decoder == exr.decoder && prefetched_exr == (yield preloader.open (multipart)));
    assert ((yield preloader.open (rgb8.source)) != (yield preloader.open (rgb8.source)));
    var reduced = yield preloader.open (source, smaller);
    assert (reduced != prefetched && reduced.preview.width == 8);

    var separate = new PreloadManager ();
    yield separate.preload ({ source }, smaller);
    assert ((yield separate.open (source, smaller)) != reduced);
    assert (prefetched == (yield preloader.open (source)));

    var changing_stream = yield source.file.read_async ();
    var changing = yield ImageSource.from_stream (changing_stream);
    yield changing_stream.close_async ();
    yield preloader.preload ({ changing });
    var cached_source = yield preloader.open (changing);
    assert (cached_source.bit_depth == 16 && cached_source == (yield preloader.open (changing)));
    yield rgb8.source.file.copy_async (changing.file, FileCopyFlags.OVERWRITE, Priority.DEFAULT, null);
    var changed = yield preloader.open (changing);
    assert (changed != cached_source && changed.bit_depth == 8);

    bool superseded = false;
    preloader.preload.begin ({ large_source }, null, (obj, reply) => {
        preloader.preload.end (reply);
        superseded = true;
    });
    yield preloader.preload ({ source, multipart });
    assert (superseded);
    assert ((yield preloader.open (large_source)) != (yield preloader.open (large_source)));
    var foreground = yield preloader.open (source);
    assert (foreground == (yield preloader.open (source)));

    var heavy = new DecodeOptions ();
    heavy.preview_size = 3000;
    yield preloader.preload ({ large_source }, heavy);
    var too_large = yield preloader.open (large_source, heavy);
    assert (too_large.preview.bytes.get_size () > 64 * 1024 * 1024);
    assert (too_large != (yield preloader.open (large_source, heavy)));
    preloader.clear ();
    string temp = DirUtils.make_tmp ("majoris-export-test-XXXXXX");
    var target = File.new_for_path (Path.build_filename (temp, "export.png"));
    try {
        var transform = new RenderOptions ();
        transform.turns = 1;
        yield new EncoderRegistry ().encode (source, target, new ExportOptions (), transform);
        var exported = yield registry.open (new ImageSource (target));
        assert (exported.width == 16 && exported.height == 24 && exported.bit_depth == 16);
    } finally {
        FileUtils.unlink (target.get_path ());
        DirUtils.remove (temp);
    }
}

private async void wait_for_eviction () {
    Timeout.add (10, () => { wait_for_eviction.callback (); return Source.REMOVE; });
    yield;
}

private float sample (PixelBuffer pixels, int x, int y, int channel) {
    unowned uint8[] data = pixels.bytes.get_data ();
    float value = 0;
    Memory.copy (&value, &data[y * pixels.stride + 16 * x + 4 * channel], 4);
    return value;
}

private double luminance (PixelBuffer pixels, int x, int y) {
    return 0.2126 * sample (pixels, x, y, 0) + 0.7152 * sample (pixels, x, y, 1) +
           0.0722 * sample (pixels, x, y, 2);
}

int main (string[] args) {
    var loop = new MainLoop ();
    int result = 0;
    verify.begin (args[1], args[2], (obj, response) => {
        try { verify.end (response); }
        catch (Error e) { stderr.printf ("%s\n", e.message); result = 1; }
        loop.quit ();
    });
    loop.run ();
    return result;
}
