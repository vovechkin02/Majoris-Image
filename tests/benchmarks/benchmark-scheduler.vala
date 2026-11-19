using ImageKit;

/** Profiles queue insertion, queued cancellation and admission independently. */
private async void measure_burst (int total, int cancel_percent) throws Error {
    var scheduler = JobScheduler.shared ();
    var blocker = new Cancellable ();
    // Saturate admissions so every submitted ticket remains queued.
    for (int index = 0; index < 4; index++) {
        yield scheduler.acquire (JobScheduler.FOREGROUND_PRIORITY, blocker);
    }

    int completed = 0;
    int canceled = 0;
    int expected_canceled = 0;
    int accepted = 0;
    var cancellations = new Gee.ArrayList<Cancellable> ();
    int64 started = get_monotonic_time ();
    for (int index = 0; index < total; index++) {
        int priority = index % (JobScheduler.PRELOAD_PRIORITY + 1);
        var token = new Cancellable ();
        bool cancel_ticket = (index * 17 % 100) < cancel_percent;
        scheduler.acquire.begin (priority, token, (source, result) => {
            try {
                scheduler.acquire.end (result);
                assert (!cancel_ticket);
                accepted++;
                scheduler.release ();
            } catch (IOError.CANCELLED error) {
                assert (cancel_ticket);
                canceled++;
            } catch (Error error) {
                assert_not_reached ();
            }
            completed++;
        });
        if (cancel_ticket) {
            expected_canceled++;
            cancellations.add (token);
        }
    }
    int64 submit_us = get_monotonic_time () - started;
    started = get_monotonic_time ();
    foreach (var token in cancellations) {
        token.cancel ();
    }
    int64 cancel_us = get_monotonic_time () - started;

    started = get_monotonic_time ();
    for (int index = 0; index < 4; index++) {
        scheduler.release ();
    }
    int64 deadline = get_monotonic_time () + 30 * TimeSpan.SECOND;
    while (completed != total && get_monotonic_time () < deadline) {
        var idle = new IdleSource ();
        idle.set_callback (() => {
            measure_burst.callback ();
            return Source.REMOVE;
        });
        idle.attach (MainContext.get_thread_default ());
        yield;
    }
    if (completed != total || canceled != expected_canceled ||
        accepted != total - expected_canceled) {
        throw new IOError.FAILED ("Scheduler cancellation benchmark violated admission results");
    }
    int64 drain_us = get_monotonic_time () - started;
    stdout.printf ("ImageKit-profile scheduler-queue %" + int64.FORMAT + " 1\n", submit_us);
    stdout.printf ("ImageKit-profile scheduler-cancel %" + int64.FORMAT + " 1\n", cancel_us);
    stdout.printf ("ImageKit-profile scheduler-drain %" + int64.FORMAT + " 1\n", drain_us);
    stdout.printf ("Scheduler accepted=%d canceled=%d total=%d canceled_percent=%d\n",
        accepted, canceled, total, cancel_percent);
}

/** Runs a deterministic cancellation-heavy queue workload for the benchmarks suite. */
int main (string[] args) {
    if (args.length != 3) {
        stderr.printf ("Usage: benchmark-scheduler COUNT CANCELLATION_PERCENT\n");
        return 2;
    }
    int total = int.parse (args[1]);
    int percent = int.parse (args[2]);
    if (total < 1 || total > 20000 || percent < 0 || percent > 100) {
        return 2;
    }
    var loop = new MainLoop ();
    int status = 0;
    measure_burst.begin (total, percent, (source, result) => {
        try {
            measure_burst.end (result);
        } catch (Error error) {
            stderr.printf ("Scheduler benchmark failed: %s\n", error.message);
            status = 1;
        }
        loop.quit ();
    });
    loop.run ();
    return status;
}
