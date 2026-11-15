using ImageKit;

// Link the test copy of the cache to the same texture constructor as the widget.
namespace ImageKit {
    [CCode (cname = "image_texture_new", cheader_filename = "image-texture.h")]
    private extern static Gdk.Texture test_texture (Bytes bytes, int width, int height, size_t stride);

    internal static Gdk.Texture texture_for_pixels (PixelBuffer pixels) {
        return test_texture (pixels.bytes, pixels.width, pixels.height, pixels.stride);
    }
}

private void assert_plan (TileBounds bounds, int width, int height, double zoom,
                          bool vector, int expected = 0) {
    var plan = TileCoverage.plan (bounds, width, height, zoom, vector);
    assert (plan.count > 0 && plan.count <= TileCoverage.MAX_SLOTS);
    if (expected > 0) assert (plan.count == expected && plan.level == 1);

    int clipped_left = (int) Math.floor (bounds.left.clamp (0, width));
    int clipped_top = (int) Math.floor (bounds.top.clamp (0, height));
    int clipped_right = (int) Math.ceil (bounds.right.clamp (0, width));
    int clipped_bottom = (int) Math.ceil (bounds.bottom.clamp (0, height));
    assert (plan.first_x <= clipped_left && plan.first_y <= clipped_top);
    assert (plan.end_x == clipped_right && plan.end_y == clipped_bottom);
    int count = 0;
    int64 covered = 0, bytes = 0;
    for (int64 y = plan.first_y; y < plan.end_y; y += plan.extent) {
        for (int64 x = plan.first_x; x < plan.end_x; x += plan.extent) {
            int64 right = int64.min (width, x + plan.extent);
            int64 bottom = int64.min (height, y + plan.extent);
            int64 read_w = int64.min (width, right + plan.bleed) - int64.max (0, x - plan.bleed);
            int64 read_h = int64.min (height, bottom + plan.bleed) - int64.max (0, y - plan.bleed);
            int64 edge = (int64) Math.ceil (int64.max (read_w, read_h) * plan.level);
            assert (edge >= 1 && edge <= 4096);
            bytes += edge * edge * 16;
            covered += (int64.min (clipped_right, right) - int64.max (clipped_left, x)) *
                       (int64.min (clipped_bottom, bottom) - int64.max (clipped_top, y));
            count++;
        }
    }
    assert (count == plan.count && bytes <= TileCoverage.MAX_BYTES);
    assert (covered == (int64) (clipped_right - clipped_left) * (clipped_bottom - clipped_top));
}

private void test_geometry () {
    assert_plan ({ 0, 0, 1920, 1080 }, 20000, 10000, 1, false, 12);
    assert_plan ({ 0, 0, 3840, 2160 }, 20000, 10000, 1, false, 40);
    assert_plan ({ 0, 0, 5120, 2880 }, 20000, 10000, 1, false, 60);
    assert_plan ({ 0, 0, 7680, 4320 }, 20000, 10000, 1, false, 135);
    assert_plan ({ 0, 0, 7680, 2160 }, 20000, 10000, 1, false, 75);
    assert_plan ({ 511, 511, 8191, 4831 }, 20000, 10000, 1, false, 160);
    assert_plan ({ 111.2, 42.4, 11926.6, 6688.55 }, 20000, 10000, 0.65, false);
    assert_plan ({ -50, -80, 12000, 8000 }, 11000, 7800, 2.5, true);
    assert_plan ({ 0, 0, int.MAX, int.MAX }, int.MAX, int.MAX, 8, true);
    assert (TileCoverage.plan ({ 0, 0, 1, 1 }, 10, 10, double.NAN, false).count == 0);
    assert (TileCoverage.plan ({ -20, -20, -1, -1 }, 10, 10, 1, false).count == 0);

    var rotated = TileBounds.from_view (100, 80, 30, 20, -100, -80, 0.5, 1, true, false);
    assert (rotated.left == 80 && rotated.right == 240);
    assert (rotated.top == 20 && rotated.bottom == 220);
    assert_plan (rotated, 400, 320, 1, false);
}

private async void delay () {
    Timeout.add (10, () => { delay.callback (); return Source.REMOVE; });
    yield;
}

private async void test_cache (string path, string exr_path, bool complete) throws Error {
    var document = yield new DecoderRegistry ().open (new ImageSource (File.new_for_path (path)));
    var cache = new TileCache ();
    cache.reset (document);
    var first = cache.visible ({0, 0, 1536, 1536}, 1);
    assert (first.size == 9 && first[0].x == 512 && first[0].y == 512);
    var canceled = first[0].request;
    assert (canceled != null && first[1].request != null && first[2].request == null);

    // Reset and immediately submit new geometry. Old worker callbacks still
    // hold their slots. They must neither publish a texture nor lose work.
    cache.reset (null);
    cache.reset (document, true);
    TileBounds revised = { 8192, 512, 9216, 1536 };
    var visible = cache.visible (revised, 1);
    assert (canceled.is_cancelled () && visible[0].request == null);
    for (int attempt = 0; attempt < 500 && !visible.all_match (tile => tile.texture != null); attempt++)
        yield delay ();
    assert (visible.all_match (tile => tile.texture != null));
    assert (first.all_match (tile => tile.texture == null));

    var retained = visible[0].texture;
    for (int i = 0; i < 20; i++) {
        var lower = cache.visible ({8190 - i, 480, 9344, 1920}, 0.45);
        assert (!lower.is_empty);
        var restored = cache.visible (revised, 1);
        assert (restored[0].texture == retained);
    }
    cache.reset (null);
    var hdr = yield new DecoderRegistry ().open (new ImageSource (File.new_for_path (exr_path)));
    assert (hdr.hdr && hdr.has_alpha && hdr.supports_regions);
    cache.reset (hdr, true);
    var hdr_tiles = cache.visible ({0, 0, hdr.width, hdr.height}, 1);
    assert (hdr_tiles.size == 1);
    for (int attempt = 0; attempt < 500 && hdr_tiles[0].texture == null; attempt++)
        yield delay ();
    assert (hdr_tiles[0].texture != null);
    var preview = yield hdr.render_preview (true);
    var base_texture = texture_for_pixels (preview);
    size_t stride = (size_t) hdr.width * 4;
    uint8[] base_pixels = new uint8[stride * hdr.height];
    uint8[] tile_pixels = new uint8[base_pixels.length];
    base_texture.download (base_pixels, stride);
    hdr_tiles[0].texture.download (tile_pixels, stride);
    for (int i = 0; i < base_pixels.length; i++) assert (base_pixels[i] == tile_pixels[i]);
    cache.reset (null);
    if (complete) {
        cache.reset (document);
        var eight_k = cache.visible ({511, 511, 8191, 4831}, 1);
        assert (eight_k.size == 160 && eight_k.all_match (tile => tile.texture == null));
        int64 deadline = get_monotonic_time () + 120 * TimeSpan.SECOND;
        while (get_monotonic_time () < deadline && !eight_k.all_match (tile => tile.texture != null))
            yield delay ();
        assert (eight_k.all_match (tile => tile.texture != null));
        var same = cache.visible ({511, 511, 8191, 4831}, 1);
        assert (same.size == 160 && same[159].texture == eight_k[159].texture);
        stdout.printf ("All 160 shifted 8K tiles rendered and retained\n");
        cache.reset (null);
    }
    // Drain the two canceled jobs before leaving the test main loop.
    for (int i = 0; i < 30; i++) yield delay ();
}

int main (string[] args) {
    test_geometry ();
    var loop = new MainLoop ();
    int status = 0;
    test_cache.begin (args[1], args[2], args.length > 3 && args[3] == "--complete", (object, response) => {
        try { test_cache.end (response); }
        catch (Error error) { stderr.printf ("%s\n", error.message); status = 1; }
        loop.quit ();
    });
    loop.run ();
    return status;
}
