using ImageKit;

private async void wait_tick () {
    Idle.add (() => { wait_tick.callback (); return Source.REMOVE; });
    yield;
}

private async void verify () throws Error {
    var scheduler = JobScheduler.shared ();
    var open = new Cancellable ();
    var canceled = new Cancellable ();
    for (int i = 0; i < 3; i++) yield scheduler.acquire (JobScheduler.PRELOAD_PRIORITY, open);
    yield scheduler.acquire (0, open);

    var order = new Gee.ArrayList<int> ();
    bool cancellation_seen = false;
    scheduler.acquire.begin (JobScheduler.PRELOAD_PRIORITY, canceled, (source, result) => {
        try { scheduler.acquire.end (result); assert_not_reached (); }
        catch (IOError.CANCELLED error) { cancellation_seen = true; }
        catch (Error error) { assert_not_reached (); }
    });
    foreach (int priority in new int[] {JobScheduler.PRELOAD_PRIORITY, 1, 0}) {
        int expected = priority;
        scheduler.acquire.begin (expected, open, (source, result) => {
            try { scheduler.acquire.end (result); }
            catch (Error error) { assert_not_reached (); }
            order.add (expected);
            scheduler.release ();
        });
    }
    canceled.cancel ();
    scheduler.release ();
    for (int i = 0; i < 100 && (order.size < 2 || !cancellation_seen); i++) yield wait_tick ();
    assert (cancellation_seen && order.size == 2);
    assert (order[0] == 0 && order[1] == 1);
    scheduler.release ();
    for (int i = 0; i < 100 && order.size < 3; i++) yield wait_tick ();
    assert (order.size == 3 && order[2] == JobScheduler.PRELOAD_PRIORITY);
    scheduler.release ();
    scheduler.release ();
}

private async void verify_seeded_queue () {
    const uint32 seed = 0x11954dc2;
    stderr.printf ("Scheduler seed %#x\n", seed);
    var random = new Rand.with_seed (seed);
    var scheduler = JobScheduler.shared ();
    int running = 0, complete = 0, canceled = 0, expected_canceled = 0;
    const int total = 120;
    for (int index = 0; index < total; index++) {
        int priority = random.int_range (0, JobScheduler.PRELOAD_PRIORITY + 1);
        bool should_cancel = random.int_range (0, 5) == 0;
        var stop = new Cancellable ();
        scheduler.acquire.begin (priority, stop, (source, result) => {
            try {
                scheduler.acquire.end (result);
                assert (!should_cancel);
                assert (++running <= 4);
                Idle.add (() => {
                    running--;
                    complete++;
                    scheduler.release ();
                    return Source.REMOVE;
                });
            } catch (IOError.CANCELLED error) {
                assert (should_cancel);
                canceled++;
                complete++;
            } catch (Error error) {
                assert_not_reached ();
            }
        });
        if (should_cancel) {
            expected_canceled++;
            stop.cancel ();
        }
    }
    int64 deadline = get_monotonic_time () + 4 * TimeSpan.SECOND;
    while (complete < total && get_monotonic_time () < deadline) yield wait_tick ();
    if (complete != total || running != 0 || canceled != expected_canceled)
        error ("Scheduler seed %#x: %d/%d completed, %d remaining, %d/%d canceled",
            seed, complete, total, running, canceled, expected_canceled);
}

int main () {
    var loop = new MainLoop ();
    verify.begin ((source, result) => {
        try { verify.end (result); }
        catch (Error error) { assert_not_reached (); }
        verify_seeded_queue.begin ((object, finished) => {
            verify_seeded_queue.end (finished);
            loop.quit ();
        });
    });
    loop.run ();
    return 0;
}
