namespace ImageKit {
    // Worker-only coefficient transform; publication, source pinning and cancellation
    // remain in EncoderRegistry's existing export transaction.
    internal class LosslessJpeg : Object {
        [CCode (cname = "image_jpeg_transform", cheader_filename = "lossless-jpeg.h")]
        private static extern bool transform_file (string source, string target, int orientation,
                                                    int turns, out int width, out int height) throws Error;
        internal static void write (string source, string target, ExportOptions options,
                                    RenderOptions transform) throws Error {
            options.validate ();
            var normalized = transform.copy ();
            int turns = ((transform.turns % 4) + 4) % 4;
            normalized.turns = 0;
            if (normalized.serialize () != new RenderOptions ().serialize () ||
                options.scale != 1 || options.width != 0 || options.height != 0 ||
                options.hdr || options.profile != "source" || options.metadata != "preserve") {
                throw new ImageError.UNSUPPORTED ("Lossless JPEG export only supports quarter turns with source profile and metadata preservation");
            }
            var metadata = new GExiv2.Metadata ();
            if (!metadata.open_path (source)) throw new ImageError.CORRUPT ("Cannot inspect JPEG metadata");
            int orientation = (int) metadata.try_get_orientation ();
            if (orientation < 1 || orientation > 8) orientation = 1;
            int width, height;
            try {
                transform_file (source, target, orientation, turns, out width, out height);
            } catch (IOError.NOT_SUPPORTED error) {
                throw new ImageError.UNSUPPORTED ("%s", error.message);
            }
            metadata.try_set_orientation (GExiv2.Orientation.NORMAL);
            metadata.try_erase_exif_thumbnail ();
            metadata.try_set_tag_long ("Exif.Photo.PixelXDimension", width);
            metadata.try_set_tag_long ("Exif.Photo.PixelYDimension", height);
            if (metadata.has_tag ("Xmp.tiff.Orientation")) metadata.try_set_tag_long ("Xmp.tiff.Orientation", 1);
            if (!metadata.save_file (target)) throw new ImageError.EXPORT ("Cannot normalize JPEG orientation metadata");
        }
    }
}
