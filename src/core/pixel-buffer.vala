namespace ImageKit {
    /** Immutable, straight-alpha, native-endian float32 RGBA in linear sRGB. */
    public sealed class PixelBuffer : Object {
        public int width { get; private set; }
        public int height { get; private set; }
        public int stride {
            get {
                return width * 16;
            }
        }
        public Bytes bytes { get; private set; }

        public PixelBuffer (int width, int height, Bytes bytes) throws ImageError {
            if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
                (int64) width * height > 16777216 || bytes.get_size () != (size_t) width * height * 16) {
                throw new ImageError.INVALID_RESPONSE ("Invalid image buffer geometry or byte count");
            }
            this.width = width;
            this.height = height;
            this.bytes = bytes;
        }
    }
}
