using ImageKit;

private class FailingInputStream : InputStream {
    private bool delivered;
    private bool cancel_read;

    public FailingInputStream (bool cancel_read) {
        this.cancel_read = cancel_read;
    }

    public override bool close (Cancellable? cancellable = null) throws IOError {
        return true;
    }

    public override ssize_t read (uint8[] buffer, Cancellable? cancellable = null) throws IOError {
        if (!delivered) {
            delivered = true;
            buffer[0] = 137;
            return 1;
        }
        if (cancel_read) {
            cancellable.cancel ();
            throw new IOError.CANCELLED ("Injected cancellation on the second read");
        }
        throw new IOError.FAILED ("Injected failure on the second read");
    }
}

private async void verify_failed_stream_ownership () throws Error {
    foreach (bool cancel_read in new bool[] { false, true }) {
        var stream = new FailingInputStream (cancel_read);
        try {
            yield ImageSource.from_stream (stream, new Cancellable ());
            assert_not_reached ();
        } catch (Error error) {
            if (cancel_read) {
                assert (error is IOError.CANCELLED);
            } else {
                assert (error is IOError.FAILED);
                assert (error.message == "Injected failure on the second read");
            }
        }
        assert (!stream.is_closed ());
        yield stream.close_async ();
        var temporary = Dir.open (Environment.get_tmp_dir ());
        string? entry;
        while ((entry = temporary.read_name ()) != null) {
            assert (!entry.has_prefix ("majoris-image-source-"));
        }
    }
}

private async File verify_staged_document (DecoderRegistry registry, ImageSource fixture)
    throws Error {
    var bytes = yield fixture.file.load_bytes_async (null, null);
    ImageSource? staged = yield ImageSource.from_bytes (bytes);
    var file = staged.file;
    var info = yield file.query_info_async (FileAttribute.UNIX_MODE, 0);
    assert ((info.get_attribute_uint32 (FileAttribute.UNIX_MODE) & 0777) == 0600);
    var document = yield registry.open (staged);
    staged = null;
    assert (file.query_exists ());
    var first = yield document.render_region (0, 0, 1, 1, 1);
    var second = yield document.render_region (0, 0, 1, 1, 1);
    assert (first.bytes.compare (second.bytes) == 0);
    assert ((yield document.open_page (1)).page == 1);
    info = yield file.query_info_async ("standard::size,time::modified", 0);
    file.set_attribute_uint64 (FileAttribute.TIME_MODIFIED,
        info.get_attribute_uint64 (FileAttribute.TIME_MODIFIED) - 10, FileQueryInfoFlags.NONE);
    assert ((yield file.query_info_async (FileAttribute.STANDARD_SIZE, 0)).get_size () ==
        info.get_size ());
    try {
        yield document.open_page (1);
        assert_not_reached ();
    } catch (ImageError.SOURCE_CHANGED error) {
        // Each page request must validate the retained source again.
    }
    try {
        yield document.render_region (0, 0, 1, 1, 1);
        assert_not_reached ();
    } catch (ImageError.SOURCE_CHANGED error) {
        // Previously rendered tiles do not authorize another read.
    }
    return file;
}

private async void verify_source_replacement (DecoderRegistry registry, ImageSource first,
    ImageSource second) throws Error {
    string directory = DirUtils.make_tmp ("majoris-source-test-XXXXXX");
    var path = File.new_for_path (Path.build_filename (directory, "image"));
    var source = new ImageSource (path);
    try {
        yield first.file.copy_async (path, FileCopyFlags.NONE);
        assert ((yield registry.inspect (source)).depth == 16);
        path.delete ();
        try {
            yield registry.open (source);
            assert_not_reached ();
        } catch (IOError.NOT_FOUND error) {
            // Inspection does not authorize a later open.
        }
        yield second.file.copy_async (path, FileCopyFlags.NONE);
        assert ((yield registry.open (source)).bit_depth == 8);
        path.delete ();
        assert (DirUtils.create (path.get_path (), 0700) == 0);
        try {
            yield registry.open (source);
            assert_not_reached ();
        } catch (IOError.INVALID_ARGUMENT error) {
            // Directory replacements still require regular-file validation.
        }
    } finally {
        FileUtils.remove (path.get_path ());
        DirUtils.remove (directory);
    }
}

async void verify_headless_core_api (string directory, string large_path) throws Error {
    yield verify_failed_stream_ownership ();
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
    yield verify_source_replacement (registry, source, rgb8.source);
    var pixels = yield document.render_region (0, 0, 1, 1, 1);
    assert (pixels.width == 1 && pixels.height == 1 && pixels.bytes.get_size () == 16);
    // Hold the expired entry open while the actual cache maintenance runs.
    string cache_path = Path.build_filename (Environment.get_user_cache_dir (), "majoris-image",
        "thumbnails-v2");
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
    while (expired.query_exists () && get_monotonic_time () < eviction_deadline) {
        yield wait_for_eviction ();
    }
    if (expired.query_exists ()) {
        throw new IOError.TIMED_OUT ("Expired thumbnail was not evicted");
    }
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
    yield verify_jxl_export (registry, multipart, exr.preview);
    var released = yield verify_staged_document (registry, multipart);
    // The completed coroutine releases its local document after resuming its caller.
    Idle.add (() => {
        verify_headless_core_api.callback ();
        return Source.REMOVE;
    });
    yield;
    assert (!released.query_exists ());
    var exr_stream = yield multipart.file.read_async ();
    var staged = yield ImageSource.from_stream (exr_stream);
    assert (!exr_stream.is_closed ());
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
    } catch (IOError.CANCELLED e) {
        // Cancellation is the expected result for this request.
    }
    var corrupt = new ImageSource (File.new_for_path (Path.build_filename (directory, "corrupt.bin")));
    try {
        yield registry.open (corrupt);
        assert_not_reached ();
    } catch (ImageError.UNSUPPORTED e) {
        // The fixture or requested operation may be unsupported.
    }

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
        var encoder = new EncoderRegistry ();
        var original = yield source.file.load_bytes_async (null, null);
        try {
            yield encoder.encode (source, source.file, new ExportOptions ());
            assert_not_reached ();
        } catch (IOError.INVALID_ARGUMENT error) {
            assert (error.message.contains ("separate destination"));
        }
        yield verify_export_alias (encoder, source, temp);
        var transform = new RenderOptions ();
        transform.turns = 1;
        yield encoder.encode (source, target, new ExportOptions (), transform);
        var exported = yield registry.open (new ImageSource (target));
        assert (exported.mime_type == "image/png");
        assert (exported.width == 16 && exported.height == 24 && exported.bit_depth == 16);

        var tiff = new ExportOptions ();
        tiff.format = "tiff";
        tiff.metadata = "remove-all";
        yield encoder.encode (source, target, tiff, transform);
        var replacement = yield registry.open (new ImageSource (target));
        assert (replacement.mime_type == "image/tiff");
        assert (replacement.width == exported.width && replacement.height == exported.height);
        assert (replacement.bit_depth == exported.bit_depth);
        for (int channel = 0; channel < 3; channel++) {
            float expected = sample (document.preview, 7, document.height - 1 - 5, channel);
            assert (Math.fabs (sample (exported.preview, 5, 7, channel) - expected) < 0.001);
            assert (Math.fabs (sample (replacement.preview, 5, 7, channel) - expected) < 0.001);
        }
        assert (original.compare (yield source.file.load_bytes_async (null, null)) == 0);
        yield verify_export_source_change (encoder, original, target);
    } finally {
        FileUtils.unlink (target.get_path ());
        DirUtils.remove (temp);
    }
}

private string install_export_test_launcher () throws Error {
    string? bwrap = Environment.find_program_in_path ("bwrap");
    if (bwrap == null) {
        throw new IOError.NOT_FOUND ("Bubblewrap is required for the export integration test");
    }
    string temporary = Environment.get_tmp_dir ();
    string directory = DirUtils.make_tmp ("export-test-launcher-XXXXXX");
    string launcher = Path.build_filename (directory, "bwrap");
    string source = Shell.quote (Path.build_filename (temporary, "mutable-export-source.png"));
    string executable = Shell.quote (bwrap);
    // The real sandbox and worker finish encoding before the launcher changes the source.
    FileUtils.set_contents (launcher, "#!/bin/sh\n" +
        "if [ -f " + source + " ]; then\n" +
        "    " + executable + " \"$@\" || exit $?\n" +
        "    printf x >> " + source + "\n" +
        "else\n    exec " + executable + " \"$@\"\nfi\n");
    if (FileUtils.chmod (launcher, 0700) != 0) {
        throw new IOError.FAILED ("Cannot make the export test launcher executable");
    }
    Environment.set_variable ("PATH", directory + ":" + Environment.get_variable ("PATH"), true);
    return launcher;
}

private async void verify_export_source_change (EncoderRegistry encoder, Bytes original,
                                                 File existing) throws Error {
    string temporary = Environment.get_tmp_dir ();
    var file = File.new_for_path (Path.build_filename (temporary, "mutable-export-source.png"));
    var missing = existing.get_parent ().get_child ("incomplete.png");
    var previous = yield existing.load_bytes_async (null, null);
    var identity = existing.query_info (FileAttribute.UNIX_INODE, 0);
    try {
        foreach (var destination in new File[] { existing, missing }) {
            yield file.replace_contents_bytes_async (original, null, false, FileCreateFlags.NONE,
                null, null);
            try {
                yield encoder.encode (new ImageSource (file), destination, new ExportOptions ());
                assert_not_reached ();
            } catch (IOError.BUSY error) {
                assert (error.message == "Image source changed during export");
            }
            var modified = yield file.load_bytes_async (null, null);
            assert (modified.get_size () == original.get_size () + 1);
        }
        assert (!missing.query_exists ());
        assert (previous.compare (yield existing.load_bytes_async (null, null)) == 0);
        assert (existing.query_info (FileAttribute.UNIX_INODE, 0).get_attribute_uint64 (
            FileAttribute.UNIX_INODE) == identity.get_attribute_uint64 (FileAttribute.UNIX_INODE));
        var jobs = Dir.open (temporary);
        string? entry;
        while ((entry = jobs.read_name ()) != null) {
            assert (!entry.has_prefix ("majoris-image-") ||
                entry.has_prefix ("majoris-image-source-"));
        }
        var destinations = Dir.open (existing.get_parent ().get_path ());
        while ((entry = destinations.read_name ()) != null) {
            assert (!entry.has_prefix (".majoris-export-"));
        }
    } finally {
        FileUtils.unlink (file.get_path ());
        FileUtils.unlink (missing.get_path ());
    }
}

private async void verify_export_alias (EncoderRegistry encoder, ImageSource source,
                                        string directory) throws Error {
    string alias_path = Path.build_filename (directory, "source-link");
    assert (FileUtils.symlink (source.file.get_path (), alias_path) == 0);
    var alias = File.new_for_path (alias_path);
    string attributes = "unix::device,unix::inode";
    var original = yield source.file.query_info_async (attributes, FileQueryInfoFlags.NONE);
    var before = yield source.file.load_bytes_async (null, null);
    try {
        try {
            yield encoder.encode (source, alias, new ExportOptions ());
            assert_not_reached ();
        } catch (IOError.INVALID_ARGUMENT error) {
            assert (error.message.contains ("separate destination"));
        }
        var current = yield alias.query_info_async (attributes, FileQueryInfoFlags.NONE);
        assert (original.get_attribute_uint32 ("unix::device") ==
            current.get_attribute_uint32 ("unix::device"));
        assert (original.get_attribute_uint64 ("unix::inode") ==
            current.get_attribute_uint64 ("unix::inode"));
        assert (FileUtils.read_link (alias_path) == source.file.get_path ());
        var after = yield source.file.load_bytes_async (null, null);
        assert (Checksum.compute_for_bytes (ChecksumType.SHA256, before) ==
            Checksum.compute_for_bytes (ChecksumType.SHA256, after));
    } finally {
        FileUtils.unlink (alias_path);
    }
}

private async void verify_jxl_export (DecoderRegistry registry, ImageSource source,
                                      PixelBuffer reference) throws Error {
    string directory = DirUtils.make_tmp ("majoris-jxl-export-XXXXXX");
    string path = Path.build_filename (directory, "export.jxl");
    var target = File.new_for_path (path);
    try {
        var options = new ExportOptions ();
        options.format = "jxl";
        options.hdr = true;
        options.bit_depth = 32;
        options.lossless = true;
        yield new EncoderRegistry ().encode (source, target, options);
        var decoded = yield registry.open (new ImageSource (target));
        assert (decoded.width == reference.width && decoded.height == reference.height);
        assert (decoded.has_alpha);
        for (int channel = 0; channel < 4; channel++) {
            float expected = sample (reference, 16, 8, channel);
            float actual = sample (decoded.preview, 16, 8, channel);
            if (!(Math.fabs (actual - expected) < 0.001)) {
                throw new IOError.INVALID_DATA (
                    "JPEG XL channel %d differs from source (%g vs %g)".printf (
                    channel, actual, expected));
            }
        }
    } finally {
        FileUtils.unlink (path);
        DirUtils.remove (directory);
    }
}

private async void wait_for_eviction () {
    Timeout.add (10, () => {
        wait_for_eviction.callback ();
        return Source.REMOVE;
    });
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
    string launcher;
    try {
        launcher = install_export_test_launcher ();
    } catch (Error error) {
        stderr.printf ("%s\n", error.message);
        return 1;
    }
    var loop = new MainLoop ();
    int result = 0;
    verify_headless_core_api.begin (args[1], args[2], (obj, response) => {
        try {
            verify_headless_core_api.end (response);
        } catch (Error e) {
            stderr.printf ("%s\n", e.message);
            result = 1;
        }
        loop.quit ();
    });
    loop.run ();
    FileUtils.unlink (launcher);
    DirUtils.remove (Path.get_dirname (launcher));
    return result;
}
