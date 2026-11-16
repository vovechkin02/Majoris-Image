namespace ImageKit {
    [Flags]
    public enum ImageCapabilities {
        STILL = 1,
        ANIMATION = 2,
        MULTI_IMAGE = 4,
        HIGH_DEPTH = 8,
        HDR = 16,
        REGION = 32,
        THUMBNAIL = 64,
        EMBEDDED_PREVIEW = 128,
        METADATA = 256,
        ENCODE = 512,
        VECTOR = 1024,
        GAIN_MAP = 2048,
        COLOR_MANAGED = 4096,
    }

    public enum RawState {
        NOT_RAW,
        RAW_PREVIEW_AVAILABLE,
        RAW_DEVELOPING,
        RAW_DEVELOPED,
        RAW_UNSUPPORTED,
        RAW_DEVELOPMENT_FAILED,
    }

    internal enum DecodeFailure {
        UNSUPPORTED,
        CODEC_MISSING,
        CORRUPT,
        CRASH,
        TIMEOUT,
        MEMORY,
        COLOR_PROFILE,
        FAILURE,
    }

    /** An application-registered worker executable using the version 1 protocol. */
    public interface DecoderBackend : Object {
        public abstract string id { get; }
        public abstract string executable { get; }
        public abstract ImageCapabilities capabilities { get; }
        public abstract string[] mime_types { owned get; }
        public abstract bool accepts (string mime, Bytes signature);
    }
}
