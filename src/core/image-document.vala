namespace MajorisImage {
    /** An oriented image page. Its source remains available for lazy region reads. */
    public sealed class ImageDocument : Object {
        public ImageSource source { get; private set; }
        public int width { get; private set; }
        public int height { get; private set; }
        public int page { get; private set; }
        public int page_count { get; private set; }
        public int bit_depth { get; private set; }
        public bool hdr { get; private set; }
        public bool tone_mapped { get; private set; }
        public bool vector { get; private set; }
        public bool supports_regions { get; private set; }
        public bool animated { get; private set; }
        public int frame { get; private set; }
        public int frame_count { get; private set; }
        public int loop_count { get; private set; }
        public int frame_duration_ms { get; private set; }
        public RawState raw_state { get; private set; }
        public ImageCapabilities capabilities { get; private set; }
        public string decoder { get; private set; }
        public string mime_type { get; private set; }
        public string warning { get; private set; }
        public ImageMetadata metadata { get; private set; }
        public PixelBuffer preview { get; private set; }
        private DecodeOptions options;
        private WorkerClient worker;
        private string identity;

        internal ImageDocument (ImageSource source, ImageProbe probe, PixelBuffer preview,
                                DecodeOptions options, WorkerClient worker, string identity) {
            this.source = source;
            this.preview = preview;
            this.options = options;
            this.worker = worker;
            this.identity = identity;
            width = probe.width;
            height = probe.height;
            page_count = probe.animated ? 1 : probe.pages;
            frame_count = probe.animated ? probe.pages : 1;
            loop_count = probe.loop_count;
            frame_duration_ms = probe.frame_duration_ms;
            page = probe.animated ? 0 : options.page;
            frame = probe.animated ? options.page : 0;
            bit_depth = probe.depth;
            hdr = probe.hdr;
            tone_mapped = hdr && options.sdr_presentation;
            vector = probe.vector;
            supports_regions = probe.region && source.file.is_native ();
            animated = probe.animated;
            raw_state = probe.raw ? (options.raw_preview_only ? RawState.RAW_PREVIEW_AVAILABLE :
                                     RawState.RAW_DEVELOPED) : RawState.NOT_RAW;
            decoder = probe.loader;
            capabilities = probe.capabilities;
            mime_type = probe.mime;
            warning = options.raw_preview_only && probe.raw ? "Showing the camera's embedded RAW preview" : "";
            metadata = new ImageMetadata (probe.metadata);
        }

        public async ImageDocument open_page (int index, Cancellable? cancel = null) throws Error {
            if (index < 0 || index >= page_count)
                throw new IOError.INVALID_ARGUMENT ("Page is outside the document");
            return yield open_index (index, cancel);
        }

        public async ImageDocument open_frame (int index, Cancellable? cancel = null) throws Error {
            if (!animated || index < 0 || index >= frame_count)
                throw new IOError.INVALID_ARGUMENT ("Frame is outside the animation");
            return yield open_index (index, cancel);
        }

        private async ImageDocument open_index (int index, Cancellable? cancel) throws Error {
            var requested = options.copy ();
            requested.page = index;
            return yield DecoderRegistry.open_with (worker, source, requested, cancel ?? new Cancellable ());
        }

        /** Coordinates refer to the oriented page at its original resolution. */
        public async PixelBuffer render_region (int x, int y, int width, int height, int edge,
                                                bool tone_map = false, Cancellable? cancel = null) throws Error {
            if (x < 0 || y < 0 || width < 1 || height < 1 || x > this.width - width ||
                y > this.height - height || edge < 1 || edge > 4096)
                throw new IOError.INVALID_ARGUMENT ("Invalid image region or output size");
            var transform = new RenderOptions ();
            transform.crop_x = (double) x / this.width;
            transform.crop_y = (double) y / this.height;
            transform.crop_width = (double) width / this.width;
            transform.crop_height = (double) height / this.height;
            var requested = options.copy ();
            requested.source_hdr = hdr ? 1 : 0;
            requested.sdr_presentation = tone_map;
            var stop = cancel ?? new Cancellable ();
            yield verify_source (stop);
            string output = yield worker.run (source.file, "tile", transform, stop, edge, requested);
            try {
                var pixels = yield WorkerClient.read_pixels (output, stop);
                yield verify_source (stop);
                return pixels;
            }
            finally { WorkerClient.cleanup (output); }
        }
        private async void verify_source (Cancellable stop) throws Error {
            if (identity != "" && identity != (yield source.stamp (stop)))
                throw new ImageError.SOURCE_CHANGED ("Image source changed; reload it before rendering more regions");
        }
    }
}
