namespace MajorisImage {
    public class ExportOptions : Object {
        public string format { get; set; default = "png"; }
        public int quality { get; set; default = 90; }
        public int bit_depth { get; set; default = 16; }
        public double scale { get; set; default = 1; }
        public int width { get; set; }
        public int height { get; set; }
        public string profile { get; set; default = "source"; }
        public string metadata { get; set; default = "preserve"; }
        public bool hdr { get; set; }
        public string tone_mode { get; set; default = "automatic"; }
        public bool lossless { get; set; }
        public bool maintain_aspect { get; set; default = true; }
        public string compression { get; set; default = "deflate"; }
        public void validate () throws Error {
            string[] formats = { "jpg", "jpeg", "png", "tif", "tiff", "webp", "avif", "heif", "heic", "jxl", "exr", "uhdr" };
            if (!(format in formats) || quality < 1 || quality > 100 || !(bit_depth in new int[] { 8, 10, 12, 16, 32 }) || !scale.is_finite () || scale <= 0 || scale > 8 || width < 0 || height < 0)
                throw new ImageError.EXPORT (_("Invalid export options."));
            if (hdr && !(format in new string[] { "exr", "tif", "tiff", "avif", "heif", "heic", "jxl", "uhdr" }))
                throw new ImageError.EXPORT (_("The selected format cannot preserve HDR."));
            if (!(compression in new string[] { "none", "deflate", "lzw" }) || (format == "png" && compression == "lzw"))
                throw new ImageError.EXPORT (_("Unsupported compression option."));
        }
    }
}
