namespace ImageKit {
    // Private closed-set routing, shared as source by Core and GTK. Labels remain
    // public diagnostics and protocol identities; unknown custom loaders use workers.
    [CCode (has_type_id = false)]
    internal enum DecoderRoute {
        WORKER,
        FALLBACK,
        SEQUENCE;

        internal static DecoderRoute from_label (string label) {
            if (label == "Glycin") {
                return FALLBACK;
            }
            if (label == "libheif-sequence") {
                return SEQUENCE;
            }
            return WORKER;
        }
    }
}
