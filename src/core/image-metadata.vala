namespace ImageKit {
    /** Retains structured image metadata in serialized JSON form. */
    public sealed class ImageMetadata : Object {
        /** Metadata encoded as JSON; an empty object represents no known entries. */
        public string json { get; private set; }
        /** Wraps serialized metadata for read-only use by consumers. */
        internal ImageMetadata (string json) {
            this.json = json;
        }
    }
}
