namespace MajorisImage {
    public class RenderOptions : Object {
        public int version { get; set; default = 2; }
        public int turns { get; set; }
        public bool flip_x { get; set; }
        public bool flip_y { get; set; }
        public double angle { get; set; }
        public double exposure { get; set; }
        public double contrast { get; set; default = 1; }
        public double brightness { get; set; }
        public double highlights { get; set; }
        public double shadows { get; set; }
        public double whites { get; set; }
        public double blacks { get; set; }
        public double saturation { get; set; default = 1; }
        public double vibrance { get; set; }
        public double temperature { get; set; }
        public double tint { get; set; }
        public double clarity { get; set; }
        public double sharpness { get; set; }
        public double vignette { get; set; }
        public string filter { get; set; default = "original"; }
        public double filter_intensity { get; set; default = 1; }
        public double crop_x { get; set; }
        public double crop_y { get; set; }
        public double crop_width { get; set; default = 1; }
        public double crop_height { get; set; default = 1; }
        public bool monochrome { get; set; }
        public bool sharpen { get; set; }
        public void validate () throws Error {
            double[] adjustments = { highlights, shadows, whites, blacks, vibrance, temperature, tint, clarity, vignette };
            foreach (double value in adjustments) if (!value.is_finite () || value < -1 || value > 1) throw new ImageError.INVALID_RECIPE (_("Invalid photographic adjustment."));
            if (version < 1 || version > 2 || !saturation.is_finite () || saturation < 0 || saturation > 3 || !sharpness.is_finite () || sharpness < 0 || sharpness > 2 || !filter_intensity.is_finite () || filter_intensity < 0 || filter_intensity > 1)
                throw new ImageError.INVALID_RECIPE (_("Invalid edit recipe version or adjustment."));
            if (!exposure.is_finite () || !angle.is_finite () || !contrast.is_finite () || !brightness.is_finite () ||
                !crop_x.is_finite () || !crop_y.is_finite () || !crop_width.is_finite () || !crop_height.is_finite () ||
                crop_x < 0 || crop_y < 0 || crop_width <= 0 || crop_height <= 0 ||
                crop_x + crop_width > 1.000001 || crop_y + crop_height > 1.000001 ||
                exposure < -10 || exposure > 10 || contrast < 0 || contrast > 4 || angle < -45 || angle > 45)
                throw new ImageError.INVALID_RECIPE ("Invalid edit parameters");
        }
        public string serialize () { return Json.gobject_to_data (this, null); }
        public static RenderOptions deserialize (string data) throws Error {
            var recipe = Json.gobject_from_data (typeof (RenderOptions), data) as RenderOptions;
            if (recipe == null) throw new ImageError.INVALID_RECIPE ("Invalid edit recipe");
            recipe.validate (); return recipe;
        }
        public RenderOptions copy () {
            try { return deserialize (serialize ()); } catch (Error e) { assert_not_reached (); }
        }
    }
}
