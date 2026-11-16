# Testing Majoris-Image

The [baseline inventory](test-inventory-2.0.0.md) records the original tests,
dependencies, fixtures, and assertion gaps. See the
[qualification report](verification-test-quality.md) for observed results.

Test sources live under `tests/unit/`, `tests/integration/`, and
`tests/security/`. Shared runners and fixture generators are in
`tests/support/`, performance workloads are in `tests/benchmarks/`, and
package and installed checks are in `tests/release/`. Meson suite membership
is independent of the source directory. Meson display names are sentence case.
The historical baseline and verification reports use the names from their runs. Shared fixture targets still write to
`build/tests/`, including the `large.tiff` used by installed qualification.

## Development and extended suites

Build on Linux with the dependencies listed in `debian/control`. Install
Bubblewrap with working unprivileged user and network namespaces. GTK
integration uses GTK4 4.16 or later, Xvfb, Xauth, and XTest. The Core-only
workflow can omit those GTK test dependencies. Glycin loader packages, libavif
and compatible libheif, libultrahdr, and the GIR compiler enable their respective
build variants.

```sh
meson setup build -Dglycin=enabled -Dsequence=enabled -Dintrospection=enabled
meson test -C build --suite unit --suite integration --suite security --print-errorlogs
python3 tests/release/qualify.py build --variant full
meson setup headless -Dgtk=false -Dglycin=enabled
python3 tests/release/qualify.py headless --variant headless
meson setup native -Dglycin=disabled -Dsequence=disabled
python3 tests/release/qualify.py native --variant native
```

The `unit`, `integration`, and `security` suites run by default. The headless
build checks that the Core library does not link GTK or GDK. `extended`
adds full 8K tile rendering and repeated GTK lifecycle tests.
`interoperability` requires the external RAW input set. With no configured RAW
directory, Meson explicitly skips that test. Missing files in a configured
manifest fail. GTK commands use the available display or report a skip.
The release gate requires the Xvfb/XTest tests and rejects mandatory skips.

```sh
meson configure build -Dextended_tests=true
meson test -C build --suite extended --print-errorlogs
meson test -C build --suite security --print-errorlogs
```

Run `meson test -C build --list` for the exact test names. The optional
`platform` suite exercises the viewer on an existing Wayland compositor.
Configure `-Dwayland_tests=true`, expose the compositor socket to the test
process, and run `meson test -C build --suite platform --print-errorlogs`.
Native Wayland gestures and physical HDR display output require separate
validation.

Each isolated test owns its HOME, XDG directories, staging files, and child
process group. The cache publication test deliberately shares a cache only
among its own clients. Set `IMAGEKIT_KEEP_TEST_ARTIFACTS=1` to retain isolated
artifacts and `IMAGEKIT_TEST_TRACE=1` to print seeded trial inputs. Meson writes
individual failures and diagnostics under `meson-logs`. Never point a failure
injection test at application data.

## Fixtures and RAW qualification

Meson generates the tiled BigTIFF, animated GIF and WebP, ICC-tagged images,
and malformed container variations in the build tree. Build those fixtures
with `meson compile -C build`. Small redistributable fixtures are documented in
[the fixture README](../tests/fixtures/README.md).

Obtain the nine CC0 camera files from the
[raw.pixls.us catalog](https://raw.pixls.us/) and rename them to the paths in
[the qualification manifest](../tests/fixtures/raw-qualification.tsv).
Match by SHA-256. The manifest records camera, developed dimensions, orientation,
precision, embedded preview, and provenance. Pass `-Draw_fixtures=/absolute/path`
and run `meson test -C build --suite interoperability --print-errorlogs`.
RAW decoded dimensions and precision depend on the approved LibRaw behavior
for these files. Additional camera variants require separate qualification.

## Sanitizers and fuzzing

```sh
meson setup sanitize -Dgtk=false -Dglycin=disabled -Dsequence=enabled \
  -Dultrahdr=disabled -Dwarning_level=3 -Db_sanitize=address,undefined -Db_lundef=false
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  meson test -C sanitize --print-errorlogs \
  'OpenEXR region decoding' 'Native image processing references' 'Thumbnail PNG decoding and validation' \
  'Native parser fuzz smoke' 'Worker protocol round trips and validation'
```

The sandbox enforces a 2 GiB address-space ceiling, so ASan-instrumented worker
processes cannot reserve their shadow address space there. Keep sandbox
qualification in the regular build. The sanitizer build exercises the same
native parsers directly. Strict-warning builds still report existing generated
Vala C warnings.

The deterministic harness mutates PNG, ICC, timed-track, and float buffer inputs.
The protocol property test exercises the production manifest and request parsers.
The native harness also supports Clang libFuzzer.

```sh
CC=clang CXX=clang++ meson setup fuzz -Dgtk=false -Dglycin=disabled \
  -Dsequence=enabled -Dlibfuzzer=true -Db_sanitize=address,undefined -Db_lundef=false \
  -Dc_args=-fsanitize=fuzzer-no-link -Dcpp_args=-fsanitize=fuzzer-no-link
meson compile -C fuzz fuzz-imagekit
python3 tests/security/seed_fuzz.py fuzz/corpus
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  fuzz/tests/security/fuzz-imagekit -max_len=524288 -max_total_time=90 -timeout=5 fuzz/corpus
```

Use a disposable working directory for libFuzzer so its crash inputs are
preserved as artifacts. Feed a specific crash file as the positional corpus
argument to reproduce it.

## Benchmarks

Run the GTK benchmarks separately from the regular tests:

```sh
meson test -C build --benchmark --verbose \
  'GTK tile planning and progressive rendering' \
  'GTK ImageView snapshots and progressive presentation'
```

The first benchmark exercises the production tile-cache source with the generated
BigTIFF at HD, 4K, 5K, shifted 8K, and ultrawide sizes. It times repeated cache
calls separately from tile decoding and reports the host process peak RSS. The
second benchmark requires Xvfb and exercises snapshot construction and progressive
presentation with one and two mapped viewers at scale 1. For the baseline,
methodology, and verification results, see [GTK viewport optimization](gtk-viewport-optimization.md).

Run `meson test -C build --benchmark --verbose` for the registered
`performance` workloads. Public API workloads use five samples, report median, P95, range,
CPU time, sampled host and worker RSS, sandbox launches, concurrency, and
temporary disk usage. Polling can miss short-lived workers, which are still
counted by their launch records. A missing RSS sample is reported as
`null`. Cache effects are evaluated through warm-open workloads and launch
counts. CPU time includes waited child processes. The warm result measures
repeated verified preload-cache hits separately from setup.

The Glycin conversion benchmarks use deterministic buffers in each Glycin
memory layout. They call the production asynchronous converter without starting
a loader. Each workload launches a fresh process, then repeats conversions in
that process. Each CSV row contains the workload, cold or warm status, wall
and process CPU time in microseconds, peak RSS in KiB, output dimensions,
source pixels per second, and SHA-256 of the complete RGBA32F output. The
frame fixture substitutes Glycin's accessors because Glycin 1 has no public
frame constructor. The decoder-selection integration test and the public API
`fallback` workload exercise installed loaders. The standalone public API runner
needs `--fault-worker BUILD/tests/security/worker-fault` for the QOI fallback
workload on hosts whose native stack decodes QOI.

```sh
meson test -C build --benchmark --verbose 'Glycin pixel conversion (large-rgb8)'
meson test -C build --benchmark --verbose 'Render recipe copy' 'Render recipe default-comparison'
```

See the [render options and Glycin verification report](verification-render-glycin.md)
for the before-and-after measurements.

For a standalone baseline, pass the built benchmark and worker to
`tests/benchmarks/run_benchmarks.py`, `--output result.json`, and one or more
`MODE=FIXTURE` arguments. Pass `--baseline previous.json` to compare workloads
with the same fixture hash. Cross-machine timings do not gate functional tests.
For scheduled comparisons use the same runner and build settings.

For Core and sandbox timing, configure both comparison builds with
`-Dworker_profiling=true`. The runner enables `MAJORIS_IMAGE_PROFILE=1` in
the benchmark process. Ordinary builds default to worker profiling disabled.
JSON samples include stage durations in microseconds and event or byte counts.
Sandbox runtime includes Bubblewrap startup, worker initialization, operation
execution, diagnostics, and reaping. The worker reports initialization and
operation timing separately. Lazy decoders can do work while materializing the
output, so worker operation time includes output serialization.

For a real non-native GIO source, run the existing runner under a session bus
with the GVfs HTTP backend available. The loopback server exercises the GVfs
transport and WorkerClient staging without reaching outside the machine.

```sh
dbus-run-session -- python3 tests/benchmarks/run_benchmarks.py --build build \
  --worker build/src/worker/majoris-image-worker \
  --executable build/tests/benchmarks/majoris-image-benchmark --loopback-gvfs \
  gvfs=tests/fixtures/rgb16.png gvfs-cancel=tests/fixtures/rgb16.png \
  gvfs-write-error=tests/fixtures/rgb16.png
```

The cancellation case interrupts the next read after a partial file appears.
The write failure case uses `prlimit` to force a short write followed by a
write error. Both cases check that the temporary directory is clean and the
sandbox never starts. Both use synthetic HTTP payloads. Add
`gvfs-limit=tests/fixtures/rgb16.png` to test rejection of a 1 GiB plus one-byte
source. The limit case needs at least 1 GiB of temporary storage per sample. Pass `--remote-uri`
with `gvfs=REFERENCE_FILE` to measure another GVfs URI serving the reference
bytes. Record the scenario as unavailable if the backend cannot open the URI.

To measure the first presented RAW preview, use the same runner with
`majoris-image-view-benchmark` as the executable and run it under
`xvfb-run -a` with `GDK_BACKEND=x11 GDK_SCALE=1 GSK_RENDERER=cairo`.
Pass `raw-first=/path/to/350D.CR2`. The `gtk-preview` phase measures the
first `PREVIEW_READY` notification and the workload ends after the developed
document is presented. See the [Core and worker verification report](verification-core-worker.md).

The native benchmark calls the production MIF1 writer and JPEG XL encoder.
It uses five samples each for the first and second operation in separate
processes. JSON reports operation timing and CPU time, fixture time, process
peak RSS, libvips tracked memory, output size and digest, and failure counts.
It reports native P95 when at least 20 samples complete per phase.

```sh
meson compile -C build majoris-image-native-benchmark
VIPS_CONCURRENCY=2 python3 tests/benchmarks/run_benchmarks.py --native \
  --build build --executable build/tests/benchmarks/majoris-image-native-benchmark \
  --output result.json mif1-lazy-16mp jxl-rgba-16mp
```

Use `--baseline previous.json` to check matching fixtures and compare timings.
A MIF1 digest mismatch fails the comparison. `--references NEW_DIRECTORY`
retains the first cold output from each workload for direct comparison.

## Installed tests and release qualification

Build stock Debian packages with `dpkg-buildpackage -us -uc -b`. Build the
vendor profile with `-Ppkg.majoris-image.gainmap` and the supported libuhdr
development and runtime packages. Install the five packages into a disposable
Debian 13 environment with build tools, Python GI, Xvfb, and Bubblewrap. Verify
installation, removal, and reinstallation. Run the following from the
package-test environment with the generated fixture available.

```sh
python3 tests/release/installed_consumers.py /absolute/build/tests/large.tiff
```

The fixture argument is required. The workflow compiles its C and Vala
consumers using installed headers and bindings, verifies ABI 2 and the GTK
ready/error state, loads both typelibs, instantiates the widget through
GtkBuilder, and runs Files, Photos, and thumbnail consumers. Photos renders,
exports, reopens, and compares dimensions and representative RGBA pixels.
Set `MAJORIS_TEST_STRESS=1` to include the concurrent installed workloads.

Full qualification requires a GTK build with `-Dextended_tests=true`,
`-Dintrospection=enabled`, `-Dglycin=enabled`, `-Dsequence=enabled`,
`-Dultrahdr=enabled`, and the manifest-pinned RAW directory. With the release
packages installed, run `python3 tests/release/qualify.py build --installed`.
The runner checks that required tests exist and actually passed, including the
complete 8K test. The protected release CI runner provides the RAW inputs and
vendor runtime. Per-commit CI covers full, headless, native-only, sanitizer,
and Debian-installed variants. Nightly CI adds extended rendering,
benchmarks, and prolonged fuzzing.

Before producing a release source archive, use
`python3 tests/release/check_release_modes.py archive.tar.gz` on a POSIX host to verify
the executable bits in Git, the archive, and its extracted scripts.
