namespace ImageKit {
    public int main (string[] args) {
#if WORKER_PROFILING
        int64 started = get_monotonic_time ();
#endif
        if (args.length != 6 || Vips.init ("majoris-image-worker") != 0) {
            return 2;
        }
        Vips.cache_set_max_mem (128 * 1024 * 1024);
        Vips.concurrency_set (2);
        try {
#if WORKER_PROFILING
            int64 initialized = get_monotonic_time ();
#endif
            WorkerDispatcher.run (args[1], args[2], args[3], args[4], args[5]);
#if WORKER_PROFILING
            stderr.printf ("ImageKit-profile worker-initialization %" + int64.FORMAT + " 1\n",
                initialized - started);
            stderr.printf ("ImageKit-profile worker-operation %" + int64.FORMAT + " 1\n",
                get_monotonic_time () - initialized);
#endif
            return 0;
        } catch (Error e) {
            stderr.printf ("%s\n", WorkerFailure.serialize (e));
            return 1;
        }
    }
}
