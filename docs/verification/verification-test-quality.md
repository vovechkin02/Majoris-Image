# Majoris-Image test-quality verification

The [baseline inventory](test-inventory-2.0.0.md) records the original suite
and its gaps. The [test guide](../TESTING.md) provides reproduction commands.

## Coverage and regression fixes

- Meson separates unit, integration, security, extended, interoperability,
  platform, and performance tests. The isolated launcher owns XDG directories,
  temporary files, outputs, and the process group for each invocation.
  Qualification checks both registration and recorded test outcomes.
- The installed tests require the generated TIFF, verify widget readiness and
  public error reporting, and run the Files and Photos applications. The
  Photos consumer compares reopened 16-bit PNG dimensions and representative
  SDR RGBA samples. The installed thumbnail consumer decodes the PNG and checks
  dimensions, thumbnail metadata, and varying pixel values.
- PNG validation had accepted CRC-valid chunks containing broken zlib streams.
  The validator now bounds compressed input and inflated scanlines, checks
  PNG filters and structure, and handles Adam7 and split IDAT streams. The
  regression tests verify exact RGBA with libvips, reject 13 malformed variants,
  and prove the public cache regenerates a CRC-valid undecodable entry.
- The cache tests share a cache among eight readers in one isolated scenario.
  They cover source changes, interrupted producer work, a kill between the
  pending-file write and rename, stale pending files, cancellation, unrelated
  requests, and eviction while an existing reader holds the file open.
- Numeric tests cover the existing ICC and HDR references, Rec.2020 primaries,
  8- and 16-bit alpha round trips, and nonfinite and out-of-range float buffer
  samples. Public decoder checks include pixel assertions for JPEG, WebP, GIF,
  JPEG 2000 where available, and existing orientation and sequence inputs.
- The export check found two alpha errors. ICC export now quantizes straight
  alpha outside the color transform. Integer decode preserves source alpha
  through color conversion and normalizes it once.
- Worker tests verify separate network namespaces, read-only source and system
  mounts, hidden application files, sandbox startup failure, address-space
  and file limits, repeated crashes, cancellation, invalid protocol data,
  process cleanup, and valid requests after failure. Malformed manifest JSON
  and mistyped fields now return `INVALID_RESPONSE` before object construction.
- The complete 8K test verifies all 160 tiles against a generated BigTIFF
  whose pixel pattern depends on source coordinates, including tile bleed.
  Seeded geometry and scheduling tests check bounded coverage and admission.
- Clang rejected OpenEXR namespace aliases that redefined existing namespaces.
  The aliases now use distinct names. No public declarations changed.

## Local verification

| Configuration | Observed result |
| --- | --- |
| Full GTK4, Glycin, timed sequences, Ultra HDR, introspection | 21 required tests passed in parallel. |
| Headless with Glycin and timed sequences, Ultra HDR disabled | 11 required tests passed. Core links neither GTK nor GDK. |
| Native GTK4 with Glycin and sequences disabled, Ultra HDR enabled | 21 required tests passed. |
| GCC AddressSanitizer and UndefinedBehaviorSanitizer, warning level 3 | Five selected native and protocol tests passed. Leak detection was disabled. |
| Clang libFuzzer with AddressSanitizer and UndefinedBehaviorSanitizer | 512,333 native inputs exercised in 76 seconds with no crashes or sanitizer findings. |
| Extended and interoperability | Full 160-tile viewport, three isolated GTK lifecycle repetitions, and all nine checksum-pinned RAW samples passed. Peak host RSS for the 8K case was 716,336 KiB. |
| Platform | The viewer lifecycle passed on headless Weston with native Wayland. Xvfb/XTest input and accessibility passed at scales 1 and 2. |
| Debian stock and gain-map profiles | Each built five amd64 packages, with 21 package-build tests passing. The external RAW test was explicitly skipped during packaging. |
| Installed consumers | Both package profiles passed clean installation, removal, reinstallation, and C, Vala, Python GI, GtkBuilder, Files, Photos, and thumbnail checks in isolated Debian 13.7. The vendor worker also round-tripped an 8-by-8 JPEG gain map and passed the 96-selection, 24-thumbnail installed stress run. |
| ABI 2 | All 285 Core and 77 GTK exports in the approved baseline remain in the full build. The no-Glycin variant checks 280 Core exports and the same 77 GTK exports. |

The stock and vendor package test runs used the packaged public headers,
bindings, libraries, typelibs, worker, and thumbnailer. Missing RAW and
installed TIFF fixtures were exercised as failures. Git and POSIX archive
checks verified executable modes for both release scripts.

The registered benchmark completed twelve workloads with five measurements
each. On this host the cold-open median was 82.3 ms, warmed preload-cache
opens averaged 1.14 ms within the median trial, and the eight-tile workload
median was 3.71 s. The largest sampled worker peak was 166,980 KiB during RAW
development. These are local measurements. The machine-readable baseline is
`build-packages/test-quality/benchmark.json`.

Physical HDR display output, real touch/pinch input, additional camera
variants, and platform-specific compositor behavior remain unverified.
Hosted GitHub Actions jobs were configured and have yet to run on their
designated runners. The local build-tree and clean-installed qualification
checks ran in separate environments. Existing generated Vala C warnings and the isolated
Xvfb session-bus warning were visible in the local logs.
