namespace ImageKit {
    public sealed class EncoderRegistry : Object {
        private WorkerClient worker = new WorkerClient ();

        /** Encode a separate file, then move the completed result to its destination. */
        public async void encode (ImageSource source, File destination, ExportOptions options,
                                  RenderOptions? transform = null, Cancellable? cancel = null) throws Error {
            options.validate ();
            if (source.file.equal (destination)) {
                throw new IOError.INVALID_ARGUMENT ("Choose a separate destination for image export");
            }
            var stop = cancel ?? new Cancellable ();
            string output = yield worker.run (source.file, "export", transform, stop, 0, null, options);
            try {
                string extension = options.format == "uhdr" ? "jpg" : options.format;
                var bytes = yield WorkerClient.read_output (output + "." + extension, 268435456, stop);
                yield destination.replace_contents_bytes_async (bytes, null, false,
                    FileCreateFlags.NONE, stop, null);
            } finally {
                WorkerClient.cleanup (output);
            }
        }
    }
}
