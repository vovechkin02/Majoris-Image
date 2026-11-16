namespace ImageKit {
    public sealed class ImageProbe : Object {
        private const string WIRE_MANAGED = "colour-managed";
        private const string WIRE_DIAGNOSTIC = "colour-diagnostic";
        private const string PROPERTY_MANAGED = "color-managed";
        private const string PROPERTY_DIAGNOSTIC = "color-diagnostic";

        internal int protocol_version { get; set; default = 1; }
        public int width { get; set; }
        public int height { get; set; }
        public int pages { get; set; default = 1; }
        public int depth { get; set; default = 8; }
        public int source_bit_depth { get; set; }
        public bool has_alpha { get; set; }
        public bool color_managed { get; set; }
        public string color_diagnostic { get; set; default = ""; }
        public bool embedded_preview { get; set; }
        public string mime { get; set; default = "application/octet-stream"; }
        public string loader { get; set; default = ""; }
        public bool raw { get; set; }
        public bool hdr { get; set; }
        public bool gain_map { get; set; }
        public bool animated { get; set; }
        public bool timed_sequence { get; set; }
        public int loop_count { get; set; }
        public int frame_duration_ms { get; set; }
        public int64 frame_timestamp_us { get; set; }
        public int64 frame_duration_us { get; set; }
        public bool vector { get; set; }
        public bool region { get; set; }
        public string metadata { get; set; default = "{}"; }
        public string image_names { get; set; default = ""; }
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
    }
}
