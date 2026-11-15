namespace MajorisImage {
    internal struct TileBounds {
        public double left;
        public double top;
        public double right;
        public double bottom;

        // Invert the same quarter-turn and mirror transform used by the snapshot.
        public static TileBounds from_view (int width, int height, double center_x, double center_y,
                                            double offset_x, double offset_y, double scale,
                                            int turns, bool flipped_x, bool flipped_y) {
            TileBounds result = { double.MAX, double.MAX, -double.MAX, -double.MAX };
            for (int i = 0; i < 4; i++) {
                double x = (i % 2 == 0 ? 0 : width) - center_x;
                double y = (i < 2 ? 0 : height) - center_y;
                for (int turn = 0; turn < turns; turn++) {
                    double old_x = x;
                    x = y;
                    y = -old_x;
                }
                x = ((flipped_x ? -x : x) - offset_x) / scale;
                y = ((flipped_y ? -y : y) - offset_y) / scale;
                result.left = double.min (result.left, x);
                result.top = double.min (result.top, y);
                result.right = double.max (result.right, x);
                result.bottom = double.max (result.bottom, y);
            }
            return result;
        }
    }

    internal struct TileCoverage {
        public const int64 MAX_BYTES = 704 * 1024 * 1024;
        public const int MAX_SLOTS = 256;
        public int first_x;
        public int first_y;
        public int end_x;
        public int end_y;
        public int count;
        public int extent;
        public int bleed;
        public double level;
        public double center_x;
        public double center_y;

        public static TileCoverage plan (TileBounds bounds, int width, int height, double zoom, bool vector) {
            TileCoverage coverage = {};
            if (width <= 0 || height <= 0 || !zoom.is_finite () || zoom <= 0 ||
                !bounds.left.is_finite () || !bounds.top.is_finite () ||
                !bounds.right.is_finite () || !bounds.bottom.is_finite ()) return coverage;

            double left = bounds.left.clamp (0, width);
            double top = bounds.top.clamp (0, height);
            double right = bounds.right.clamp (0, width);
            double bottom = bounds.bottom.clamp (0, height);
            if (left >= right || top >= bottom) return coverage;
            coverage.end_x = (int) Math.ceil (right);
            coverage.end_y = (int) Math.ceil (bottom);
            coverage.center_x = (left + right) / 2;
            coverage.center_y = (top + bottom) / 2;

            int dimension = int.max (width, height);
            double requested = zoom.clamp (1.0 / dimension, vector ? 8 : 1);
            // Retain power-of-two levels for ordinary views. If the padded grid
            // would exceed the budget, first spend samples at the display scale.
            double level = Math.pow (2, Math.ceil (Math.log2 (requested)));
            while (true) {
                int extent = (int) double.min (dimension, Math.floor (512 / level));
                int bleed = (int) double.min (dimension, Math.ceil (1 / level));
                int first_x = (int) left / extent * extent;
                int first_y = (int) top / extent * extent;
                int64 columns = ((int64) coverage.end_x - first_x + extent - 1) / extent;
                int64 rows = ((int64) coverage.end_y - first_y + extent - 1) / extent;
                int64 edge = (int64) Math.ceil (int64.min (dimension, (int64) extent + 2L * bleed) * level);
                // Include worst-case alignment so a pan does not switch
                // sampling levels merely because it crosses a grid edge.
                int64 max_columns = (int64) Math.ceil ((right - left) / extent) + 1;
                int64 max_rows = (int64) Math.ceil ((bottom - top) / extent) + 1;
                int64 slots = max_columns * max_rows;
                if (slots <= MAX_SLOTS && slots * edge * edge * 16 <= MAX_BYTES) {
                    coverage.count = (int) (columns * rows);
                    coverage.extent = extent;
                    coverage.bleed = bleed;
                    coverage.level = level;
                    coverage.first_x = first_x;
                    coverage.first_y = first_y;
                    return coverage;
                }
                level = level > requested ? requested : level / 2;
            }
        }
    }

    internal class ImageTile : Object {
        public string key;
        public int x;
        public int y;
        public int width;
        public int height;
        public Gdk.Texture? texture;
        public int64 used;
        public int edge;
        public int64 charge;
        public int decode_x;
        public int decode_y;
        public int decode_width;
        public int decode_height;
        public Cancellable? request;
        public bool failed;
    }

    internal class TileCache : Object {
        private const int64 WARM_BYTES = 256 * 1024 * 1024;
        private Gee.HashMap<string, ImageTile> tiles = new Gee.HashMap<string, ImageTile> ();
        private Gee.ArrayQueue<ImageTile> pending = new Gee.ArrayQueue<ImageTile> ();
        private ImageDocument? document;
        private bool tone_map;
        private uint active;
        private uint generation;
        private int64 reserved_bytes;
        public signal void changed ();

        public void reset (ImageDocument? source, bool tone_map = false) {
            if (source != null && !source.supports_regions) source = null;
            if (document == source && this.tone_map == tone_map) return;
            generation++;
            this.tone_map = tone_map;
            foreach (var tile in tiles.values) tile.request?.cancel ();
            document = source;
            tiles.clear ();
            pending.clear ();
            reserved_bytes = 0;
            // Cancelled jobs still occupy slots until their completion callbacks.
        }

        public Gee.List<ImageTile> visible (TileBounds bounds, double zoom) {
            var visible = new Gee.ArrayList<ImageTile> ();
            if (document == null) return visible;
            var plan = TileCoverage.plan (bounds, document.width, document.height, zoom, document.vector);
            pending.clear ();
            var keep = new Gee.HashSet<ImageTile> ();
            int64 visible_bytes = 0;
            if (plan.count > 0) {
                for (int64 y = plan.first_y; y < plan.end_y; y += plan.extent) {
                    for (int64 x = plan.first_x; x < plan.end_x; x += plan.extent) {
                        var tile = tile_at ((int) x, (int) y, plan);
                        visible.add (tile);
                        keep.add (tile);
                        visible_bytes += tile.charge;
                    }
                }
                visible.sort ((a, b) => {
                    double first = distance (a, plan), second = distance (b, plan);
                    return first < second ? -1 : first > second ? 1 : 0;
                });
                foreach (var tile in visible) queue_tile (tile);

                // Prefetch neighbours nearest the visible centre after the visible work.
                var center = visible[0];
                bool visible_waiting = !pending.is_empty;
                int64 next_x = ((int64) plan.end_x + plan.extent - 1) / plan.extent * plan.extent;
                int64 next_y = ((int64) plan.end_y + plan.extent - 1) / plan.extent * plan.extent;
                if (next_x < document.width)
                    visible_bytes = prefetch ((int) next_x, center.y, plan, keep, visible_bytes, visible_waiting);
                if (next_y < document.height)
                    visible_bytes = prefetch (center.x, (int) next_y, plan, keep, visible_bytes, visible_waiting);
            }
            evict (keep, int64.max (WARM_BYTES, visible_bytes));
            pump ();
            return visible;
        }

        private static double distance (ImageTile tile, TileCoverage coverage) {
            double dx = tile.x + tile.width / 2.0 - coverage.center_x;
            double dy = tile.y + tile.height / 2.0 - coverage.center_y;
            return dx * dx + dy * dy;
        }

        private int64 prefetch (int x, int y, TileCoverage plan, Gee.HashSet<ImageTile> keep, int64 used, bool visible_waiting) {
            if (keep.size >= TileCoverage.MAX_SLOTS) return used;
            var tile = tile_at (x, y, plan);
            if (used + tile.charge > TileCoverage.MAX_BYTES) return used;
            if (visible_waiting) tile.request?.cancel ();
            keep.add (tile);
            queue_tile (tile);
            return used + tile.charge;
        }

        private ImageTile tile_at (int x, int y, TileCoverage plan) {
            string key = "%d:%d:%d:%s".printf (plan.extent, x, y, plan.level.to_string ());
            var tile = tiles[key];
            if (tile == null) {
                tile = new ImageTile ();
                tile.key = key;
                tile.x = x;
                tile.y = y;
                tile.width = int.min (plan.extent, document.width - x);
                tile.height = int.min (plan.extent, document.height - y);
                tile.decode_x = int.max (0, x - plan.bleed);
                tile.decode_y = int.max (0, y - plan.bleed);
                tile.decode_width = (int) int64.min (document.width, (int64) x + tile.width + plan.bleed) - tile.decode_x;
                tile.decode_height = (int) int64.min (document.height, (int64) y + tile.height + plan.bleed) - tile.decode_y;
                tile.edge = (int) Math.ceil (int.max (tile.decode_width, tile.decode_height) * plan.level);
                tile.charge = (int64) tile.edge * tile.edge * 16;
                reserved_bytes += tile.charge;
                tiles[key] = tile;
            }
            tile.used = get_monotonic_time ();
            return tile;
        }

        private void queue_tile (ImageTile tile) {
            if (tile.texture == null && tile.request == null && !tile.failed) pending.offer (tile);
        }

        private void evict (Gee.HashSet<ImageTile> keep, int64 budget) {
            var unused = new Gee.ArrayList<ImageTile> ();
            foreach (var tile in tiles.values) {
                if (!keep.contains (tile)) {
                    tile.request?.cancel ();
                    unused.add (tile);
                }
            }
            if (reserved_bytes <= budget && tiles.size <= TileCoverage.MAX_SLOTS) return;
            unused.sort ((a, b) => a.used < b.used ? -1 : a.used > b.used ? 1 : 0);
            foreach (var tile in unused) {
                if (reserved_bytes <= budget && tiles.size <= TileCoverage.MAX_SLOTS) break;
                reserved_bytes -= tile.charge;
                tiles.unset (tile.key);
            }
        }

        private void pump () {
            while (active < 2 && !pending.is_empty) {
                var tile = pending.poll ();
                if (tiles[tile.key] != tile) continue;
                tile.request = new Cancellable ();
                active++;
                load.begin (document, tile, generation, tone_map, tile.request);
            }
        }

        private async void load (ImageDocument source, ImageTile tile, uint request_generation, bool tone_map, Cancellable stop) {
            try {
                var pixels = yield source.render_region (tile.decode_x, tile.decode_y,
                    tile.decode_width, tile.decode_height, tile.edge, tone_map, stop);
                if (pixels.width > tile.edge || pixels.height > tile.edge ||
                    pixels.bytes.get_size () > tile.charge)
                    throw new ImageError.INVALID_RESPONSE ("Region decoder exceeded the tile allocation");
                if (!stop.is_cancelled () && generation == request_generation && tiles[tile.key] == tile)
                    tile.texture = texture_for_pixels (pixels);
            } catch (Error e) {
                if (!stop.is_cancelled ()) {
                    tile.failed = true;
                    debug ("Tile unavailable: %s", e.message);
                }
            } finally {
                tile.request = null;
                active--;
                pump ();
                if (request_generation == generation) changed ();
            }
        }
    }
}
