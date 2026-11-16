namespace ImageKit {
    // Machine-readable samples for the existing benchmark runner.
    internal class OperationProfile {
        internal static int64 begin () {
            return Environment.get_variable ("MAJORIS_IMAGE_PROFILE") == "1"
                ? get_monotonic_time () : 0;
        }

        internal static void record (string stage, int64 started, uint64 count = 1) {
            if (started != 0) {
                stderr.printf ("ImageKit-profile %s %" + int64.FORMAT + " %" + uint64.FORMAT + "\n",
                    stage, get_monotonic_time () - started, count);
            }
        }
    }
}
