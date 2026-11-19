# GTK viewport optimization

The baseline was `daa6be265457da3f37356843f82b6899e303fe97`. Results for the
updated code refer to that revision with this patch applied.

## Tile lifecycle and cache ownership

An ImageView snapshot inverts its display transform to obtain source-space
viewport bounds. Before the change, every snapshot called `TileCoverage.plan()`,
looked up or allocated the visible tiles, sorted them by distance from the clipped
viewport center, rebuilt the pending queue, looked up two adjacent prefetch
candidates, scanned for eviction, and pumped the queue. A worker completion
released its slot and called `changed()`, which queued a snapshot and repeated
all this work at the same geometry.

The cache now keeps one read-only list of visible tile objects and up to two
prefetch candidates. All four source-space bounds and the requested pixel zoom
must match exactly for reuse. A small pan can reverse center priority even when
it covers the same grid. Source dimensions and vector status are fixed on the
document. Changing the document or tone-map decision, including unmapping and
clearing the source, drops the plan and queue. Panning, zoom, orientation, and
allocation or scale changes flow through the snapshot's existing bounds and
pixel zoom. The selected level and bleed are still derived by `TileCoverage`.

`plan_viewport()` and `plan_prefetch()` resolve geometry and tile identities.
`rebuild_pending()` admits missing visible tiles in center order before adjacent
prefetch. On a cache miss, eviction protects the new plan's visible and admitted
prefetch tiles. The previous plan is released before eviction. The 704 MiB
visible estimate, 256 MiB warm target, 256 planning slots, adaptive resolution,
and two active slots use the baseline values.

The queue advances on completion. Pending work is replaced on a plan change.
Canceled requests occupy slots until their callbacks return. A callback can
publish a texture only if its request is uncanceled, its generation is current,
and the cache still owns that instance. A canceled tile that became wanted again
before the callback returned is requeued in the current priority order. Failed
tiles remain ineligible until removed by eviction or a source/presentation reset.
The snapshot uses the existing preview mask, clip, textures, scaling, and
presentation paths.

## Transform updates

`apply_fit_mode()` coordinates mode changes with the existing fit-mode
notification handler. A direct assignment applies the mode through the handler.
Widget operations apply synchronously when the mode is already set or GObject
notifications are frozen. The recorded applied mode lets the deferred notification
recognize a transform that already ran. The view emits `notify::zoom` when the
effective zoom changes and `transformed` for an applied transform with an
allocation. Identical custom zoom, actual size, and centered contain calls skip
the repeated update. Resize, map, source replacement, pan, rotation, and flips
continue to apply geometry as needed. A display scale notification reapplies
actual-size geometry when its effective zoom changes.

## Measurements

Both builds used Meson `release`, GTK 4.18.6, Vala 0.56.18, and GCC 14.2.0
on Debian 13 under WSL2. The generated 20,000 × 10,000 patterned BigTIFF had
SHA-256 `f72d65df106a1b1b44009fa2d6e45453ba0142ad8177aa70fcc3fa3e4facfa61`
in both builds. The cache benchmark uses real workers without a display, at tile
zoom 1 except for the zoom workload. The view benchmark used Xvfb X11, Cairo,
and device scale 1, with the same benchmark binary linked against each build's
libraries. The 1920 × 1080 widget allocation was verified by the benchmark.

The planner reports wall time per call from seven batches of 1,000 calls, with
no completion callbacks dispatched during each batch. Cold tile runs report one
observed time for first and full completion. The redraw measurement isolates the
call to `visible()` from texture discovery and iteration within the callback.
Widget timings use seven batches of 200 iterations. An iteration forces each
viewer to build a fresh GSK snapshot. Preview timing includes the public load
through preview readiness. Tile latency starts when the view switches to actual
size, then polls render nodes on the GTK main loop until textures appear.

| Cache viewport | Visible tiles | Unchanged before / after (µs) | Small pan before / after (µs) | First completion before / after (ms) | All tiles before / after (s) |
|---|---:|---:|---:|---:|---:|
| 1920 × 1080 | 20 | 10.934 / 0.028 | 10.330 / 11.346 | 120.3 / 122.6 | 1.219 / 1.207 |
| 3840 × 2160 | 54 | 22.905 / 0.028 | 22.263 / 23.960 | 125.9 / 126.5 | 3.358 / 3.336 |
| 5120 × 2880 | 77 | 31.753 / 0.036 | 33.713 / 35.224 | 120.8 / 125.1 | 4.186 / 4.297 |
| 7680 × 4320 | 160 | 64.338 / 0.028 | 65.727 / 70.335 | 127.4 / 127.4 | 10.121 / 8.720 |
| 7680 × 2160 | 96 | 39.195 / 0.113 | 39.947 / 42.496 | 124.5 / 125.7 | 5.375 / 5.232 |

| Workload | Before | After | Observed change |
|---|---:|---:|---|
| Shifted 8K unchanged planner, seven-sample range | 61.969–65.382 µs | 0.028–0.029 µs | Over 99% lower median |
| Shifted 8K large pan and return, median | 136.241 µs | 141.890 µs | 4% higher |
| Shifted 8K alternating 1 and 0.45 zoom, median | 83.980 µs | 86.772 µs | 3% higher |
| Shifted 8K reset and replan, median | 120.037 µs | 124.953 µs | 4% higher |
| Shifted 8K planner time per completion-triggered redraw, mean | 165.90 µs | 0.73 µs | 99.6% lower |
| Shifted 8K visible completions during progressive run | 160 | 160 | Same visible count |
| One viewer, unchanged snapshot median | 18.52 µs | 6.84 µs | 63% lower |
| Two viewers, unchanged snapshot median | 27.04 µs | 9.49 µs | 65% lower |
| One viewer, rapid pan/zoom snapshot median | 13.96 µs | 13.83 µs | 1% lower |
| Two viewers, rapid pan/zoom snapshot median | 23.99 µs | 25.62 µs | 7% higher |
| One viewer, preview / first tile / complete | 2.100 / 0.272 / 1.079 s | 2.089 / 0.180 / 1.129 s | Mixed in one run |
| Two viewers, preview / first tile / complete | 4.250 / 0.221 / 0.977 s | 4.250 / 0.262 / 1.010 s | Mixed in one run |
| Cache benchmark host peak RSS | 718,016 KiB | 755,772 KiB | 5% higher in one run |
| Widget benchmark host peak RSS | 345,736 KiB | 356,924 KiB | 3% higher in one run |
| Cache benchmark total host and worker CPU, separate GNU time runs | 27.77 s | 27.85 s | 0.3% higher in one pair |

The deterministic reuse and fitted-to-custom signal checks fail on the baseline
and pass with the patch.
For a warm batch of 1,000 identical calls, that path eliminates 1,000 coverage
calculations, visible sorts, keep-set constructions, and queue rebuilds. Those
counts follow from the identity assertion and the branches taken by the two
implementations. Sorting and scheduling time are included in the planner timings.
Separate per-phase CPU time, heap allocation counts, and decoder launches for
prefetch were not measured. Process startup and decoder time dominate the
latency measurements.
The latency and RSS samples show run-to-run variation and are insufficient to
attribute a decode-time or memory trend to the patch.
An additional complete 8K run using the original pixel-check harness,
`MALLOC_PERTURB_=0`, and each version of the cache measured 716,124 KiB before
and 716,196 KiB after.

## Verification

- The original and patched builds covered the shifted 8K grid. The patched
  integration test rendered all 160 tiles, verified positions, compared every
  decoded pixel against the TIFF pattern, and checked the retained byte
  estimate and host peak RSS. Coverage also checks fractional, rotated,
  ultrawide, vector, 4K, 5K, and adaptive plans.
- The tile test verifies plan identity across completions, current center order
  after a one-pixel pan, and texture reuse after a zoom round trip. It checks
  generation changes, recovery when a canceled request becomes visible again,
  continued progress after a render validation failure, and eviction followed
  by new tile identity.
- The view test checks transform and zoom-notification counts, including frozen
  notifications, direct property writes, idempotent operations, resize, and
  remap. The existing XTest interaction suite checked anchored zoom after
  rotation and flips, drag, wheel and keyboard input, and accessibility at
  scale 1 and 2. The lifecycle test also ran on the WSLg Wayland compositor.
- The EXR test compares SDR preview pixels to tile pixels and verifies that
  the mapped widget's translucent pixels match before and after the tile
  replaces its preview. The progressive widget benchmark reaches a full
  viewport for both viewer counts.
- Generated GTK C header, VAPI, GIR, typelib, and the two pkg-config files
  matched byte for byte. Dynamic exports matched the baseline for Core and GTK.
  The ABI check verified 285 Core and 77 GTK baseline exports with SONAME 2.
- The release gate passed its 24 mandatory tests, using all nine manifest-pinned
  camera fixtures, the installed consumer checks, and all four benchmarks. The
  final GTK configuration passed all 25 tests, including Wayland. The
  reduced-codec GTK configuration passed native qualification with 21 mandatory
  tests. The final headless configuration passed 12 tests with the RAW fixtures.
- Debian binary package construction passed. The installed C, Vala, GtkBuilder,
  GI, Photos export, and thumbnail consumers passed using the extracted
  packages in a private mount namespace. The strict `-Dwerror=true` build
  stopped on pre-existing Vala warnings in Core and the worker. The diagnostic
  build at warning level 3 completed and passed native qualification.
- The full configuration retains the 25 baseline test names, suites, and
  parallel settings. Timeouts match except for the camera RAW test, which
  uses the existing 180-second timeout when fixtures are configured and
  30 seconds otherwise. Both GTK benchmarks are registered under `performance`.

Physical HDR display output, native Wayland input gestures, and GPU-specific
timing were outside this environment. Changing the tile resolution policy or
worker startup is a separate performance investigation. Exact input matching
keeps viewport changes reviewable and preserves priority when small pans
cross the center between tiles.

## Changed files

- `src/gtk/tile-cache.vala` and `src/gtk/image-view.vala` implement the cache
  and transform changes.
- `tests/integration/test-tile-coverage.vala` and
  `tests/integration/test-image-view.vala` add deterministic regression checks.
- `tests/benchmarks/meson.build` registers the two GTK workloads.
  `tests/benchmarks/benchmark-image-view.vala` drives the mapped viewers.
- `docs/TESTING.md` and this report document measurement and qualification.
