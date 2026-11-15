namespace ImageKit {
    public class ToneOperations : Object {
        public static Vips.Image apply (Vips.Image image, RenderOptions recipe) throws Error {
            if (recipe.exposure == 0 && recipe.brightness == 0 && recipe.contrast == 1 && recipe.highlights == 0 && recipe.shadows == 0 && recipe.whites == 0 && recipe.blacks == 0) return image;
            var rgb = FloatOps.band (image, 0, 3);
            rgb = FloatOps.linear (rgb, Math.pow (2, recipe.exposure) * recipe.contrast,
                recipe.brightness + 0.18 * (1 - recipe.contrast));
            var y = FloatOps.nonnegative (FloatOps.luma (rgb));
            var one = FloatOps.linear (y, 0, 1);
            var shadow = FloatOps.divide (one, FloatOps.linear (y, 4, 1));
            shadow = FloatOps.multiply (shadow, shadow);
            var highlight = FloatOps.divide (y, FloatOps.linear (y, 1, 1));
            var gain = FloatOps.power_two (FloatOps.add (FloatOps.linear (highlight, recipe.highlights * 0.7 + recipe.whites * 0.5), FloatOps.linear (shadow, recipe.shadows * 0.6)));
            rgb = FloatOps.add (FloatOps.multiply (rgb, gain), FloatOps.linear (shadow, recipe.blacks * 0.04));
            if (image.has_alpha ()) rgb = FloatOps.join (rgb, FloatOps.band (image, 3));
            rgb.set_int ("majoris-image-working-linear", 1); return rgb;
        }
    }
}
