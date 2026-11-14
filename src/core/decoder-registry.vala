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
            try {
                string catalogue = yield WorkerClient.read_text (output, stop);
                var parser = new Json.Parser ();
                parser.load_from_data (catalogue);
                var root = parser.get_root ();
                if (root == null || root.get_node_type () != Json.NodeType.ARRAY)
                    throw new ImageError.INVALID_RESPONSE ("Invalid codec catalogue");
                var entries = root.get_array ();
                var mimes = GlycinFallback.installed ();
                if (mimes.length == 0) return catalogue;
                var backend = new Json.Object ();
                backend.set_string_member ("operation", "glycin");
                backend.set_boolean_member ("encoder", false);
                backend.set_string_member ("capabilities", "Still, thumbnail, dimensions, alpha and decoded sample depth; no source ICC or HDR guarantees");
                var supported = new Json.Array ();
                foreach (var mime in mimes) supported.add_string_element (mime);
                backend.set_array_member ("mime-types", supported);
                entries.add_object_element (backend);
                var generator = new Json.Generator ();
                generator.set_root (parser.get_root ());
                return generator.to_data (null);
            }
            finally { WorkerClient.cleanup (output); }
        }

        public async ImageProbe inspect (ImageSource source, Cancellable? cancel = null) throws Error {
            var stop = cancel ?? new Cancellable ();
            var worker = yield select (source, ImageCapabilities.METADATA, stop);
            try {
                string output = yield worker.run (source.file, "probe", null, stop);
                try { return ImageProbe.deserialize (yield WorkerClient.read_text (output, stop)); }
                finally { WorkerClient.cleanup (output); }
            } catch (Error error) {
                if (!GlycinFallback.eligible (error)) throw error;
                var options = new DecodeOptions ();
                options.preview_size = 1;
                return (yield GlycinFallback.open (source, options, stop, error, timeout_seconds)).probe;
            }
        }

        public async ImageDocument open (ImageSource source, DecodeOptions? options = null,
                                         Cancellable? cancel = null) throws Error {
            var request = (options ?? new DecodeOptions ()).copy ();
            var stop = cancel ?? new Cancellable ();
            var worker = yield select (source, request.required_capabilities, stop);
            if (request.preview_size < 1 || request.preview_size > 4096)
                throw new IOError.INVALID_ARGUMENT ("Preview edge must be between 1 and 4096");
            try { return yield open_with (worker, source, request, stop); }
            catch (Error error) {
                if (!GlycinFallback.eligible (error)) throw error;
                ImageProbe? animation = null;
                if (request.page > 0) {
                    try {
                        var header = yield inspect (source, stop);
                        if (header.animated && header.mime == "image/apng") {
                            if (request.page >= header.pages)
                                throw new IOError.INVALID_ARGUMENT ("Frame is outside the animation");
                            animation = header;
                        }
                    } catch (Error probe_error) {
                        if (probe_error is IOError.CANCELLED || probe_error is IOError.INVALID_ARGUMENT)
                            throw probe_error;
                    }
                }
                return yield GlycinFallback.open (source, request, stop, error, timeout_seconds, animation);
            }
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
