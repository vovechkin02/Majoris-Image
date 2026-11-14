namespace MajorisImage {
    public class MetadataReader : Object {
        // Only called in the isolated worker. Missing optional tags are omitted.
        public static Json.Object read (string path, int page = 0, int hdr_hint = -1, ImageProbe? inspected = null) throws Error {
            var probe = inspected ?? ImageInspector.inspect (path,hdr_hint,page); var output = new Json.Object ();
            output.set_string_member ("format", probe.mime); output.set_int_member ("width", probe.width); output.set_int_member ("height", probe.height);
            output.set_int_member ("bit-depth", probe.depth); output.set_boolean_member ("raw", probe.raw); output.set_boolean_member ("hdr", probe.hdr); output.set_boolean_member ("gain-map", probe.gain_map);
            output.set_string_member ("backend", probe.loader); output.set_int_member ("pages", probe.pages);
            output.set_boolean_member ("animated", probe.animated);
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
                Vips.Image? image=probe.loader == "openexr-native" ? ImageNative.exr_image (path,page) : page > 0 ? Vips.Image.from_file (path,"page",page,"n",1,null) : Vips.Image.from_file (path,null);
                if (image != null) {
                    ImageNative.annotate_heif (image, path);
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
