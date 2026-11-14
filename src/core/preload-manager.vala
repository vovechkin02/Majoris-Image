namespace MajorisImage {
    /** Bounded cache for adjacent document previews. Use one instance per navigation session. */
    public sealed class PreloadManager : Object {
        private class Entry : Object {
            public string key;
            public ImageDocument document;
            public Entry (string key, ImageDocument document) { this.key = key; this.document = document; }
        }

        private Gee.ArrayList<Entry> entries = new Gee.ArrayList<Entry> ();
        private Cancellable pending = new Cancellable ();
        private uint generation;
        private size_t bytes;

        public void clear () {
            generation++;
            pending.cancel ();
            pending = new Cancellable ();
            entries.clear ();
            bytes = 0;
        }

        public async ImageDocument open (ImageSource source, DecodeOptions? options = null,
                                         Cancellable? cancel = null) throws Error {
            var request = (options ?? new DecodeOptions ()).copy ();
            var stop = cancel ?? new Cancellable ();
            var key = yield key_for (source, request, stop);
            if (key != null) {
                for (int i = 0; i < entries.size; i++) {
                    var entry = entries[i];
                    if (entry.key != key) continue;
                    entries.remove_at (i);
                    entries.add (entry);
                    return entry.document;
                }
            }
            return yield new DecoderRegistry ().open (source, request, stop);
        }

        public async void preload (ImageSource[] neighbours, DecodeOptions? options = null) {
            clear ();
            var stop = pending;
            uint request_id = generation;
            var request = (options ?? new DecodeOptions ()).copy ();
            // Capture the generation before IO. A subsequent preload discards this batch.
            for (int i = 0; i < int.min (neighbours.length, 4); i++) {
                try {
                    var source = neighbours[i];
                    var key = yield key_for (source, request, stop);
                    if (key == null) continue;
                    var document = yield DecoderRegistry.open_with (new WorkerClient (), source, request, stop, true);
                    var after = yield key_for (source, request, stop);
                    if (request_id != generation) return;
                    if (key != after) continue;
                    size_t length = document.preview.bytes.get_size ();
                    if (bytes + length > 64 * 1024 * 1024) return;
                    entries.add (new Entry (key, document));
                    bytes += length;
                } catch (IOError.CANCELLED e) { return; }
                catch (Error e) { debug ("Adjacent image unavailable: %s", e.message); }
            }
        }

        private async string? key_for (ImageSource source, DecodeOptions options, Cancellable stop) throws Error {
            var info = yield source.file.query_info_async (ThumbnailService.STAMP_ATTRIBUTES, 0, Priority.LOW, stop);
            string stamp = ThumbnailService.source_stamp (source.file, info);
            if (stamp == "") return null;
            return stamp + "\n" + Json.gobject_to_data (options, null);
        }
    }
}
