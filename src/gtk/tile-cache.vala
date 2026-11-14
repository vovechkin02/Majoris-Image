namespace MajorisImage {
    internal class ImageTile : Object {
        public string key;
        public int x;
        public int y;
        public int width;
        public int height;
        public Gdk.Texture? texture;
        public int64 used;
        public double level;
        public int decode_x;
        public int decode_y;
        public int decode_width;
        public int decode_height;
        public Cancellable? request;
        public bool failed;
    }

    internal class TileCache : Object {
        private Gee.HashMap<string, ImageTile> tiles = new Gee.HashMap<string, ImageTile> ();
        private Gee.ArrayQueue<ImageTile> pending = new Gee.ArrayQueue<ImageTile> ();
        private ImageDocument? document;
        private uint active;
        private uint generation;
        public signal void changed ();

        public void reset (ImageDocument? source) {
            generation++;
            foreach (var tile in tiles.values) tile.request?.cancel ();
            document = source != null && source.supports_regions ? source : null;
            tiles.clear ();
            pending.clear ();
            active = 0;
        }

        public Gee.List<ImageTile> visible (double left, double top, double right, double bottom, double zoom) {
            var visible = new Gee.ArrayList<ImageTile> ();
            if (document == null || zoom <= 0) return visible;
            // Pyramid levels only change at powers of two, preventing zoom-cache churn.
            double level = Math.pow (2, Math.ceil (Math.log (double.min (document.vector ? 8 : 1, zoom)) / Math.log (2)));
            int extent = int.max (1, (int) (512 / level));
            int first_x = (int) (double.max (0, left) / extent) * extent;
            int first_y = (int) (double.max (0, top) / extent) * extent;
            int end_x = int.min (document.width, (int) Math.ceil (right));
            int end_y = int.min (document.height, (int) Math.ceil (bottom));
            pending.clear ();
            for (int y = first_y; y < end_y && visible.size < 48; y += extent) {
                for (int x = first_x; x < end_x && visible.size < 48; x += extent) {
                    var tile = tile_at (x, y, extent, level);
                    visible.add (tile);
                    queue_tile (tile);
                }
            }
            // Keep two adjacent tiles warm, behind every visible request.
            var keep = new Gee.ArrayList<ImageTile> ();
            keep.add_all (visible);
            if (!visible.is_empty && visible.size < 48) {
                int next_x = (end_x + extent - 1) / extent * extent;
                int next_y = (end_y + extent - 1) / extent * extent;
                if (next_x < document.width) {
                    var tile = tile_at (next_x, first_y, extent, level);
                    keep.add (tile);
                    queue_tile (tile);
                }
                if (next_y < document.height) {
                    var tile = tile_at (first_x, next_y, extent, level);
                    keep.add (tile);
                    queue_tile (tile);
                }
            }
            foreach (var tile in tiles.values)
                if (!keep.contains (tile)) tile.request?.cancel ();
            evict (keep);
            pump ();
            return visible;
        }

        private ImageTile tile_at (int x, int y, int extent, double level) {
            string key = "%d:%d:%d".printf (extent, x, y);
            var tile = tiles[key];
            if (tile == null) {
                tile = new ImageTile ();
                tile.key = key;
                tile.x = x;
                tile.y = y;
                tile.width = int.min (extent, document.width - x);
                tile.height = int.min (extent, document.height - y);
                tile.level = level;
                int bleed = int.max (1, (int) Math.ceil (1 / level));
                tile.decode_x = int.max (0, x - bleed);
                tile.decode_y = int.max (0, y - bleed);
                tile.decode_width = int.min (document.width, x + tile.width + bleed) - tile.decode_x;
                tile.decode_height = int.min (document.height, y + tile.height + bleed) - tile.decode_y;
                tiles[key] = tile;
            }
            tile.used = get_monotonic_time ();
            return tile;
        }

        private void queue_tile (ImageTile tile) {
            if (tile.texture == null && tile.request == null && !tile.failed) pending.offer (tile);
        }

        private void evict (Gee.List<ImageTile> keep) {
            while (tiles.size > 64) {
                ImageTile? oldest = null;
                foreach (var tile in tiles.values) {
                    if (keep.contains (tile)) continue;
                    if (oldest == null || tile.used < oldest.used) oldest = tile;
                }
                if (oldest == null) return;
                oldest.request?.cancel ();
                tiles.unset (oldest.key);
            }
        }

        private void pump () {
            while (active < 2 && !pending.is_empty) {
                var tile = pending.poll ();
                if (tiles[tile.key] != tile) continue;
                tile.request = new Cancellable ();
                active++;
                load.begin (document, tile, generation, tile.request);
            }
        }

        private async void load (ImageDocument source, ImageTile tile, uint request_generation, Cancellable stop) {
            try {
                int edge = (int) Math.ceil (int.max (tile.decode_width, tile.decode_height) * tile.level);
                var pixels = yield source.render_region (tile.decode_x, tile.decode_y,
                    tile.decode_width, tile.decode_height, edge, true, stop);
                if (!stop.is_cancelled () && generation == request_generation && tiles[tile.key] == tile)
                    tile.texture = texture_for_pixels (pixels);
            } catch (Error e) {
                if (!stop.is_cancelled ()) {
                    tile.failed = true;
                    debug ("Tile unavailable: %s", e.message);
                }
            } finally {
                tile.request = null;
                if (request_generation == generation) {
                    active--;
                    pump ();
                    changed ();
                }
            }
        }
    }
}
