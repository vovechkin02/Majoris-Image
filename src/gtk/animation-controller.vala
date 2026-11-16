namespace ImageKit {
    internal class AnimationFrame : Object {
        public Gdk.Texture texture;
        public int64 duration_us;
        public ImageDocument? source;
        public PresentationDecision decision;

        public AnimationFrame (Gdk.Texture texture, int64 duration_us) {
            this.texture = texture;
            this.duration_us = duration_us;
        }
    }

    internal class AnimationController : Object {
        // Timers and in-flight frame loads may outlive the widget.
        private weak ImageView? view;
        private bool playback_requested;
        private uint animation_timer;
        private bool frame_pending;
        private uint playback_generation;
        private Cancellable playback = new Cancellable ();
        private int64 frame_delay_us;
        private int64 frames_presented;
        private AnimationFrame? prefetched;
        private bool advance_due;
        private bool manual_advance;

        internal AnimationController (ImageView view) {
            this.view = view;
        }

        internal void reset () {
            pause ();
            cancel_frame_work ();
            frame_pending = advance_due = manual_advance = false;
            prefetched = null;
            frames_presented = 0;
        }

        internal void detach () {
            view = null;
        }

        internal void set_initial_frame (ImageDocument source) {
            frame_delay_us = source.frame_duration_us;
            frames_presented = source.frame + 1;
        }

        internal void resume_if_requested () {
            if (playback_requested) {
                play ();
            }
        }

        internal void hold_for_presentation () {
            prefetched = null;
            stop_timer ();
        }

        internal void resume_after_presentation () {
            if (advance_due) {
                advance_when_ready ();
            } else {
                schedule_frame ();
            }
        }

        private void stop_timer () {
            if (animation_timer != 0) {
                Source.remove (animation_timer);
                animation_timer = 0;
            }
        }

        private void cancel_frame_work () {
            playback_generation++;
            playback.cancel ();
            playback = new Cancellable ();
        }

        private bool is_current (uint request) {
            return view != null && view.is_current (request);
        }

        private void publish_frame (AnimationFrame frame, int index, int64 count) {
            frames_presented = count;
            frame_delay_us = frame.duration_us;
            view?.show_animation_frame (frame, index);
        }

        internal void play () {
            if (view == null || view.is_disposed () || !view.animated) {
                return;
            }
            playback_requested = true;
            if (view.playing || !view.get_mapped ()) {
                return;
            }
            view.update_playing (true);
            if (playback_finished ()) {
                uint request = view.get_document_generation ();
                seek_frame.begin (0, null, (object, reply) => {
                    try {
                        seek_frame.end (reply);
                    } catch (Error error) {
                        if (is_current (request) && !(error is IOError.CANCELLED)) {
                            view.report_animation_error (error);
                        }
                    }
                });
            } else {
                schedule_frame ();
            }
        }

        internal void suspend () {
            stop_timer ();
            advance_due = manual_advance = false;
            view?.update_playing (false);
        }

        internal void pause () {
            playback_requested = false;
            suspend ();
        }

        internal void step_frame () {
            pause ();
            if (view == null || !view.animated) {
                return;
            }
            manual_advance = advance_due = true;
            advance_when_ready ();
        }

        internal async void seek_frame (int frame, Cancellable? cancel = null) throws Error {
            var source = view?.document;
            if (view == null || view.is_disposed () || source == null || !view.animated ||
                frame < 0 || frame >= source.frame_count) {
                throw new IOError.INVALID_ARGUMENT ("Frame is outside the active sequence");
            }
            stop_timer ();
            cancel_frame_work ();
            var stop = playback;
            ulong handler = cancel != null ? cancel.connect (() => stop.cancel ()) : 0;
            prefetched = null;
            advance_due = manual_advance = false;
            frame_pending = true;
            uint request = view.get_document_generation ();
            uint seek = playback_generation;
            try {
                var loaded = yield load_frame (source, frame, stop);
                stop.set_error_if_cancelled ();
                if (is_current (request) && seek == playback_generation) {
                    publish_frame (loaded, frame, frame + 1);
                }
            } finally {
                if (handler != 0) {
                    cancel.disconnect (handler);
                }
                if (is_current (request) && seek == playback_generation) {
                    frame_pending = false;
                    schedule_frame ();
                }
            }
        }

        private bool playback_finished () {
            var source = view?.document;
            return source != null && source.frame_count > 1 && source.loop_count > 0 &&
                frames_presented >= (int64) source.frame_count * source.loop_count;
        }

        private void schedule_frame () {
            if (view == null || view.is_disposed () || !view.playing || !view.get_mapped () ||
                view.is_presentation_pending () || animation_timer != 0 || advance_due) {
                return;
            }
            animation_timer = Timeout.add ((uint) (frame_delay_us / 1000).clamp (10, uint.MAX), () => {
                animation_timer = 0;
                if (playback_finished ()) {
                    pause ();
                } else {
                    advance_due = true;
                    advance_when_ready ();
                }
                return Source.REMOVE;
            });
            if (!playback_finished () && view.document != null &&
                view.document.preview.bytes.get_size () <= 64 * 1024 * 1024) {
                prefetch.begin ();
            }
        }

        private int next_frame_index () {
            var source = view?.document;
            return source != null && source.frame_count > 1
                ? (view.current_frame + 1) % source.frame_count
                : (int) ((frames_presented + 1) % int.MAX);
        }

        private void advance_when_ready () {
            if (view == null || view.is_presentation_pending () || !advance_due ||
                (!manual_advance && (!view.playing || !view.get_mapped ()))) {
                return;
            }
            if (prefetched == null) {
                prefetch.begin ();
                return;
            }
            var loaded = prefetched;
            prefetched = null;
            if (loaded.source != null && loaded.source.hdr &&
                loaded.decision.tone_map != Presentation.inspect (view, view.presentation_policy, true).tone_map) {
                prefetch.begin ();
                return;
            }
            advance_due = manual_advance = false;
            publish_frame (loaded, next_frame_index (), frames_presented + 1);
            schedule_frame ();
        }

        private async AnimationFrame load_frame (ImageDocument source, int index,
            Cancellable stop) throws Error {
            var frame = yield source.open_frame (index, stop);
            while (true) {
                if (view == null) {
                    throw new IOError.CANCELLED ("Viewer was disposed");
                }
                var decision = Presentation.inspect (view, view.presentation_policy, frame.hdr);
                var pixels = yield Presentation.render_pixels (frame, decision, stop);
                stop.set_error_if_cancelled ();
                if (decision.tone_map != Presentation.inspect (view, view.presentation_policy, frame.hdr).tone_map) {
                    continue;
                }
                var result = new AnimationFrame (texture_for_pixels (pixels), frame.frame_duration_us);
                result.source = frame;
                result.decision = decision;
                return result;
            }
        }

        private async void prefetch () {
            var source = view?.document;
            if (source == null || prefetched != null || frame_pending) {
                return;
            }
            frame_pending = true;
            int index = next_frame_index ();
            uint request = view.get_document_generation ();
            uint seek = playback_generation;
            var stop = playback;
            try {
                var loaded = yield load_frame (source, index, stop);
                if (is_current (request) && seek == playback_generation) {
                    prefetched = loaded;
                }
            } catch (Error error) {
                if (!stop.is_cancelled () && is_current (request) && seek == playback_generation) {
                    var owner = view;
                    pause ();
                    owner.report_animation_error (error);
                }
            } finally {
                if (is_current (request) && seek == playback_generation) {
                    frame_pending = false;
                    advance_when_ready ();
                }
            }
        }
    }
}
