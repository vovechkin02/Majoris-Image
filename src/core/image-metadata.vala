namespace ImageKit {
    public sealed class ImageMetadata : Object {
        public string json { get; private set; }
        internal ImageMetadata (string json) {
            this.json = json;
        }
    }
}
