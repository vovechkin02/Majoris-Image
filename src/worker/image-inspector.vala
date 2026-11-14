namespace MajorisImage {
    // Invoked in the worker: no untrusted native parser runs in the GTK process.
    public class ImageInspector : Object {
        public static ImageProbe inspect (string path, int hdr_hint = -1, int page = 0) throws Error {
            var probe = new ImageProbe ();
            var stream = File.new_for_path (path).read ();
            var bytes = stream.read_bytes (65536); stream.close ();
            bool uncertain; probe.mime = ContentType.get_mime_type (ContentType.guess (null, bytes.get_data (), out uncertain));
            if (probe.mime == "application/pdf" || probe.mime == "application/postscript" ||
                probe.mime.has_prefix ("video/") || probe.mime.has_prefix ("application/vnd.openxmlformats"))
                throw new ImageError.UNSUPPORTED ("This source is not a supported image");
            probe.metadata = ImageNative.raw_info (path);
            var parser = new Json.Parser (); parser.load_from_data (probe.metadata);
            var raw = parser.get_root ().get_object ();
            probe.raw = raw.get_boolean_member ("raw");
            if (probe.raw) {
                if (page != 0) throw new IOError.INVALID_ARGUMENT ("RAW has one developed image");
                probe.width = (int) raw.get_int_member ("width"); probe.height = (int) raw.get_int_member ("height");
                int orientation=(int)raw.get_int_member ("orientation"); if (orientation == 5 || orientation == 6) { int width=probe.width; probe.width=probe.height; probe.height=width; }
                probe.depth = 16; probe.loader = "LibRaw"; return probe;
            }
            unowned uint8[] signature = bytes.get_data ();
            bool raw_signature = signature.length > 12 && ((signature[0] == 'I' && signature[1] == 'I' && signature[8] == 'C' && signature[9] == 'R') || (signature[0] == 'F' && signature[1] == 'U' && signature[2] == 'J' && signature[3] == 'I'));
            if (raw_signature || probe.mime in new string[] { "image/x-adobe-dng", "image/x-canon-cr2", "image/x-canon-cr3", "image/x-nikon-nef", "image/x-sony-arw", "image/x-fuji-raf", "image/x-panasonic-rw2", "image/x-olympus-orf" })
                throw new ImageError.RAW_UNSUPPORTED (_("LibRaw cannot develop this RAW variant. The original is unchanged."));
            probe.gain_map = ImageNative.ultra_probe (path);
            if (probe.gain_map && page > 0) throw new ImageError.CODEC_MISSING (_("This gain-map codec cannot select additional images within this HEIF container."));
            bool exr=signature.length >= 4 && signature[0] == 0x76 && signature[1] == 0x2f && signature[2] == 0x31 && signature[3] == 0x01;
            Vips.Image? image=exr ? ImageNative.exr_image (path,page) : page > 0 ? Vips.Image.from_file (path,"page",page,"n",1,null) : Vips.Image.from_file (path,null);
            if (image == null) {
                string detail = Vips.error_buffer ();
                if (detail.contains ("not a known file format")) {
                    if (probe.mime.has_prefix ("image/")) throw new ImageError.CODEC_MISSING ("%s", detail);
                    throw new ImageError.UNSUPPORTED ("%s", detail);
                }
                throw new ImageError.CORRUPT ("%s", detail);
            }
            if (probe.mime in new string[] { "image/heif", "image/heic", "image/avif" }) ImageNative.annotate_heif (image, path);
            probe.width = image.width (); probe.height = image.height ();
            if (probe.width < 1 || probe.height < 1 || probe.width > 1000000 || probe.height > 1000000)
                throw new ImageError.MEMORY ("Image dimensions exceed decoder limits");
            int format = image.format (); probe.depth = format == 2 || format == 3 ? 16 : format >= 4 ? 32 : 8;
            int source_depth=0; if (image.type_of ("majoris-image-bit-depth") != Type.INVALID && image.get_int ("majoris-image-bit-depth",out source_depth) == 0) probe.depth=source_depth;
            int pages = 1; if (image.type_of ("n-pages") != Type.INVALID && image.get_int ("n-pages", out pages) == 0) probe.pages = int.max (1, pages);
            unowned string loader = "";
            if (image.type_of ("vips-loader") != Type.INVALID && image.get_string ("vips-loader", out loader) == 0) probe.loader = loader;
            probe.animated = probe.pages > 1 && image.type_of ("delay") != Type.INVALID && (probe.loader.contains ("gif") || probe.loader.contains ("webp") || probe.loader.contains ("png"));
            if (probe.animated) {
                int loops = 0;
                if (image.type_of ("loop") != Type.INVALID) image.get_int ("loop", out loops);
                probe.loop_count = int.max (0, loops);
                probe.frame_duration_ms = ImageNative.frame_duration (image, page);
            }
            probe.vector = probe.loader.contains ("svg");
            if (exr) probe.mime="image/x-exr";
            if (probe.vector) probe.mime=signature.length>=2 && signature[0] == 0x1f && signature[1] == 0x8b ? "image/svg+xml-compressed" : "image/svg+xml";
            probe.region = !probe.gain_map && (probe.vector || probe.loader.contains ("tiff") || probe.loader.contains ("openslide") || exr);
            unowned string labels=""; if (image.type_of ("majoris-image-image-names") != Type.INVALID && image.get_string ("majoris-image-image-names",out labels) == 0) probe.image_names=labels;
            int marked_hdr = 0; if (image.type_of ("majoris-image-hdr") != Type.INVALID) image.get_int ("majoris-image-hdr", out marked_hdr);
            probe.hdr = exr || (image.type_of ("majoris-image-hdr") != Type.INVALID ? marked_hdr == 1 : image.type_of ("majoris-image-transfer") != Type.INVALID || image.type_of ("majoris-image-source-transfer") != Type.INVALID);
            if (image.type_of ("majoris-image-hdr") == Type.INVALID && !probe.hdr && (format == 6 || format == 7)) {
                if (hdr_hint >= 0) probe.hdr=hdr_hint == 1;
                else { double maximum; ImageProcessor.check (Vips.maximum (ImageNative.to_linear (image),out maximum,null)); probe.hdr=maximum > 1.0001; }
            }
            if (probe.gain_map) { probe.hdr = true; probe.depth = 16; probe.loader = "libultrahdr"; }
            return probe;
        }
    }
}
