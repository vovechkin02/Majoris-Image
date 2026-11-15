[CCode (gir_namespace = "MajorisImageGtk", gir_version = "1.0")]
namespace MajorisImage {
    public enum ViewState { EMPTY, LOADING, PREVIEW_READY, READY, UNSUPPORTED, DECODER_UNAVAILABLE, CORRUPT, CANCELLED, ERROR }
    public enum FitMode { CONTAIN, WIDTH, HEIGHT, ACTUAL, COVER, CUSTOM }
    public enum PresentationPolicy { AUTOMATIC, SDR, HDR_PREFERRED }
    public enum PresentationStatus { NONE, SDR, SDR_TONE_MAPPED, NATIVE_HDR, HDR_UNAVAILABLE }

    [CCode (cname = "majoris_image_texture_new", cheader_filename = "image-texture.h")]
    private extern static Gdk.Texture create_texture (Bytes bytes, int width, int height, size_t stride);

    internal static Gdk.Texture texture_for_pixels (PixelBuffer buffer) {
        return create_texture (buffer.bytes, buffer.width, buffer.height, buffer.stride);
    }

    [CCode (cname = "MajorisImageView")]
    public sealed class ImageView : Gtk.Widget {
        public ViewState state { get; private set; default = ViewState.EMPTY; }
        public ImageDocument? document { get { return current_document; } }
        private ImageDocument? current_document;
        public int image_width { get; private set; }
        public int image_height { get; private set; }
        public int page_count { get; private set; }
        public int current_page { get; private set; }
        public ImageMetadata? metadata { get; private set; }
        public string decoder { get; private set; default = ""; }
        public string warning { get; private set; default = ""; }
        public string error_message { get; private set; default = ""; }
        public bool hdr_source { get; private set; }
        public PresentationPolicy presentation_policy { get; set; default = PresentationPolicy.AUTOMATIC; }
        public PresentationStatus presentation_status { get; private set; }
        public string presentation_diagnostic { get {
            return document != null ? Presentation.inspect (this, presentation_policy, hdr_source).diagnostic : "";
        } }
        public bool animated { get; private set; }
        public bool playing { get; private set; }
        public int current_frame { get; private set; }
        public int frame_duration_ms { get; private set; }
        public FitMode fit_mode { get; set; default = FitMode.CONTAIN; }
        public bool allow_zoom { get; set; default = true; }
        public bool allow_pan { get; set; default = true; }
        public bool nearest_neighbour { get; set; }
        public double minimum_zoom { get; set; default = 0.01; }
        public double maximum_zoom { get; set; default = 64; }
        public double zoom { get { return effective_zoom (); } }
        public signal void transformed ();

        private Gdk.Texture? texture;
        private Cancellable pending = new Cancellable ();
        private uint generation;
        private bool disposed;
        private bool playback_requested;
        private bool animation_loading;
        private TileCache tiles = new TileCache ();
        private double custom_zoom = 1;
        private double pan_x;
        private double pan_y;
        private double start_x;
        private double start_y;
        private double pinch_start;
        private double pointer_x;
        private double pointer_y;
        private int turns;
        private bool flipped_x;
        private bool flipped_y;
        private uint animation_timer;
        private bool frame_pending;
        private uint playback_generation;
        private Cancellable playback = new Cancellable ();
        private int64 frame_delay;
        private int64 frames_presented;
        private AnimationFrame? prefetched;
        private bool advance_due;
        private bool manual_advance;
        private weak Gdk.Surface? watched_surface;
        private ulong entered_monitor;
        private ulong left_monitor;
#if HAVE_GLYCIN
        private Gly.Image? animation;
#endif

        public static void register_type () { typeof (ImageView).ensure (); }

        construct {
            hexpand = true;
            vexpand = true;
            focusable = true;
            overflow = Gtk.Overflow.HIDDEN;
            notify["fit-mode"].connect (() => update_transform ());
            notify["nearest-neighbour"].connect (() => queue_draw ());
            tiles.changed.connect (() => queue_draw ());
            notify["presentation-policy"].connect (update_presentation);
            notify["scale-factor"].connect (update_presentation);
            map.connect (() => {
                watch_output ();
                update_presentation ();
                update_transform ();
                if (playback_requested) play ();
            });
            unmap.connect (() => {
                watch_output (false);
                tiles.reset (null);
                suspend_playback ();
                notify_property ("presentation-diagnostic");
            });

            var motion = new Gtk.EventControllerMotion ();
            motion.motion.connect ((x, y) => { pointer_x = x; pointer_y = y; });
            add_controller (motion);
            var scroll = new Gtk.EventControllerScroll (Gtk.EventControllerScrollFlags.VERTICAL);
            scroll.scroll.connect ((dx, dy) => {
                if (!allow_zoom || texture == null) return false;
                zoom_to (zoom * Math.pow (1.15, -dy), pointer_x, pointer_y);
                return true;
            });
            add_controller (scroll);
            var drag = new Gtk.GestureDrag ();
            drag.drag_begin.connect (() => { grab_focus (); start_x = pan_x; start_y = pan_y; });
            drag.drag_update.connect ((x, y) => {
                if (!allow_pan) return;
                pan_x = start_x + x;
                pan_y = start_y + y;
                update_transform ();
            });
            add_controller (drag);
            var pinch = new Gtk.GestureZoom ();
            pinch.begin.connect (() => pinch_start = zoom);
            pinch.scale_changed.connect (scale => {
                if (!allow_zoom) return;
                double x, y;
                if (pinch.get_bounding_box_center (out x, out y)) zoom_to (pinch_start * scale, x, y);
            });
            add_controller (pinch);
            var click = new Gtk.GestureClick ();
            click.pressed.connect ((count, x, y) => {
                if (allow_zoom && count == 2) {
                    if (fit_mode != FitMode.CONTAIN) fit_to_view ();
                    else zoom_to (1.0 / device_scale (), x, y);
                }
            });
            add_controller (click);
            var keys = new Gtk.EventControllerKey ();
            keys.key_pressed.connect ((key, code, modifiers) => {
                switch (key) {
                case Gdk.Key.plus: case Gdk.Key.equal: if (allow_zoom) zoom_in (); return allow_zoom;
                case Gdk.Key.minus: if (allow_zoom) zoom_out (); return allow_zoom;
                case 0x30: if (allow_zoom) fit_to_view (); return allow_zoom;
                case Gdk.Key.Left: if (allow_pan) pan_by (64, 0); return allow_pan;
                case Gdk.Key.Right: if (allow_pan) pan_by (-64, 0); return allow_pan;
                case Gdk.Key.Up: if (allow_pan) pan_by (0, 64); return allow_pan;
                case Gdk.Key.Down: if (allow_pan) pan_by (0, -64); return allow_pan;
                default: return false;
                }
            });
            add_controller (keys);
        }

        private uint invalidate () {
            generation++;
            pending.cancel ();
            pending = new Cancellable ();
            pause ();
            playback_generation++;
            playback.cancel ();
            playback = new Cancellable ();
            frame_pending = animation_loading = advance_due = manual_advance = false;
            prefetched = null;
            frames_presented = 0;
#if HAVE_GLYCIN
            animation = null;
#endif
            animated = false;
            current_frame = frame_duration_ms = 0;
            tiles.reset (null);
            return generation;
        }

        public void clear () {
            if (!disposed) clear_request ();
        }

        private uint clear_request () {
            freeze_notify ();
            uint request = invalidate ();
            current_document = null;
            notify_property ("document");
            texture = null;
            image_width = image_height = current_page = page_count = 0;
            metadata = null;
            decoder = warning = error_message = "";
            hdr_source = false;
            notify_property ("presentation-diagnostic");
            presentation_status = PresentationStatus.NONE;
            state = ViewState.EMPTY;
            turns = 0;
            flipped_x = flipped_y = false;
            pan_x = pan_y = 0;
            if (fit_mode == FitMode.CUSTOM) fit_mode = FitMode.CONTAIN;
            thaw_notify ();
            if (is_current (request)) update_transform ();
            return request;
        }

        public void set_texture (Gdk.Texture value) {
            if (disposed) return;
            uint request = clear_request ();
            if (!is_current (request)) return;
            freeze_notify ();
            texture = value;
            image_width = value.get_width ();
            image_height = value.get_height ();
            page_count = 1;
            presentation_status = PresentationStatus.SDR;
            state = ViewState.READY;
            thaw_notify ();
            if (is_current (request)) update_transform ();
        }

        public void set_document (ImageDocument value) {
            if (disposed) return;
            uint request = clear_request ();
            if (is_current (request)) apply_document.begin (value, request, pending);
        }

        private async void apply_document (ImageDocument value, uint request, Cancellable stop) {
            state = ViewState.LOADING;
            try {
                yield present_document (value, request, stop);
#if HAVE_GLYCIN
                if (is_current (request) && use_streaming_loader (value))
                    yield prepare_animation (value.source.file, request, playback);
#endif
            }
            catch (Error e) { if (is_current (request)) show_error (e); }
        }

        private bool is_current (uint request) { return !disposed && request == generation; }

        private async void present_document (ImageDocument value, uint request, Cancellable stop) throws Error {
            var decision = Presentation.inspect (this, presentation_policy, value.hdr);
            var pixels = yield Presentation.pixels (value, decision, stop);
            stop.set_error_if_cancelled ();
            if (is_current (request)) show_document (value, pixels);
            stop.set_error_if_cancelled ();
        }

        private void watch_output (bool attach = true) {
            if (watched_surface != null) {
                watched_surface.disconnect (entered_monitor);
                watched_surface.disconnect (left_monitor);
            }
            watched_surface = attach ? get_native ()?.get_surface () : null;
            if (watched_surface != null) {
                entered_monitor = watched_surface.enter_monitor.connect ((monitor) => update_presentation ());
                left_monitor = watched_surface.leave_monitor.connect ((monitor) => update_presentation ());
            }
        }

        private void update_presentation () {
            if (disposed) return;
            if (document != null) {
                var decision = Presentation.inspect (this, presentation_policy, hdr_source);
                presentation_status = decision.status;
                tiles.reset (get_mapped () && !animated ? document : null, decision.tone_map);
            }
            notify_property ("presentation-diagnostic");
            queue_draw ();
        }

        private void show_document (ImageDocument value, PixelBuffer? presentation = null) {
            uint request = generation;
            freeze_notify ();
            current_document = value;
            notify_property ("document");
            texture = texture_for_pixels (presentation ?? value.preview);
            image_width = value.width;
            image_height = value.height;
            page_count = value.page_count;
            current_page = value.page;
            decoder = value.decoder;
            warning = value.warning;
            metadata = value.metadata;
            hdr_source = value.hdr;
            animated = value.animated;
            current_frame = value.frame;
            frame_duration_ms = value.frame_duration_ms;
            frame_delay = value.frame_duration_us;
            frames_presented = value.frame + 1;
            update_presentation ();
            state = value.raw_state == RawState.RAW_PREVIEW_AVAILABLE ? ViewState.PREVIEW_READY : ViewState.READY;
            thaw_notify ();
            if (is_current (request)) update_transform ();
        }

        public async void load_file (File file, DecodeOptions? options = null, Cancellable? cancel = null) throws Error {
            yield load_source (new ImageSource (file), options, cancel);
        }

        /** Keep the supplied source alive, including privately staged stream or memory inputs. */
        public async void load_source (ImageSource source, DecodeOptions? options = null,
                                        Cancellable? cancel = null) throws Error {
            if (disposed) throw new IOError.CLOSED ("Image view is disposed");
            uint request = clear_request ();
            if (!is_current (request)) throw new IOError.CANCELLED ("Image request was replaced");
            var stop = pending;
            ulong handler = cancel != null ? cancel.connect (() => stop.cancel ()) : 0;
            state = ViewState.LOADING;
            var decode = (options ?? new DecodeOptions ()).copy ();
            decode.sdr_presentation = false;
            var registry = new DecoderRegistry ();
            try {
                ImageDocument? preview = null;
                var header = yield registry.inspect (source, stop);
                if (header.raw && decode.raw_preview_first && !decode.raw_preview_only) {
                    var fast = decode.copy ();
                    fast.raw_preview_only = true;
                    try {
                        preview = yield registry.open (source, fast, stop);
                        stop.set_error_if_cancelled ();
                        if (is_current (request)) yield present_document (preview, request, stop);
                    } catch (Error e) { stop.set_error_if_cancelled (); }
                }
                try {
                    var result = yield registry.open (source, decode, stop);
                    stop.set_error_if_cancelled ();
                    if (is_current (request)) yield present_document (result, request, stop);
#if HAVE_GLYCIN
                    if (is_current (request) && use_streaming_loader (result))
                        yield prepare_animation (source.file, request, playback);
#endif
                } catch (Error e) {
                    if (preview == null || stop.is_cancelled ()) throw e;
                    if (is_current (request)) warning = "Full RAW development failed: " + e.message;
                }
            } catch (Error e) {
                if (is_current (request)) show_error (e);
                throw e;
            } finally { if (handler != 0) cancel.disconnect (handler); }
        }

        private void show_error (Error error) {
            freeze_notify ();
            error_message = (error is IOError.CANCELLED) ? "" : error.message;
            if (error is IOError.CANCELLED) state = ViewState.CANCELLED;
            else if (error is ImageError.UNSUPPORTED || error is ImageError.RAW_UNSUPPORTED) state = ViewState.UNSUPPORTED;
            else if (error is ImageError.CODEC_MISSING) state = ViewState.DECODER_UNAVAILABLE;
            else if (error is ImageError.CORRUPT) state = ViewState.CORRUPT;
            else state = ViewState.ERROR;
            thaw_notify ();
        }

        public async void set_page (int page, Cancellable? cancel = null) throws Error {
            if (disposed) throw new IOError.CLOSED ("Image view is disposed");
            if (document == null) throw new IOError.INVALID_ARGUMENT ("No image is loaded");
            var source = document;
            freeze_notify ();
            uint request = invalidate ();
            var stop = pending;
            ulong handler = cancel != null ? cancel.connect (() => stop.cancel ()) : 0;
            state = ViewState.LOADING;
            thaw_notify ();
            try {
                var result = yield source.open_page (page, stop);
                if (is_current (request)) yield present_document (result, request, stop);
            } catch (Error e) {
                if (is_current (request)) show_error (e);
                throw e;
            } finally { if (handler != 0) cancel.disconnect (handler); }
        }

#if HAVE_GLYCIN
        private bool use_streaming_loader (ImageDocument value) {
            // Sequential Glycin loading is inexpensive for the established
            // GIF, WebP and APNG loaders. Other decoders use indexed documents.
            return !value.hdr && value.frame == 0 && value.mime_type in new string[] {
                "image/png", "image/apng", "image/gif", "image/webp" };
        }

        private async void prepare_animation (File file, uint request, Cancellable stop) {
            if (animation_loading || !is_current (request)) return;
            animation_loading = true;
            try {
                var loader = new Gly.Loader (file);
                loader.set_sandbox_selector (Gly.SandboxSelector.BWRAP);
                var image = yield loader.load_async (stop);
                if ((uint64) image.get_width () * image.get_height () > 16777216) return;
                var frame = yield image.next_frame_async (stop);
                if (!is_current (request) || stop.is_cancelled () || frame.get_delay () <= 0 || frame.get_delay () == int64.MAX) return;
                freeze_notify ();
                animation = image;
                frames_presented = 1;
                current_frame = 0;
                animated = true;
                frame_delay = frame.get_delay ();
                frame_duration_ms = (int) (frame_delay / 1000).clamp (0, int.MAX);
                texture = GlyGtk4.frame_get_texture (frame);
                tiles.reset (null);
                queue_draw ();
                thaw_notify ();
            } catch (Error e) {
                if (is_current (request) && document != null && document.animated && !stop.is_cancelled ())
                    warning = "Animation unavailable: " + e.message;
            } finally { if (is_current (request)) animation_loading = false; }
        }
#endif

        public void play () {
            if (disposed || !animated) return;
            playback_requested = true;
            if (playing || !get_mapped ()) return;
            playing = true;
            if (playback_finished ()) {
                seek_frame.begin (0, null, (object, reply) => {
                    try { seek_frame.end (reply); }
                    catch (Error error) { if (!(error is IOError.CANCELLED)) warning = error.message; }
                });
            } else schedule_frame ();
        }

        private void suspend_playback () {
            if (animation_timer != 0) { Source.remove (animation_timer); animation_timer = 0; }
            advance_due = manual_advance = false;
            playing = false;
        }

        public void pause () {
            playback_requested = false;
            suspend_playback ();
        }

        public void step_frame () {
            pause ();
            if (!animated) return;
            manual_advance = advance_due = true;
            advance_when_ready ();
        }

        /** Seek within the active timed sequence. Older frame requests are cancelled. */
        public async void seek_frame (int frame, Cancellable? cancel = null) throws Error {
            var source = document;
            if (disposed || source == null || !source.animated || frame < 0 || frame >= source.frame_count)
                throw new IOError.INVALID_ARGUMENT ("Frame is outside the active sequence");
            if (animation_timer != 0) { Source.remove (animation_timer); animation_timer = 0; }
            playback_generation++;
            playback.cancel ();
            playback = new Cancellable ();
            var stop = playback;
            ulong handler = cancel != null ? cancel.connect (() => stop.cancel ()) : 0;
            prefetched = null;
            advance_due = manual_advance = false;
            frame_pending = true;
#if HAVE_GLYCIN
            animation = null;
#endif
            uint request = generation, seek = playback_generation;
            try {
                var loaded = yield load_frame (source, frame, stop);
                stop.set_error_if_cancelled ();
                if (is_current (request) && seek == playback_generation)
                    show_frame (loaded, frame, frame + 1);
            } finally {
                if (handler != 0) cancel.disconnect (handler);
                if (is_current (request) && seek == playback_generation) {
                    frame_pending = false;
                    schedule_frame ();
                }
            }
        }

        private bool playback_finished () {
            return document != null && document.frame_count > 1 && document.loop_count > 0 &&
                frames_presented >= (int64) document.frame_count * document.loop_count;
        }

        private void schedule_frame () {
            if (disposed || !playing || !get_mapped () || animation_timer != 0 || advance_due) return;
            animation_timer = Timeout.add ((uint) (frame_delay / 1000).clamp (10, uint.MAX), () => {
                animation_timer = 0;
                if (playback_finished ()) pause ();
                else {
                    advance_due = true;
                    advance_when_ready ();
                }
                return Source.REMOVE;
            });
            if (!playback_finished () && document != null && document.preview.bytes.get_size () <= 64 * 1024 * 1024)
                prefetch.begin ();
        }

        private int next_frame_index () {
            return document != null && document.frame_count > 1 ?
                (current_frame + 1) % document.frame_count : (int) ((frames_presented + 1) % int.MAX);
        }

        private void advance_when_ready () {
            if (!advance_due || (!manual_advance && (!playing || !get_mapped ()))) return;
            if (prefetched == null) { prefetch.begin (); return; }
            var loaded = prefetched;
            prefetched = null;
            advance_due = manual_advance = false;
            show_frame (loaded, next_frame_index (), frames_presented + 1);
            schedule_frame ();
        }

        private void show_frame (AnimationFrame loaded, int index, int64 presented) {
            freeze_notify ();
            frames_presented = presented;
            current_frame = index;
            frame_delay = loaded.interval;
            frame_duration_ms = (int) (frame_delay / 1000).clamp (0, int.MAX);
            texture = loaded.texture;
            queue_draw ();
            thaw_notify ();
        }

        private async AnimationFrame load_frame (ImageDocument source, int index, Cancellable stop) throws Error {
#if HAVE_GLYCIN
            var stream = animation;
            if (stream != null) {
                var loaded = yield stream.next_frame_async (stop);
                return new AnimationFrame (GlyGtk4.frame_get_texture (loaded), loaded.get_delay ());
            }
#endif
            var frame = yield source.open_frame (index, stop);
            var decision = Presentation.inspect (this, presentation_policy, frame.hdr);
            var pixels = yield Presentation.pixels (frame, decision, stop);
            return new AnimationFrame (texture_for_pixels (pixels), frame.frame_duration_us);
        }

        private async void prefetch () {
            var source = document;
            if (source == null || prefetched != null || frame_pending) return;
            frame_pending = true;
            int index = next_frame_index ();
            uint request = generation, seek = playback_generation;
            var stop = playback;
            try {
                var loaded = yield load_frame (source, index, stop);
                if (is_current (request) && seek == playback_generation) prefetched = loaded;
            } catch (Error error) {
                if (!stop.is_cancelled () && is_current (request) && seek == playback_generation) {
                    pause ();
                    warning = error.message;
                }
            } finally {
                if (is_current (request) && seek == playback_generation) {
                    frame_pending = false;
                    advance_when_ready ();
                }
            }
        }

        private double device_scale () {
            var surface = get_native ()?.get_surface ();
            return surface != null ? surface.get_scale () : get_scale_factor ();
        }

        private double effective_zoom () {
            if (texture == null || image_width < 1 || image_height < 1) return 1;
            if (fit_mode == FitMode.CUSTOM) return custom_zoom;
            if (get_width () < 1 || get_height () < 1) return 1;
            double width = (double) get_width () / (turns % 2 == 0 ? image_width : image_height);
            double height = (double) get_height () / (turns % 2 == 0 ? image_height : image_width);
            switch (fit_mode) {
            case FitMode.WIDTH: return width;
            case FitMode.HEIGHT: return height;
            case FitMode.ACTUAL: return 1.0 / device_scale ();
            case FitMode.COVER: return double.max (width, height);
            default: return double.min (width, height);
            }
        }

        private void update_transform () {
            if (get_width () < 1 || get_height () < 1) {
                notify_property ("zoom");
                queue_draw ();
                return;
            }
            double scale = zoom;
            double max_x = double.max (0, ((turns % 2 == 0 ? image_width : image_height) * scale - get_width ()) / 2);
            double max_y = double.max (0, ((turns % 2 == 0 ? image_height : image_width) * scale - get_height ()) / 2);
            pan_x = pan_x.clamp (-max_x, max_x);
            pan_y = pan_y.clamp (-max_y, max_y);
            notify_property ("zoom");
            queue_draw ();
            transformed ();
        }

        protected override void size_allocate (int width, int height, int baseline) {
            base.size_allocate (width, height, baseline);
            update_transform ();
        }

        public void zoom_to (double value, double x = -1, double y = -1) {
            if (texture == null || !value.is_finite ()) return;
            double low = minimum_zoom.is_finite () ? minimum_zoom.clamp (0.001, 64) : 0.01;
            double high = maximum_zoom.is_finite () ? maximum_zoom.clamp (low, 64) : 64;
            double next = value.clamp (low, high);
            double anchor_x = x >= 0 ? x - get_width () / 2.0 : 0;
            double anchor_y = y >= 0 ? y - get_height () / 2.0 : 0;
            double ratio = next / zoom;
            pan_x = anchor_x - (anchor_x - pan_x) * ratio;
            pan_y = anchor_y - (anchor_y - pan_y) * ratio;
            custom_zoom = next;
            fit_mode = FitMode.CUSTOM;
            update_transform ();
        }

        public void zoom_in () { zoom_to (zoom * 1.25); }
        public void zoom_out () { zoom_to (zoom / 1.25); }
        public void fit_to_view () { pan_x = pan_y = 0; fit_mode = FitMode.CONTAIN; update_transform (); }
        public void actual_size () { fit_mode = FitMode.ACTUAL; update_transform (); }
        public void pan_by (double x, double y) {
            if (!x.is_finite () || !y.is_finite ()) return;
            pan_x += x;
            pan_y += y;
            update_transform ();
        }
        public void rotate_clockwise () { turns = (turns + 1) % 4; update_transform (); }
        public void rotate_counterclockwise () { turns = (turns + 3) % 4; update_transform (); }
        public void flip_horizontal () {
            if (turns % 2 == 0) flipped_x = !flipped_x; else flipped_y = !flipped_y;
            update_transform ();
        }
        public void flip_vertical () {
            if (turns % 2 == 0) flipped_y = !flipped_y; else flipped_x = !flipped_x;
            update_transform ();
        }
        public void reset_view () { turns = 0; flipped_x = flipped_y = false; fit_to_view (); }

        private Graphene.Rect bounds (double x, double y, double width, double height) {
            Graphene.Rect rect = {};
            return rect.init ((float) x, (float) y, (float) width, (float) height);
        }

        protected override void snapshot (Gtk.Snapshot snapshot) {
            if (texture == null) return;
            double scale = zoom;
            double center_x = get_width () / 2.0 + pan_x, center_y = get_height () / 2.0 + pan_y;
            double offset_x = -image_width * scale / 2.0, offset_y = -image_height * scale / 2.0;
            double pixel_zoom = scale * device_scale ();
            var filter = nearest_neighbour ? Gsk.ScalingFilter.NEAREST : Gsk.ScalingFilter.LINEAR;
            Graphene.Point center = {};
            center.init ((float) center_x, (float) center_y);
            snapshot.save ();
            snapshot.translate (center);
            snapshot.rotate (90 * turns);
            snapshot.scale (flipped_x ? -1 : 1, flipped_y ? -1 : 1);
            var preview_rect = bounds (offset_x, offset_y, -2 * offset_x, -2 * offset_y);
            if (animated || texture.get_width () >= image_width * pixel_zoom && (document == null || !document.vector)) {
                snapshot.append_scaled_texture (texture, filter, preview_rect);
                snapshot.restore ();
                return;
            }

            // Invert the quarter-turn and mirror transform for the visible source rectangle.
            double left = double.MAX, top = double.MAX, right = -double.MAX, bottom = -double.MAX;
            for (int i = 0; i < 4; i++) {
                double x = (i % 2 == 0 ? 0 : get_width ()) - center_x;
                double y = (i < 2 ? 0 : get_height ()) - center_y;
                for (int turn = 0; turn < turns; turn++) { double old = x; x = y; y = -old; }
                x = ((flipped_x ? -x : x) - offset_x) / scale;
                y = ((flipped_y ? -y : y) - offset_y) / scale;
                left = double.min (left, x); top = double.min (top, y);
                right = double.max (right, x); bottom = double.max (bottom, y);
            }
            var visible = tiles.visible (left, top, right, bottom, pixel_zoom);
            bool ready = false;
            foreach (var tile in visible) if (tile.texture != null) { ready = true; break; }
            if (ready) {
                // Punch out tile areas so transparent pixels composite exactly once.
                snapshot.push_mask (Gsk.MaskMode.INVERTED_ALPHA);
                Gdk.RGBA opaque = { 0, 0, 0, 1 };
                foreach (var tile in visible) if (tile.texture != null)
                    snapshot.append_color (opaque, bounds (offset_x + tile.x * scale, offset_y + tile.y * scale,
                                                          tile.width * scale, tile.height * scale));
                snapshot.pop ();
            }
            snapshot.append_scaled_texture (texture, filter, preview_rect);
            if (ready) snapshot.pop ();
            foreach (var tile in visible) {
                if (tile.texture == null) continue;
                snapshot.push_clip (bounds (offset_x + tile.x * scale, offset_y + tile.y * scale,
                                            tile.width * scale, tile.height * scale));
                snapshot.append_scaled_texture (tile.texture, filter,
                    bounds (offset_x + tile.decode_x * scale, offset_y + tile.decode_y * scale,
                            tile.decode_width * scale, tile.decode_height * scale));
                snapshot.pop ();
            }
            snapshot.restore ();
        }

        protected override void dispose () {
            if (!disposed) {
                disposed = true;
                watch_output (false);
                invalidate ();
                current_document = null;
                texture = null;
            }
            base.dispose ();
        }
    }
}
