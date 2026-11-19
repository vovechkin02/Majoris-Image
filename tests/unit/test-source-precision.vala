using ImageKit;

/** Verifies preservation of source sample precision across independent metadata probes. */
private void verify_reconciled_source_depth () {
    try {
        // Either probe can be absent. Missing metadata is unknown, not 8-bit.
        assert (checked_source_sample_depth (0, 16, 16) == 16);
        assert (checked_source_sample_depth (16, 0, 16) == 16);
        assert (checked_source_sample_depth (0, 0, 8) == 0);
        // Disagreeing sources must retain the stronger precision contract.
        assert (checked_source_sample_depth (8, 16, 16) == 16);
        assert (checked_source_sample_depth (16, 8, 16) == 16);
        assert (checked_source_sample_depth (8, 0, 16) == 8);
        assert (checked_source_sample_depth (32, 16, 32) == 32);
    } catch (Error error) {
        assert_not_reached ();
    }
}

/** Requires an explicit error when a decoder truncates known source samples. */
private void verify_precision_loss_is_rejected () {
    int64[] color_depths = { 0, 8, 16 };
    int[] animation_depths = { 16, 16, 0 };
    for (int index = 0; index < color_depths.length; index++) {
        try {
            checked_source_sample_depth (color_depths[index], animation_depths[index], 8);
            assert_not_reached ();
        } catch (ImageError.UNSUPPORTED error) {
            // The fallback must decline decoded 8-bit output from a 16-bit source.
        } catch (Error error) {
            assert_not_reached ();
        }
    }
}

/** Rejects malformed precision reports instead of silently weakening validation. */
private void verify_invalid_precision_is_rejected () {
    int64[] invalid_depths = { -1, 65, ((int64) int.MAX) + 1 };
    foreach (int64 invalid in invalid_depths) {
        try {
            checked_source_sample_depth (invalid, 0, 16);
            assert_not_reached ();
        } catch (ImageError.INVALID_RESPONSE error) {
            // Invalid worker-reported source precision must never wrap an int.
        } catch (Error error) {
            assert_not_reached ();
        }
    }
    try {
        checked_source_sample_depth (8, -1, 16);
        assert_not_reached ();
    } catch (ImageError.INVALID_RESPONSE error) {
        // An invalid animation source depth is not an unknown zero value.
    } catch (Error error) {
        assert_not_reached ();
    }
}

/** Registers isolated source-precision tests without initializing the image runtime. */
int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/source-precision/reconcile", verify_reconciled_source_depth);
    Test.add_func ("/source-precision/no-truncation", verify_precision_loss_is_rejected);
    Test.add_func ("/source-precision/invalid-metadata", verify_invalid_precision_is_rejected);
    return Test.run ();
}
