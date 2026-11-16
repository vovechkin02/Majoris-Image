using ImageKit;

private RenderOptions adjusted_recipe () {
    var recipe = new RenderOptions ();
    recipe.version = 1;
    recipe.turns = -3;
    recipe.nearest_neighbor = true;
    recipe.flip_x = true;
    recipe.flip_y = true;
    recipe.angle = -45;
    recipe.exposure = 10;
    recipe.contrast = 4;
    recipe.brightness = -0.125;
    recipe.highlights = 1;
    recipe.shadows = -1;
    recipe.whites = 0.24;
    recipe.blacks = -0.17;
    recipe.saturation = 3;
    recipe.vibrance = -0.23;
    recipe.temperature = 0.47;
    recipe.tint = -0.31;
    recipe.clarity = 0.12;
    recipe.sharpness = 2;
    recipe.vignette = -0.21;
    recipe.filter = "warm";
    recipe.filter_intensity = 0.25;
    recipe.crop_x = 0.25;
    recipe.crop_y = 0.125;
    recipe.crop_width = 0.75;
    recipe.crop_height = 0.875;
    recipe.monochrome = true;
    recipe.sharpen = true;
    return recipe;
}

// Exercise the GLib property inventory so a new property needs a nondefault test value.
private void check_properties () {
    var original = adjusted_recipe ();
    var defaults = new RenderOptions ();
    assert (defaults.is_default ());
    assert (defaults.copy ().is_default ());
    assert (defaults.serialize () == defaults.copy ().serialize ());
    var copy = original.copy ();
    foreach (var property in ((ObjectClass) typeof (RenderOptions).class_ref ()).list_properties ()) {
        var value = Value (property.value_type);
        var copied = Value (property.value_type);
        original.get_property (property.name, ref value);
        copy.get_property (property.name, ref copied);
        assert (!property.value_defaults (value));
        assert (property.values_cmp (value, copied) == 0);

        var isolated = new RenderOptions ();
        isolated.set_property (property.name, value);
        assert (!isolated.is_default ());
        assert (isolated.serialize () != defaults.serialize ());
    }
    foreach (int version in new int[] {1, 2}) {
        original.version = version;
        var expected = original.serialize ();
        copy = original.copy ().copy ().copy ();
        assert (copy != original);
        assert (copy.version == version);
        assert (copy.serialize () == expected);
        assert (expected.contains ("nearest-neighbour"));
    }
    copy.filter = "";
    copy.crop_x = 0;
    assert (original.filter == "warm");
    assert (original.crop_x == 0.25);
}

private void check_legacy_defaults () {
    try {
        var recipe = RenderOptions.deserialize ("{\"version\":1,\"nearest-neighbour\":true}");
        assert (recipe.copy ().version == 1);
        assert (recipe.copy ().nearest_neighbor);
        assert (!recipe.is_default ());
    } catch (Error failure) {
        error ("Legacy recipe failed: %s", failure.message);
    }
    var defaults = new RenderOptions ();
    var candidate = new RenderOptions ();
    candidate.exposure = -0.0;
    candidate.crop_x = -0.0;
    candidate.angle = -1e-90;
    candidate.filter = null;
    var copy = candidate.copy ();
    assert (Math.copysign (1.0, copy.exposure) == 1.0);
    assert (Math.copysign (1.0, copy.crop_x) == 1.0);
    assert (copy.angle == 0);
    assert (copy.filter == "original");
    assert (copy.is_default ());
    assert (copy.serialize () == defaults.serialize ());
    candidate.angle = double.NAN;
    candidate.crop_width = double.NAN;
    assert (candidate.copy ().is_default ());
    try {
        candidate.validate ();
        assert_not_reached ();
    } catch (Error failure) {
        assert (failure is ImageError.INVALID_RECIPE);
    }
    candidate.angle = -1e-89;
    candidate.crop_width = 1;
    candidate.contrast = -0.0;
    candidate.filter = "";
    copy = candidate.copy ();
    assert (copy.angle == candidate.angle);
    assert (copy.filter == "");
    assert (Math.copysign (1.0, copy.contrast) == -1.0);
    assert (!copy.is_default ());
}

private void check_normalized_preview () {
    var edits = new RenderOptions () {
        crop_x = 0.25,
        crop_width = 0.75,
        nearest_neighbor = true,
        exposure = -0.0
    };
    string default_json = new RenderOptions ().serialize ();
    foreach (int version in new int[] {1, 2}) {
        edits.version = version;
        var normalized = edits.copy ();
        normalized.crop_x = 0;
        normalized.crop_width = 1;
        normalized.nearest_neighbor = false;
        assert (normalized.is_default () == (normalized.serialize () == default_json));
        assert (normalized.is_default () == (version == 2));
    }
}

private void check_invalid_copy () {
    if (Test.subprocess ()) {
        var invalid = new RenderOptions ();
        invalid.crop_width = 0;
        invalid.copy ();
        return;
    }
    Test.trap_subprocess (null, 5 * TimeSpan.SECOND, 0);
    Test.trap_assert_failed ();
    Test.trap_assert_stderr ("*code should not be reached*");
}

int main (string[] args) {
    Test.init (ref args);
    Test.add_func ("/render-options/property-inventory", check_properties);
    Test.add_func ("/render-options/legacy-defaults", check_legacy_defaults);
    Test.add_func ("/render-options/normalized-preview", check_normalized_preview);
    Test.add_func ("/render-options/invalid-copy", check_invalid_copy);
    return Test.run ();
}
