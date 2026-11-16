namespace ImageKit {
    public class ToneAdjustments : Object {
        public static Vips.Image apply (Vips.Image image, RenderOptions recipe) throws Error {
            if (recipe.exposure == 0 && recipe.brightness == 0 && recipe.contrast == 1 &&
                recipe.highlights == 0 && recipe.shadows == 0 && recipe.whites == 0 && recipe.blacks == 0) {
                return image;
            }
            var rgb = FloatOps.extract_bands (image, 0, 3);
            rgb = FloatOps.scale_offset (rgb, Math.pow (2, recipe.exposure) * recipe.contrast,
                recipe.brightness + 0.18 * (1 - recipe.contrast));
            var y = FloatOps.nonnegative (FloatOps.luma (rgb));
            var one = FloatOps.scale_offset (y, 0, 1);
            var shadow = FloatOps.divide (one, FloatOps.scale_offset (y, 4, 1));
            shadow = FloatOps.multiply (shadow, shadow);
            var highlight = FloatOps.divide (y, FloatOps.scale_offset (y, 1, 1));
            var gain = FloatOps.power_two (FloatOps.add (FloatOps.scale_offset (highlight,
                recipe.highlights * 0.7 + recipe.whites * 0.5),
                FloatOps.scale_offset (shadow, recipe.shadows * 0.6)));
            rgb = FloatOps.add (FloatOps.multiply (rgb, gain), FloatOps.scale_offset (shadow, recipe.blacks * 0.04));
            if (image.has_alpha ()) {
                rgb = FloatOps.join_bands (rgb, FloatOps.extract_bands (image, 3));
            }
            rgb.set_int ("majoris-image-working-linear", 1);
            return rgb;
        }
    }
}
