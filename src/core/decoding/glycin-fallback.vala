namespace ImageKit {
    /** Indicates where Glycin-derived color interpretation originated. */
    internal enum GlycinColorProvenance {
        /** Source explicitly identifies an sRGB color state. */
        TAGGED_SRGB,
        /** ICC conversion could not be independently verified. */
        ICC_CONVERSION_UNVERIFIED,
        /** No source profile was available, so sRGB is assumed. */
        ASSUMED_SRGB,
        /** The source color state could not be determined. */
        UNKNOWN,
        /** QOI metadata explicitly indicates the sRGB transfer function. */
        TAGGED_QOI_SRGB,
        /** QOI metadata explicitly indicates linear transfer values. */
        QOI_LINEAR
    }

    /** Adapts Glycin inspection/rendering to ImageKit document and pixel contracts. */
    internal class GlycinFallback : Object {
        // Glycin 1 does not expose ICC, transfer functions, page counts or source depth.
        // Negotiate decoder capabilities using the installed loader and sandboxed metadata.
        private const ImageCapabilities FEATURES = ImageCapabilities.STILL |
            ImageCapabilities.THUMBNAIL | ImageCapabilities.METADATA;

        /** Checks whether a native-decoder failure permits Glycin fallback. */
        internal static bool eligible (Error error) {
            return error is ImageError.CODEC_MISSING || error is ImageError.UNSUPPORTED;
        }
#if HAVE_GLYCIN
        /** Obtains MIME types supported by the installed Glycin loader. */
        [CCode (cname = "image_discover_glycin_loaders", cheader_filename = "glycin-discovery.h",
                array_length = false, array_null_terminated = true)]
        private extern static string[] discover ();

        /** Compares MIME types using the native Glycin/GLib compatibility rules. */
        [CCode (cname = "image_mime_matches", cheader_filename = "glycin-discovery.h")]
        private extern static bool same_mime (string a, string b);
#endif

        /** Lists the Glycin loader formats available in this build. */
        internal static string[] installed () {
#if HAVE_GLYCIN
            return discover ();
#else
            return {};
#endif
        }
#if HAVE_GLYCIN
        /** Converts a Glycin frame to ImageKit pixel storage with a bounded preview edge. */
        [CCode (cname = "image_glycin_pixels_profile_async", finish_name = "image_glycin_pixels_finish")]
        private extern static async Bytes convert (Gly.Frame frame, int edge, double left, double top,
                                                   double crop_width, double crop_height, bool nearest,
                                                   bool gray_icc, bool linear_rgb, Cancellable stop,
                                                   out int w, out int h, out int depth) throws Error;

        /** Tests whether a Glycin loader can read a given MIME type. */
        private static bool has_loader (string mime, string[] loaders) {
            foreach (var loader in loaders) {
                if (same_mime (mime, loader)) {
                    return true;
                }
            }
            return false;
        }

        /** Extracts color provenance and profile diagnostics from Glycin metadata. */
        private static async Json.Object color_metadata (ImageSource source, Cancellable stop,
            bool preload) throws Error {
            string? path = null;
            try {
                path = yield new WorkerClient ().run (source.file, "colour-probe", null, stop, 0,
                    null, null, preload);
                var parser = new Json.Parser ();
                parser.load_from_data (yield WorkerClient.read_text (path, stop));
                var root = parser.get_root ();
                if (root != null && root.get_node_type () == Json.NodeType.OBJECT) {
                    return root.get_object ();
                }
            } catch (Error error) {
                stop.set_error_if_cancelled ();
                debug ("Fallback source color could not be verified: %s", error.message);
            } finally {
                if (path != null) {
                    WorkerClient.cleanup (path);
                }
            }
            return new Json.Object ();
        }

        /** Reads a bounded leading signature for format detection. */
        private static async string sniff (File source, Cancellable stop, bool preload) throws Error {
            int priority = preload ? Priority.LOW : Priority.DEFAULT;
            var stream = yield source.read_async (priority, stop);
            int64 started = OperationProfile.begin ();
            var prefix = yield read_sniff_stream (stream, 4096, priority, stop);
            OperationProfile.record ("source-header-read", started, prefix.get_size ());
            unowned uint8[] b = prefix.get_data ();
            if (b.length >= 14 && b[0] == 'q' && b[1] == 'o' && b[2] == 'i' && b[3] == 'f') {
                return "image/qoi";
            }
            bool uncertain;
            string mime = ContentType.get_mime_type (ContentType.guess (null, b, out uncertain));
            if (!uncertain && mime.has_prefix ("image/")) {
                return mime;
            }
            // A TGA 2.0 footer is a content signature, even on an extensionless GFile.
            started = OperationProfile.begin ();
            var info = yield source.query_info_async ("standard::size", 0, priority, stop);
            OperationProfile.record ("source-query", started);
            if (source.is_native () && info.get_size () >= 26) {
                var footer = yield source.read_async (priority, stop);
                var signature = yield read_sniff_stream (footer, 18, priority, stop, true);
                if (signature != null) {
                    uint8[] marker = "TRUEVISION-XFILE.".data;
                    unowned uint8[] end = signature.get_data ();
                    if (end.length == 18 && Memory.cmp (end, marker, 17) == 0 && end[17] == 0) {
                        return "image/x-tga";
                    }
                }
            }
            return "";
        }
#endif

        /** Builds an ImageDocument from Glycin frames after fallback has been authorized. */
        internal static async ImageDocument open (ImageSource source, DecodeOptions request, Cancellable stop,
                                                   Error original, uint seconds = 120,
                                                       ImageProbe? animation = null,
                                                       RenderOptions? crop = null,
                                                       bool preload = false) throws Error {
#if HAVE_GLYCIN
            var supported = FEATURES | ImageCapabilities.HIGH_DEPTH | ImageCapabilities.COLOR_MANAGED;
            if (animation != null) {
                supported |= ImageCapabilities.ANIMATION;
            }
            if ((request.page != 0 && animation == null) ||
                (request.required_capabilities & ~supported) != 0) {
                throw original;
            }
            OperationProfile.event ("glycin-attempt");
            var identity = yield source.stamp (stop);
            var mime = animation != null ? animation.mime : yield sniff (source.file, stop, preload);
            if (mime == "") {
                throw original;
            }
            var loaders = installed ();
            if (!has_loader (mime, loaders)) {
                throw original;
            }
            var view = crop ?? new RenderOptions ();
            view.validate ();

            var job = new Cancellable ();
            ulong handler = stop.connect (() => job.cancel ());
            var timed_out = false;
            var timeout = new TimeoutSource.seconds (seconds.clamp (1, 120));
            timeout.set_callback (() => {
                timed_out = true;
                job.cancel ();
                return Source.REMOVE;
            });
            timeout.attach (MainContext.get_thread_default ());
            var scheduler = JobScheduler.shared ();
            var admitted = false;
            try {
                var color = yield color_metadata (source, job, preload);
                var tagged = color.get_boolean_member_with_default ("icc", false);
                if (color.get_boolean_member_with_default ("icc-invalid", false)) {
                    throw new ImageError.COLOR_PROFILE ("Invalid embedded ICC profile");
                }
                if (color.get_boolean_member_with_default ("unverified-transfer", false)) {
                    throw new ImageError.COLOR_PROFILE ("Glycin 1 cannot verify the source transfer and primaries");
                }
                int source_depth = (int) color.get_int_member_with_default ("source-bit-depth", 0);
                var known_header = color.get_boolean_member_with_default ("header-read", false);
                // PNG sRGB has no ICC transform to observe. The PNG loader publishes
                // its encoded samples in the declared sRGB space on this path.
                var provenance = (mime == "image/png" || mime == "image/apng") &&
                    color.get_boolean_member_with_default ("explicit-srgb", false)
                    ? GlycinColorProvenance.TAGGED_SRGB
                    : tagged ? GlycinColorProvenance.ICC_CONVERSION_UNVERIFIED
                    : known_header ? GlycinColorProvenance.ASSUMED_SRGB : GlycinColorProvenance.UNKNOWN;
                if (color.get_boolean_member_with_default ("invalid-qoi-colour", false)) {
                    throw new ImageError.COLOR_PROFILE ("Invalid QOI color declaration");
                }
                int qoi_transfer = (int) color.get_int_member_with_default ("qoi-transfer", -1);
                if (qoi_transfer == 0) {
                    provenance = GlycinColorProvenance.TAGGED_QOI_SRGB;
                }
                if (qoi_transfer == 1) {
                    provenance = GlycinColorProvenance.QOI_LINEAR;
                }
                var verified = provenance == GlycinColorProvenance.TAGGED_SRGB ||
                    provenance == GlycinColorProvenance.TAGGED_QOI_SRGB;
                if (!verified && (request.required_capabilities & ImageCapabilities.COLOR_MANAGED) != 0) {
                    throw new ImageError.COLOR_PROFILE ("Glycin 1 cannot attest the decoded color state");
                }

                yield scheduler.acquire (preload ? JobScheduler.PRELOAD_PRIORITY :
                    JobScheduler.FOREGROUND_PRIORITY, job);
                admitted = true;
                var loader = new Gly.Loader (source.file);
                loader.set_sandbox_selector (Gly.SandboxSelector.BWRAP);
                var image = yield loader.load_async (job);
                if (!same_mime (mime, image.get_mime_type ()) &&
                    !(animation != null && mime == "image/apng" && image.get_mime_type () == "image/png")) {
                    throw new ImageError.INVALID_RESPONSE ("Glycin loader MIME does not match the source signature");
                }
                if ((uint64) image.get_width () * image.get_height () > 67108864) {
                    throw new ImageError.MEMORY ("Glycin image exceeds the fallback pixel budget");
                }
                var frame = yield image.next_frame_async (job);
                if (animation == null && frame.get_delay () > 0) {
                    // A still preview can use frame zero, but Glycin 1 does
                    // not expose counts or loop metadata for unknown sequences.
                    if ((request.required_capabilities & ImageCapabilities.ANIMATION) != 0) {
                        throw original;
                    }
                }
                if (animation != null) {
                    if (frame.get_delay () <= 0) {
                        throw new ImageError.UNSUPPORTED ("Glycin has no animation loader for this image");
                    }
                    for (int index = 0; index < request.page; index++) {
                        frame = yield image.next_frame_async (job);
                    }
                }
                int width, height, depth;
                var bytes = yield convert (frame, request.preview_size, view.crop_x, view.crop_y,
                    view.crop_width, view.crop_height, view.nearest_neighbor,
                    color.get_boolean_member_with_default ("gray-icc", false), qoi_transfer == 1, job,
                    out width, out height, out depth);
                if (source_depth > depth) {
                    throw new ImageError.UNSUPPORTED ("Glycin would reduce precision from the source samples");
                }
                job.set_error_if_cancelled ();
                if (identity != "" && identity != (yield source.stamp (job))) {
                    throw new ImageError.SOURCE_CHANGED ("Image changed during Glycin decoding");
                }

                var probe = new ImageProbe ();
                probe.width = (int) frame.get_width ();
                probe.height = (int) frame.get_height ();
                probe.depth = depth;
                probe.source_bit_depth = source_depth;
                probe.color_managed = verified;
                probe.color_diagnostic = verified ? "Validated sRGB declaration and decoder output contract."
                    : qoi_transfer == 1 ? "QOI declares linear samples. sRGB primaries are assumed."
                    : tagged ? "Glycin 1 applies the embedded ICC profile through Little CMS. Output color state is unverified."
                    : known_header ? "Untagged source uses the sRGB default."
                        : "Source color metadata unavailable. Preview uses the sRGB default.";
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
                if ((probe.capabilities & request.required_capabilities) != request.required_capabilities) {
                    throw new ImageError.UNSUPPORTED ("Glycin output does not satisfy the requested precision");
                }
                var metadata = new Json.Object ();
                if (animation != null) {
                    var source_metadata = new Json.Parser ();
                    source_metadata.load_from_data (animation.metadata);
                    if (source_metadata.get_root ()?.get_node_type () == Json.NodeType.OBJECT) {
                        metadata.set_member ("source-image", source_metadata.get_root ().copy ());
                    }
                }
                metadata.set_boolean_member ("alpha", frame.get_memory_format ().has_alpha ());
                metadata.set_int_member ("effective-bit-depth", depth);
                metadata.set_member ("colour-probe", new Json.Node.alloc ().init_object (color));
                metadata.set_string_member ("colour", probe.color_diagnostic);
                metadata.set_string_member ("colour-provenance", provenance.to_string ());
                metadata.set_string_member ("decoder-output-colour", verified ? "sRGB (declared)"
                    : qoi_transfer == 1 ? "linear RGB (sRGB primaries assumed)" :
                    tagged ? "ICC-converted output unverified" : "sRGB assumed");
                metadata.set_string_member ("working-colour", "linear sRGB");
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
                    if (timed_out && !stop.is_cancelled ()) {
                        throw new ImageError.TIMEOUT ("Glycin image decoding timed out");
                    }
                    job.set_error_if_cancelled ();
                }
                var message = error.message.down ();
                if (message.contains ("bwrap") || message.contains ("sandbox")) {
                    throw new ImageError.SANDBOX ("%s", error.message);
                }
                throw new ImageError.CORRUPT ("Glycin could not decode the image: %s", error.message);
            } catch (IOError.CANCELLED error) {
                if (timed_out && !stop.is_cancelled ()) {
                    throw new ImageError.TIMEOUT ("Glycin image decoding timed out");
                }
                throw error;
            } finally {
                if (admitted) {
                    scheduler.release ();
                }
                stop.disconnect (handler);
                timeout.destroy ();
            }
#else
            throw original;
#endif
        }

        /** Renders source pixels with Glycin, preserving cancellation and source checks. */
        internal static async PixelBuffer render (ImageSource source, DecodeOptions options,
                                                 RenderOptions transform, Cancellable stop,
                                                 Error original, uint seconds = 120) throws Error {
            var document = yield open (source, options, stop, original, seconds, null, transform);
            var edits = transform.copy ();
            edits.crop_x = edits.crop_y = 0;
            edits.crop_width = edits.crop_height = 1;
            edits.nearest_neighbor = false;
            if (edits.is_default ()) {
                return document.preview;
            }

            options.sdr_presentation = false;
            return yield new WorkerClient (null, seconds).render_pixels (document.preview, edits,
                                                                          options.preview_size, options, stop);
        }
    }
}
