[CCode (gir_namespace = "MajorisImageGtk", gir_version = "1.0")]
namespace MajorisImage {
    public enum ViewState { EMPTY, LOADING, PREVIEW_READY, READY, UNSUPPORTED, DECODER_UNAVAILABLE, CORRUPT, CANCELLED, ERROR }
    public enum FitMode { CONTAIN, WIDTH, HEIGHT, ACTUAL, COVER, CUSTOM }
    public enum PresentationPolicy { AUTOMATIC, SDR, HDR_PREFERRED }
    public enum PresentationStatus { NONE, SDR, SDR_TONE_MAPPED }

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
            return hdr_source ? "This widget has no verified HDR presentation path. It uses SDR tone mapping." : "";
        } }
        public bool animated { get; private set; }
        public bool playing { get; private set; }
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
#if HAVE_GLYCIN
        private Gly.Image? animation;
        private int64 frame_delay;
        private int frames_presented;
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
            map.connect (() => {
                tiles.reset (document);
#if HAVE_GLYCIN
                if (document != null && document.animated && animation == null && !frame_pending)
                    prepare_animation.begin (document.source.file, generation, playback);
#endif
            });
            unmap.connect (() => {
                pending.cancel ();
                tiles.reset (null);
                pause ();
#if HAVE_GLYCIN
                animation = null;
#endif
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
#if HAVE_GLYCIN
            animation = null;
#endif
            animated = false;
            tiles.reset (null);
            return generation;
        }

        public void clear () {
            invalidate ();
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
            update_transform ();
        }

        public void set_texture (Gdk.Texture value) {
            clear ();
            texture = value;
            image_width = value.get_width ();
            image_height = value.get_height ();
            page_count = 1;
            presentation_status = PresentationStatus.SDR;
            state = ViewState.READY;
            update_transform ();
        }

        public void set_document (ImageDocument value) {
            clear ();
            if (!value.hdr || value.tone_mapped) {
                show_document (value);
                return;
            }
            state = ViewState.LOADING;
            show_sdr.begin (value, generation, pending);
        }

        private async void show_sdr (ImageDocument value, uint request, Cancellable stop) {
            try {
                var pixels = yield value.render_region (0, 0, value.width, value.height, 1600, true, stop);
                if (request == generation) show_document (value, pixels);
            } catch (Error e) { if (request == generation) show_error (e); }
        }

        private void show_document (ImageDocument value, PixelBuffer? presentation = null) {
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
            notify_property ("presentation-diagnostic");
            presentation_status = hdr_source ? PresentationStatus.SDR_TONE_MAPPED : PresentationStatus.SDR;
            if (get_mapped ()) tiles.reset (document);
            state = value.raw_state == RawState.RAW_PREVIEW_AVAILABLE ? ViewState.PREVIEW_READY : ViewState.READY;
            update_transform ();
        }

        public async void load_file (File file, DecodeOptions? options = null, Cancellable? cancel = null) throws Error {
            clear ();
            uint request = generation;
            var stop = pending;
            ulong handler = cancel != null ? cancel.connect (() => stop.cancel ()) : 0;
            state = ViewState.LOADING;
            var decode = (options ?? new DecodeOptions ()).copy ();
            decode.sdr_presentation = true;
            var source = new ImageSource (file);
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
                        if (request == generation) show_document (preview);
                    } catch (Error e) { stop.set_error_if_cancelled (); }
                }
                try {
                    var result = yield registry.open (source, decode, stop);
                    stop.set_error_if_cancelled ();
                    if (request == generation) show_document (result);
#if HAVE_GLYCIN
                    if (request == generation && result.page == 0 && !result.hdr &&
                        (result.animated || result.mime_type in new string[] { "image/png", "image/gif", "image/webp" }))
                        yield prepare_animation (file, request, stop);
#endif
                } catch (Error e) {
                    if (preview == null || stop.is_cancelled ()) throw e;
                    if (request == generation) warning = "Full RAW development failed: " + e.message;
                }
            } catch (Error e) {
                if (request == generation) show_error (e);
                throw e;
            } finally { if (handler != 0) cancel.disconnect (handler); }
        }

        private void show_error (Error error) {
            error_message = error.message;
            if (error is IOError.CANCELLED) state = ViewState.CANCELLED;
            else if (error is ImageError.UNSUPPORTED || error is ImageError.RAW_UNSUPPORTED) state = ViewState.UNSUPPORTED;
            else if (error is ImageError.CODEC_MISSING) state = ViewState.DECODER_UNAVAILABLE;
            else if (error is ImageError.CORRUPT) state = ViewState.CORRUPT;
            else state = ViewState.ERROR;
        }

        public async void set_page (int page, Cancellable? cancel = null) throws Error {
            if (document == null) throw new IOError.INVALID_ARGUMENT ("No image is loaded");
            var source = document;
            uint request = invalidate ();
            var stop = pending;
            ulong handler = cancel != null ? cancel.connect (() => stop.cancel ()) : 0;
            state = ViewState.LOADING;
            try {
                var result = yield source.open_page (page, stop);
                if (request == generation) set_document (result);
            } catch (Error e) {
                if (request == generation) show_error (e);
                throw e;
            } finally { if (handler != 0) cancel.disconnect (handler); }
        }

#if HAVE_GLYCIN
        private async void prepare_animation (File file, uint request, Cancellable stop) {
            try {
                var loader = new Gly.Loader (file);
                loader.set_sandbox_selector (Gly.SandboxSelector.BWRAP);
                var image = yield loader.load_async (stop);
                var frame = yield image.next_frame_async (stop);
                if (request != generation || frame.get_delay () <= 0 || frame.get_delay () == int64.MAX) return;
                animation = image;
                frames_presented = 1;
                animated = true;
                frame_delay = frame.get_delay ();
                texture = GlyGtk4.frame_get_texture (frame);
                tiles.reset (null);
                queue_draw ();
            } catch (Error e) {
                if (request == generation && document != null && document.animated && !stop.is_cancelled ())
                    warning = "Animation unavailable: " + e.message;
            }
        }
#endif

        public void play () {
            if (!animated || playing || !get_mapped ()) return;
            playing = true;
#if HAVE_GLYCIN
            if (animation == null || playback_finished ()) {
                restart_animation.begin ();
                return;
            }
#endif
            schedule_frame ();
        }
        public void pause () {
            playing = false;
            playback_generation++;
            playback.cancel ();
            playback = new Cancellable ();
            if (animation_timer != 0) { Source.remove (animation_timer); animation_timer = 0; }
        }
        public void step_frame () {
            pause ();
            if (animated) next_frame.begin ();
        }
#if HAVE_GLYCIN
        private bool playback_finished () {
            return document != null && document.frame_count > 1 && document.loop_count > 0 &&
                (int64) frames_presented >= (int64) document.frame_count * document.loop_count;
        }

        private async void restart_animation () {
            if (document == null) { pause (); return; }
            uint request = generation;
            yield prepare_animation (document.source.file, request, playback);
            if (generation == request && playing) {
                if (animation == null) pause ();
                else schedule_frame ();
            }
        }
#endif

        private void schedule_frame () {
#if HAVE_GLYCIN
            if (!playing || frame_pending || animation_timer != 0) return;
            if (playback_finished ()) { pause (); return; }
            animation_timer = Timeout.add ((uint) (frame_delay / 1000).clamp (10, 600000), () => {
                animation_timer = 0;
                next_frame.begin ();
                return Source.REMOVE;
            });
#endif
        }
        private async void next_frame () {
#if HAVE_GLYCIN
            if (animation == null || frame_pending) return;
            frame_pending = true;
            var image = animation;
            uint request = generation, playback_request = playback_generation;
            var stop = playback;
            try {
                var frame = yield image.next_frame_async (stop);
                if (request == generation && playback_request == playback_generation) {
                    frames_presented++;
                    texture = GlyGtk4.frame_get_texture (frame);
                    frame_delay = frame.get_delay ();
                    queue_draw ();
                }
            } catch (Error e) {
                if (!stop.is_cancelled () && request == generation) { pause (); warning = e.message; }
            } finally {
                frame_pending = false;
                schedule_frame ();
            }
#endif
        }

        private double device_scale () {
            var surface = get_native ()?.get_surface ();
            return surface != null ? surface.get_scale () : get_scale_factor ();
        }

        private double effective_zoom () {
            if (texture == null || image_width < 1 || image_height < 1 || get_width () < 1 || get_height () < 1) return 1;
            double width = (double) get_width () / (turns % 2 == 0 ? image_width : image_height);
            double height = (double) get_height () / (turns % 2 == 0 ? image_height : image_width);
            switch (fit_mode) {
            case FitMode.WIDTH: return width;
            case FitMode.HEIGHT: return height;
            case FitMode.ACTUAL: return 1.0 / device_scale ();
            case FitMode.COVER: return double.max (width, height);
            case FitMode.CUSTOM: return custom_zoom;
            default: return double.min (width, height);
            }
        }

        private void update_transform () {
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
            invalidate ();
            base.dispose ();
        }
    }
}
