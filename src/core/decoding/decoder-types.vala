namespace ImageKit {
    /** Flags describing operations supported by a decoder or decoded image. */
    [Flags]
    public enum ImageCapabilities {
        /** Supports decoding a standalone still image. */
        STILL = 1,
        /** Supports decoding a sequence of animated frames. */
        ANIMATION = 2,
        /** Supports independent pages or images within one source. */
        MULTI_IMAGE = 4,
        /** Supports source precision above eight bits per channel. */
        HIGH_DEPTH = 8,
        /** Supports a high-dynamic-range image representation. */
        HDR = 16,
        /** Supports source-space region decoding. */
        REGION = 32,
        /** Supports generation of desktop-compatible thumbnails. */
        THUMBNAIL = 64,
        /** Can extract a source-provided embedded preview. */
        EMBEDDED_PREVIEW = 128,
        /** Exposes source image metadata. */
        METADATA = 256,
        /** Supports output encoding for the selected format. */
        ENCODE = 512,
        /** Supports vector image sources or operations. */
        VECTOR = 1024,
        /** Recognizes an HDR gain-map representation. */
        GAIN_MAP = 2048,
        /** Supports known color-profile interpretation or conversion. */
        COLOR_MANAGED = 4096,
    }

    /** Internal classification of native-decoder failures for fallback policy. */
    internal enum DecodeFailure {
        /** Input format or requested operation is unsupported. */
        UNSUPPORTED,
        /** Required decoder support is not installed. */
        CODEC_MISSING,
        /** Decoder input or response failed structural validation. */
        CORRUPT,
        /** Native decoding worker stopped unexpectedly. */
        CRASH,
        /** Native operation exceeded its execution time limit. */
        TIMEOUT,
        /** Decoder operation exhausted an enforced memory budget. */
        MEMORY,
        /** Color-profile handling prevented safe decoding. */
        COLOR_PROFILE,
        /** Other decoder failure with no more specific classification. */
        FAILURE,
    }
}
