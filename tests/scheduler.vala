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

int main () {
    var loop = new MainLoop ();
    verify.begin ((source, result) => {
        try { verify.end (result); }
        catch (Error error) { assert_not_reached (); }
        loop.quit ();
    });
    loop.run ();
    return 0;
}
