# Majoris Image 2.0.1 — Platform and Matched Qualification Pass

**Input:** `Majoris-Image(20261119-201426).zip`  
**Standard:** `docs/engineering-standards-v1.8.md`  
**Scope:** Qualification orchestration, licensed RAW fixture preflight,
physical-GPU memory evidence, and a limited comment/format-only cleanup.
No decoder, scheduler, worker protocol, public API, or processing algorithm is
intentionally changed.

## Implemented

- `tests/release/qualify_matched.py`: creates an isolated reference source
  from an earlier archive, fingerprints both source implementations, runs the
  baseline and candidate qualification in separate build directories on the
  same machine, and records statuses and detailed logs. A CPU-only pass is
  expressly distinguished from full GPU qualification.
- `tests/release/prepare_baseline.py`: synchronizes the candidate's matched
  benchmark harness and **new release-only support files** into the reference
  tree. Old production C/C++/Vala and runtime behavior remain byte-identical
  to the earlier archive. This prevents a newly added Meson test registration
  from referring to missing test files in the reference checkout.
- `tests/release/qualify_performance.py`: retains enabled/disabled Glycin
  profiles and adds optional `-Draw_fixtures` to both. A real-camera RAW
  manifest preflight runs before compilation and the native interoperability
  suite runs explicitly when licensed fixture files are supplied.
- `tests/release/verify_raw_fixtures.py`: checks nine manifest entries,
  dimensions, source precision, model, license/provenance, regular-file status,
  and SHA-256 checksums without silently downloading or replacing sources.
- `tests/benchmarks/measure_gpu_memory.py`: adds process-attributed DRM fdinfo
  resident-memory telemetry for supporting AMD/Intel/other DRM drivers, with
  a bounded NVIDIA fallback. It deduplicates multiple file descriptors for
  one GPU client. Unavailable or zero/unsupported attribution is not a pass.
  Graphics-compositor memory and physical HDR scanout remain outside scope.
- `tests/release/test_platform_qualification.py`: seven Python tests covering
  matching and substituted RAW fixtures, missing samples, DRM fdinfo units,
  unavailable telemetry, and source fingerprint changes. The Meson unit suite
  registers this test.
- `src/core/execution/job-scheduler.vala`: corrected the inaccurate `acquire`
  and `release` documentation to describe explicit admission release, not an
  imaginary returned ticket. The class's executable code is unchanged.
- Source ZIP POSIX execution permissions were normalized for Debian rules
  and executable scripts, because the uploaded source archive omitted POSIX
  mode metadata. Existing executable modes were recovered from the earlier
  POSIX source release where available.
- Selected low-risk production Vala signatures/expressions were rewrapped
  without changing evaluation order or tokens. Longer compatibility literals
  and complex source expressions were left unchanged for separate review rather
  than running an unsafe blanket formatter over Vala.

## Verified in this authoring environment

- Source ZIP extraction and baseline preparation succeeded with the earlier
  2.0.1 archive; the production source SHA-256 hashes were recorded separately.
- The `qualify_matched.py` dry run **correctly reported `blocked`**: Meson and
  `valac` are not installed here. Neither native profile nor matched APNG,
  scheduler, viewer, or GPU measurement was recorded as passed.
- Seven newly added release-preflight policy tests and the existing 51
  performance-policy tests passed using Python.
- The repository's source declaration-documentation gate passed. All new
  Python methods/classes have docstrings and the implementation uses the
  established `snake_case`, PEP 257, and `var`/explicit-type rules.
- No physical GPU, compositor, vendor RAW input set, or external native
  toolchain was available for actual performance certification in this run.

## Debian 13 rerun on 20 November 2026

A clean build of the working tree at `0a80143`, including the local
platform qualification changes, succeeded on Debian 13.5 under WSL2
with Meson 1.7.0 and Vala 0.56.18. GTK4, Glycin, timed sequences,
Ultra HDR, and introspection were enabled. All 32 unit, integration,
and security entries passed. An initial attempt with a reused build
timed out waiting for the GTK test viewport to resize. The clean build
included the new preflight entry and passed the GTK test.

All 40 entries passed in one Meson benchmark run. The table records
Meson's entry wall times, rounded to two decimal places.

| Benchmark | Seconds |
|---|---:|
| Scheduler cancellation-heavy queue 120 | 0.01 |
| Scheduler cancellation-heavy queue 1200 | 0.01 |
| Scheduler cancellation-heavy queue 4800 | 0.07 |
| Bounded worker output memory and throughput | 34.87 |
| Encoded export transfer memory and throughput | 24.31 |
| Public API performance | 108.55 |
| Native pixel output and JPEG XL encoding performance | 249.07 |
| GTK tile planning and progressive rendering | 26.23 |
| GTK ImageView snapshots and progressive presentation | 10.26 |
| GTK ImageView repeated disposal and RSS | 9.59 |
| GTK ImageView eight large tiled images and RSS | 141.36 |
| Glycin pixel conversion (small-rgb8) | 0.02 |
| Glycin pixel conversion (large-rgb8) | 0.62 |
| Glycin pixel conversion (rgba8) | 0.36 |
| Glycin pixel conversion (premultiplied-rgba8) | 1.28 |
| Glycin pixel conversion (gray8) | 0.33 |
| Glycin pixel conversion (small-rgb16) | 0.02 |
| Glycin pixel conversion (threshold-rgb16) | 0.01 |
| Glycin pixel conversion (rgb16) | 0.40 |
| Glycin pixel conversion (rgba16) | 0.45 |
| Glycin pixel conversion (gray16) | 0.40 |
| Glycin pixel conversion (gray-alpha16) | 0.45 |
| Glycin pixel conversion (premultiplied-rgba16) | 1.35 |
| Glycin pixel conversion (premultiplied-gray-alpha16) | 1.24 |
| Glycin pixel conversion (linear-rgb16) | 0.50 |
| Glycin pixel conversion (fractional-rgb16) | 0.12 |
| Glycin pixel conversion (concurrent-rgb16) | 0.41 |
| Glycin pixel conversion (concurrent-rgb16-gray16) | 0.50 |
| Glycin pixel conversion (linear-rgb) | 0.45 |
| Glycin pixel conversion (large-downscale) | 0.27 |
| Glycin pixel conversion (fractional-crop) | 0.32 |
| Glycin pixel conversion (concurrent) | 0.35 |
| Sandboxed Glycin loading and conversion (RGB16 PNG) | 0.06 |
| Sandboxed Glycin loading and conversion (RGBA16 PNG) | 0.06 |
| Sandboxed Glycin loading and conversion (Grayscale16 PNG) | 0.06 |
| Sandboxed Glycin loading and conversion (Grayscale-alpha16 PNG) | 0.06 |
| Sandboxed Glycin loading and conversion (Profiled grayscale PNG) | 0.04 |
| Sandboxed Glycin loading and conversion (Profiled TIFF) | 0.04 |
| Render recipe copy | 0.01 |
| Render recipe default-comparison | 0.00 |

Both lifecycle workloads released all eight viewers per cycle. All eight
tiled viewers presented decoded tiles in each cycle. The following are
the raw host process counters reported by those benchmarks in KiB.

| Workload | Cycle | Live RSS | Settled RSS | VmHWM |
|---|---:|---:|---:|---:|
| Preview | 1 | 122256 | 122256 | 122344 |
| Preview | 2 | 124144 | 124144 | 124144 |
| Preview | 3 | 124752 | 124752 | 124752 |
| Preview | 4 | 124752 | 124752 | 124752 |
| Preview | 5 | 124752 | 124752 | 124752 |
| Preview | 6 | 124752 | 124752 | 124752 |
| Tiled | 1 | 493928 | 375716 | 492784 |
| Tiled | 2 | 644488 | 587972 | 642936 |
| Tiled | 3 | 654712 | 590900 | 654012 |
| Tiled | 4 | 721584 | 637792 | 720820 |
| Tiled | 5 | 775720 | 747460 | 774168 |
| Tiled | 6 | 713304 | 713304 | 774168 |

Preview initial and final RSS were 104,320 and 124,752 KiB. Tiled
initial and final RSS were 104,468 and 713,304 KiB. The recorded tiled
VmHWM was below live RSS in cycles 1 through 5. Those samples would
fail the collector's consistency check, so this run establishes
benchmark execution and viewer release only.

The test and benchmark logs, including benchmark stdout and Meson JSON,
are saved in `~/majoris-verification/rerun-f561/` in Debian WSL.
External RAW fixtures, physical GPU telemetry, and matched baseline
qualification were outside this rerun.

## Outstanding native release acceptance

1. On Debian 13 with complete codec dependencies, execute the matched
   baseline/candidate qualification using **seven or more** independent samples.
2. Supply the nine external licensed RAW files to qualify the interoperability
   suite in both Glycin configurations; missing inputs remain not measured.
3. On a real Majoris desktop, run with `--require-gpu`. Preserve the driver,
   compositor, GPU-counter provenance, and the results; Xvfb/Cairo results do
   not satisfy this requirement.
4. Examine the stage-level APNG setup timings before considering any metadata
   cache. Do not change source-freshness, color or source-depth behavior in the
   absence of performance evidence.
5. Review full native test reports, ABI/GIR/VAPI and installed-consumer checks
   before marking version 2.0.1 release-qualified.

**Release status:** The Debian build, regular tests, and registered Meson
benchmarks passed. Matched performance, RAW interoperability, and physical
GPU measurements remain open.
