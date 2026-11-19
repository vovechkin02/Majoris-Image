# Render options and Glycin verification

Baseline commit `3cab98fc95b6b47c0244d13a4111b0e8ed38e21e`. Measurements
were collected on x86-64 Debian 13 under WSL 2, with GCC 14, Vala 0.56,
Meson 1.7, Glycin 1.2.1, GTK 4.18.6, and libvips 8.16.1. The baseline used
an unmodified Git archive. The full release build used the same optimization
level and codec stack before and after.

## A. Implementation summary

| Area | Files | Change |
| --- | --- | --- |
| Object copy | `src/core/render-options.vala` | Assign all 28 properties directly and validate the constructed recipe. Match JSON-GLib's normalization of omitted defaults. |
| Default preview | `src/core/render-options.vala`, `src/core/glycin-fallback.vala` | Compare the normalized copy against the defaults in memory. |
| Pixel conversion | `src/core/glycin-pixels.c` | Lazily initialize one 256-entry double table per transfer function and select the table once per eligible frame. Mark the unused GTask callback parameter. |
| Stream ownership | `src/core/source-sniff-stream.vala`, `src/core/glycin-fallback.vala`, `src/core/decoder-registry.vala`, `src/core/meson.build` | Close opened header and footer streams in a shared async cleanup path. |
| Diagnostics and scheduling | `src/core/operation-profile.vala`, `src/core/decoder-registry.vala`, `src/core/glycin-fallback.vala`, `src/core/image-document.vala`, `src/core/job-scheduler.vala`, `src/core/worker-client.vala` | Profile occurrences with a zero elapsed time and count one in the existing record format. Use named priorities 0–3 with the same slot reservation. |
| Catalog | `native/codec-catalog.c` | Explicitly initialize each loader field. |
| Tests and benchmarks | `tests/unit/test-render-options.vala`, `tests/unit/test-source-sniff-stream.vala`, `tests/unit/test-glycin-pixels.c`, `tests/unit/meson.build`, `tests/benchmarks/benchmark-render-options.vala`, `tests/benchmarks/meson.build` | Register compatibility, numerical, and cleanup regressions and opt-in performance workloads. |
| Documentation | `CONTRIBUTING.md`, `docs/TESTING.md` | Describe direct worker compilation, clarify the CI Ruff 0.15.0 pin and the historical Ruff 0.17.5 result, and document benchmark commands. |

## B. RenderOptions verification

Copy and comparison cover `version`, `turns`, `nearest_neighbor`, `flip_x`,
`flip_y`, `angle`, `exposure`, `contrast`, `brightness`, `highlights`,
`shadows`, `whites`, `blacks`, `saturation`, `vibrance`, `temperature`,
`tint`, `clarity`, `sharpness`, `vignette`, `filter`, `filter_intensity`,
`crop_x`, `crop_y`, `crop_width`, `crop_height`, `monochrome`, and
`sharpen`. The test enumerates the GObject property inventory and requires a
nondefault fixture value for each property. It tests repeated copies, source
independence, recipe versions 1 and 2, and the historical `nearest-neighbour`
JSON key.

Previously, copying serialized a recipe, parsed it, constructed another object,
and validated it. An unchanged baseline probe confirmed that JSON-GLib omits
default doubles using GParamSpecDouble's 1e-90 epsilon. A tiny difference from a
zero default, including negative zero, became positive zero. Negative zero on a
property with default 1 was preserved. The baseline also omitted NaN and null
strings, restoring the property default when copied. Direct copy reproduces
those cases and calls the existing validator. Explicit validation still rejects
NaN and invalid recipes still abort when the old copy would fail. Tests cover
those behaviors, an empty filter, and finite values at and across the omission
boundary. Serialization and deserialization continue to use the existing code.

Glycin compares the copied version, filter, flags, and doubles after resetting
crop and nearest-neighbor fields. The test compares the new verdict with the
old serialized comparison for normalized version 1 and version 2 recipes.
The existing integration suite exercises cropped previews, nearest-neighbor
rendering, and a rotation requiring the worker.

## C. Glycin numerical verification

The native regression test calls the production GTask entry point. Glycin 1
has no public constructor for arbitrary frame layouts, so the test supplies
frame metadata and buffers through linker-wrapped accessors. It covers all
18 supported integer formats at all three transfer settings. Cases include
RGB and BGR, RGBA and BGRA, ARGB and ABGR, grayscale and grayscale-plus-alpha,
8-bit and 16-bit samples, and all supported premultiplied formats. It checks
padded strides, exact float32 samples, zero and partial alpha, near-black and
full-scale levels, fractional area coverage across a sharp transparent edge,
nearest-neighbor output, odd extents, crop origin, repeated runs and concurrent
cold conversion. The test applies gray ICC gamma only to grayscale layouts and
checks that linear RGB bypasses transfer.

Benchmarks on the original and updated production converters agreed on the
SHA-256 of every complete output buffer for all workloads, including cropped
and downscaled output. The maximum observed sample difference was **0**. The
unit tests use exact comparisons without a numerical tolerance. Premultiplied
samples retain unpremultiplication and clamping before transfer, and 16-bit
samples retain the original formulas. The full decoder-selection test also
exercises actual loaders for QOI, linear QOI, BMP, ICO, TGA, DDS and PGM when
available. Loader-produced frames were not individually forced into all 18
layouts. The synthetic test supplies that layout coverage.

## D. Performance comparison

Recipe timings are medians of five process runs with 20,000 iterations each.
The old default test serializes both the validated input and a new default
object, as the fallback used to do. The measurement excludes copying so the
comparison cost is visible.

| Workload | Previous | Updated | Change | Median process peak RSS, KiB |
| --- | ---: | ---: | ---: | ---: |
| Recipe copy | 7.869 µs | 0.162 µs | −97.9% | 4,752 → 4,696 |
| Default comparison | 6.820 µs | 0.009 µs | −99.9% | 4,616 → 4,632 |
| Complete QOI fallback, cold process | 40.931 ms | 40.989 ms | +0.1% | 10,536 → 10,748 |
| Highest median peak RSS among conversion workloads | 41,836 KiB | 41,844 KiB | +8 KiB | Large RGB8 |

Full fallback uses seven fresh benchmark processes and the fixture worker to
force codec-missing for QOI. It includes worker startup, sandboxing, loader
startup, and decoding of a 2 × 1 image. Median process and waited-child CPU time
was 17.0 → 17.5 ms. Median time inside the public API workload was 21.15 →
21.22 ms. Worker RSS sampling missed the short-lived workers. Both builds
launched two workers per sample. The complete fallback elapsed-time difference is
within the observed run-to-run variation.

For conversion-only measurements, the same harness was linked against the
original and updated converter with GCC `-O3`. Three process launches per case
give three cold timings and 15 warm timings, or 300 warm timings for the small
case. Conversion time includes GTask dispatch and result delivery, and excludes
frame construction, loading, and digest computation. RSS is the median process
high-water mark for warm samples, including source and destination buffers.
Throughput counts source pixels in the crop per second, including both frames
in the concurrent workload. Values are previous → updated.

| Workload | Output | Cold wall, ms | Warm wall, ms | Warm change | Warm CPU, ms | Warm Mpix/s | Peak RSS, KiB |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Small RGB8 | 31 × 19 | 0.468 → 0.461 | 0.083 → 0.062 | −25% | 0.073 → 0.053 | 7.1 → 9.5 | 6,168 → 6,184 |
| Large RGB8 | 1601 × 1203 | 94.885 → 24.484 | 84.921 → 14.856 | −83% | 84.918 → 14.823 | 22.7 → 129.6 | 41,836 → 41,844 |
| RGBA8 | 701 × 527 | 118.558 → 15.126 | 116.332 → 13.021 | −89% | 115.878 → 12.995 | 16.6 → 147.9 | 19,416 → 19,464 |
| Premultiplied RGBA8 | 701 × 527 | 124.236 → 126.322 | 121.680 → 124.193 | +2% | 121.356 → 123.892 | 15.8 → 15.5 | 19,424 → 19,416 |
| Grayscale ICC 8 | 701 × 527 | 123.479 → 13.635 | 121.068 → 11.119 | −91% | 120.852 → 11.111 | 15.9 → 173.2 | 13,796 → 13,816 |
| RGB16 | 701 × 527 | 139.110 → 145.260 | 138.613 → 141.405 | +2% | 138.587 → 141.378 | 13.9 → 13.6 | 23,164 → 23,288 |
| RGBA16 | 701 × 527 | 118.010 → 120.364 | 115.879 → 118.175 | +2% | 115.861 → 118.154 | 16.6 → 16.3 | 26,984 → 26,948 |
| Linear RGB | 701 × 527 | 18.503 → 17.368 | 16.559 → 15.695 | −5% | 16.557 → 15.683 | 116.3 → 122.7 | 17,396 → 17,392 |
| Large downscale | 113 × 77 | 156.590 → 8.966 | 155.944 → 7.302 | −95% | 155.938 → 7.287 | 27.3 → 583.3 | 18,864 → 18,860 |
| Fractional crop and resize | 713 × 611 | 90.898 → 14.008 | 88.239 → 11.077 | −87% | 88.235 → 11.071 | 12.5 → 99.9 | 20,468 → 20,436 |
| Two concurrent conversions | 701 × 527 each | 144.000 → 13.859 | 147.087 → 11.324 | −92% | 292.660 → 22.379 | 26.2 → 340.2 | 23,340 → 23,308 |

Direct first-call measurements of the production initializer in 21 fresh
processes per transfer gave median 12 µs for sRGB and 14 µs for grayscale
ICC. Cold CPU in the large RGB8 case was 95.04 → 24.65 ms, and in the grayscale
case it was 123.69 → 13.82 ms. The benchmark emits cold CPU and RSS per
sample as well. Both tables together reserve 4,096 bytes of static data plus
two GLib initialization guards. The converter allocates no per-frame table.
Conversion still allocates one RGBA32F output buffer and one GTask per frame.

## E. Resource and build verification

Stream fault tests verify successful close, primary read and seek error
precedence when close fails, cancellation during read with uncanceled cleanup,
a standalone close error, unsupported and rejected seeks, and successful
footer seeking. Converter fault tests cover unsupported layouts, zero and
over-budget extents, invalid stride, truncated input, edge 4097, allocation
failure, and deterministic cancellation during coverage accumulation.

| Profile | Result |
| --- | --- |
| Original full GTK and Glycin baseline | 21 passed, one RAW skip with no fixture directory configured. |
| Updated full GTK and Glycin release | 24 passed, including all 21 mandatory tests. AVIF and HEIF sequences and Ultra HDR were enabled. |
| Updated headless Core and Glycin release | 14 passed, including all 11 mandatory tests. Core dependency check passed. |
| Updated GTK native-only release | 23 passed, including all 21 mandatory tests. Glycin, timed sequences and Ultra HDR were disabled. |
| Extended GTK and camera RAW | Three passed, including both extended GTK workloads and the nine-camera RAW test. Full qualification also passed X11 tests at scales 1 and 2. The final combined run passed all 27 registered tests. |
| ASan and UBSan | Eight focused tests passed, including Glycin, stream cleanup, recipe copy, parser and protocol checks. Leak checking was disabled per the test guide. |
| Debian gain-map binary build | Five binary packages and four debug-symbol packages built. Package build tests recorded 24 passed and one unconfigured RAW skip. |
| Installed package consumers | Headless, Vala GTK, C GtkBuilder, GIR, Photos export, thumbnail and vendor JPEG gain-map checks passed against extracted Debian packages in an isolated mount namespace. |
| Strict warning level 3 with warnings as errors | Seven catalog missing-field errors removed. The build still fails on existing first-party diagnostics and generated-code warnings. |

The strict baseline had seven `-Wmissing-field-initializers` errors in
`codec-catalog.c`, and an unused callback parameter in the Glycin converter.
Both touched C sources and the new C regression harness pass focused strict
compilation. Remaining first-party diagnostics include a signedness comparison
in `native/sequences.c`, GNU `_Float16` pedantic errors in `native/ultra.c`,
and unused parameters in unrelated test or application code. Generated Vala C
reports unused parameters and locals, discarded qualifiers in GLib atomic
macros, and other pedantic diagnostics. There were no independently compiled
third-party sources in this build. No warning flags were relaxed.

Catalog JSON compared byte for byte. The Core and GTK SONAMEs stayed at 2.
Exported symbol names and types matched exactly, with 285 approved Core and
77 approved GTK exports. The Glycin-disabled Core verified 280 applicable
exports. Public VAPIs, GIR files, and generated C headers for both libraries matched
byte for byte.

The existing 22 registrations retained their names, suites, and qualification
requirements. Matching test metadata verified 21 identical registrations.
The RAW test used its existing 180-second runnable configuration with camera
fixtures, up from the 30-second skip configuration in the baseline.
New unit registrations are `Glycin frame layouts and resampling` (Glycin
development library present), `Render recipe copying and compatibility`, and
`Source sniffing stream closure and error precedence`, each with the Meson
default 30-second timeout. The extended 160-second lifecycle and 180-second
8K tile tests were enabled for this run. The new performance cases run only
through Meson's benchmark mode. C formatting passed locally with ClangFormat
19.1.7. The new Meson blocks were checked with Meson 1.7. Existing Meson source
ordering and formatting were preserved. The exact CI formatter pins, Wayland
compositor and physical HDR display checks were unavailable in this run.

## F. Deferred conversion changes

The [follow-up 16-bit conversion study](verification-glycin-16bit.md) evaluates
actual loader frames and documents the selective tables now in use.

A temporary two-table, 16-bit candidate preserved the output digests. Its
warm median wall times were 11.1 ms for RGB16 and 12.7 ms for RGBA16,
compared with 138.6 ms and 115.9 ms in the original converter. The candidate
requires 512 KiB per transfer, adds cold fill work, and increases the RGB16
process high-water mark by about 568 KiB. The workload establishes a benefit
for large synthetic 16-bit frames. There is insufficient loader-level usage
data to justify that persistent footprint for typical previews, so the
candidate was discarded.

Premultiplied values after division by alpha fall between the original sample
levels. They continue through the formula path. A table covering both sample
and alpha was not evaluated. The 8-bit linear path continues to use normalized
samples directly.
