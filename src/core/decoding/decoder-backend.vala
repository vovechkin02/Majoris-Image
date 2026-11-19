namespace ImageKit {
    /** An application-registered worker executable using the version 1 protocol. */
    public interface DecoderBackend : Object {
        public abstract string id { get; }
        public abstract string executable { get; }
        public abstract ImageCapabilities capabilities { get; }
        public abstract string[] mime_types { owned get; }
        public abstract bool accepts (string mime, Bytes signature);
    }
}
