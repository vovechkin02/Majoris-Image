namespace MajorisImage {
    public sealed class DecoderRegistry : Object {
        public uint timeout_seconds { get; set; default = 120; }
        private Gee.ArrayList<DecoderBackend> custom_workers = new Gee.ArrayList<DecoderBackend> ();

        /** Register a trusted executable. Its parsers still run inside Bubblewrap. */
        public void register_backend (DecoderBackend backend) throws IOError {
            if (!Path.is_absolute (backend.executable) || backend.id == "")
                throw new IOError.INVALID_ARGUMENT ("Decoder workers require an ID and an absolute executable path");
            lock (custom_workers) {
                foreach (var existing in custom_workers)
                    if (existing.id == backend.id) throw new IOError.EXISTS ("Decoder ID already registered");
                custom_workers.add (backend);
            }
        }

        private async WorkerClient select (ImageSource source, ImageCapabilities required, Cancellable stop) throws Error {
            DecoderBackend[] snapshot;
            lock (custom_workers) { snapshot = custom_workers.to_array (); }
            if (snapshot.length > 0) {
                yield source.stamp (stop);
                var stream = yield source.file.read_async (Priority.DEFAULT, stop);
                var bytes = yield stream.read_bytes_async (65536, Priority.DEFAULT, stop);
                yield stream.close_async (Priority.DEFAULT, stop);
                bool uncertain;
                string content_type = ContentType.guess (null, bytes.get_data (), out uncertain);
                string mime = ContentType.get_mime_type (content_type);
                foreach (var backend in snapshot) {
                    if ((backend.capabilities & required) == required && backend.accepts (mime, bytes))
                        return new WorkerClient (backend.executable, timeout_seconds);
                }
            }
            return new WorkerClient (null, timeout_seconds);
        }

        /** Runtime catalogue produced using the same codec stack as decoding. */
        public async string installed_codecs (Cancellable? cancel = null) throws Error {
            var stop = cancel ?? new Cancellable ();
            string output = yield new WorkerClient ().run (null, "codecs", null, stop);
            try { return yield WorkerClient.read_text (output, stop); }
            finally { WorkerClient.cleanup (output); }
        }

        public async ImageProbe inspect (ImageSource source, Cancellable? cancel = null) throws Error {
            var stop = cancel ?? new Cancellable ();
            var worker = yield select (source, ImageCapabilities.METADATA, stop);
            string output = yield worker.run (source.file, "probe", null, stop);
            try { return ImageProbe.deserialize (yield WorkerClient.read_text (output, stop)); }
            finally { WorkerClient.cleanup (output); }
        }

        public async ImageDocument open (ImageSource source, DecodeOptions? options = null,
                                         Cancellable? cancel = null) throws Error {
            var request = (options ?? new DecodeOptions ()).copy ();
            var stop = cancel ?? new Cancellable ();
            var worker = yield select (source, request.required_capabilities, stop);
            return yield open_with (worker, source, request, stop);
        }

        internal static async ImageDocument open_with (WorkerClient worker, ImageSource source,
                                                        DecodeOptions request, Cancellable stop, bool preload = false) throws Error {
            if (request.preview_size < 1 || request.preview_size > 4096)
                throw new IOError.INVALID_ARGUMENT ("Preview edge must be between 1 and 4096");
            string identity = yield source.stamp (stop);
            string output = yield worker.run (source.file, "decode", null, stop, request.preview_size, request, null, preload);
            try {
                string manifest = Path.build_filename (Path.get_dirname (output), "image.json");
                var probe = ImageProbe.deserialize (yield WorkerClient.read_text (manifest, stop));
                if ((probe.capabilities & request.required_capabilities) != request.required_capabilities)
                    throw new ImageError.UNSUPPORTED ("Image does not meet the requested decoder capabilities");
                var preview = yield WorkerClient.read_pixels (output, stop);
                if (preview.width > request.preview_size || preview.height > request.preview_size)
                    throw new ImageError.INVALID_RESPONSE ("Decoder exceeded the requested image size");
                if (identity != "" && identity != (yield source.stamp (stop)))
                    throw new ImageError.SOURCE_CHANGED ("Image source changed during decoding");
                return new ImageDocument (source, probe, preview, request, worker, identity);
            } finally { WorkerClient.cleanup (output); }
        }

        /** Render a transformed source without modifying it. */
        public async PixelBuffer render (ImageSource source, RenderOptions transform, int edge,
                                         DecodeOptions? options = null, Cancellable? cancel = null) throws Error {
            if (edge < 1 || edge > 4096) throw new IOError.INVALID_ARGUMENT ("Invalid output edge");
            var request = (options ?? new DecodeOptions ()).copy ();
            var stop = cancel ?? new Cancellable ();
            var worker = yield select (source, request.required_capabilities, stop);
            string output = yield worker.run (source.file, "decode", transform, stop, edge, request);
            try { return yield WorkerClient.read_pixels (output, stop); }
            finally { WorkerClient.cleanup (output); }
        }
    }
}
