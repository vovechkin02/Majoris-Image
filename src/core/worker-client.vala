namespace MajorisImage {
    internal sealed class WorkerClient : Object {
        [CCode (cname = "majoris_image_worker_path")]
        private extern static unowned string installed_worker ();

        private string? backend;
        private uint timeout_seconds;
        internal WorkerClient (string? backend = null, uint timeout_seconds = 120) {
            this.backend = backend;
            this.timeout_seconds = timeout_seconds.clamp (1, 120);
        }
        private string executable () {
            return backend ?? Environment.get_variable ("MAJORIS_IMAGE_WORKER") ?? installed_worker ();
        }

        private async string read_diagnostics (InputStream stream) throws Error {
            var log = new ByteArray ();
            while (true) {
                var bytes = yield stream.read_bytes_async (16384, Priority.DEFAULT, null);
                if (bytes.get_size () == 0) break;
                log.append (bytes.get_data ());
                if (log.len > 65536) log.remove_range (0, log.len - 65536);
            }
            log.append ({0});
            return ((string) log.data).make_valid ();
        }

        internal async string run (File? source, string operation, RenderOptions? transform,
                                   Cancellable cancel, int size = 0, DecodeOptions? options = null, ExportOptions? encoding = null, bool preload = false) throws Error {
            if (size < 0 || size > 4096)
                throw new IOError.INVALID_ARGUMENT ("Image output edge must be at most 4096 pixels");
            int priority = preload ? 3 : operation == "thumbnail" ? 2 : operation == "tile" ? 1 : 0;
            var scheduler = JobScheduler.shared ();
            yield scheduler.acquire (priority, cancel);
            try { return yield execute (source, operation, transform, cancel, size, options, encoding); }
            finally { scheduler.release (); }
        }

        private async string execute (File? source, string operation, RenderOptions? transform,
                                      Cancellable cancel, int size, DecodeOptions? options, ExportOptions? encoding) throws Error {
            string job = DirUtils.make_tmp ("majoris-image-XXXXXX");
            string output = Path.build_filename (job, "output");
            string request_path = Path.build_filename (job, "request.json");
            try {
                string? input = source?.get_path ();
                string mtime = "";
                if (source != null) {
                    var info = yield source.query_info_async ("standard::type,standard::size,time::modified", 0, Priority.DEFAULT, cancel);
                    if (info.get_file_type () != FileType.REGULAR)
                        throw new IOError.INVALID_ARGUMENT ("The image source must be a regular file");
                    if (info.has_attribute (FileAttribute.TIME_MODIFIED))
                        mtime = info.get_attribute_uint64 (FileAttribute.TIME_MODIFIED).to_string ();
                    if (input == null) {
                        input = Path.build_filename (job, "input");
                        var stream = yield source.read_async (Priority.DEFAULT, cancel);
                        var target = yield File.new_for_path (input).create_async (FileCreateFlags.PRIVATE, Priority.DEFAULT, cancel);
                        size_t total = 0;
                        while (true) {
                            var bytes = yield stream.read_bytes_async (256 * 1024, Priority.DEFAULT, cancel);
                            if (bytes.get_size () == 0) break;
                            total += bytes.get_size ();
                            if (total > 1073741824) throw new ImageError.MEMORY ("Remote source exceeds 1 GiB");
                            size_t written;
                            yield target.write_all_async (bytes.get_data (), Priority.DEFAULT, cancel, out written);
                        }
                        yield target.close_async (Priority.DEFAULT, cancel);
                    }
                }
                var request = new WorkerRequest ();
                request.transform = transform ?? new RenderOptions ();
                request.options = options ?? new DecodeOptions ();
                request.hdr_hint = request.options.source_hdr;
                request.sequence_frames = request.options.sequence_frames;
                if (operation == "thumbnail" && source != null) {
                    request.thumbnail_uri = source.get_uri ();
                    request.thumbnail_mtime = mtime;
                }
                request.export_options = encoding ?? new ExportOptions ();
                FileUtils.set_contents (request_path, request.serialize ());

                string[] argv = { "bwrap", "--unshare-all", "--die-with-parent", "--new-session", "--cap-drop", "ALL",
                    "--ro-bind", "/usr", "/usr", "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64",
                    "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--dir", "/etc",
                    "--ro-bind-try", "/etc/ld.so.cache", "/etc/ld.so.cache", "--ro-bind-try", "/etc/fonts", "/etc/fonts",
                    "--ro-bind", executable (), "/worker", "--bind", job, "/job", "--chdir", "/job", "--clearenv",
                    "--setenv", "PATH", "/usr/bin", "--setenv", "HOME", "/tmp",
                    "--setenv", "OMP_NUM_THREADS", "2", "--setenv", "OMP_THREAD_LIMIT", "2" };
                if (input != null) {
                    argv += "--ro-bind"; argv += input; argv += "/source";
                }
                foreach (string arg in new string[] { "/usr/bin/prlimit", "--as=2147483648", "--cpu=90",
                          "--nofile=128", "--fsize=1073741824", "--core=0",
                          "/worker", operation, "/source", "/job/output", "/job/request.json", size.to_string () })
                    argv += arg;
                Subprocess process;
                try { process = new Subprocess.newv (argv, SubprocessFlags.STDOUT_SILENCE | SubprocessFlags.STDERR_PIPE); }
                catch (Error e) { throw new ImageError.SANDBOX ("Cannot start sandbox: %s", e.message); }
                ulong handler = cancel.connect (() => process.force_exit ());
                bool timed_out = false;
                var timeout = new TimeoutSource.seconds (timeout_seconds);
                timeout.set_callback (() => { timed_out = true; process.force_exit (); return Source.REMOVE; });
                timeout.attach (MainContext.get_thread_default ());
                string diagnostic = "";
                try {
                    diagnostic = yield read_diagnostics (process.get_stderr_pipe ());
                    yield process.wait_async (null);
                } catch (Error e) {
                    process.force_exit ();
                    yield process.wait_async (null);
                    throw e;
                } finally {
                    cancel.disconnect (handler);
                    timeout.destroy ();
                }
                cancel.set_error_if_cancelled ();
                if (timed_out) throw new ImageError.TIMEOUT ("Image decoder exceeded its time limit");
                if (process.get_if_signaled ()) throw new ImageError.DECODER_CRASH ("Image decoder stopped unexpectedly");
                if (!process.get_successful ()) {
                    int status = process.get_exit_status ();
                    if (status == 139 || status == 134) throw new ImageError.DECODER_CRASH ("Image decoder stopped unexpectedly");
                    if (status == 137 || status == 152 || status == 153) throw new ImageError.MEMORY ("Decoder exceeded its resource budget");
                    if (status == 127 && diagnostic.contains ("shared libraries"))
                        throw new ImageError.CODEC_MISSING ("A required decoder library is missing");
                    if (diagnostic.has_prefix ("bwrap:") || diagnostic.has_prefix ("prlimit:"))
                        throw new ImageError.SANDBOX ("%s", diagnostic.strip ());
                    throw WorkerFailure.deserialize (diagnostic);
                }
                return output;
            } catch (Error e) {
                cleanup (output);
                throw e;
            }
        }

        internal static void cleanup (string output) {
            string dir = Path.get_dirname (output);
            try {
                var files = Dir.open (dir);
                string? name;
                while ((name = files.read_name ()) != null)
                    remove_tree (Path.build_filename (dir, name));
            } catch (Error e) { debug ("Decoder directory cleanup: %s", e.message); }
            DirUtils.remove (dir);
        }

        private static void remove_tree (string path) {
            if (FileUtils.remove (path) == 0 || FileUtils.test (path, FileTest.IS_SYMLINK)) return;
            if (!FileUtils.test (path, FileTest.IS_DIR)) return;
            try {
                var entries = Dir.open (path);
                string? name;
                while ((name = entries.read_name ()) != null) remove_tree (Path.build_filename (path, name));
                DirUtils.remove (path);
            } catch (Error e) { debug ("Cannot clean decoder output: %s", e.message); }
        }

        internal static async Bytes read_output (string path, int64 limit, Cancellable stop) throws Error {
            var file = File.new_for_path (path);
            FileInfo info;
            try {
                info = yield file.query_info_async ("standard::type,standard::size", FileQueryInfoFlags.NOFOLLOW_SYMLINKS,
                                                    Priority.DEFAULT, stop);
            } catch (IOError.NOT_FOUND e) {
                throw new ImageError.INVALID_RESPONSE ("Decoder did not produce an output file");
            }
            if (info.get_file_type () != FileType.REGULAR || info.get_size () < 1 || info.get_size () > limit)
                throw new ImageError.INVALID_RESPONSE ("Invalid decoder response type or size");
            return yield file.load_bytes_async (stop, null);
        }

        internal static async string read_text (string path, Cancellable stop) throws Error {
            var bytes = yield read_output (path, 1024 * 1024, stop);
            unowned uint8[] data = bytes.get_data ();
            uint8[] text = new uint8[data.length + 1];
            Memory.copy (text, data, data.length);
            if (!((string) text).validate (data.length))
                throw new ImageError.INVALID_RESPONSE ("Invalid decoder UTF-8 response");
            return (string) text;
        }

        internal static async PixelBuffer read_pixels (string path, Cancellable stop) throws Error {
            var bytes = yield read_output (path, 268435468, stop);
            unowned uint8[] data = bytes.get_data ();
            if (data.length < 12 || data[0] != 'M' || data[1] != 'I' || data[2] != 'F' || data[3] != '1')
                throw new ImageError.INVALID_RESPONSE ("Invalid decoder pixel header");
            uint w = 0, h = 0;
            for (int i = 0; i < 4; i++) {
                w = (w << 8) | data[4 + i];
                h = (h << 8) | data[8 + i];
            }
            return new PixelBuffer ((int) w, (int) h, new Bytes.from_bytes (bytes, 12, bytes.get_size () - 12));
        }
    }
}
