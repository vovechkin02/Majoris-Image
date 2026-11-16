namespace ImageKit {
    public sealed class EncoderRegistry : Object {
        private const int64 MAX_EXPORT_BYTES = 268435456;

        private WorkerClient worker = new WorkerClient ();

        [CCode (cname = "image_export_prepare_async",
                finish_name = "image_export_prepare_finish",
                cheader_filename = "export-transfer.h")]
        private static extern async void* prepare_export (File source, File destination,
                                                           Cancellable stop) throws Error;

        [CCode (cname = "image_export_source_free", cheader_filename = "export-transfer.h")]
        private static extern void release_export_source (void* protected_source);

        [CCode (cname = "image_export_transfer_async",
                finish_name = "image_export_transfer_finish", cheader_filename = "export-transfer.h")]
        private static extern async Bytes? transfer_output (string path, File destination,
                                                             void* protected_source,
                                                             int64 limit, Cancellable stop)
            throws Error;

        /** Encode a separate file, then move the completed result to its destination. */
        public async void encode (ImageSource source, File destination, ExportOptions options,
                                  RenderOptions? transform = null, Cancellable? cancel = null) throws Error {
            options.validate ();
            if (source.file.equal (destination)) {
                throw new IOError.INVALID_ARGUMENT ("Choose a separate destination for image export");
            }
            var stop = cancel ?? new Cancellable ();
            void* protected_source = null;
            if (source.file.is_native () && destination.is_native ()) {
                protected_source = yield prepare_export (source.file, destination, stop);
            }
            string? output = null;
            try {
                output = yield worker.run (source.file, "export", transform, stop, 0, null,
                    options, false, protected_source);
                string extension = options.format == "uhdr" ? "jpg" : options.format;
                int64 started = OperationProfile.begin ();
                try {
                    var payload = yield transfer_output (output + "." + extension, destination,
                        protected_source, MAX_EXPORT_BYTES, stop);
                    // Null means native publication completed. Bytes are validated for GIO.
                    if (payload != null) {
                        // Backend-specific replacement begins only after validation succeeds.
                        stop.set_error_if_cancelled ();
                        yield destination.replace_contents_bytes_async (payload, null, false,
                            FileCreateFlags.NONE, stop, null);
                    }
                } finally {
                    OperationProfile.record ("export-transfer", started);
                }
            } finally {
                if (output != null) {
                    WorkerClient.cleanup (output);
                }
                release_export_source (protected_source);
            }
        }
    }
}
