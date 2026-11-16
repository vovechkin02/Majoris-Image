using ImageKit;

const uint32 PROTOCOL_SEED = 0xf4a02019u;
const int TRIAL_COUNT = 512;

void check_protocol_round_trips () {
    stderr.printf ("Worker protocol seed %#x\n", PROTOCOL_SEED);
    var random = new Rand.with_seed (PROTOCOL_SEED);
    for (int trial = 0; trial < TRIAL_COUNT; trial++) {
        int width = random.int_range (-1000000, 2000000);
        int page = random.int_range (-50000, 150000);
        int version = random.int_range (0, 3);
        string manifest = ("{\"protocol-version\":%d,\"width\":%d,\"height\":1,"
            + "\"loader\":\"test\"}").printf (version, width);
        string request = "{\"version\":%d,\"options\":{\"page\":%d}}".printf (version, page);
        if (Environment.get_variable ("IMAGEKIT_TEST_TRACE") == "1") {
            stderr.printf ("trial %d: %s / %s\n", trial, manifest, request);
        }
        bool valid_manifest = version == 1 && width >= 1 && width <= 1000000;
        try {
            var probe = ImageProbe.deserialize (manifest);
            assert (valid_manifest && probe.width == width && probe.height == 1);
            assert (ImageProbe.deserialize (probe.serialize ()).width == width);
        } catch (Error error) {
            assert (!valid_manifest && error is ImageError.INVALID_RESPONSE);
        }
        bool valid_request = version == 1 && page >= 0 && page <= 100000;
        try {
            var parsed = WorkerRequest.deserialize (request);
            assert (valid_request && parsed.options.page == page);
            assert (WorkerRequest.deserialize (parsed.serialize ()).options.page == page);
        } catch (Error error) {
            assert (!valid_request && error is ImageError.DECODE);
        }
    }
}

int main () {
    check_protocol_round_trips ();
    return 0;
}
