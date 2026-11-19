namespace ImageKit {
    /** An oriented image page. Its source remains available for lazy region reads. */
    public sealed class ImageDocument : Object {
        /** Original source object retained by this document. */
        public ImageSource source { get; private set; }
        /** Image width in source pixels, or requested output pixels when set on export options. */
        public int width { get; private set; }
        /** Image height in source pixels, or requested output pixels when set on export options. */
        public int height { get; private set; }
        /** Zero-based page index requested or represented by this image. */
        public int page { get; private set; }
        /** Number of document pages, distinct from its animation frame count. */
        public int page_count { get; private set; }
        /** Bit depth of the represented buffer or target export, according to its owning type. */
        public int bit_depth { get; private set; }
        /** Source sample precision, or zero when the decoder cannot establish it. */
        public int source_bit_depth { get; private set; }
        /** Precision in bits per channel of the materialized preview buffer. */
        public int pixel_buffer_bit_depth {
            get {
                return 32;
            }
        }
        /** Whether the decoded image exposes an alpha channel. */
        public bool has_alpha { get; private set; }
        /** Whether an identified color profile or equivalent color handling applies. */
        public bool color_managed { get; private set; }
        /** Human-readable diagnostic describing color-profile interpretation or fallback. */
        public string color_diagnostic { get; private set; }
        /** Whether the source or requested output uses HDR image data, per its owning type. */
        public bool hdr { get; private set; }
        /** Whether source HDR values have been converted for SDR presentation. */
        public bool tone_mapped { get; private set; }
        /** Whether the source is vector-based rather than solely raster pixels. */
        public bool vector { get; private set; }
        /** Whether this opened document supports efficient source-region rendering. */
        public bool supports_regions { get; private set; }
        /** Whether the image contains animation frames. */
        public bool animated { get; private set; }
        /** Whether frame timing is represented as an explicit sequence timeline. */
        public bool timed_sequence { get; private set; }
        /** Zero-based frame index represented by this document. */
        public int frame { get; private set; }
        /** Number of animation frames, independent of document pages. */
        public int frame_count { get; private set; }
        /** Animation repeat count supplied by the source; interpret with decoder semantics. */
        public int loop_count { get; private set; }
        /** Duration of the current frame in milliseconds. */
        public int frame_duration_ms { get; private set; }
        /** Presentation timestamp of the frame in microseconds. */
        public int64 frame_timestamp_us { get; private set; }
        /** Duration of the timed-sequence frame in microseconds. */
        public int64 frame_duration_us { get; private set; }
        /** RAW preview/development state observed when this document was opened. */
        public RawState raw_state { get; private set; }
        /** Capabilities supported by the selected backend or reported image. */
        public ImageCapabilities capabilities { get; private set; }
        /** Human-readable name of the decoder that opened the image. */
        public string decoder { get; private set; }
        /** MIME type associated with the opened document. */
        public string mime_type { get; private set; }
        /** Nonfatal decoder or presentation diagnostic shown to the caller. */
        public string warning { get; private set; }
        /** Image metadata object or serialized metadata content, according to the owning type. */
        public ImageMetadata metadata { get; private set; }
        /** Decoded preview pixels retained for immediate display; held by the document. */
        public PixelBuffer preview { get; private set; }
        /** Decoder probe metadata used to construct this document. */
        internal ImageProbe probe { get; private set; }
        private DecodeOptions options;
        private WorkerClient worker;
        private string identity;

        /** Creates a document from a verified probe and preview with retained source identity. */
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
            frame_duration_us = probe.frame_duration_us > 0 ? probe.frame_duration_us
                : (int64) frame_duration_ms * 1000;
            page = probe.animated ? 0 : options.page;
            frame = probe.animated ? options.page : 0;
            bit_depth = probe.depth;
            source_bit_depth = probe.source_bit_depth;
            has_alpha = probe.has_alpha;
            color_managed = probe.color_managed;
            color_diagnostic = probe.color_diagnostic;
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
            if (options.raw_preview_only && probe.raw) {
                warning = "Showing the camera's embedded RAW preview";
            } else if (probe.timed_sequence && !probe.animated) {
                warning = "The installed decoder supplied a still preview of this timed sequence";
            } else {
                warning = DecoderRoute.from_label (probe.loader) == DecoderRoute.FALLBACK
                    ? probe.color_diagnostic : "";
            }
            metadata = new ImageMetadata (probe.metadata);
        }

        /**
         * Opens a zero-based page without mutating the existing document. Errors propagate to
         * the caller.
         */
        public async ImageDocument open_page (int index, Cancellable? cancel = null) throws Error {
            if (index < 0 || index >= page_count) {
                throw new IOError.INVALID_ARGUMENT ("Page is outside the document");
            }
            return yield open_index (index, cancel);
        }

        /**
         * Opens a zero-based animation frame with its corresponding time metadata. Errors
         * propagate to the caller.
         */
        public async ImageDocument open_frame (int index, Cancellable? cancel = null) throws Error {
            if (!animated || index < 0 || index >= frame_count) {
                throw new IOError.INVALID_ARGUMENT ("Frame is outside the animation");
            }
            return yield open_index (index, cancel, true);
        }

        /** Opens a particular page/frame index according to decoder capabilities. */
        private async ImageDocument open_index (int index, Cancellable? cancel,
            bool animation_frame = false) throws Error {
            var stop = cancel ?? new Cancellable ();
            yield verify_source (stop);
            var route = DecoderRoute.from_label (decoder);
            if (route == DecoderRoute.FALLBACK && !animated && index == 0) {
                return this;
            }
            var requested = options.copy ();
            requested.page = index;
            if (route == DecoderRoute.SEQUENCE) {
                requested.sequence_frames = frame_count;
            }
            if (animated && mime_type == "image/apng" &&
                (index != frame || (animation_frame && route != DecoderRoute.FALLBACK))) {
                try {
                    return yield GlycinFallback.open (source, requested, stop,
                        new ImageError.CODEC_MISSING ("APNG frame decoding requires Glycin"), 120, probe);
                } catch (ImageError.CODEC_MISSING error) {
                    if (index == frame) {
                        return this;
                    }
                    throw error;
                }
            }
            if (animated && index == frame) {
                return this;
            }
            // This request already verified the document stamp before choosing the page path.
            return yield DecoderRegistry.open_with (worker, source, requested, stop, false,
                identity == "" ? null : identity);
        }

        /** Render presentation pixels from the existing preview when possible. */
        public async PixelBuffer render_preview (bool tone_map = false,
            Cancellable? cancel = null) throws Error {
            var stop = cancel ?? new Cancellable ();
            stop.set_error_if_cancelled ();
            if (!hdr || tone_mapped == tone_map) {
                return preview;
            }
            if (!tone_map) {
                return yield render_region (0, 0, width, height,
                    int.max (preview.width, preview.height), false, stop);
            }
            var requested = options.copy ();
            requested.sdr_presentation = true;
            return yield new WorkerClient ().render_pixels (preview, null, 0, requested, stop);
        }

        /** Coordinates refer to the oriented page at its original resolution. */
        public async PixelBuffer render_region (int x, int y, int width, int height, int edge,
                                                bool tone_map = false,
                                                    Cancellable? cancel = null) throws Error {
            if (x < 0 || y < 0 || width < 1 || height < 1 || x > this.width - width ||
                y > this.height - height || edge < 1 || edge > 4096) {
                throw new IOError.INVALID_ARGUMENT ("Invalid image region or output size");
            }
            var transform = new RenderOptions ();
            transform.crop_x = (double) x / this.width;
            transform.crop_y = (double) y / this.height;
            transform.crop_width = (double) width / this.width;
            transform.crop_height = (double) height / this.height;
            var requested = options.copy ();
            requested.source_hdr = hdr ? 1 : 0;
            var route = DecoderRoute.from_label (decoder);
            if (route == DecoderRoute.SEQUENCE) {
                requested.sequence_frames = frame_count;
            }
            requested.sdr_presentation = tone_map;
            var stop = cancel ?? new Cancellable ();
            yield verify_source (stop);
            if (route == DecoderRoute.FALLBACK) {
                requested.preview_size = edge;
                var fallback = yield GlycinFallback.open (source, requested, stop,
                    new ImageError.UNSUPPORTED ("Glycin could not re-decode this region"), 120,
                    animated ? probe : null, transform);
                return fallback.preview;
            }
            var output = yield worker.run (source.file, "tile", transform, stop, edge, requested);
            try {
                var pixels = yield WorkerClient.read_pixels (output, stop);
                yield verify_source (stop);
                return pixels;
            } finally {
                WorkerClient.cleanup (output);
            }
        }

        /** Rechecks the source stamp and rejects a source replaced after opening. */
        private async void verify_source (Cancellable stop) throws Error {
            if (identity != "" && identity != (yield source.stamp (stop))) {
                OperationProfile.event ("source-validation-failure");
                throw new ImageError.SOURCE_CHANGED ("Image source changed; reload it before rendering more regions");
            }
        }
    }
}
