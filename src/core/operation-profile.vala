namespace ImageKit {
    // Machine-readable samples for the existing benchmark runner.
    /** Optional diagnostic timing/event recorder for image operations. */
    internal class OperationProfile {
        /** Starts a profiling interval when diagnostic recording is enabled. */
        internal static int64 begin () {
            return Environment.get_variable ("MAJORIS_IMAGE_PROFILE") == "1"
                ? get_monotonic_time () : 0;
        }

        /** Records an elapsed timing sample under its operation category. */
        internal static void record (string stage, int64 started, uint64 count = 1) {
            if (started != 0) {
                emit (stage, get_monotonic_time () - started, count);
            }
        }

#if !PROFILE_TIMINGS_ONLY
        /** Records a diagnostic event without altering worker or image behavior. */
        internal static void event (string stage) {
            if (Environment.get_variable ("MAJORIS_IMAGE_PROFILE") == "1") {
                emit (stage, 0, 1);
            }
        }
#endif

        /** Outputs accumulated operation metrics to the configured diagnostic channel. */
        private static void emit (string stage, int64 elapsed, uint64 count) {
            stderr.printf ("ImageKit-profile %s %" + int64.FORMAT + " %" + uint64.FORMAT + "\n",
                stage, elapsed, count);
        }
    }
}
