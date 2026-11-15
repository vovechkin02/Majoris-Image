namespace MajorisImage {
    internal enum OutputCapability {
        UNREALIZED, SDR, VERIFIED_HDR, UNKNOWN
    }

    /** One decision for base images, animation frames and tiles. */
    internal struct PresentationDecision {
        public PresentationStatus status;
        public bool tone_map;
        public unowned string diagnostic;
    }

    internal class Presentation : Object {
        private static OutputCapability output_capability (Gtk.Widget view) {
            var native = view.get_native ();
            var surface = native?.get_surface ();
            var renderer = native?.get_renderer ();
            if (!view.get_mapped () || surface == null || !surface.get_mapped () ||
                renderer == null || !renderer.is_realized ()) return OutputCapability.UNREALIZED;
            var display = surface.get_display ();
            if (display.get_monitor_at_surface (surface) == null) return OutputCapability.UNKNOWN;
            // The Cairo renderer presents Cairo image surfaces with SDR colour.
            if (renderer is Gsk.CairoRenderer) return OutputCapability.SDR;
            // Public GDK has no surface-level HDR negotiation or scanout query.
            return OutputCapability.UNKNOWN;
        }

        internal static PresentationDecision inspect (Gtk.Widget view, PresentationPolicy policy, bool hdr) {
            return choose (policy, hdr, output_capability (view));
        }

        internal static PresentationDecision choose (PresentationPolicy policy, bool hdr, OutputCapability output) {
            PresentationDecision result = {};
            if (!hdr) {
                result.status = PresentationStatus.SDR;
                result.diagnostic = "SDR source.";
            } else if (policy != PresentationPolicy.SDR && output == OutputCapability.VERIFIED_HDR) {
                result.status = PresentationStatus.NATIVE_HDR;
                result.diagnostic = "Native HDR presentation verified.";
            } else {
                result.tone_map = true;
                result.status = policy == PresentationPolicy.HDR_PREFERRED ?
                    PresentationStatus.HDR_UNAVAILABLE : PresentationStatus.SDR_TONE_MAPPED;
                if (policy == PresentationPolicy.SDR) result.diagnostic = "SDR policy: colour-managed tone mapping.";
                else if (output == OutputCapability.UNREALIZED) result.diagnostic = "No realized output surface. Showing tone-mapped SDR.";
                else if (output == OutputCapability.SDR) result.diagnostic = "SDR renderer. Showing tone-mapped SDR.";
                else result.diagnostic = "GDK cannot verify HDR output for this surface. Showing tone-mapped SDR.";
            }
            return result;
        }

        internal static async PixelBuffer pixels (ImageDocument document, PresentationDecision decision,
                                                   Cancellable stop) throws Error {
            return yield document.render_preview (decision.tone_map, stop);
        }
    }

    internal class AnimationFrame : Object {
        public Gdk.Texture texture;
        public int64 interval;
        public ImageDocument? source;
        public PresentationDecision decision;
        public AnimationFrame (Gdk.Texture texture, int64 interval) {
            this.texture = texture;
            this.interval = interval;
        }
    }
}
