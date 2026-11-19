namespace ImageKit {
    // Small algebra helpers keep photo-processing policy in Vala and retain
    // libvips' lazy region evaluation instead of materializing entire buffers.
    namespace FloatOps {
        /** Maps a float sample into another affine range. */
        internal Vips.Image scale_offset (Vips.Image image, double scale, double offset = 0) throws Error {
            Vips.Image output;
            ImageProcessor.check (Vips.linear (image, out output, scale, offset, null));
            return output;
        }

        /** Computes a per-sample sum for the requested image bands. */
        internal Vips.Image add (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.add (a, b, out result, null));
            return result;
        }

        /** Computes a per-sample difference for the requested image bands. */
        internal Vips.Image subtract (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.subtract (a, b, out result, null));
            return result;
        }

        /** Computes a per-sample product for the requested image bands. */
        internal Vips.Image multiply (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.multiply (a, b, out result, null));
            return result;
        }

        /** Computes a per-sample quotient for the requested image bands. */
        internal Vips.Image divide (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.divide (a, b, out result, null));
            return result;
        }

        /** Extracts the requested channel range from a float image. */
        internal Vips.Image extract_bands (Vips.Image image, int first, int count = 1) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.extract_band (image, out result, first, "n", count, null));
            return result;
        }

        /** Combines image bands into one multi-channel float image. */
        internal Vips.Image join_bands (Vips.Image a, Vips.Image b) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.bandjoin (a, b, out result, null));
            return result;
        }

        /** Computes luminance from image channels for subsequent tonal processing. */
        internal Vips.Image luma (Vips.Image rgb) throws Error {
            Vips.Image result;
            var matrix = Vips.Image.matrix (3, 1, { 0.2126, 0.7152, 0.0722 });
            ImageProcessor.check (Vips.recomb (rgb, out result, matrix, null));
            return result;
        }

        /** Clamps negative sample values to the nonnegative range. */
        internal Vips.Image nonnegative (Vips.Image image) throws Error {
            Vips.Image mask;
            Vips.Image result;
            ImageProcessor.check (Vips.relational (image, out mask, Vips.Relation.MORE, 0, null));
            ImageProcessor.check (Vips.choose (mask, image, scale_offset (image, 0), out result, null));
            return result;
        }

        /** Computes absolute sample magnitudes. */
        internal Vips.Image absolute (Vips.Image image) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.absolute (image, out result, null));
            return result;
        }

        /** Squares sample values for image-math processing. */
        internal Vips.Image power_two (Vips.Image image) throws Error {
            Vips.Image result;
            ImageProcessor.check (Vips.math (scale_offset (image, Math.log (2)), out result,
                Vips.MathOperation.EXP, null));
            return result;
        }

        /** Computes the per-sample minimum for float image operands. */
        internal Vips.Image minimum (Vips.Image a, Vips.Image b) throws Error {
            return scale_offset (subtract (add (a, b), absolute (subtract (a, b))), 0.5);
        }

        /** Computes the per-sample maximum for float image operands. */
        internal Vips.Image maximum (Vips.Image a, Vips.Image b) throws Error {
            return scale_offset (add (add (a, b), absolute (subtract (a, b))), 0.5);
        }
    }
}
