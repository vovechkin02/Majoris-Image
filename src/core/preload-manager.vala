namespace ImageKit {
    /** Bounded cache for adjacent document previews. Use one instance per navigation session. */
    public sealed class PreloadManager : Object {
        /** One cached or in-flight preload request and its retained cancellation state. */
        private class Entry : Object {
            public string key;
            public ImageDocument document;
            /** Initializes the preload entry with its source and cancellation state. */
            public Entry (string key, ImageDocument document) {
                this.key = key;
                this.document = document;
            }
        }

        private DecoderRegistry registry;

        /** Initializes the bounded cache for upcoming image documents. */
        public PreloadManager () {
            registry = new DecoderRegistry ();
        }

        /** Reuse the same registered backends and timeout as the foreground consumer. */
        public PreloadManager.with_registry (DecoderRegistry registry) {
            this.registry = registry;
        }

        private Gee.ArrayList<Entry> entries = new Gee.ArrayList<Entry> ();
        private Cancellable pending = new Cancellable ();
        private uint generation;
        private size_t bytes;

        /** Cancels pending work and drops cached entries belonging to this owner. */
        public void clear () {
            generation++;
            pending.cancel ();
            pending = new Cancellable ();
            entries.clear ();
            bytes = 0;
        }

        /**
         * Returns an already preloaded document or opens it while respecting cache ownership.
         * Errors propagate to the caller.
         */
        public async ImageDocument open (ImageSource source, DecodeOptions? options = null,
                                         Cancellable? cancel = null) throws Error {
            var request = (options ?? new DecodeOptions ()).copy ();
            var stop = cancel ?? new Cancellable ();
            string? key = null;
            try {
                key = yield key_for (source, request, stop);
            } catch (Error error) {
                stop.set_error_if_cancelled ();
            }
            stop.set_error_if_cancelled ();
            if (key != null) {
                for (int i = 0; i < entries.size; i++) {
                    var entry = entries[i];
                    if (entry.key != key) {
                        continue;
                    }
                    entries.remove_at (i);
                    entries.add (entry);
                    return entry.document;
                }
            }
            return yield registry.open (source, request, stop);
        }

        /** Starts a bounded background open for a likely next image. */
        public async void preload (ImageSource[] neighbors, DecodeOptions? options = null) {
            clear ();
            var stop = pending;
            uint request_id = generation;
            var request = (options ?? new DecodeOptions ()).copy ();
            // Capture the generation before IO. A subsequent preload discards this batch.
            for (int i = 0; i < int.min (neighbors.length, 4); i++) {
                try {
                    var source = neighbors[i];
                    var key = yield key_for (source, request, stop);
                    if (key == null) {
                        continue;
                    }
                    if (entries.any_match (entry => entry.key == key)) {
                        continue;
                    }
                    var document = yield registry.open_internal (source, request, stop, true);
                    var after = yield key_for (source, request, stop);
                    if (request_id != generation) {
                        return;
                    }
                    if (key != after) {
                        continue;
                    }
                    size_t length = document.preview.bytes.get_size ();
                    if (length > 64 * 1024 * 1024 - bytes) {
                        return;
                    }
                    entries.add (new Entry (key, document));
                    bytes += length;
                } catch (IOError.CANCELLED e) {
                    return;
                } catch (Error e) {
                    debug ("Adjacent image unavailable: %s", e.message);
                }
            }
        }

        /** Builds a stable cache key from source identity and decoding parameters. */
        private async string? key_for (ImageSource source, DecodeOptions options,
            Cancellable stop) throws Error {
            int64 started = OperationProfile.begin ();
            FileInfo info = null;
            try {
                info = yield source.file.query_info_async (ThumbnailService.STAMP_ATTRIBUTES, 0,
                    Priority.LOW, stop);
            } finally {
                OperationProfile.record ("source-query", started);
            }
            started = OperationProfile.begin ();
            var stamp = ThumbnailService.source_stamp (source.file, info);
            OperationProfile.record ("source-stamp", started);
            if (stamp == "") {
                return null;
            }
            return stamp + "\n" + Json.gobject_to_data (options, null);
        }
    }
}
