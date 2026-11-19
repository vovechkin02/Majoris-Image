namespace ImageKit {
    /** An admission permit whose lifetime reserves one worker scheduler slot. */
    private class JobTicket : Object {
        public int priority;
        /** Monotonically increasing submission number for equal-priority FIFO order. */
        public uint64 sequence;
        public bool granted;
        public bool queued = true;
        public SourceFunc resume;
        public MainContext context = MainContext.ref_thread_default ();
        /** Rechecks the queued scheduler ticket and resumes it when admission is available. */
        public void wake () {
            var idle = new IdleSource ();
            idle.set_callback ((owned) resume);
            idle.attach (context);
        }
    }

    // Shared admission queue with a reserved slot for interactive work.
    /** Coordinates prioritized worker admissions under the process-wide limit. */
    internal class JobScheduler : Object {
        internal const int FOREGROUND_PRIORITY = 0;
        internal const int TILE_PRIORITY = 1;
        internal const int THUMBNAIL_PRIORITY = 2;
        internal const int PRELOAD_PRIORITY = 3;
        private static JobScheduler? instance;
        private Mutex mutex;
        // A heap avoids re-sorting every queued thumbnail or prefetch request.
        // An explicit ordinal preserves FIFO for tickets with identical priority.
        private Gee.PriorityQueue<JobTicket> queue = new Gee.PriorityQueue<JobTicket> ((a, b) => {
            if (a.priority != b.priority) {
                return a.priority < b.priority ? -1 : 1;
            }
            return a.sequence < b.sequence ? -1 : a.sequence > b.sequence ? 1 : 0;
        });
        private uint64 next_sequence;
        private int active;

        /** Returns the shared scheduler controlling worker admission and priority. */
        internal static JobScheduler shared () {
            lock (instance) {
                if (instance == null) {
                    instance = new JobScheduler ();
                }
                return instance;
            }
        }

        /**
         * Waits asynchronously for admission to the shared worker pool.
         *
         * The caller must invoke release() after a successful admission.
         * Cancellation removes a pending request, or releases admission if
         * cancellation occurs after it was granted.
         */
        internal async void acquire (int priority, Cancellable cancel) throws Error {
            cancel.set_error_if_cancelled ();
            var ticket = new JobTicket ();
            ticket.priority = priority;
            ticket.resume = () => {
                acquire.callback ();
                return Source.REMOVE;
            };
            mutex.lock ();
            ticket.sequence = next_sequence++;
            queue.offer (ticket);
            mutex.unlock ();
            ulong handler = cancel.connect (() => {
                mutex.lock ();
                if (ticket.queued) {
                    ticket.queued = false;
                    queue.remove (ticket);
                    ticket.wake ();
                    dispatch ();
                }
                mutex.unlock ();
            });
            mutex.lock ();
            dispatch ();
            mutex.unlock ();
            yield;
            cancel.disconnect (handler);
            if (cancel.is_cancelled ()) {
                if (ticket.granted) {
                    release ();
                }
                cancel.set_error_if_cancelled ();
            }
        }

        /** Admits the next eligible pending worker ticket without violating concurrency limits. */
        private void dispatch () {
            while (!queue.is_empty && active < 4) {
                var next = queue.peek ();
                if (next == null) {
                    return;
                }
                if (next.priority >= THUMBNAIL_PRIORITY && active >= 3) {
                    return;
                }
                var ticket = queue.poll ();
                if (ticket == null) {
                    return;
                }
                active++;
                ticket.granted = true;
                ticket.queued = false;
                ticket.wake ();
            }
        }

        /**
         * Releases one previously granted worker admission and dispatches
         * the next eligible waiting request. Call exactly once per admission.
         */
        internal void release () {
            mutex.lock ();
            active--;
            dispatch ();
            mutex.unlock ();
        }
    }
}
