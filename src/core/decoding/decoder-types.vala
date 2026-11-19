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
}
