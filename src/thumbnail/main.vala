int main (string[] args) {
    if (args.length != 4) {
        stderr.printf ("Usage: majoris-image-thumbnailer INPUT OUTPUT EDGE\n");
        return 2;
    }
    var loop = new MainLoop ();
    int result = 1;
    var service = new ImageKit.ThumbnailService ();
    var source = new ImageKit.ImageSource (File.new_for_commandline_arg (args[1]));
    var stop = new Cancellable ();
    var sigterm = new Unix.SignalSource (Posix.Signal.TERM);
    sigterm.set_callback (() => {
        stop.cancel ();
        return Source.REMOVE;
    });
    sigterm.attach (null);
    service.generate.begin (source, int.parse (args[3]), stop, (obj, response) => {
        try {
            var bytes = service.generate.end (response);
            FileUtils.set_data (args[2], bytes.get_data ());
            result = 0;
        } catch (Error e) {
            stderr.printf ("%s\n", e.message);
        }
        loop.quit ();
    });
    loop.run ();
    sigterm.destroy ();
    return result;
}
