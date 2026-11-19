namespace ImageKit {
    /** An admission permit whose lifetime reserves one worker scheduler slot. */
    private class JobTicket : Object {
        public int priority;
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
        private Gee.ArrayList<JobTicket> queue = new Gee.ArrayList<JobTicket> ();
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

        /** Waits for a scheduler slot and returns a ticket that releases it on disposal. */
        internal async void acquire (int priority, Cancellable cancel) throws Error {
            cancel.set_error_if_cancelled ();
            var ticket = new JobTicket ();
            ticket.priority = priority;
            ticket.resume = () => {
                acquire.callback ();
                return Source.REMOVE;
            };
            mutex.lock ();
            queue.add (ticket);
            queue.sort ((a, b) => a.priority - b.priority);
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
            while (queue.size > 0 && active < 4) {
                if (queue[0].priority >= THUMBNAIL_PRIORITY && active >= 3) {
                    return;
                }
                var ticket = queue.remove_at (0);
                active++;
                ticket.granted = true;
                ticket.queued = false;
                ticket.wake ();
            }
        }

        /** Returns this ticket to the shared admission pool without double release. */
        internal void release () {
            mutex.lock ();
            active--;
            dispatch ();
            mutex.unlock ();
        }
    }
}
