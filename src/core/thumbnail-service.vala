namespace MajorisImage {
    /** Headless PNG generation and a shared, private disk cache. */
    public sealed class ThumbnailService : Object {
        internal const string STAMP_ATTRIBUTES = "standard::type,standard::size,etag::value,id::file,time::modified,time::modified-usec,time::changed,time::changed-usec";
        private const int MAX_PNG = 8 * 1024 * 1024;
        private static int64 next_eviction;
        private WorkerClient worker = new WorkerClient ();

        public async Bytes generate (ImageSource source, int edge, Cancellable? cancel = null) throws Error {
            return yield generate_page (source, edge, 0, cancel);
        }

        public async Bytes generate_page (ImageSource source, int edge, int page,
                                          Cancellable? cancel = null) throws Error {
            if (edge < 1 || edge > 1024 || page < 0)
                throw new IOError.INVALID_ARGUMENT ("Invalid thumbnail size or page");
            var stop = cancel ?? new Cancellable ();
            var info = yield source.file.query_info_async (STAMP_ATTRIBUTES, 0, Priority.LOW, stop);
            string key = source_stamp (source.file, info);
            var directory = File.new_for_path (Path.build_filename (Environment.get_user_cache_dir (), "majoris-image", "thumbnails-v2"));
            File? saved = null;
            if (key != "" && DirUtils.create_with_parents (directory.get_path (), 0700) == 0) {
                string cache_key = Checksum.compute_for_string (ChecksumType.SHA256, "%s:%d:%d".printf (key, edge, page));
                saved = directory.get_child (cache_key + ".png");
                try {
                    var cached = yield WorkerClient.read_output (saved.get_path (), MAX_PNG, stop);
                    if (valid_png (cached, edge)) return cached;
                } catch (IOError.CANCELLED e) { throw e; }
                catch (Error e) { debug ("Thumbnail cache miss: %s", e.message); }
            }

            var options = new DecodeOptions ();
            options.page = page;
            string output = yield worker.run (source.file, "thumbnail", null, stop, edge, options);
            try {
                var png = yield WorkerClient.read_output (output + ".png", MAX_PNG, stop);
                if (!valid_png (png, edge)) throw new ImageError.INVALID_RESPONSE ("Invalid PNG thumbnail");
                if (saved != null) {
                    try {
                        var current = yield source.file.query_info_async (STAMP_ATTRIBUTES, 0, Priority.LOW, stop);
                        if (key == source_stamp (source.file, current))
                            yield saved.replace_contents_bytes_async (png, null, false,
                                FileCreateFlags.PRIVATE | FileCreateFlags.REPLACE_DESTINATION, stop, null);
                    } catch (IOError.CANCELLED e) { throw e; }
                    catch (Error e) { debug ("Thumbnail cache write: %s", e.message); }
                    bool maintain = false;
                    lock (next_eviction) {
                        if (next_eviction <= get_monotonic_time ()) {
                            next_eviction = get_monotonic_time () + 5 * TimeSpan.MINUTE;
                            maintain = true;
                        }
                    }
                    if (maintain) evict.begin (directory);
                }
                return png;
            } finally { WorkerClient.cleanup (output); }
        }

        internal static string source_stamp (File file, FileInfo info) {
            if (info.get_file_type () != FileType.REGULAR ||
                (info.get_etag () == null && !info.has_attribute (FileAttribute.TIME_MODIFIED))) return "";
            return "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s".printf (
                file.get_uri (), info.get_etag () ?? "", info.get_attribute_string (FileAttribute.ID_FILE) ?? "",
                info.get_size ().to_string (), info.get_attribute_uint64 (FileAttribute.TIME_MODIFIED).to_string (),
                info.get_attribute_uint32 (FileAttribute.TIME_MODIFIED_USEC).to_string (),
                info.get_attribute_uint64 (FileAttribute.TIME_CHANGED).to_string (),
                info.get_attribute_uint32 (FileAttribute.TIME_CHANGED_USEC).to_string ());
        }

        [CCode (cname = "majoris_image_thumbnail_valid")]
        private extern static bool valid_png (Bytes png, int edge);

        private static async void evict (File directory) {
            try {
                var entries = new Gee.ArrayList<FileInfo> ();
                var children = yield directory.enumerate_children_async ("standard::name,standard::type,standard::size,time::modified",
                    FileQueryInfoFlags.NOFOLLOW_SYMLINKS, Priority.LOW, null);
                int64 total = 0;
                while (true) {
                    var batch = yield children.next_files_async (128, Priority.LOW, null);
                    if (batch == null) break;
                    foreach (var info in batch) {
                        if (info.get_file_type () != FileType.REGULAR || info.get_name ().length != 68 ||
                            !info.get_name ().has_suffix (".png")) continue;
                        entries.add (info);
                        total += info.get_size ();
                    }
                }
                yield children.close_async (Priority.LOW, null);
                entries.sort ((a, b) => {
                    uint64 first = a.get_attribute_uint64 (FileAttribute.TIME_MODIFIED);
                    uint64 second = b.get_attribute_uint64 (FileAttribute.TIME_MODIFIED);
                    return first < second ? -1 : first > second ? 1 : 0;
                });
                int64 oldest = new DateTime.now_utc ().to_unix () - 30 * 86400;
                int remaining = entries.size;
                foreach (var info in entries) {
                    if (total <= 256 * 1024 * 1024 && remaining <= 4096 &&
                        (int64) info.get_attribute_uint64 (FileAttribute.TIME_MODIFIED) >= oldest) break;
                    try {
                        yield directory.get_child (info.get_name ()).delete_async (Priority.LOW, null);
                        total -= info.get_size ();
                    } catch (Error e) { debug ("Thumbnail eviction: %s", e.message); }
                    remaining--;
                }
            } catch (Error e) { debug ("Thumbnail cache maintenance: %s", e.message); }
        }
    }
}
