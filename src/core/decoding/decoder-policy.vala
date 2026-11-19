namespace ImageKit {
    // Private closed-set routing, shared as source by Core and GTK. Labels remain
    // public diagnostics and protocol identities; unknown custom loaders use workers.
    /** Private routing decision derived from legacy decoder labels. */
    [CCode (has_type_id = false)]
    internal enum DecoderRoute {
        /** Use the native worker's decoding path. */
        WORKER,
        /** Use the Glycin fallback decoder. */
        FALLBACK,
        SEQUENCE;

        /** Maps a serialized decoder label to its private route without changing the label. */
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
