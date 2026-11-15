namespace ImageKit {
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
                var cached = yield read_cached (saved, edge, stop);
                if (cached != null) {
                    if (key != (yield source.stamp (stop)))
                        throw new ImageError.SOURCE_CHANGED ("Image changed while reading its thumbnail");
                    return cached;
                }
            }

            int fd = -1;
            try {
                if (saved != null) {
                    // Stable striped lock files avoid unbounded inode growth and unlink races.
                    string lock_path = Path.build_filename (directory.get_path (), saved.get_basename ().substring (0, 3) + ".lock");
                    while ((fd = try_lock (lock_path)) == -2) {
                        stop.set_error_if_cancelled ();
                        var wait = new TimeoutSource (25);
                        wait.set_callback (() => { generate_page.callback (); return Source.REMOVE; });
                        wait.attach (MainContext.get_thread_default ());
                        yield;
                    }
                    stop.set_error_if_cancelled ();
                    if (fd < 0) saved = null;
                    else {
                        if (key != (yield source.stamp (stop)))
                            throw new ImageError.SOURCE_CHANGED ("Image changed while waiting for its thumbnail");
                        var cached = yield read_cached (saved, edge, stop);
                        if (cached != null) return cached;
                    }
                }

                var options = new DecodeOptions ();
                options.page = page;
                Bytes? png = null;
                try {
                    string output = yield worker.run (source.file, "thumbnail", null, stop, edge, options);
                    try { png = yield WorkerClient.read_output (output + ".png", MAX_PNG, stop); }
                    finally { WorkerClient.cleanup (output); }
                } catch (Error error) {
#if HAVE_GLYCIN
                    if (!GlycinFallback.eligible (error)) throw error;
                    options.preview_size = edge;
                    var fallback = yield GlycinFallback.open (source, options, stop, error);
                    string mtime = info.has_attribute (FileAttribute.TIME_MODIFIED) ?
                        info.get_attribute_uint64 (FileAttribute.TIME_MODIFIED).to_string () : "";
                    png = encode_srgb_png (fallback.preview.bytes, fallback.preview.width, fallback.preview.height,
                        source.file.get_uri (), mtime, stop);
#else
                    throw error;
#endif
                }
                if (png == null || !valid_png (png, edge)) throw new ImageError.INVALID_RESPONSE ("Invalid PNG thumbnail");

                if (saved != null) {
                    if (key != (yield source.stamp (stop)))
                        throw new ImageError.SOURCE_CHANGED ("Image changed during thumbnail generation");
                    try { yield publish (saved.get_path (), png, stop); }
                    catch (IOError.CANCELLED e) { throw e; }
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
            } finally { if (fd >= 0) release_lock (fd); }
        }

        private static async Bytes? read_cached (File saved, int edge, Cancellable stop) throws Error {
            try {
                var value = yield WorkerClient.read_output (saved.get_path (), MAX_PNG, stop);
                if (valid_png (value, edge)) return value;
            } catch (IOError.CANCELLED e) { throw e; }
            catch (Error e) { debug ("Thumbnail cache miss: %s", e.message); }
            return null;
        }

        [CCode (cname = "image_cache_try_lock")]
        private extern static int try_lock (string path);
        [CCode (cname = "image_cache_unlock")]
        private extern static void release_lock (int fd);
        [CCode (cname = "image_cache_publish_async", finish_name = "image_cache_publish_finish")]
        private extern static async void publish (string path, Bytes png, Cancellable stop) throws Error;

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

#if HAVE_GLYCIN
        [CCode (cname = "image_glycin_png")]
        private extern static Bytes encode_srgb_png (Bytes pixels, int width, int height,
                                                      string uri, string mtime, Cancellable stop) throws Error;
#endif

        [CCode (cname = "image_thumbnail_valid")]
        private extern static bool valid_png (Bytes png, int edge);

        private static async void evict (File directory) {
            try {
                var entries = new Gee.ArrayList<FileInfo> ();
                var children = yield directory.enumerate_children_async ("standard::name,standard::type,standard::size,id::file,time::modified,time::modified-usec",
                    FileQueryInfoFlags.NOFOLLOW_SYMLINKS, Priority.LOW, null);
                int64 total = 0;
                while (true) {
                    var batch = yield children.next_files_async (128, Priority.LOW, null);
                    if (batch == null) break;
                    foreach (var info in batch) {
                        if (info.get_file_type () != FileType.REGULAR) continue;
                        if (info.get_name ().length == 76 && info.get_name ().has_suffix (".png.pending")) {
                            int fd = try_lock (Path.build_filename (directory.get_path (), info.get_name ().substring (0, 3) + ".lock"));
                            if (fd >= 0) {
                                try { yield directory.get_child (info.get_name ()).delete_async (Priority.LOW, null); }
                                catch (Error e) { debug ("Unpublished thumbnail cleanup: %s", e.message); }
                                finally { release_lock (fd); }
                            }
                            continue;
                        }
                        if (info.get_name ().length != 68 || !info.get_name ().has_suffix (".png")) continue;
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
                    int fd = try_lock (Path.build_filename (directory.get_path (), info.get_name ().substring (0, 3) + ".lock"));
                    if (fd < 0) continue;
                    try {
                        var entry = directory.get_child (info.get_name ());
                        var current = yield entry.query_info_async ("standard::size,id::file,time::modified,time::modified-usec",
                            FileQueryInfoFlags.NOFOLLOW_SYMLINKS, Priority.LOW, null);
                        if (current.get_attribute_string (FileAttribute.ID_FILE) != info.get_attribute_string (FileAttribute.ID_FILE) ||
                            current.get_attribute_uint64 (FileAttribute.TIME_MODIFIED) != info.get_attribute_uint64 (FileAttribute.TIME_MODIFIED) ||
                            current.get_attribute_uint32 (FileAttribute.TIME_MODIFIED_USEC) != info.get_attribute_uint32 (FileAttribute.TIME_MODIFIED_USEC))
                            continue;
                        yield entry.delete_async (Priority.LOW, null);
                        total -= current.get_size ();
                    } catch (Error e) { debug ("Thumbnail eviction: %s", e.message); }
                    finally { release_lock (fd); }
                    remaining--;
                }
            } catch (Error e) { debug ("Thumbnail cache maintenance: %s", e.message); }
        }
    }
}
