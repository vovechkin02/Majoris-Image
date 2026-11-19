namespace ImageKit {
    /** Immutable, straight-alpha, native-endian float32 RGBA in linear sRGB. */
    public sealed class PixelBuffer : Object {
        /** Width of this pixel buffer in pixels, independently of the encoded source size. */
        public int width { get; private set; }
        /** Height of this pixel buffer in pixels, independently of the encoded source size. */
        public int height { get; private set; }
        /** Distance between consecutive image rows in bytes. */
        public int stride {
            get {
                return width * 16;
            }
        }
        /**
         * Immutable pixel/sample payload retained by this buffer; ownership is
         * reference-counted.
         */
        public Bytes bytes { get; private set; }

        /** Retains image dimensions and immutable pixel bytes in the expected layout. */
        public PixelBuffer (int width, int height, Bytes bytes) throws ImageError {
            if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
                (int64) width * height > 16777216 ||
                bytes.get_size () != (size_t) width * height * 16) {
                throw new ImageError.INVALID_RESPONSE ("Invalid image buffer geometry or byte count");
            }
            this.width = width;
            this.height = height;
            this.bytes = bytes;
        }
    }
}
