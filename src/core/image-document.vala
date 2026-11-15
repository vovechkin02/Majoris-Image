namespace MajorisImage {
    /** An oriented image page. Its source remains available for lazy region reads. */
    public sealed class ImageDocument : Object {
        public ImageSource source { get; private set; }
        public int width { get; private set; }
        public int height { get; private set; }
        public int page { get; private set; }
        public int page_count { get; private set; }
        public int bit_depth { get; private set; }
        /** Source sample precision, or zero when the decoder cannot establish it. */
        public int source_bit_depth { get; private set; }
        public int pixel_buffer_bit_depth { get { return 32; } }
        public bool has_alpha { get; private set; }
        public bool colour_managed { get; private set; }
        public string colour_diagnostic { get; private set; }
        public bool hdr { get; private set; }
        public bool tone_mapped { get; private set; }
        public bool vector { get; private set; }
        public bool supports_regions { get; private set; }
        public bool animated { get; private set; }
        public bool timed_sequence { get; private set; }
        public int frame { get; private set; }
        public int frame_count { get; private set; }
        public int loop_count { get; private set; }
        public int frame_duration_ms { get; private set; }
        public int64 frame_timestamp_us { get; private set; }
        public int64 frame_duration_us { get; private set; }
        public RawState raw_state { get; private set; }
        public ImageCapabilities capabilities { get; private set; }
        public string decoder { get; private set; }
        public string mime_type { get; private set; }
        public string warning { get; private set; }
        public ImageMetadata metadata { get; private set; }
        public PixelBuffer preview { get; private set; }
        internal ImageProbe probe { get; private set; }
        private DecodeOptions options;
        private WorkerClient worker;
        private string identity;

        internal ImageDocument (ImageSource source, ImageProbe probe, PixelBuffer preview,
                                DecodeOptions options, WorkerClient worker, string identity) {
            this.source = source;
            this.probe = probe;
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
            frame_timestamp_us = probe.frame_timestamp_us;
            frame_duration_us = probe.frame_duration_us > 0 ? probe.frame_duration_us : (int64) frame_duration_ms * 1000;
            page = probe.animated ? 0 : options.page;
            frame = probe.animated ? options.page : 0;
            bit_depth = probe.depth;
            source_bit_depth = probe.source_bit_depth;
            has_alpha = probe.has_alpha;
            colour_managed = probe.colour_managed;
            colour_diagnostic = probe.colour_diagnostic;
            hdr = probe.hdr;
            tone_mapped = hdr && options.sdr_presentation;
            vector = probe.vector;
            supports_regions = options.allow_regions && probe.region && source.file.is_native ();
            animated = probe.animated;
            timed_sequence = probe.timed_sequence;
            raw_state = probe.raw ? (options.raw_preview_only ? RawState.RAW_PREVIEW_AVAILABLE :
                                     RawState.RAW_DEVELOPED) : RawState.NOT_RAW;
            decoder = probe.loader;
            capabilities = probe.capabilities;
            mime_type = probe.mime;
            if (options.raw_preview_only && probe.raw) warning = "Showing the camera's embedded RAW preview";
            else if (probe.timed_sequence && !probe.animated) warning = "The installed decoder supplied a still preview of this timed sequence";
            else warning = probe.loader == "Glycin" ? probe.colour_diagnostic : "";
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
            return yield open_index (index, cancel, true);
        }

        private async ImageDocument open_index (int index, Cancellable? cancel, bool animation_frame = false) throws Error {
            var stop = cancel ?? new Cancellable ();
            yield verify_source (stop);
            if (decoder == "Glycin" && !animated && index == 0) return this;
            var requested = options.copy ();
            requested.page = index;
            if (decoder == "libheif-sequence") requested.sequence_frames = frame_count;
            if (animated && mime_type == "image/apng" && (index != frame || (animation_frame && decoder != "Glycin"))) {
                try {
                    return yield GlycinFallback.open (source, requested, stop,
                        new ImageError.CODEC_MISSING ("APNG frame decoding requires Glycin"), 120, probe);
                } catch (ImageError.CODEC_MISSING error) {
                    if (index == frame) return this;
                    throw error;
                }
            }
            if (animated && index == frame) return this;
            return yield DecoderRegistry.open_with (worker, source, requested, stop);
        }

        /** Render presentation pixels from the existing preview when possible. */
        public async PixelBuffer render_preview (bool tone_map = false, Cancellable? cancel = null) throws Error {
            var stop = cancel ?? new Cancellable ();
            stop.set_error_if_cancelled ();
            if (!hdr || tone_mapped == tone_map) return preview;
            if (!tone_map)
                return yield render_region (0, 0, width, height, int.max (preview.width, preview.height), false, stop);
            var requested = options.copy ();
            requested.sdr_presentation = true;
            return yield new WorkerClient ().render_pixels (preview, null, 0, requested, stop);
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
            if (decoder == "libheif-sequence") requested.sequence_frames = frame_count;
            requested.sdr_presentation = tone_map;
            var stop = cancel ?? new Cancellable ();
            yield verify_source (stop);
            if (decoder == "Glycin") {
                requested.preview_size = edge;
                var fallback = yield GlycinFallback.open (source, requested, stop,
                    new ImageError.UNSUPPORTED ("Glycin could not re-decode this region"), 120,
                    animated ? probe : null, transform);
                return fallback.preview;
            }
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
