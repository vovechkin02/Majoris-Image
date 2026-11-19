namespace ImageKit {
    namespace WorkerDispatcher {
        /**
         * Starts the requested sandboxed worker operation with cancellation and resource limits.
         */
        internal void run (string operation, string source, string output, string request_path,
                           string edge_argument) throws Error {
            if (operation == "codecs") {
                FileUtils.set_contents (output, ImageNative.codec_catalog_json ());
                return;
            }
            string json;
            FileUtils.get_contents (request_path, out json);
            var request = WorkerRequest.deserialize (json);
            var options = request.options;
            int edge = int.parse (edge_argument);
            if (edge < 0 || edge > 4096) {
                throw new IOError.INVALID_ARGUMENT ("Invalid output edge");
            }
            if (operation == "render-buffer") {
                render_buffer (source, output, request, edge);
                return;
            }
            if (operation == "colour-probe") {
                FileUtils.set_contents (output, MetadataReader.color_probe (source));
                return;
            }
            // Perfect JPEG transforms operate on coefficients before any pixel decode.
            if (operation == "export" && request.export_options.lossless &&
                request.export_options.format in new string[] { "jpeg", "jpg" }) {
                LosslessJpeg.write (source, output + "." + request.export_options.format,
                                    request.export_options, request.transform);
                return;
            }
            Vips.Image? prepared;
            var probe = ImageInspector.inspect_and_prepare_pixels (source, request.hdr_hint, options.page,
                out prepared, request.sequence_frames);
            if (operation == "probe") {
                MetadataReader.update_probe_metadata (source, options, request.hdr_hint, probe, prepared);
                FileUtils.set_contents (output, probe.serialize ());
                return;
            }
            if (operation != "thumbnail" && operation != "decode" && operation != "tile" &&
                operation != "export") {
                throw new IOError.INVALID_ARGUMENT ("Unknown decoder operation");
            }
            process_source (operation, source, output, request, edge, probe, prepared);
        }

        /** Produces a bounded worker pixel-buffer representation. */
        private void render_buffer (string source, string output, WorkerRequest request, int edge)
                                    throws Error {
            var image = ImageProcessor.render_image (ImageNative.read_pixels (source),
                request.transform, edge);
            if (request.options.sdr_presentation) {
                image = ToneMapper.map (image, request.options.tone_mode);
            }
            ImageNative.write_pixels (image, output);
        }

        /** Loads the requested source inside the validated worker environment. */
        private void process_source (string operation, string source, string output,
                                     WorkerRequest request, int edge, ImageProbe probe,
                                     Vips.Image? prepared) throws Error {
            var options = request.options;
            if (operation == "thumbnail" && probe.raw) {
                options.raw_preview_only = options.raw_preview_first;
            }
            Vips.Image input;
            try {
                input = prepared ?? ImageInspector.open_pixels (source, probe, options);
            } catch (Error e) {
                if (operation != "thumbnail" || !options.raw_preview_only) {
                    throw e;
                }
                options.raw_preview_only = false;
                input = ImageInspector.open_pixels (source, probe, options);
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
            if (operation == "decode") {
                MetadataReader.update_probe_metadata (source, options, request.hdr_hint, probe, prepared);
            }
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
                var extension = request.export_options.format == "uhdr" ? "jpg"
                    : request.export_options.format;
                ExportEngine.write (image, output + "." + extension, request.export_options, probe.hdr);
            } else if (operation == "thumbnail") {
                write_thumbnail (image, output, request, probe.hdr);
            } else {
                if (probe.hdr && options.sdr_presentation) {
                    image = ToneMapper.map (image, options.tone_mode);
                }
                ImageNative.write_pixels (image, output);
                if (operation == "decode") {
                    FileUtils.set_contents ("/job/image.json", probe.serialize ());
                }
            }
        }

        /** Produces a thumbnail in the desktop cache-compatible format. */
        private void write_thumbnail (Vips.Image input, string output, WorkerRequest request,
                                      bool hdr) throws Error {
            var image = hdr ? ToneMapper.map (input, request.options.tone_mode) : input;
            Vips.Image srgb;
            ImageProcessor.check (Vips.colorspace (image, out srgb, 22, null));
            ImageNative.tag_srgb (srgb);
            foreach (var field in srgb.fields ()) {
                if (field.has_prefix ("exif-") || field.has_prefix ("png-comment-") ||
                        field == "exif-data" || field == "xmp-data" || field == "iptc-data") {
                    srgb.remove (field);
                }
            }
            if (request.thumbnail_uri != "" && request.thumbnail_mtime != "") {
                srgb.set_string ("png-comment-0-Thumb::URI", request.thumbnail_uri);
                srgb.set_string ("png-comment-1-Thumb::MTime", request.thumbnail_mtime);
            }
            ImageProcessor.check (srgb.write_to_file (output + ".png", null));
        }
    }
}
