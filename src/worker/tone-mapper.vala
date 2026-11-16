namespace ImageKit {
    public class ToneMapper : Object {
        // Luminance-preserving Reinhard compression, with three documented
        // exposure placements. No operation touches the source file or alpha.
        public static Vips.Image map (Vips.Image input, string mode = "automatic") throws Error {
            var image = ImageNative.to_linear (input);
            var rgb = FloatOps.band (image, 0, 3);
            var luminance = FloatOps.nonnegative (FloatOps.luma (rgb));
            double placement = mode == "highlights" ? 0.9 : mode == "bright" ? 2.4 : 1.6;
            var denominator = FloatOps.linear (luminance, placement, 1);
            var ratio = FloatOps.divide (FloatOps.linear (luminance, 0, placement), denominator);
            var output = FloatOps.multiply (rgb, ratio);
            // Compress chroma towards the mapped luminance to fit the SDR sRGB
            // gamut. This retains hue and avoids per-channel highlight clipping.
            var mapped_y = FloatOps.multiply (luminance, ratio);
            var chroma = FloatOps.subtract (output, mapped_y);
            var low = FloatOps.band (chroma, 0);
            var high = low;
            for (int channel = 1; channel < 3; channel++) {
                var band = FloatOps.band (chroma, channel);
                low = FloatOps.minimum (low, band);
                high = FloatOps.maximum (high, band);
            }
            var one = FloatOps.linear (mapped_y, 0, 1);
            var upper = FloatOps.divide (FloatOps.linear (mapped_y, -1, 1),
                FloatOps.linear (FloatOps.nonnegative (high), 1, 0.000001));
            var lower = FloatOps.divide (mapped_y,
                FloatOps.linear (FloatOps.nonnegative (FloatOps.linear (low, -1)), 1, 0.000001));
            var chroma_scale = FloatOps.nonnegative (FloatOps.minimum (one, FloatOps.minimum (upper, lower)));
            output = FloatOps.add (mapped_y, FloatOps.multiply (chroma, chroma_scale));
            if (image.has_alpha ()) {
                output = FloatOps.join (output, FloatOps.band (image, 3));
            }
            Vips.Image tagged;
            ImageProcessor.check (Vips.copy (output, out tagged, "interpretation", 28, null));
            output = tagged;
            output.set_int ("majoris-image-working-linear", 1);
            output.set_int ("majoris-image-tone-mapped", 1);
            output.set_int ("majoris-image-hdr", 0);
            return output;
        }

        public static double reference (double luminance, string mode = "automatic") {
            double placement = mode == "highlights" ? 0.9 : mode == "bright" ? 2.4 : 1.6;
            return double.max (0, luminance) * placement / (1 + double.max (0, luminance) * placement);
        }
    }
}
