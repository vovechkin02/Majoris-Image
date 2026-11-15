namespace ImageKit {
    public class ExportEngine : Object {
        public static void write (Vips.Image input, string target, ExportOptions options, bool source_hdr) throws Error {
            Vips.Image image = input;
            options.validate ();
            if (options.width > 0 || options.height > 0 || options.scale != 1) {
                bool alpha=image.has_alpha ();
                if (alpha) { Vips.Image premultiplied; ImageProcessor.check (Vips.premultiply (image,out premultiplied,"max_alpha",1.0,null)); image=premultiplied; }
                double scale = options.width > 0 ? (double) options.width / image.width () : options.height > 0 ? (double) options.height / image.height () : options.scale;
                if (options.width > 0 && options.height > 0) scale = double.min (scale, (double) options.height / image.height ());
                Vips.Image resized;
                if (!options.maintain_aspect && options.width > 0 && options.height > 0)
                    ImageProcessor.check (Vips.resize (image, out resized, (double)options.width/image.width (), "vscale", (double)options.height/image.height (), null));
                else ImageProcessor.check (Vips.resize (image, out resized, scale, null));
                image = resized;
                if (alpha) { Vips.Image straight; ImageProcessor.check (Vips.unpremultiply (image,out straight,"max_alpha",1.0,null)); image=straight; }
            }
            if (!options.hdr && source_hdr) image = ToneMapper.map (image, options.tone_mode);
            // Privacy policy is independent of the color profile. Remove opaque
            // source blobs before encoding; filtered metadata is reattached below.
            foreach (string field in image.fields ()) {
                if (options.metadata == "remove-all" && (field.has_prefix ("exif-") || field.has_prefix ("xmp-") || field == "exif-data" || field == "xmp-data" || field == "iptc-data")) image.remove (field);
            }
            string format = options.format;
            if (options.hdr) {
                if (format == "uhdr" || format == "avif" || format == "heif" || format == "heic") {
                    ImageNative.ultra_encode (image, target, format == "avif" ? 2 : format == "uhdr" ? 0 : 1, options.quality); return;
                }
                if (format == "exr") { ImageNative.exr_encode (image, target, options.bit_depth); return; }
                if (format == "jxl") { ImageNative.jxl_encode (image, target, options.bit_depth, options.quality, options.lossless); return; }
                if (format == "tif" || format == "tiff") {
                    if (options.bit_depth != 32) throw new ImageError.EXPORT (_("Linear HDR TIFF requires 32-bit floating-point output."));
                    ImageNative.tag_linear (image);
                    ImageProcessor.check (image.write_to_file (target, "compression", options.compression == "none" ? 0 : options.compression == "lzw" ? 5 : 2, null)); return;
                }
                throw new ImageError.EXPORT (_("This encoder has no verified HDR output path."));
            }
            bool eight_only = format == "jpg" || format == "jpeg" || format == "webp";
            if (eight_only && options.bit_depth != 8) throw new ImageError.EXPORT (_("This format supports 8-bit output. Select 8-bit explicitly."));
            bool heif = format == "avif" || format == "heif" || format == "heic";
            if (heif ? !(options.bit_depth in new int[] { 8, 10, 12 }) : !(options.bit_depth in new int[] { 8, 16 })) throw new ImageError.EXPORT (_("The selected precision is unavailable for this encoder."));
            if (!ImageNative.encoder_available (format)) throw new ImageError.EXPORT (_("The selected encoder is not installed."));
            var encoded = ImageNative.convert_profile (image, options.profile, options.bit_depth > 8 ? 16 : 8);
            // These describe the internal working image, not encoded pixels.
            foreach (string field in encoded.fields ()) if (field.has_prefix ("majoris-image-")) encoded.remove (field);
            if (heif) ImageProcessor.check (encoded.write_to_file (target, "Q", options.quality, "bitdepth", options.bit_depth, "lossless", options.lossless, null));
            else if (format == "webp" || format == "jxl") ImageProcessor.check (encoded.write_to_file (target, "Q", options.quality, "lossless", options.lossless, null));
            else if (format == "png") ImageProcessor.check (encoded.write_to_file (target, "compression", options.compression == "none" ? 0 : 6, null));
            else if (format == "tif" || format == "tiff") ImageProcessor.check (encoded.write_to_file (target, "compression", options.compression == "none" ? 0 : options.compression == "lzw" ? 5 : 2, null));
            else if (format == "jpg" || format == "jpeg")
                ImageProcessor.check (encoded.write_to_file (target, "Q", options.quality, null));
            else ImageProcessor.check (encoded.write_to_file (target, null));
            if (options.metadata == "remove-gps" || options.metadata == "remove-identifying") {
                var metadata = new GExiv2.Metadata ();
                if (!metadata.open_path (target)) throw new ImageError.EXPORT (_("Cannot validate exported metadata."));
                metadata.try_delete_gps_info ();
                if (options.metadata == "remove-identifying") {
                    foreach (string tag in metadata.get_exif_tags ()) {
                        string name=tag.down ();
                        if (name.contains ("serial") || name.contains ("owner") || name.contains ("artist") || name.contains ("copyright") || name.contains ("usercomment") || name.contains ("makernote")) metadata.try_clear_tag (tag);
                    }
                    foreach (string tag in metadata.get_xmp_tags ()) metadata.try_clear_tag (tag);
                    foreach (string tag in metadata.get_iptc_tags ()) metadata.try_clear_tag (tag);
                }
                if (!metadata.save_file (target)) throw new ImageError.EXPORT (_("Cannot write the selected metadata policy."));
            }
        }
    }
}
