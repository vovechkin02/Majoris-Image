using ImageKit;

private int copy_recipes (int repetitions) {
    var recipe = new RenderOptions () {
        exposure = 1.5,
        contrast = 1.25,
        highlights = -0.25,
        filter = "warm",
        crop_width = 0.75,
        nearest_neighbor = true
    };
    int copies = 0;
    for (int i = 0; i < repetitions; i++) {
        var result = recipe.copy ();
        if (result.filter == recipe.filter && result.crop_width == recipe.crop_width) {
            copies++;
        }
    }
    return copies;
}

private int compare_defaults (int repetitions) {
    var edits = new RenderOptions ();
    int matches = 0;
    for (int i = 0; i < repetitions; i++) {
        edits.flip_x = i % 2 != 0;
        if (edits.is_default ()) {
            matches++;
        }
    }
    return matches;
}

int main (string[] args) {
    if (args.length != 2 || (args[1] != "copy" && args[1] != "default-comparison")) {
        return 2;
    }
    const int repetitions = 20000;
    bool copying = args[1] == "copy";
    int64 started = get_monotonic_time ();
    int matches = copying ? copy_recipes (repetitions) : compare_defaults (repetitions);
    int64 elapsed = get_monotonic_time () - started;
    assert (matches == (copying ? repetitions : repetitions / 2));
    print ("%s %.3f us/op (%d repetitions)\n", args[1], (double) elapsed / repetitions,
        repetitions);
    return 0;
}
