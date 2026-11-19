namespace ImageKit {
    internal sealed class WorkerRequest : Object {
        public int version { get; set; default = 1; }
        public RenderOptions transform { get; set; default = new RenderOptions (); }
        public DecodeOptions options { get; set; default = new DecodeOptions (); }
        public ExportOptions export_options { get; set; default = new ExportOptions (); }
        public string thumbnail_uri { get; set; default = ""; }
        public string thumbnail_mtime { get; set; default = ""; }
        // Request-only hint, to skip scanning a floating-point file on each tile.
        public int hdr_hint { get; set; default = -1; }

        public int sequence_frames { get; set; }

#if !WORKER_PROCESS
        public string serialize () {
            var node = Json.gobject_serialize (this);
            RenderOptions.translate_recipe (node.get_object ().get_member ("transform"), true);
            return Json.to_string (node, false);
        }
#endif

#if !CORE_WORKER_CLIENT
        public static WorkerRequest deserialize (string text) throws Error {
            var parser = new Json.Parser ();
            parser.load_from_data (text);
            var node = parser.get_root ();
            // Keep JSON-GLib's original error for an invalid root.
            if (node == null || node.get_node_type () != Json.NodeType.OBJECT) {
                return (WorkerRequest) Json.gobject_from_data (typeof (WorkerRequest), text);
            }
            RenderOptions.translate_recipe (node.get_object ().get_member ("transform"), false);
            var request = Json.gobject_deserialize (typeof (WorkerRequest), node) as WorkerRequest;
            if (request == null || request.version != 1 || request.transform == null ||
                request.options == null || request.options.page < 0 || request.options.page > 100000 ||
                request.options.raw_quality < 0 || request.options.raw_quality > 1 ||
                request.sequence_frames < 0 || request.sequence_frames > 10000 ||
                request.hdr_hint < -1 || request.hdr_hint > 1) {
                throw new ImageError.DECODE ("Invalid decoder request");
            }
            request.transform.validate ();
            return request;
        }
#endif
    }
}
