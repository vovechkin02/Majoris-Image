namespace MajorisImage {
    public class ImageProcessor : Object {
        public static void check (int result) throws Error {
            if (result != 0) throw new ImageError.EXPORT ("%s", Vips.error_buffer ());
        }
        public static Vips.Image render (string source, RenderOptions recipe, int preview = 0) throws Error {
            recipe.validate ();
            var image = Vips.Image.from_file (source, null);
            if (image == null) throw new ImageError.DECODE ("%s", Vips.error_buffer ());
            ImageNative.annotate_heif (image, source);
            return render_image (image, recipe, preview);
        }
        public static Vips.Image render_image (Vips.Image input, RenderOptions recipe, int preview = 0) throws Error {
            Vips.Image image = input;
            recipe.validate ();
            Vips.Image next;
            check (Vips.autorot (image, out next, null)); image = next;
            int x = int.min (image.width () - 1, (int) Math.floor (recipe.crop_x * image.width () + 1e-7));
            int y = int.min (image.height () - 1, (int) Math.floor (recipe.crop_y * image.height () + 1e-7));
            int w = int.max (1, int.min ((int) Math.floor (recipe.crop_width * image.width () + 1e-7), image.width () - x));
            int h = int.max (1, int.min ((int) Math.floor (recipe.crop_height * image.height () + 1e-7), image.height () - y));
            check (Vips.crop (image, out next, x, y, w, h, null)); image = next;
            if (recipe.turns % 4 != 0) { check (Vips.rot (image, out next, ((recipe.turns % 4) + 4) % 4, null)); image = next; }
            if (recipe.flip_x) { check (Vips.flip (image, out next, 0, null)); image = next; }
            if (recipe.flip_y) { check (Vips.flip (image, out next, 1, null)); image = next; }
            if (recipe.angle != 0) { check (Vips.similarity (image, out next, "angle", recipe.angle, null)); image = next; }
            image = ImageNative.to_linear (image);
            if (preview > 0 && (image.width () > preview || image.height () > preview)) {
                double factor = double.min ((double)preview/image.width (),(double)preview/image.height ());
                bool alpha = image.has_alpha ();
                if (alpha) { check (Vips.premultiply (image,out next,"max_alpha",1.0,null)); image=next; }
                if (recipe.nearest_neighbour) check (Vips.resize (image, out next, factor, "kernel", 0, null));
                else check (Vips.resize (image, out next, factor, null)); image=next;
                if (alpha) { check (Vips.unpremultiply (image,out next,"max_alpha",1.0,null)); image=next; }
            }
            image = ToneOperations.apply (image, recipe);
            image = ColorOperations.apply (image, recipe);
            check (Vips.copy (image, out next, "interpretation", 28, null));
            next.set_int ("majoris-image-working-linear", 1); return next;
        }
        public static void export_image (string source, string target, RenderOptions recipe, bool strip = false, int depth = 16) throws Error {
            var options = new ExportOptions ();
            options.format = target.substring (target.last_index_of (".") + 1).down ();
            options.bit_depth = depth; options.metadata = strip ? "remove-all" : "preserve";
            ExportEngine.write (render (source, recipe), target, options, false);
        }
    }
}
