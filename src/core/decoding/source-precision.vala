namespace ImageKit {
    /**
     * Reconciles independently reported source-sample depths and rejects lossy fallback.
     *
     * Zero means that the corresponding probe did not establish a source depth.
     * A positive depth from either probe is authoritative for the minimum precision;
     * disagreement must never lower that requirement. The decoded depth describes
     * decoder sample precision, not ImageKit's float32 pixel-buffer representation.
     *
     * Throws INVALID_RESPONSE for malformed metadata and UNSUPPORTED if the
     * Glycin output has fewer sample bits than either known source depth.
     */
    internal int checked_source_sample_depth (int64 color_depth, int animation_depth,
                                               int decoded_depth) throws Error {
        if (color_depth < 0 || color_depth > 64 ||
            animation_depth < 0 || animation_depth > 64 ||
            decoded_depth < 1 || decoded_depth > 64) {
            throw new ImageError.INVALID_RESPONSE ("Invalid source or decoded sample precision");
        }
        int source_depth = color_depth > animation_depth
            ? (int) color_depth : animation_depth;
        if (source_depth > decoded_depth) {
            throw new ImageError.UNSUPPORTED (
                "Glycin would reduce precision from the source samples");
        }
        return source_depth;
    }
}
