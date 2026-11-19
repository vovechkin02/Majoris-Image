[CCode (gir_namespace = "ImageKitGtk", gir_version = "2.0")]
namespace ImageKit {
    /** Lifecycle states exposed by the GTK image widget. */
    public enum ViewState {
        /** No document is currently displayed. */
        EMPTY,
        /** A source open request is in progress. */
        LOADING,
        /** Preview pixels are available while additional work may continue. */
        PREVIEW_READY,
        /** The current image is ready for normal interaction. */
        READY,
        /** The source or requested format is not supported. */
        UNSUPPORTED,
        /** The necessary decoder is unavailable. */
        DECODER_UNAVAILABLE,
        /** The source or its decoder response is invalid. */
        CORRUPT,
        /** The requested load was canceled. */
        CANCELED,
        /** An image loading or presentation operation failed. */
        ERROR
    }

    /** Policy for fitting an image within an available view rectangle. */
    public enum FitMode {
        /** Fit the entire image inside the available viewport. */
        CONTAIN,
        /** Scale the image to match the viewport width. */
        WIDTH,
        /** Scale the image to match the viewport height. */
        HEIGHT,
        /** Use the image actual-size scale. */
        ACTUAL,
        /** Fill the viewport, permitting parts of the image to be clipped. */
        COVER,
        /** Preserve the caller-selected zoom multiplier. */
        CUSTOM
    }

    /** Policy for mapping user scrolling to zoom interaction. */
    public enum ScrollZoomMode {
        /** Allow scroll input to control zoom. */
        ALWAYS,
        /** Require the control modifier to enable scroll-wheel zoom. */
        CONTROL,
        /** Do not change zoom in response to scrolling. */
        DISABLED
    }
}
