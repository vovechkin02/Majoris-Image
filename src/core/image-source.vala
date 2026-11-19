[CCode (gir_namespace = "ImageKit", gir_version = "2.0")]
namespace ImageKit {
    /** The original file or GVfs URI. The decoder receives a read-only mount. */
    public sealed class ImageSource : Object {
        /** Underlying GIO file identifying the image source. */
        public File file { get; construct; }
        // The last ImageSource reference releases storage owned by stream and memory sources.
        private bool remove_after_use;
        /** Creates a source wrapper around a GIO file. */
        public ImageSource (File file) {
            Object (file: file);
        }

        /** Stage an input stream without closing the caller's stream. */
        public static async ImageSource from_stream (InputStream stream,
            Cancellable? cancel = null) throws Error {
            FileIOStream storage;
            var file = File.new_tmp ("majoris-image-source-XXXXXX", out storage);
            try {
                int64 staging = OperationProfile.begin ();
                size_t total = 0;
                while (true) {
                    var bytes = yield stream.read_bytes_async (
                        256 * 1024, Priority.DEFAULT, cancel);
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
                cancel?.set_error_if_cancelled ();
                yield storage.close_async (Priority.DEFAULT, null);
                cancel?.set_error_if_cancelled ();
                OperationProfile.record ("stream-staging", staging, total);
                var source = new ImageSource (file);
                source.remove_after_use = true;
                return source;
            } catch (Error e) {
                if (!storage.is_closed ()) {
                    try {
                        yield storage.close_async (Priority.DEFAULT, null);
                    } catch (Error close_error) {
                        debug ("Cannot close image source storage: %s", close_error.message);
                    }
                }
                FileUtils.unlink (file.get_path ());
                throw e;
            }
        }

        /** Creates a source from a finite in-memory buffer. Errors propagate to the caller. */
        public static async ImageSource from_bytes (Bytes bytes,
            Cancellable? cancel = null) throws Error {
            return yield from_stream (new MemoryInputStream.from_bytes (bytes), cancel);
        }

        /** Rejects files that are unsuitable for safe native decoder access. */
        internal async void validate_regular (Cancellable stop) throws Error {
            yield query_regular_info (FileAttribute.STANDARD_TYPE, stop);
        }

        /** Returns a source-identity stamp for detecting changes during operations. */
        internal async string stamp (Cancellable stop) throws Error {
            var info = yield query_regular_info (ThumbnailService.STAMP_ATTRIBUTES, stop);
            int64 started = OperationProfile.begin ();
            var stamp = ThumbnailService.source_stamp (file, info);
            OperationProfile.record ("source-stamp", started);
            return stamp;
        }

        /** Queries regular-file identity and type for source validation. */
        private async FileInfo query_regular_info (string attributes, Cancellable stop)
            throws Error {
            int64 started = OperationProfile.begin ();
            FileInfo info = null;
            try {
                info = yield file.query_info_async (attributes, 0,
                    Priority.DEFAULT, stop);
            } finally {
                OperationProfile.record ("source-query", started);
            }
            if (info.get_file_type () != FileType.REGULAR) {
                throw new IOError.INVALID_ARGUMENT ("The image source must be a regular file");
            }
            return info;
        }

        ~ImageSource () {
            if (remove_after_use) {
                FileUtils.unlink (file.get_path ());
            }
        }
    }
}
