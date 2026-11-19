namespace ImageKit {
    /** Renames one JSON object member without changing its value or wire representation. */
    internal static void rename_json_member (Json.Object object, string from, string to) {
        if (!object.has_member (from)) {
            return;
        }
        object.set_member (to, object.get_member (from).copy ());
        object.remove_member (from);
    }

    /** Versioned non-destructive recipe for rotation, crop and photographic edits. */
    public class RenderOptions : Object {
        /** Version of this request or edit recipe used for compatibility validation. */
        public int version { get; set; default = 2; }
        /** Number of clockwise quarter turns applied before arbitrary-angle rotation. */
        public int turns { get; set; }
        /** Whether pixel sampling avoids interpolation in rendering or display. */
        public bool nearest_neighbor { get; set; }
        /** Whether the image is mirrored across its horizontal coordinate axis. */
        public bool flip_x { get; set; }
        /** Whether the image is mirrored across its vertical coordinate axis. */
        public bool flip_y { get; set; }
        /** Additional arbitrary rotation angle in degrees; validation limits it to -45..45. */
        public double angle { get; set; }
        /** Exposure compensation in stops, restricted to -10..10. */
        public double exposure { get; set; }
        /** Multiplicative contrast adjustment; 1 is unchanged, permitted range 0..4. */
        public double contrast { get; set; default = 1; }
        /** Brightness adjustment applied during image rendering. */
        public double brightness { get; set; }
        /** Highlight-region tonal adjustment from -1 to 1. */
        public double highlights { get; set; }
        /** Shadow-region tonal adjustment from -1 to 1. */
        public double shadows { get; set; }
        /** White-point tonal adjustment from -1 to 1. */
        public double whites { get; set; }
        /** Black-point tonal adjustment from -1 to 1. */
        public double blacks { get; set; }
        /** Color saturation multiplier; 1 is unchanged, permitted range 0..3. */
        public double saturation { get; set; default = 1; }
        /** Selective saturation adjustment from -1 to 1. */
        public double vibrance { get; set; }
        /** Color temperature adjustment from -1 to 1. */
        public double temperature { get; set; }
        /** Green/magenta color tint adjustment from -1 to 1. */
        public double tint { get; set; }
        /** Local-contrast adjustment from -1 to 1. */
        public double clarity { get; set; }
        /** Sharpening strength from 0 to 2. */
        public double sharpness { get; set; }
        /** Edge-darkening adjustment from -1 to 1. */
        public double vignette { get; set; }
        /** Named photographic filter; original selects no creative filter. */
        public string filter { get; set; default = "original"; }
        /** Blend strength for the selected filter, from 0 to 1. */
        public double filter_intensity { get; set; default = 1; }
        /** Left edge of the normalized crop rectangle in source-relative coordinates. */
        public double crop_x { get; set; }
        /** Top edge of the normalized crop rectangle in source-relative coordinates. */
        public double crop_y { get; set; }
        /** Crop rectangle width as a fraction of source width; default 1. */
        public double crop_width { get; set; default = 1; }
        /** Crop rectangle height as a fraction of source height; default 1. */
        public double crop_height { get; set; default = 1; }
        /** Whether rendering removes chroma to produce grayscale output. */
        public bool monochrome { get; set; }
        /** Whether the boolean sharpening option is enabled. */
        public bool sharpen { get; set; }
        /**
         * Rejects invalid options, unsupported combinations or out-of-range values. Errors
         * propagate to the caller.
         */
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
                exposure < -10 || exposure > 10 || contrast < 0 || contrast > 4 ||
                angle < -45 || angle > 45) {
                throw new ImageError.INVALID_RECIPE ("Invalid edit parameters");
            }
        }
        // The on-disk recipe and the worker request share this field spelling.
        private const string RECIPE_NEAREST = "nearest-neighbour";
        private const string PROPERTY_NEAREST = "nearest-neighbor";

        /** Preserves legacy nearest-neighbor JSON field spelling across serialization. */
        internal static void translate_recipe (Json.Node? recipe, bool writing) {
            if (recipe == null || recipe.get_node_type () != Json.NodeType.OBJECT) {
                return;
            }
            rename_json_member (recipe.get_object (),
                writing ? PROPERTY_NEAREST : RECIPE_NEAREST,
                writing ? RECIPE_NEAREST : PROPERTY_NEAREST);
        }

        /** Encodes the current request, probe or recipe using its versioned JSON schema. */
        public string serialize () {
            var node = Json.gobject_serialize (this);
            translate_recipe (node, true);
            return Json.to_string (node, false);
        }

        /**
         * Parses and validates a versioned JSON representation, rejecting malformed fields.
         * Errors propagate to the caller.
         */
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

        /** Returns a separately mutable options snapshot with identical effective values. */
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
        /** Tests whether normalized recipe parameters equal the unedited defaults. */
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

        /** Preserves JSON-GLib default-equivalence when copying a float adjustment. */
        private static double copy_double (double value, double default_value) {
            // Match GParamSpecDouble's default comparison used by JSON-GLib.
            // This restores the default for signed zero, tiny differences, and NaN.
            return Math.fabs (value - default_value) > 1e-90 ? value : default_value;
        }
    }
}
