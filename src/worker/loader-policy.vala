namespace ImageKit {
    // Internal loader decisions. Keep these labels out of the image and metadata consumers.
    internal class WorkerLoaderPolicy : Object {
        internal static bool uses_native_exr (string loader) {
            return loader == "openexr-native";
        }

        internal static bool needs_heif_annotation (string loader) {
            // Keep the existing substring behavior for compatible libvips loader names.
            return loader.contains ("heif");
        }
    }
}
