namespace MajorisImage {
    /** One decision for base images, animation frames and tiles. */
    internal struct PresentationDecision {
        public PresentationStatus status;
        public bool tone_map;
        public unowned string diagnostic;
    }

    internal class Presentation : Object {
        [CCode (cname = "majoris_image_hdr_output", cheader_filename = "image-texture.h")]
        private extern static int output_state (Gtk.Widget view);

        internal static PresentationDecision inspect (Gtk.Widget view, PresentationPolicy policy, bool hdr) {
            return choose (policy, hdr, output_state (view));
        }

        // This policy also has a verified-output branch for future public GDK
        // backends. The probe must never infer verification from texture support.
        internal static PresentationDecision choose (PresentationPolicy policy, bool hdr, int output) {
            PresentationDecision result = {};
            if (!hdr) {
                result.status = PresentationStatus.SDR;
                result.diagnostic = "SDR source.";
            } else if (policy != PresentationPolicy.SDR && output == 2) {
                result.status = PresentationStatus.NATIVE_HDR;
                result.diagnostic = "Native HDR presentation verified.";
            } else {
                result.tone_map = true;
                result.status = policy == PresentationPolicy.HDR_PREFERRED ?
                    PresentationStatus.HDR_UNAVAILABLE : PresentationStatus.SDR_TONE_MAPPED;
                if (policy == PresentationPolicy.SDR) result.diagnostic = "SDR policy: colour-managed tone mapping.";
                else if (output == 0) result.diagnostic = "No realized output surface. Showing tone-mapped SDR.";
                else result.diagnostic = "GDK cannot verify HDR output for this surface. Showing tone-mapped SDR.";
            }
            return result;
        }

        internal static async PixelBuffer pixels (ImageDocument document, PresentationDecision decision,
                                                   Cancellable stop) throws Error {
            if (!decision.tone_map || document.tone_mapped) return document.preview;
            return yield document.render_region (0, 0, document.width, document.height,
                int.max (document.preview.width, document.preview.height), true, stop);
        }
    }

    internal class AnimationFrame : Object {
        public Gdk.Texture texture;
        public int64 interval;
        public AnimationFrame (Gdk.Texture texture, int64 interval) {
            this.texture = texture;
            this.interval = interval;
        }
    }
}
