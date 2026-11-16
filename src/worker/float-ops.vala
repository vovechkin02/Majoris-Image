namespace ImageKit {
    // Small algebra helpers keep photo-processing policy in Vala and retain
    // libvips' lazy region evaluation instead of materializing entire buffers.
    public class FloatOps : Object {
        public static Vips.Image linear (Vips.Image image, double scale, double offset = 0) throws Error {
            Vips.Image output;
            ImageProcessor.check (Vips.linear (image, out output, scale, offset, null));
            return output;
        }

        public static Vips.Image add (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.add (a, b, out result, null));
            return result;
        }

        public static Vips.Image subtract (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.subtract (a, b, out result, null));
            return result;
        }

        public static Vips.Image multiply (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.multiply (a, b, out result, null));
            return result;
        }

        public static Vips.Image divide (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.divide (a, b, out result, null));
            return result;
        }

        public static Vips.Image band (Vips.Image image, int first, int count = 1) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.extract_band (image, out result, first, "n", count, null));
            return result;
        }

        public static Vips.Image join (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.bandjoin (a, b, out result, null));
            return result;
        }

        public static Vips.Image luma (Vips.Image rgb) throws Error {
            Vips.Image result;
            var matrix = Vips.Image.matrix (3, 1, { 0.2126, 0.7152, 0.0722 });
            ImageProcessor.check (Vips.recomb (rgb, out result, matrix, null));
            return result;
        }

        public static Vips.Image nonnegative (Vips.Image image) throws Error {
            Vips.Image mask;
            Vips.Image result;
            ImageProcessor.check (Vips.relational (image, out mask, Vips.Relation.MORE, 0, null));
            ImageProcessor.check (Vips.choose (mask, image, linear (image, 0), out result, null));
            return result;
        }

        public static Vips.Image absolute (Vips.Image image) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.absolute (image, out result, null));
            return result;
        }

        public static Vips.Image power_two (Vips.Image image) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.math (linear (image, Math.log (2)), out result,
                Vips.MathOperation.EXP, null));
            return result;
        }

        public static Vips.Image minimum (Vips.Image a, Vips.Image b) throws Error {
            return linear (subtract (add (a, b), absolute (subtract (a, b))), 0.5);
        }

        public static Vips.Image maximum (Vips.Image a, Vips.Image b) throws Error {
            return linear (add (add (a, b), absolute (subtract (a, b))), 0.5);
        }
    }
}
