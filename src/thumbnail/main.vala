int main (string[] args) {
    if (args.length != 4) {
        stderr.printf ("Usage: majoris-image-thumbnailer INPUT OUTPUT EDGE\n");
        return 2;
    }
    var loop = new MainLoop ();
    int result = 1;
    var service = new MajorisImage.ThumbnailService ();
    var source = new MajorisImage.ImageSource (File.new_for_commandline_arg (args[1]));
    service.generate.begin (source, int.parse (args[3]), null, (obj, response) => {
        try {
            var bytes = service.generate.end (response);
            FileUtils.set_data (args[2], bytes.get_data ());
            result = 0;
        } catch (Error e) { stderr.printf ("%s\n", e.message); }
        loop.quit ();
    });
    loop.run ();
    return result;
}
