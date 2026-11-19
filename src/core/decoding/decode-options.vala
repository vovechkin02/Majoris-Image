namespace ImageKit {
    /** Per-request decoder preferences, including RAW preview and region policies. */
    public sealed class DecodeOptions : Object {
        /** Zero-based page index requested or represented by this image. */
        public int page { get; set; }
        /** Decoder capability flags required for this request. */
        public ImageCapabilities required_capabilities { get; set; default = ImageCapabilities.STILL; }
        /** Whether region decoding is acceptable when supported by the backend. */
        public bool allow_regions { get; set; default = true; }
        /** Maximum preview edge in pixels passed to the decoder. */
        public int preview_size { get; set; default = 1600; }
        /** Whether embedded RAW previews are preferred before full development. */
        public bool raw_preview_first { get; set; default = true; }
        /** Whether RAW decoding must stop at an embedded preview. */
        public bool raw_preview_only { get; set; }
        /** Quality/pipeline selection for RAW development, as understood by the decoder. */
        public int raw_quality { get; set; default = 1; }
        /** Whether RAW development uses camera-supplied white balance. */
        public bool camera_white_balance { get; set; default = true; }
        /** Tone-mapping mode selected for decoding or export. */
        public string tone_mode { get; set; default = "automatic"; }
        /** Whether decoding should target an SDR presentation. */
        public bool sdr_presentation { get; set; }
        /** Requested frame count for sequence decoding; internal worker hint. */
        internal int sequence_frames { get; set; }
        /** Internal hint describing HDR state of the source; -1 means unknown. */
        internal int source_hdr { get; set; default = -1; }

        /** Returns a separately mutable options snapshot with identical effective values. */
        public DecodeOptions copy () {
            var copy = new DecodeOptions ();
            copy.page = page;
            copy.required_capabilities = required_capabilities;
            copy.preview_size = preview_size;
            copy.allow_regions = allow_regions;
            copy.raw_preview_first = raw_preview_first;
            copy.raw_preview_only = raw_preview_only;
            copy.raw_quality = raw_quality;
            copy.camera_white_balance = camera_white_balance;
            copy.tone_mode = tone_mode;
            copy.sdr_presentation = sdr_presentation;
            copy.source_hdr = source_hdr;
            copy.sequence_frames = sequence_frames;
            return copy;
        }
    }
}
