using MajorisImage;

int main () {
    PresentationPolicy[] policies = {
        PresentationPolicy.AUTOMATIC, PresentationPolicy.SDR, PresentationPolicy.HDR_PREFERRED
    };
    foreach (var policy in policies) {
        for (int output = 0; output < 3; output++) {
            var sdr = Presentation.choose (policy, false, output);
            assert (sdr.status == PresentationStatus.SDR && !sdr.tone_map);
            var hdr = Presentation.choose (policy, true, output);
            if (output == 2 && policy != PresentationPolicy.SDR) {
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
