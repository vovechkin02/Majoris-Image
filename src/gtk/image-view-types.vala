[CCode (gir_namespace = "ImageKitGtk", gir_version = "2.0")]
namespace ImageKit {
    public enum ViewState {
        EMPTY, LOADING, PREVIEW_READY, READY, UNSUPPORTED, DECODER_UNAVAILABLE, CORRUPT, CANCELED, ERROR
    }

    public enum FitMode {
        CONTAIN, WIDTH, HEIGHT, ACTUAL, COVER, CUSTOM
    }

    public enum ScrollZoomMode {
        ALWAYS, CONTROL, DISABLED
    }
}
