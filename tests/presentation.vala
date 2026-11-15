using ImageKit;

int main () {
    PresentationPolicy[] policies = {
        PresentationPolicy.AUTOMATIC, PresentationPolicy.SDR, PresentationPolicy.HDR_PREFERRED
    };
    OutputCapability[] outputs = { OutputCapability.UNREALIZED, OutputCapability.SDR,
        OutputCapability.VERIFIED_HDR, OutputCapability.UNKNOWN };
    foreach (var policy in policies) {
        foreach (var output in outputs) {
            var sdr = Presentation.choose (policy, false, output);
            assert (sdr.status == PresentationStatus.SDR && !sdr.tone_map);
            var hdr = Presentation.choose (policy, true, output);
            if (output == OutputCapability.VERIFIED_HDR && policy != PresentationPolicy.SDR) {
                assert (hdr.status == PresentationStatus.NATIVE_HDR && !hdr.tone_map);
                continue;
            }
            assert (hdr.tone_map);
            assert (hdr.status == (policy == PresentationPolicy.HDR_PREFERRED ?
                PresentationStatus.HDR_UNAVAILABLE : PresentationStatus.SDR_TONE_MAPPED));
        }
    }
    return 0;
}
