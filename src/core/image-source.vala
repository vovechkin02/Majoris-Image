[CCode (gir_namespace = "ImageKit", gir_version = "2.0")]
namespace ImageKit {
    /** The original file or GVfs URI. The decoder receives a read-only mount. */
    public sealed class ImageSource : Object {
        public File file { get; construct; }
        private bool remove_after_use;
        public ImageSource (File file) {
            Object (file: file);
        }

        /** Stage an input stream without closing the caller's stream. */
        public static async ImageSource from_stream (InputStream stream,
            Cancellable? cancel = null) throws Error {
            FileIOStream storage;
            var file = File.new_tmp ("majoris-image-source-XXXXXX", out storage);
            try {
                size_t total = 0;
                while (true) {
                    var bytes = yield stream.read_bytes_async (256 * 1024, Priority.DEFAULT, cancel);
                    if (bytes.get_size () == 0) {
                        break;
                    }
                    total += bytes.get_size ();
                    if (total > 1073741824) {
                        throw new ImageError.MEMORY ("Image stream exceeds 1 GiB");
                    }
                    size_t written;
                    yield storage.output_stream.write_all_async (bytes.get_data (),
                        Priority.DEFAULT, cancel, out written);
                }
                yield storage.close_async (Priority.DEFAULT, cancel);
                var source = new ImageSource (file);
                source.remove_after_use = true;
                return source;
            } catch (Error e) {
                FileUtils.unlink (file.get_path ());
                throw e;
            }
        }

        public static async ImageSource from_bytes (Bytes bytes, Cancellable? cancel = null) throws Error {
            return yield from_stream (new MemoryInputStream.from_bytes (bytes), cancel);
        }

        internal async string stamp (Cancellable stop) throws Error {
            var info = yield file.query_info_async (ThumbnailService.STAMP_ATTRIBUTES, 0,
                Priority.DEFAULT, stop);
            if (info.get_file_type () != FileType.REGULAR) {
                throw new IOError.INVALID_ARGUMENT ("The image source must be a regular file");
            }
            return ThumbnailService.source_stamp (file, info);
        }

        ~ImageSource () {
            if (remove_after_use) {
                FileUtils.unlink (file.get_path ());
            }
        }
    }
}
