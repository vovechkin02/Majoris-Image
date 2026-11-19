[CCode (gir_namespace = "ImageKitGtk", gir_version = "2.0")]
namespace ImageKit {
    /** Requested policy for HDR or SDR presentation. */
    public enum PresentationPolicy {
        /** Select output presentation from supported compositor capabilities. */
        AUTOMATIC,
        /** Prefer standard-dynamic-range presentation. */
        SDR,
        /** Prefer HDR output where the monitor and renderer support it. */
        HDR_PREFERRED
    }

    /** Selected presentation mode after output capability inspection. */
    public enum PresentationStatus {
        /** No presentation has been established. */
        NONE,
        /** Samples are presented using an SDR color state. */
        SDR,
        /** HDR samples have been tone mapped for SDR output. */
        SDR_TONE_MAPPED,
        /** Samples are presented using an HDR-capable output state. */
        NATIVE_HDR,
        /** Requested HDR output is unavailable on the current surface. */
        HDR_UNAVAILABLE,
        /** External texture color/output state could not be fully verified. */
        EXTERNAL_UNVERIFIED
    }
}
