namespace ImageKit {
    public class ColorAdjustments : Object {
        public static Vips.Image apply (Vips.Image image, RenderOptions input_recipe) throws Error {
            if (!input_recipe.monochrome && !input_recipe.sharpen && input_recipe.saturation == 1 &&
                input_recipe.vibrance == 0 && input_recipe.temperature == 0 &&
                input_recipe.tint == 0 && input_recipe.clarity == 0 &&
                input_recipe.sharpness == 0 && input_recipe.vignette == 0 &&
                (input_recipe.filter == "original" || input_recipe.filter_intensity == 0)) {
                return image;
            }
            var recipe = input_recipe.copy ();
            double intensity = recipe.filter_intensity;
            switch (recipe.filter) {
                case "warm":
                    recipe.temperature += 0.4 * intensity;
                    recipe.saturation *= 1 + 0.05 * intensity;
                    break;
                case "cool":
                    recipe.temperature -= 0.35 * intensity;
                    break;
                case "high-contrast":
                    recipe.contrast *= 1 + 0.25 * intensity;
                    break;
                case "soft":
                    recipe.contrast *= 1 - 0.15 * intensity;
                    recipe.brightness += 0.025 * intensity;
                    break;
                case "monochrome":
                    recipe.saturation *= 1 - intensity;
                    break;
                case "faded":
                    recipe.contrast *= 1 - 0.2 * intensity;
                    recipe.saturation *= 1 - 0.3 * intensity;
                    break;
                case "vintage":
                    recipe.temperature += 0.25 * intensity;
                    recipe.saturation *= 1 - 0.25 * intensity;
                    break;
            }
            var rgb = FloatOps.extract_bands (image, 0, 3);
            if (recipe.filter == "high-contrast" || recipe.filter == "soft" || recipe.filter == "faded") {
                double contrast = recipe.filter == "high-contrast" ? 1 + 0.25 * intensity
                    : recipe.filter == "soft" ? 1 - 0.15 * intensity : 1 - 0.2 * intensity;
                rgb = FloatOps.scale_offset (rgb, contrast,
                    0.18 * (1 - contrast) + (recipe.filter == "soft" ? 0.025 * intensity : 0));
            }
            double red = Math.pow (2, recipe.temperature * 0.25 + recipe.tint * 0.08);
            double green = Math.pow (2, -recipe.tint * 0.16);
            double blue = Math.pow (2, -recipe.temperature * 0.25 + recipe.tint * 0.08);
            Vips.Image adjusted;
            var matrix = Vips.Image.matrix (3, 3, { red, 0, 0, 0, green, 0, 0, 0, blue });
            ImageProcessor.check (Vips.recomb (rgb, out adjusted, matrix, null));
            rgb = adjusted;
            var y = FloatOps.luma (rgb);
            double saturation = recipe.monochrome ? 0 : recipe.saturation;
            var chroma = FloatOps.subtract (rgb, y);
            var strength = FloatOps.divide (FloatOps.scale_offset (y, 0, recipe.vibrance * 0.35),
                FloatOps.scale_offset (FloatOps.absolute (chroma), 2, 1));
            rgb = FloatOps.add (y,
                FloatOps.multiply (chroma, FloatOps.scale_offset (strength, saturation, saturation)));
            if (recipe.clarity != 0) {
                Vips.Image smooth;
                ImageProcessor.check (Vips.blur (rgb, out smooth,
                    double.max (1, int.min (image.width (), image.height ())*0.006), null));
                rgb = FloatOps.add (rgb,
                    FloatOps.scale_offset (FloatOps.subtract (rgb, smooth), recipe.clarity * 0.5));
            }
            if (recipe.sharpen || recipe.sharpness > 0) {
                Vips.Image smooth;
                ImageProcessor.check (Vips.blur (rgb, out smooth, 1, null));
                rgb = FloatOps.add (rgb,
                    FloatOps.scale_offset (FloatOps.subtract (rgb, smooth),
                    recipe.sharpness + (recipe.sharpen ? 0.4 : 0)));
            }
            if (recipe.vignette != 0) {
                Vips.Image xy;
                ImageProcessor.check (Vips.coordinates (out xy, image.width (), image.height (), null));
                var x = FloatOps.scale_offset (FloatOps.extract_bands (xy, 0), 2.0 / image.width (), -1);
                var ycoord = FloatOps.scale_offset (FloatOps.extract_bands (xy, 1), 2.0 / image.height (), -1);
                var radius = FloatOps.scale_offset (FloatOps.add (FloatOps.multiply (x, x),
                    FloatOps.multiply (ycoord, ycoord)), 0.5);
                var denominator = FloatOps.scale_offset (FloatOps.multiply (radius, radius),
                    recipe.vignette * 0.7, 1);
                rgb = FloatOps.divide (rgb, denominator);
            }
            if (image.has_alpha ()) {
                rgb = FloatOps.join_bands (rgb, FloatOps.extract_bands (image, 3));
            }
            rgb.set_int ("majoris-image-working-linear", 1);
            return rgb;
        }
    }
}
