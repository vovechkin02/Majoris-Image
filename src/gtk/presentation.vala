namespace ImageKit {
    /** A snapshot of available output color/HDR capabilities. */
    internal enum OutputCapability {
        UNREALIZED, SDR, VERIFIED_HDR, UNKNOWN
    }

    /** One decision for base images, animation frames and tiles. */
    internal struct PresentationDecision {
        public PresentationStatus status;
        public bool tone_map;
        public unowned string diagnostic;
    }

    /** Chooses an output color state and constructs presentation-ready textures. */
    internal class Presentation : Object {
#if !PRESENTATION_POLICY_ONLY
        /** Inspects output and compositor support for color-aware HDR presentation. */
        private static OutputCapability output_capability (Gtk.Widget view) {
            var native = view.get_native ();
            var surface = native?.get_surface ();
            var renderer = native?.get_renderer ();
            if (!view.get_mapped () || surface == null || !surface.get_mapped () ||
                renderer == null || !renderer.is_realized ()) {
                return OutputCapability.UNREALIZED;
            }
            var display = surface.get_display ();
            if (display.get_monitor_at_surface (surface) == null) {
                return OutputCapability.UNKNOWN;
            }
            // The Cairo renderer presents Cairo image surfaces with SDR color.
            if (renderer is Gsk.CairoRenderer) {
                return OutputCapability.SDR;
            }
            // Public GDK has no surface-level HDR negotiation or scanout query.
            return OutputCapability.UNKNOWN;
        }

        /** Reads output capability information without changing the current presentation. */
        internal static PresentationDecision inspect (Gtk.Widget view,
            PresentationPolicy policy, bool hdr) {
            return choose (policy, hdr, output_capability (view));
        }
#endif

        /** Selects the requested output color policy subject to detected capability. */
        internal static PresentationDecision choose (PresentationPolicy policy, bool hdr,
            OutputCapability output) {
            PresentationDecision result = {};
            if (!hdr) {
                result.status = PresentationStatus.SDR;
                result.diagnostic = "SDR source.";
            } else if (policy != PresentationPolicy.SDR &&
                       output == OutputCapability.VERIFIED_HDR) {
                result.status = PresentationStatus.NATIVE_HDR;
                result.diagnostic = "Native HDR presentation verified.";
            } else {
                result.tone_map = true;
                result.status = policy == PresentationPolicy.HDR_PREFERRED ?
                    PresentationStatus.HDR_UNAVAILABLE : PresentationStatus.SDR_TONE_MAPPED;
                if (policy == PresentationPolicy.SDR) {
                    result.diagnostic = "SDR policy: color-managed tone mapping.";
                } else if (output == OutputCapability.UNREALIZED) {
                    result.diagnostic = "No realized output surface. Showing tone-mapped SDR.";
                } else if (output == OutputCapability.SDR) {
                    result.diagnostic = "SDR renderer. Showing tone-mapped SDR.";
                } else {
                    result.diagnostic = "GDK cannot verify HDR output for this surface. Showing tone-mapped SDR.";
                }
            }
            return result;
        }

#if !PRESENTATION_POLICY_ONLY
        /** Produces a diagnostic description for an externally owned texture. */
        [CCode (cname = "image_texture_external_description", cheader_filename = "image-texture.h")]
        private extern static unowned string external_description (Gdk.Texture texture,
            out bool sdr, out bool hdr);

        /** Describes an already constructed texture without taking ownership of its pixels. */
        internal static PresentationDecision external (Gdk.Texture texture, out bool hdr) {
            PresentationDecision decision = {};
            bool sdr;
            decision.diagnostic = external_description (texture, out sdr, out hdr);
            decision.status = sdr ? PresentationStatus.SDR : PresentationStatus.EXTERNAL_UNVERIFIED;
            return decision;
        }

        /** Converts a decoded buffer into a texture for the selected output color state. */
        internal static async PixelBuffer render_pixels (ImageDocument document,
            PresentationDecision decision, Cancellable stop) throws Error {
            return yield document.render_preview (decision.tone_map, stop);
        }
#endif
    }
}
