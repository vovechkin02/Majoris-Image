using ImageKit;

private enum SeekResult {
    UNSUPPORTED, REJECTED, FAILED, SUCCEEDED
}

private class SniffStream : InputStream, Seekable {
    public bool fail_read;
    public bool fail_close;
    public bool cancel_read;
    public uint closes;
    public SeekResult seek_result;
    private int64 position;

    public override ssize_t read (uint8[] buffer, Cancellable? stop = null) throws IOError {
        if (cancel_read) {
            stop.cancel ();
            stop.set_error_if_cancelled ();
        }
        if (fail_read) {
            throw new IOError.INVALID_DATA ("Injected read failure");
        }
        buffer[0] = 42;
        return 1;
    }

    public override bool close (Cancellable? stop = null) throws IOError {
        assert (stop == null);
        closes++;
        if (fail_close) {
            throw new IOError.FAILED ("Injected close failure");
        }
        return true;
    }

    public int64 tell () {
        return position;
    }

    public bool can_seek () {
        return seek_result != SeekResult.UNSUPPORTED;
    }

    public bool seek (int64 offset, SeekType type, Cancellable? stop = null) throws Error {
        assert (type == SeekType.END);
        if (seek_result == SeekResult.FAILED) {
            throw new IOError.INVALID_ARGUMENT ("Injected seek failure");
        }
        if (seek_result == SeekResult.REJECTED) {
            return false;
        }
        position = offset;
        return true;
    }

    public bool can_truncate () {
        return false;
    }

    public bool truncate (int64 offset, Cancellable? stop = null) throws Error {
        throw new IOError.NOT_SUPPORTED ("Truncation is unavailable");
    }
}

private void check_read (SniffStream stream, int expected_error = -1, bool footer = false) {
    var loop = new MainLoop ();
    var stop = new Cancellable ();
    read_sniff_stream.begin (stream, footer ? 18 : 65536, Priority.DEFAULT, stop, footer,
        (object, result) => {
            try {
                var bytes = read_sniff_stream.end (result);
                assert (expected_error == -1);
                if (footer && stream.seek_result != SeekResult.SUCCEEDED) {
                    assert (bytes == null);
                } else {
                    assert (bytes.get_size () == 1);
                    assert (bytes.get_data ()[0] == 42);
                }
            } catch (Error failure) {
                assert (failure.domain == IOError.quark () && failure.code == expected_error);
            }
            assert (stream.is_closed ());
            assert (stream.closes == 1);
            loop.quit ();
        });
    loop.run ();
}

private void check_header_cleanup () {
    check_read (new SniffStream ());
    check_read (new SniffStream () { fail_read = true, fail_close = true }, IOError.INVALID_DATA);
    check_read (new SniffStream () { cancel_read = true, fail_close = true }, IOError.CANCELLED);
    check_read (new SniffStream () { fail_close = true }, IOError.FAILED);
}

private void check_footer_cleanup () {
    check_read (new SniffStream () { fail_close = true }, -1, true);
    check_read (new SniffStream () { seek_result = SeekResult.REJECTED }, -1, true);
    check_read (new SniffStream () { seek_result = SeekResult.FAILED, fail_close = true },
        IOError.INVALID_ARGUMENT, true);
    var success = new SniffStream () { seek_result = SeekResult.SUCCEEDED };
    check_read (success, -1, true);
    assert (success.tell () == -18);
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/source-sniff/header-cleanup", check_header_cleanup);
    Test.add_func ("/source-sniff/footer-cleanup", check_footer_cleanup);
    return Test.run ();
}
