using MajorisImage;

private async void round_trip (ImageSource source, File output) throws Error {
    var options = new ExportOptions ();
    options.format = "uhdr";
    options.hdr = true;
    yield new EncoderRegistry ().encode (source, output, options);
    var reopened = yield new DecoderRegistry ().open (new ImageSource (output));
    assert (reopened.hdr && (reopened.capabilities & ImageCapabilities.GAIN_MAP) != 0);
    var sdr = yield reopened.render_preview (true);
    assert (sdr.width == reopened.width && sdr.height == reopened.height);
    stdout.printf ("Installed vendor worker exported and decoded a %dx%d JPEG gain map\n",
                   reopened.width, reopened.height);
}

int main (string[] args) {
    if (args.length != 3) return 2;
    var loop = new MainLoop ();
    int status = 0;
    round_trip.begin (new ImageSource (File.new_for_path (args[1])), File.new_for_path (args[2]), (obj, result) => {
        try { round_trip.end (result); }
        catch (Error error) { stderr.printf ("%s\n", error.message); status = 1; }
        loop.quit ();
    });
    loop.run ();
    return status;
}
