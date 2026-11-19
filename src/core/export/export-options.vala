namespace ImageKit {
    /** Validated target-format, quality, color and metadata settings for export. */
    public class ExportOptions : Object {
        /** Requested output format identifier or filename extension. */
        public string format { get; set; default = "png"; }
        /** Output encoder quality setting; exact interpretation is codec-specific. */
        public int quality { get; set; default = 90; }
        /** Bit depth of the represented buffer or target export, according to its owning type. */
        public int bit_depth { get; set; default = 16; }
        /** Output resizing factor; 1 keeps the source scale. */
        public double scale { get; set; default = 1; }
        /** Image width in source pixels, or requested output pixels when set on export options. */
        public int width { get; set; }
        /** Image height in source pixels, or requested output pixels when set on export options. */
        public int height { get; set; }
        /** Output color-profile policy or target profile identifier. */
        public string profile { get; set; default = "source"; }
        /** Image metadata object or serialized metadata content, according to the owning type. */
        public string metadata { get; set; default = "preserve"; }
        /** Whether the source or requested output uses HDR image data, per its owning type. */
        public bool hdr { get; set; }
        /** Tone-mapping mode selected for decoding or export. */
        public string tone_mode { get; set; default = "automatic"; }
        /** Request lossless output where the selected encoder supports it. */
        public bool lossless { get; set; }
        /** Whether output resizing preserves the source aspect ratio. */
        public bool maintain_aspect { get; set; default = true; }
        /** Output compression setting; supported values depend on the format. */
        public string compression { get; set; default = "deflate"; }
        /**
         * Rejects invalid options, unsupported combinations or out-of-range values. Errors
         * propagate to the caller.
         */
        public void validate () throws Error {
            string[] formats = { "jpg", "jpeg", "png", "tif", "tiff", "webp", "avif", "heif",
                "heic", "jxl", "exr", "uhdr" };
            if (!(format in formats) || quality < 1 || quality > 100 || !(bit_depth in new int[] {
                8, 10, 12, 16, 32
            }) || !scale.is_finite () || scale <= 0 || scale > 8 || width < 0 || height < 0) {
                throw new ImageError.EXPORT (_("Invalid export options."));
            }
            if (hdr && !(format in new string[] {
                "exr", "tif", "tiff", "avif", "heif", "heic", "jxl", "uhdr"
            })) {
                throw new ImageError.EXPORT (_("The selected format cannot preserve HDR."));
            }
            if (!(compression in new string[] {
                "none", "deflate", "lzw"
            }) || (format == "png" && compression == "lzw")) {
                throw new ImageError.EXPORT (_("Unsupported compression option."));
            }
        }
    }
}
