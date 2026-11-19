namespace ImageKit {
    /** An application-registered worker executable using the version 1 protocol. */
    public interface DecoderBackend : Object {
        /** Stable backend identifier used for registration and diagnostics. */
        public abstract string id { get; }
        /** Executable path of the sandboxed decoder backend. */
        public abstract string executable { get; }
        /** Capabilities supported by the selected backend or reported image. */
        public abstract ImageCapabilities capabilities { get; }
        /** MIME types recognized by this backend; returned array is owned by the caller. */
        public abstract string[] mime_types { owned get; }
        /** Determines whether this backend can decode the MIME type and leading signature. */
        public abstract bool accepts (string mime, Bytes signature);
    }
}
