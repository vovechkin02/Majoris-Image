namespace ImageKit {
    /** Progress and outcome states for developing camera RAW source data. */
    public enum RawState {
        /** Source is not a camera RAW image. */
        NOT_RAW,
        /** An embedded RAW preview can be displayed before full development. */
        RAW_PREVIEW_AVAILABLE,
        /** Full RAW development is currently in progress. */
        RAW_DEVELOPING,
        /** Full-resolution RAW development completed successfully. */
        RAW_DEVELOPED,
        /** The available codecs cannot develop this RAW source. */
        RAW_UNSUPPORTED,
        /** The attempted RAW development failed. */
        RAW_DEVELOPMENT_FAILED,
    }

    /** Validated decoder-manifest information about image geometry and capabilities. */
    public sealed class ImageProbe : Object {
        private const string WIRE_MANAGED = "colour-managed";
        private const string WIRE_DIAGNOSTIC = "colour-diagnostic";
        private const string PROPERTY_MANAGED = "color-managed";
        private const string PROPERTY_DIAGNOSTIC = "color-diagnostic";

        /** Manifest protocol version; version 1 is the current wire contract. */
        internal int protocol_version { get; set; default = 1; }
        /** Width of the probed source image in pixels. */
        public int width { get; set; }
        /** Height of the probed source image in pixels. */
        public int height { get; set; }
        /** Number of pages reported by the decoder; animated frames are tracked separately. */
        public int pages { get; set; default = 1; }
        /**
         * Decoder-reported precision in bits per channel, which can differ from source
         * precision.
         */
        public int depth { get; set; default = 8; }
        /** Precision in bits per sample reported for the original encoded source. */
        public int source_bit_depth { get; set; }
        /** Whether the decoded image exposes an alpha channel. */
        public bool has_alpha { get; set; }
        /** Whether an identified color profile or equivalent color handling applies. */
        public bool color_managed { get; set; }
        /** Human-readable diagnostic describing color-profile interpretation or fallback. */
        public string color_diagnostic { get; set; default = ""; }
        /** Whether a source-embedded preview is available. */
        public bool embedded_preview { get; set; }
        /** MIME type inferred for the source image. */
        public string mime { get; set; default = "application/octet-stream"; }
        /** Decoder loader identifier reported by the worker; keep wire spelling stable. */
        public string loader { get; set; default = ""; }
        /** Whether the source is a RAW camera format. */
        public bool raw { get; set; }
        /** Whether the probed source contains HDR image data. */
        public bool hdr { get; set; }
        /** Whether a gain map is present in the source image. */
        public bool gain_map { get; set; }
        /** Whether the image contains animation frames. */
        public bool animated { get; set; }
        /** Whether frame timing is represented as an explicit sequence timeline. */
        public bool timed_sequence { get; set; }
        /** Animation repeat count supplied by the source; interpret with decoder semantics. */
        public int loop_count { get; set; }
        /** Duration of the current frame in milliseconds. */
        public int frame_duration_ms { get; set; }
        /** Presentation timestamp of the frame in microseconds. */
        public int64 frame_timestamp_us { get; set; }
        /** Duration of the timed-sequence frame in microseconds. */
        public int64 frame_duration_us { get; set; }
        /** Whether the source is vector-based rather than solely raster pixels. */
        public bool vector { get; set; }
        /** Whether the backend supports decoding a region of the source. */
        public bool region { get; set; }
        /** JSON-encoded metadata object from the decoder manifest. */
        public string metadata { get; set; default = "{}"; }
        /** Source-provided image names used to identify pages or frames. */
        public string image_names { get; set; default = ""; }
        /** Capabilities supported by the selected backend or reported image. */
        public ImageCapabilities capabilities {
            get {
                var flags = ImageCapabilities.STILL | ImageCapabilities.THUMBNAIL | ImageCapabilities.METADATA;
                if (pages > 1 && !animated) {
                    flags |= ImageCapabilities.MULTI_IMAGE;
                }
                if (animated) {
                    flags |= ImageCapabilities.ANIMATION;
                }
                if (depth > 8) {
                    flags |= ImageCapabilities.HIGH_DEPTH;
                }
                if (hdr) {
                    flags |= ImageCapabilities.HDR;
                }
                if (color_managed) {
                    flags |= ImageCapabilities.COLOR_MANAGED;
                }
                if (region) {
                    flags |= ImageCapabilities.REGION;
                }
                if (embedded_preview) {
                    flags |= ImageCapabilities.EMBEDDED_PREVIEW;
                }
                if (vector) {
                    flags |= ImageCapabilities.VECTOR;
                }
                if (gain_map) {
                    flags |= ImageCapabilities.GAIN_MAP;
                }
                return flags;
            }
        }

#if !CORE_WORKER_CLIENT
        /** Encodes the current request, probe or recipe using its versioned JSON schema. */
        internal string serialize () {
            var node = Json.gobject_serialize (this);
            var object = node.get_object ();
            object.set_int_member ("protocol-version", 1);
            rename_json_member (object, PROPERTY_MANAGED, WIRE_MANAGED);
            rename_json_member (object, PROPERTY_DIAGNOSTIC, WIRE_DIAGNOSTIC);
            var generator = new Json.Generator ();
            generator.set_root (node);
            return generator.to_data (null);
        }

#endif
#if !WORKER_PROCESS
        /** Parses and validates a versioned JSON representation, rejecting malformed fields. */
        internal static ImageProbe deserialize (string text) throws Error {
            var parser = new Json.Parser ();
            try {
                parser.load_from_data (text);
            } catch (Error error) {
                throw new ImageError.INVALID_RESPONSE ("Invalid decoder manifest JSON: %s", error.message);
            }
            var root = parser.get_root ();
            if (root == null || root.get_node_type () != Json.NodeType.OBJECT) {
                throw new ImageError.INVALID_RESPONSE ("Invalid decoder manifest");
            }
            var object = root.get_object ();
            var version = object.get_member ("protocol-version");
            if (version == null || version.get_value_type () != typeof (int64) || version.get_int () != 1) {
                throw new ImageError.INVALID_RESPONSE ("Unsupported decoder manifest version");
            }
            object.remove_member ("protocol-version");
            object.remove_member ("capabilities");
            rename_json_member (object, WIRE_MANAGED, PROPERTY_MANAGED);
            rename_json_member (object, WIRE_DIAGNOSTIC, PROPERTY_DIAGNOSTIC);
            // JSON-GLib accepts numeric coercions and emits criticals for some
            // mismatched GObject properties. Reject malformed fields first.
            ObjectClass klass = (ObjectClass) typeof (ImageProbe).class_ref ();
            foreach (unowned string member in object.get_members ()) {
                unowned ParamSpec? property = klass.find_property (member);
                if (property == null) {
                    continue;
                }
                var node = object.get_member (member);
                Type expected = property.value_type == typeof (int) ? typeof (int64) : property.value_type;
                if (node.get_node_type () != Json.NodeType.VALUE || node.get_value_type () != expected ||
                    (property.value_type == typeof (int) &&
                        (node.get_int () < int.MIN || node.get_int () > int.MAX))) {
                    throw new ImageError.INVALID_RESPONSE ("Invalid decoder manifest field: %s", member);
                }
            }
            var result = Json.gobject_deserialize (typeof (ImageProbe), root) as ImageProbe;
            if (result == null || result.width < 1 || result.height < 1 ||
                result.width > 1000000 || result.height > 1000000 ||
                result.pages < 1 || result.pages > 100001 || result.depth < 1 ||
                    result.depth > 64 || result.loader == "") {
                throw new ImageError.INVALID_RESPONSE ("Invalid image dimensions, page count, precision or decoder");
            }
            return result;
        }
#endif
    }
}
