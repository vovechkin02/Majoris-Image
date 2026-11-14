namespace MajorisImage {
    private Vips.Image open_pixels (string source, ImageProbe probe, DecodeOptions options) throws Error {
        if (probe.raw) {
            if (options.raw_preview_only) {
                ImageNative.raw_preview (source, "/job/embedded-preview.jpg");
                source = "/job/embedded-preview.jpg";
            } else {
                ImageNative.develop_raw_options (source, "/job/developed.tif",
                                                 options.raw_quality, options.camera_white_balance);
                source = "/job/developed.tif";
            }
        }
        if (probe.gain_map) return ImageNative.ultra_decode (source, false);
        if (probe.loader == "openexr-native") return ImageNative.exr_image (source, options.page);
        var image = options.page == 0 ? Vips.Image.from_file (source, "fail_on", 2, null) :
            Vips.Image.from_file (source, "page", options.page, "n", 1, "fail_on", 2, null);
        if (image == null) throw new ImageError.CORRUPT ("%s", Vips.error_buffer ());
        if (probe.loader.contains ("heif")) ImageNative.annotate_heif (image, source);
        if (probe.raw && !options.raw_preview_only) {
            image.set_int ("majoris-image-transfer", 8);
            image.set_int ("majoris-image-primaries", 1);
        }
        return image;
    }

    private void update_metadata (string source, DecodeOptions options, WorkerRequest request,
                                  ImageProbe probe) throws Error {
        var metadata = MetadataReader.read (source, options.page, request.hdr_hint, probe);
        metadata.set_int_member ("source-bit-depth", probe.source_bit_depth);
        metadata.set_int_member ("effective-bit-depth", probe.depth);
        metadata.set_int_member ("pixel-buffer-bit-depth", 32);
        metadata.set_boolean_member ("alpha", probe.has_alpha);
        metadata.set_boolean_member ("embedded-preview", probe.raw && options.raw_preview_only);
        if (probe.raw && !options.raw_preview_only) {
            metadata.set_int_member ("raw-quality", options.raw_quality);
            metadata.set_boolean_member ("camera-white-balance", options.camera_white_balance);
        }
        var node = new Json.Node (Json.NodeType.OBJECT);
        node.set_object (metadata);
        var generator = new Json.Generator ();
        generator.set_root (node);
        probe.metadata = generator.to_data (null);
    }

    public int main (string[] args) {
        if (args.length != 6 || Vips.init ("majoris-image-worker") != 0) return 2;
        Vips.cache_set_max_mem (128 * 1024 * 1024);
        Vips.concurrency_set (2);
        try {
            string operation = args[1], source = args[2], output = args[3];
            if (operation == "codecs") {
                FileUtils.set_contents (output, ImageNative.codec_json ());
                return 0;
            }
            string json;
            FileUtils.get_contents (args[4], out json);
            var request = WorkerRequest.deserialize (json);
            var options = request.options;
            int edge = int.parse (args[5]);
            if (edge < 0 || edge > 4096) throw new IOError.INVALID_ARGUMENT ("Invalid output edge");
            var probe = ImageInspector.inspect (source, request.hdr_hint, options.page);
            if (operation == "probe") {
                update_metadata (source, options, request, probe);
                FileUtils.set_contents (output, probe.serialize ());
                return 0;
            }
            if (operation != "thumbnail" && operation != "decode" && operation != "tile" && operation != "export")
                throw new IOError.INVALID_ARGUMENT ("Unknown decoder operation");
            if (operation == "thumbnail" && probe.raw) {
                options.raw_preview_only = options.raw_preview_first;
            }
            Vips.Image input;
            try { input = open_pixels (source, probe, options); }
            catch (Error e) {
                if (operation != "thumbnail" || !options.raw_preview_only) throw e;
                options.raw_preview_only = false;
                input = open_pixels (source, probe, options);
            }
            Vips.Image oriented;
            ImageProcessor.check (Vips.autorot (input, out oriented, null));
            probe.width = oriented.width ();
            probe.height = oriented.height ();
            probe.has_alpha = input.has_alpha ();
            if (options.raw_preview_only && probe.raw) {
                probe.depth = input.format () == 2 || input.format () == 3 ? 16 : 8;
                probe.embedded_preview = true;
                probe.region = false;
                probe.loader = "LibRaw embedded preview";
            }
            if (operation == "decode") update_metadata (source, options, request, probe);
            if (probe.vector && edge > 0) {
                var crop = request.transform;
                input = ImageNative.svg_region (source, crop.crop_x, crop.crop_y, crop.crop_width,
                                                crop.crop_height, probe.width, probe.height, edge);
                request.transform.crop_x = request.transform.crop_y = 0;
                request.transform.crop_width = request.transform.crop_height = 1;
            }
            var image = ImageProcessor.render_image (input, request.transform, edge);
            if (operation == "export") {
                request.export_options.validate ();
                string extension = request.export_options.format == "uhdr" ? "jpg" : request.export_options.format;
                ExportEngine.write (image, output + "." + extension, request.export_options, probe.hdr);
            } else if (operation == "thumbnail") {
                if (probe.hdr) image = ToneMapper.map (image, options.tone_mode);
                Vips.Image srgb;
                ImageProcessor.check (Vips.colourspace (image, out srgb, 22, null));
                ImageNative.tag_srgb (srgb);
                foreach (string field in srgb.fields ())
                    if (field.has_prefix ("exif-") || field.has_prefix ("png-comment-") ||
                        field == "exif-data" || field == "xmp-data" || field == "iptc-data")
                        srgb.remove (field);
                if (request.thumbnail_uri != "" && request.thumbnail_mtime != "") {
                    srgb.set_string ("png-comment-0-Thumb::URI", request.thumbnail_uri);
                    srgb.set_string ("png-comment-1-Thumb::MTime", request.thumbnail_mtime);
                }
                ImageProcessor.check (srgb.write_to_file (output + ".png", null));
            } else {
                if (probe.hdr && options.sdr_presentation) image = ToneMapper.map (image, options.tone_mode);
                ImageNative.write_pixels (image, output);
                if (operation == "decode") FileUtils.set_contents ("/job/image.json", probe.serialize ());
            }
            return 0;
        } catch (Error e) {
            stderr.printf ("%s\n", WorkerFailure.serialize (e));
            return 1;
        }
    }
}
