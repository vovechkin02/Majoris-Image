namespace ImageKit {
    // Internal loader decisions. Keep these labels out of the image and metadata consumers.
    /** Centralizes worker-local OpenEXR and HEIF loader identity predicates. */
    internal class WorkerLoaderPolicy : Object {
        /** Detects the exact loader identifier requiring native OpenEXR handling. */
        internal static bool uses_native_exr (string loader) {
            return loader == "openexr-native";
        }

        /** Detects loaders that require HEIF metadata annotation. */
        internal static bool needs_heif_annotation (string loader) {
            // Keep the existing substring behavior for compatible libvips loader names.
            return loader.contains ("heif");
        }
    }
}
