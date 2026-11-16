namespace ImageKit {
    internal static void rename_json_member (Json.Object object, string from, string to) {
        if (!object.has_member (from)) {
            return;
        }
        object.set_member (to, object.get_member (from).copy ());
        object.remove_member (from);
    }

    public class RenderOptions : Object {
        public int version { get; set; default = 2; }
        public int turns { get; set; }
        public bool nearest_neighbor { get; set; }
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
            double[] adjustments = { highlights, shadows, whites, blacks, vibrance, temperature,
                tint, clarity, vignette };
            foreach (double value in adjustments) {
                if (!value.is_finite () || value < -1 || value > 1) {
                    throw new ImageError.INVALID_RECIPE (_("Invalid photographic adjustment."));
                }
            }
            if (version < 1 || version > 2 || !saturation.is_finite () || saturation < 0 ||
                saturation > 3 || !sharpness.is_finite () || sharpness < 0 || sharpness > 2 ||
                !filter_intensity.is_finite () || filter_intensity < 0 || filter_intensity > 1) {
                throw new ImageError.INVALID_RECIPE (_("Invalid edit recipe version or adjustment."));
            }
            if (!exposure.is_finite () || !angle.is_finite () || !contrast.is_finite () ||
                !brightness.is_finite () ||
                !crop_x.is_finite () || !crop_y.is_finite () || !crop_width.is_finite () ||
                    !crop_height.is_finite () ||
                crop_x < 0 || crop_y < 0 || crop_width <= 0 || crop_height <= 0 ||
                crop_x + crop_width > 1.000001 || crop_y + crop_height > 1.000001 ||
                exposure < -10 || exposure > 10 || contrast < 0 || contrast > 4 || angle < -45 || angle > 45) {
                throw new ImageError.INVALID_RECIPE ("Invalid edit parameters");
            }
        }
        // The on-disk recipe and the worker request share this field spelling.
        private const string RECIPE_NEAREST = "nearest-neighbour";
        private const string PROPERTY_NEAREST = "nearest-neighbor";

        internal static void translate_recipe (Json.Node? recipe, bool writing) {
            if (recipe == null || recipe.get_node_type () != Json.NodeType.OBJECT) {
                return;
            }
            rename_json_member (recipe.get_object (),
                writing ? PROPERTY_NEAREST : RECIPE_NEAREST,
                writing ? RECIPE_NEAREST : PROPERTY_NEAREST);
        }

        public string serialize () {
            var node = Json.gobject_serialize (this);
            translate_recipe (node, true);
            return Json.to_string (node, false);
        }

        public static RenderOptions deserialize (string data) throws Error {
            var parser = new Json.Parser ();
            parser.load_from_data (data);
            var node = parser.get_root ();
            // Keep JSON-GLib's original error for an invalid root.
            if (node == null || node.get_node_type () != Json.NodeType.OBJECT) {
                return (RenderOptions) Json.gobject_from_data (typeof (RenderOptions), data);
            }
            translate_recipe (node, false);
            var recipe = Json.gobject_deserialize (typeof (RenderOptions), node) as RenderOptions;
            if (recipe == null) {
                throw new ImageError.INVALID_RECIPE ("Invalid edit recipe");
            }
            recipe.validate ();
            return recipe;
        }

        public RenderOptions copy () {
            var result = new RenderOptions ();
            result.version = version;
            result.turns = turns;
            result.nearest_neighbor = nearest_neighbor;
            result.flip_x = flip_x;
            result.flip_y = flip_y;
            result.angle = copy_double (angle, 0.0);
            result.exposure = copy_double (exposure, 0.0);
            result.contrast = copy_double (contrast, 1.0);
            result.brightness = copy_double (brightness, 0.0);
            result.highlights = copy_double (highlights, 0.0);
            result.shadows = copy_double (shadows, 0.0);
            result.whites = copy_double (whites, 0.0);
            result.blacks = copy_double (blacks, 0.0);
            result.saturation = copy_double (saturation, 1.0);
            result.vibrance = copy_double (vibrance, 0.0);
            result.temperature = copy_double (temperature, 0.0);
            result.tint = copy_double (tint, 0.0);
            result.clarity = copy_double (clarity, 0.0);
            result.sharpness = copy_double (sharpness, 0.0);
            result.vignette = copy_double (vignette, 0.0);
            result.filter = filter ?? "original";
            result.filter_intensity = copy_double (filter_intensity, 1.0);
            result.crop_x = copy_double (crop_x, 0.0);
            result.crop_y = copy_double (crop_y, 0.0);
            result.crop_width = copy_double (crop_width, 1.0);
            result.crop_height = copy_double (crop_height, 1.0);
            result.monochrome = monochrome;
            result.sharpen = sharpen;
            try {
                result.validate ();
            } catch (Error error) {
                assert_not_reached ();
            }
            return result;
        }

#if RENDER_OPTIONS_DEFAULT_CHECK
        internal bool is_default () {
            // The comparison runs on a validated copy after crop and sampling are normalized.
            return version == 2 && turns == 0 && !nearest_neighbor && !flip_x && !flip_y &&
                angle == 0 && exposure == 0 && contrast == 1 && brightness == 0 &&
                highlights == 0 && shadows == 0 && whites == 0 && blacks == 0 &&
                saturation == 1 && vibrance == 0 && temperature == 0 && tint == 0 &&
                clarity == 0 && sharpness == 0 && vignette == 0 && filter == "original" &&
                filter_intensity == 1 && crop_x == 0 && crop_y == 0 &&
                crop_width == 1 && crop_height == 1 && !monochrome && !sharpen;
        }
#endif

        private static double copy_double (double value, double default_value) {
            // Match GParamSpecDouble's default comparison used by JSON-GLib.
            // This restores the default for signed zero, tiny differences, and NaN.
            return Math.fabs (value - default_value) > 1e-90 ? value : default_value;
        }
    }
}
