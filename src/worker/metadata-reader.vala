namespace MajorisImage {
    public class MetadataReader : Object {
        internal static string colour_probe (string path) throws Error {
            var output = new Json.Object ();
            var input = File.new_for_path (path).read ();
            var signature = input.read_bytes (8);
            unowned uint8[] bytes = signature.get_data ();
            bool colour_known = false;
            bool reached_pixels = false;
            bool explicit_transfer = false;
            bool have_cicp = false;
            bool have_icc = false;
            if (bytes.length == 8 && bytes[0] == 137 && bytes[1] == 'P' &&
                bytes[2] == 'N' && bytes[3] == 'G') {
                // Examine PNG colour declaration chunks without decoding pixel data.
                var chunks = new DataInputStream (input);
                chunks.set_byte_order (DataStreamByteOrder.BIG_ENDIAN);
                int metadata_count = 0;
                while (++metadata_count <= 4096) {
                    uint32 length = chunks.read_uint32 ();
                    uint32 kind = chunks.read_uint32 ();
                    if (kind == 0x49444154) { reached_pixels = true; break; } // IDAT
                    if (kind == 0x63494350) have_cicp = true;
                    if (kind == 0x69434350) have_icc = true;
                    if (kind == 0x67414d41 || kind == 0x6348524d)
                        explicit_transfer = true; // gAMA, cHRM
                    if (kind == 0x73524742) {
                        if (length != 1) throw new IOError.INVALID_DATA ("Invalid PNG sRGB declaration");
                        uint8 intent = chunks.read_byte ();
                        uint32[] checksums = { 0xaece1ce9u, 0xd9c92c7fu, 0x40c07dc5u, 0x37c74d53u };
                        if (intent >= checksums.length || chunks.read_uint32 () != checksums[intent])
                            throw new IOError.INVALID_DATA ("Invalid PNG sRGB declaration");
                        if (colour_known) throw new IOError.INVALID_DATA ("Duplicate PNG sRGB declaration");
                        colour_known = true;
                        continue;
                    }
                    if (!chunks.seek ((int64)length + 4, SeekType.CUR))
                        throw new IOError.INVALID_DATA ("Invalid PNG colour chunks");
                }
            }
            input.close ();
            var image = Vips.Image.from_file (path, null);
            output.set_boolean_member ("header-read", image != null);
            if (image != null) {
                int format = image.format ();
                output.set_int_member ("source-bit-depth", format == 2 || format == 3 ? 16 : format >= 4 ? 32 : 8);
                if (bytes.length == 8 && bytes[4] == 'f' && bytes[5] == 't' && bytes[6] == 'y' && bytes[7] == 'p') {
                    ImageNative.annotate_heif (image, path);
                    have_cicp = image.type_of ("majoris-image-transfer") != Type.INVALID;
                }
                var details = new Json.Parser ();
                details.load_from_data (ImageNative.image_details (image));
                output.set_object_member ("source-colour", details.get_root ().get_object ());
                var colour = details.get_root ().get_object ();
                output.set_boolean_member ("icc", colour.has_member ("profile"));
                output.set_boolean_member ("icc-invalid", colour.has_member ("profile-error") ||
                    (have_icc && !colour.has_member ("profile")));
                output.set_boolean_member ("gray-icc", colour.has_member ("colour-space") &&
                    colour.get_string_member ("colour-space") == "Gray");
            }
            if (have_icc && !output.get_boolean_member_with_default ("icc", false))
                output.set_boolean_member ("icc-invalid", true);
            output.set_boolean_member ("explicit-srgb", colour_known && reached_pixels && image != null &&
                !have_cicp && !output.get_boolean_member_with_default ("icc", false));
            output.set_boolean_member ("unverified-transfer", have_cicp || (explicit_transfer && !colour_known &&
                !output.get_boolean_member_with_default ("icc", false)));
            var node = new Json.Node (Json.NodeType.OBJECT);
            node.set_object (output);
            var generator = new Json.Generator ();
            generator.set_root (node);
            return generator.to_data (null);
        }

        // Only called in the isolated worker. Missing optional tags are omitted.
        public static Json.Object read (string path, int page = 0, int hdr_hint = -1, ImageProbe? inspected = null, Vips.Image? prepared = null) throws Error {
            var probe = inspected ?? ImageInspector.inspect (path,hdr_hint,page); var output = new Json.Object ();
            output.set_string_member ("format", probe.mime); output.set_int_member ("width", probe.width); output.set_int_member ("height", probe.height);
            output.set_int_member ("bit-depth", probe.depth); output.set_boolean_member ("raw", probe.raw); output.set_boolean_member ("hdr", probe.hdr); output.set_boolean_member ("gain-map", probe.gain_map);
            output.set_string_member ("backend", probe.loader); output.set_int_member ("pages", probe.pages);
            output.set_boolean_member ("animated", probe.animated);
            if (probe.animated) {
                output.set_int_member ("frame-index", page);
                output.set_int_member ("frame-timestamp-us", probe.frame_timestamp_us);
                output.set_int_member ("frame-duration-us", probe.frame_duration_us > 0 ?
                                       probe.frame_duration_us : (int64) probe.frame_duration_ms * 1000);
                output.set_int_member ("loop-count", probe.loop_count);
            }
            var raw_parser = new Json.Parser (); raw_parser.load_from_data (probe.metadata); output.set_member ("raw-details", raw_parser.get_root ().copy ());
            var metadata = new GExiv2.Metadata ();
            try {
                if (metadata.open_path (path)) {
                    string[] mappings = { "camera-make", "Exif.Image.Make", "camera-model", "Exif.Image.Model", "lens", "Exif.Photo.LensModel", "focal-length", "Exif.Photo.FocalLength", "aperture", "Exif.Photo.FNumber", "shutter", "Exif.Photo.ExposureTime", "iso", "Exif.Photo.ISOSpeedRatings", "exposure-compensation", "Exif.Photo.ExposureBiasValue", "white-balance", "Exif.Photo.WhiteBalance", "flash", "Exif.Photo.Flash", "software", "Exif.Image.Software", "creator", "Exif.Image.Artist", "copyright", "Exif.Image.Copyright", "capture-date", "Exif.Photo.DateTimeOriginal", "orientation", "Exif.Image.Orientation", "dpi", "Exif.Image.XResolution", "title", "Xmp.dc.title", "description", "Xmp.dc.description", "comments", "Exif.Photo.UserComment" };
                    for (int i=0;i<mappings.length;i+=2) {
                        try { string value = metadata.try_get_tag_interpreted_string (mappings[i+1]); if (value != "") output.set_string_member (mappings[i], value); }
                        catch (Error e) { }
                    }
                    try { double longitude, latitude, altitude; if (metadata.try_get_gps_info (out longitude, out latitude, out altitude)) { output.set_double_member ("latitude", latitude); output.set_double_member ("longitude", longitude); output.set_double_member ("altitude", altitude); } }
                    catch (Error e) { }
                }
            } catch (Error e) { debug ("Optional EXIF unavailable: %s",e.message); }
            if (!probe.raw) {
                Vips.Image? image = prepared;
                if (image == null) {
                    if (probe.loader == "openexr-native") image = ImageNative.exr_image (path, page);
                    else if (page > 0) image = Vips.Image.from_file (path, "page", page, "n", 1, null);
                    else image = Vips.Image.from_file (path, null);
                }
                if (image != null) {
                    if (prepared == null) ImageNative.annotate_heif (image, path);
                    output.set_boolean_member ("alpha", image.has_alpha ());
                    var parser = new Json.Parser (); parser.load_from_data (ImageNative.image_details (image)); output.set_member ("colour", parser.get_root ().copy ());
                    var details = new Json.Object ();
                    foreach (string field in image.fields ()) if (field.has_prefix ("exr") || field == "compression" || field == "xres" || field == "yres") { string value; if (image.get_as_string (field, out value) == 0) details.set_string_member (field,value); }
                    output.set_object_member ("image-details", details);
                }
            }
            return output;
        }
    }
}
