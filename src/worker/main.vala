namespace ImageKit {
    public int main (string[] args) {
        if (args.length != 6 || Vips.init ("majoris-image-worker") != 0) {
            return 2;
        }
        Vips.cache_set_max_mem (128 * 1024 * 1024);
        Vips.concurrency_set (2);
        try {
            WorkerDispatcher.run (args[1], args[2], args[3], args[4], args[5]);
            return 0;
        } catch (Error e) {
            stderr.printf ("%s\n", WorkerFailure.serialize (e));
            return 1;
        }
    }
}
