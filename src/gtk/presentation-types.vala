[CCode (gir_namespace = "ImageKitGtk", gir_version = "2.0")]
namespace ImageKit {
    public enum PresentationPolicy {
        AUTOMATIC, SDR, HDR_PREFERRED
    }

    public enum PresentationStatus {
        NONE, SDR, SDR_TONE_MAPPED, NATIVE_HDR, HDR_UNAVAILABLE, EXTERNAL_UNVERIFIED
    }
}
