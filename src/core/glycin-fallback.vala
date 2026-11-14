namespace MajorisImage {
    internal class GlycinFallback : Object {
        // Glycin 1 does not expose ICC, transfer functions, page counts or source depth.
        // Only use it for known SDR formats when the requested capabilities are provable.
        private const ImageCapabilities FEATURES = ImageCapabilities.STILL |
            ImageCapabilities.THUMBNAIL | ImageCapabilities.METADATA;

        internal static bool eligible (Error error) {
            return error is ImageError.CODEC_MISSING || error is ImageError.UNSUPPORTED;
        }

        internal static string[] installed () {
            string[] result = {};
#if HAVE_GLYCIN
            string[] roots = { Environment.get_user_data_dir () };
            foreach (var root in Environment.get_system_data_dirs ()) roots += root;
            foreach (var root in roots) {
                string folder = Path.build_filename (root, "glycin-loaders", "1+", "conf.d");
                try {
                    var directory = Dir.open (folder);
                    string? entry;
                    while ((entry = directory.read_name ()) != null) {
                        if (!entry.has_suffix (".conf")) continue;
                        var config = new KeyFile ();
                        try {
                            config.load_from_file (Path.build_filename (folder, entry), KeyFileFlags.NONE);
                            foreach (string group in config.get_groups ()) {
                                if (!group.has_prefix ("loader:")) continue;
                                var mime = group.substring (7);
                                if (safe_format (mime) && !(mime in result) &&
                                    FileUtils.test (config.get_string (group, "Exec"), FileTest.IS_EXECUTABLE))
                                    result += mime;
                            }
                        } catch (Error error) { debug ("Glycin loader configuration: %s", error.message); }
                    }
                } catch (FileError error) { }
            }
#endif
            return result;
        }

        private static bool safe_format (string mime) {
            return mime in new string[] { "image/bmp", "image/x-ms-bmp", "image/vnd.microsoft.icon",
                "image/x-icon", "image/qoi", "image/x-qoi", "image/x-tga",
                "image/vnd-ms.dds", "image/x-dds", "image/x-portable-anymap",
                "image/x-portable-bitmap", "image/x-portable-pixmap", "image/x-portable-graymap" };
        }

#if HAVE_GLYCIN
        [CCode (cname = "majoris_image_glycin_pixels_async", finish_name = "majoris_image_glycin_pixels_finish")]
        private extern static async Bytes convert (Gly.Frame frame, int edge, Cancellable stop,
                                                   out int w, out int h, out int depth) throws Error;

        private static async string sniff (File source, Cancellable stop) throws Error {
            var stream = yield source.read_async (Priority.DEFAULT, stop);
            var prefix = yield stream.read_bytes_async (4096, Priority.DEFAULT, stop);
            yield stream.close_async (Priority.DEFAULT, stop);
            unowned uint8[] b = prefix.get_data ();
            if (b.length >= 14 && b[0] == 'q' && b[1] == 'o' && b[2] == 'i' && b[3] == 'f') {
                return "image/qoi";
            }
            bool uncertain;
            string mime = ContentType.get_mime_type (ContentType.guess (null, b, out uncertain));
            if (!uncertain && safe_format (mime)) return mime;
            // A TGA 2.0 footer is a content signature, even on an extensionless GFile.
            var info = yield source.query_info_async ("standard::size", 0, Priority.DEFAULT, stop);
            if (source.is_native () && info.get_size () >= 26) {
                var footer = yield source.read_async (Priority.DEFAULT, stop);
                if (footer.can_seek () && footer.seek (-18, SeekType.END, stop)) {
                    var signature = yield footer.read_bytes_async (18, Priority.DEFAULT, stop);
                    yield footer.close_async (Priority.DEFAULT, stop);
                    uint8[] marker = "TRUEVISION-XFILE.".data;
                    unowned uint8[] end = signature.get_data ();
                    if (end.length == 18 && Memory.cmp (end, marker, 17) == 0 && end[17] == 0)
                        return "image/x-tga";
                }
            }
            return "";
        }
#endif

        internal static async ImageDocument open (ImageSource source, DecodeOptions request, Cancellable stop,
                                                   Error original, uint seconds = 120, ImageProbe? animation = null) throws Error {
#if HAVE_GLYCIN
            var supported = FEATURES | ImageCapabilities.HIGH_DEPTH;
            if (animation != null) supported |= ImageCapabilities.ANIMATION;
            if ((request.page != 0 && animation == null) || request.raw_preview_only ||
                (request.required_capabilities & ~supported) != 0) throw original;
            var identity = yield source.stamp (stop);
            string mime = animation != null ? animation.mime : yield sniff (source.file, stop);
            if (mime == "") throw original;
            string[] loaders = installed ();
            if (animation == null && !(mime in loaders) && !(mime == "image/qoi" && "image/x-qoi" in loaders) &&
                !(mime == "image/x-icon" && "image/vnd.microsoft.icon" in loaders) &&
                !(mime == "image/x-ms-bmp" && "image/bmp" in loaders)) throw original;

            var job = new Cancellable ();
            ulong handler = stop.connect (() => job.cancel ());
            bool timed_out = false;
            var timeout = new TimeoutSource.seconds (seconds.clamp (1, 120));
            timeout.set_callback (() => { timed_out = true; job.cancel (); return Source.REMOVE; });
            timeout.attach (MainContext.get_thread_default ());
            var scheduler = JobScheduler.shared ();
            bool admitted = false;
            try {
                yield scheduler.acquire (0, job);
                admitted = true;
                var loader = new Gly.Loader (source.file);
                loader.set_sandbox_selector (Gly.SandboxSelector.BWRAP);
                var image = yield loader.load_async (job);
                if (animation == null && !safe_format (image.get_mime_type ())) throw original;
                if ((uint64) image.get_width () * image.get_height () > 67108864)
                    throw new ImageError.MEMORY ("Glycin image exceeds the fallback pixel budget");
                var frame = yield image.next_frame_async (job);
                if (animation == null && frame.get_delay () > 0) throw original;
                if (animation != null) {
                    if (frame.get_delay () <= 0) throw new ImageError.UNSUPPORTED ("Glycin has no animation loader for this image");
                    for (int index = 0; index < request.page; index++) frame = yield image.next_frame_async (job);
                }
                int width, height, depth;
                var bytes = yield convert (frame, request.preview_size, job, out width, out height, out depth);
                job.set_error_if_cancelled ();
                if (identity != "" && identity != (yield source.stamp (job)))
                    throw new ImageError.SOURCE_CHANGED ("Image changed during Glycin decoding");

                var probe = new ImageProbe ();
                probe.width = (int) frame.get_width ();
                probe.height = (int) frame.get_height ();
                probe.depth = depth;
                probe.has_alpha = frame.get_memory_format ().has_alpha ();
                probe.loader = "Glycin";
                probe.mime = image.get_mime_type ();
                if (animation != null) {
                    probe.mime = animation.mime;
                    probe.animated = probe.timed_sequence = true;
                    probe.pages = animation.pages;
                    probe.loop_count = animation.loop_count;
                    probe.frame_duration_ms = (int) (frame.get_delay () / 1000).clamp (0, int.MAX);
                    probe.source_bit_depth = animation.source_bit_depth;
                }
                if ((probe.capabilities & request.required_capabilities) != request.required_capabilities)
                    throw new ImageError.UNSUPPORTED ("Glycin output does not satisfy the requested precision");
                var metadata = new Json.Object ();
                if (animation != null) {
                    var source_metadata = new Json.Parser ();
                    source_metadata.load_from_data (animation.metadata);
                    if (source_metadata.get_root ()?.get_node_type () == Json.NodeType.OBJECT)
                        metadata.set_member ("source-image", source_metadata.get_root ().copy ());
                }
                metadata.set_boolean_member ("alpha", frame.get_memory_format ().has_alpha ());
                metadata.set_int_member ("effective-bit-depth", depth);
                metadata.set_string_member ("colour", "Decoder RGB, interpreted as sRGB");
                var node = new Json.Node (Json.NodeType.OBJECT);
                node.set_object (metadata);
                var generator = new Json.Generator ();
                generator.set_root (node);
                probe.metadata = generator.to_data (null);
                return new ImageDocument (source, probe, new PixelBuffer (width, height, bytes),
                    request, new WorkerClient (), identity);
            } catch (Gly.LoaderError.UNKNOWN_IMAGE_FORMAT error) {
                throw new ImageError.CODEC_MISSING ("No Glycin loader accepted %s", mime);
            } catch (Gly.LoaderError.FAILED error) {
                if (job.is_cancelled ()) {
                    if (timed_out && !stop.is_cancelled ()) throw new ImageError.TIMEOUT ("Glycin image decoding timed out");
                    job.set_error_if_cancelled ();
                }
                string message = error.message.down ();
                if (message.contains ("bwrap") || message.contains ("sandbox"))
                    throw new ImageError.SANDBOX ("%s", error.message);
                throw new ImageError.CORRUPT ("Glycin could not decode the image: %s", error.message);
            } catch (IOError.CANCELLED error) {
                if (timed_out && !stop.is_cancelled ())
                    throw new ImageError.TIMEOUT ("Glycin image decoding timed out");
                throw error;
            } finally {
                if (admitted) scheduler.release ();
                stop.disconnect (handler);
                timeout.destroy ();
            }
#else
            throw original;
#endif
        }
    }
}
