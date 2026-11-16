namespace ImageKit {
    public sealed class DecodeOptions : Object {
        public int page { get; set; }
        public ImageCapabilities required_capabilities { get; set; default = ImageCapabilities.STILL; }
        public bool allow_regions { get; set; default = true; }
        public int preview_size { get; set; default = 1600; }
        public bool raw_preview_first { get; set; default = true; }
        public bool raw_preview_only { get; set; }
        public int raw_quality { get; set; default = 1; }
        public bool camera_white_balance { get; set; default = true; }
        public string tone_mode { get; set; default = "automatic"; }
        public bool sdr_presentation { get; set; }
        internal int sequence_frames { get; set; }
        internal int source_hdr { get; set; default = -1; }

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
