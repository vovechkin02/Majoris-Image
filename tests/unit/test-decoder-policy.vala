using ImageKit;

private void verify_worker_route (string label) {
    assert (DecoderRoute.from_label (label) == DecoderRoute.WORKER);
}

private void verify_loader_routing () {
    assert (DecoderRoute.from_label ("Glycin") == DecoderRoute.FALLBACK);
    assert (DecoderRoute.from_label ("libheif-sequence") == DecoderRoute.SEQUENCE);
    // Custom and native loader labels must not accidentally acquire fallback or
    // sequence semantics, including labels differing only in case or suffix.
    verify_worker_route ("pngload");
    verify_worker_route ("heifload");
    verify_worker_route ("libavif-sequence");
    verify_worker_route ("custom-worker");
    verify_worker_route ("glycin");
    verify_worker_route ("Glycin-custom");
    verify_worker_route ("libheif-sequence-custom");
    verify_worker_route ("");
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/decoder-policy/loader-routing", verify_loader_routing);
    return Test.run ();
}
