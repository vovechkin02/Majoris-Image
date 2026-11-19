# Majoris-Image 2.0.1 — performance qualification implementation

**Engineering standard:** Majoris Cross-Project Engineering Standards v1.8  
**Scope:** dual Glycin builds; APNG frame scaling and metadata accounting; scheduler
cancellation load; repeated GTK viewer disposal and resident memory.

## What was implemented

| Priority | Implementation | What a successful run establishes |
|---|---|---|
| Native build configurations | `tests/release/qualify_performance.py` | Meson setup, compile and full tests for both `-Dglycin=enabled` and `-Dglycin=disabled` |
| Long-animation qualification | Deterministic 10-, 50-, and 100-frame APNGs from `tests/support/make_apng_benchmarks.py` | Timing, output fingerprint, subprocess count, source size and resident memory of the existing forward-cursor path |
| Metadata overhead | Passive `OperationProfile` stages in `GlycinFallback.open()` | Time and call count of source identity, loader discovery, color probing, scheduling, frame advancement and conversion |
| Queue cancellation scaling | `benchmark-scheduler.vala` at 120, 1,200 and 4,800 requests (75% canceled) | Admission, cancellation, drain times and completed/canceled counts |
| Repeated viewer disposal | `benchmark-viewer-lifecycle.vala` under Xvfb | Initial, per-cycle and final host VmRSS, peak VmHWM and public document state after disposal |

## Reproducible qualification

Install the Debian 13 dependencies documented in the README. From the repository root:

```sh
python3 tests/release/qualify_performance.py \
  --source . \
  --workdir /tmp/majoris-image-performance-qualify \
  --output /tmp/majoris-image-performance-qualify.json
```

The script exits nonzero on a missing compiler, a failed build, failing tests or failing
benchmarks. Every command has a separate output log. It **must not** report `passed`
when tools are missing or when a test was not executed. Keep the generated workdir
outside the source checkout and use a fresh location per qualification run.

For a manual targeted run:

```sh
meson setup build-glycin-on -Dglycin=enabled -Dgtk=true -Dtests=true
meson compile -C build-glycin-on
meson test -C build-glycin-on --no-rebuild --print-errorlogs
meson test -C build-glycin-on --benchmark --suite performance --print-errorlogs
meson setup build-glycin-off -Dglycin=disabled -Dgtk=true -Dtests=true
meson compile -C build-glycin-off
meson test -C build-glycin-off --no-rebuild --print-errorlogs
```

## Interpreting the results

- The APNG profiling records source freshness checks separately from loader
  discovery and color-probe work. The profile emits counts and microseconds;
  the benchmark runner aggregates them by workload and persists them with
  fixture SHA-256 and host memory measurements.
- Compare the 10-, 50-, and 100-frame workload ratios. Median per-frame
  latency should not grow roughly linearly with the requested frame index
  when forward decoder reuse is successful. **No improvement is claimed
  without actual matched results.**
- The scheduler benchmark uses deterministic mixed priorities and a 75%
  queued cancellation rate. Validate completed/canceled counts before
  comparing queue, cancellation, and drain times across request counts.
- The viewer benchmark measures Linux process RSS, **not** graphics-device
  allocations. Xvfb/Cairo does not validate VRAM reclamation. Qualify that
  separately using a real Majoris compositor and a supported GPU driver
  profiler (e.g., a per-process GPU-memory tool for the hardware). Record
  process tree and memory after multiple idle/quiescent cycles.
- Memory retained by a GLib allocator is not automatically a leak; correlate
  trends with live-object and GPU telemetry before changing ownership.

## Invariants and non-changes

No caching of color/ICC data or bypass of source stamps is introduced. Source
verification, sandbox selection, Glycin cancellation, worker protocol, public
ABI, GTK properties and 256 MiB worker limits remain unchanged. The new
`OperationProfile` calls emit only when `MAJORIS_IMAGE_PROFILE=1`. Other than
those passive measurements, the changes affect benchmark and qualification
code, not image-processing algorithms or threading.

## Qualification status

**Source/configuration additions only.** The editing runtime lacked Meson,
`valac` and native development headers. Native compiles, runtime checks,
benchmarks and physical GPU memory measurements have **not** passed in this
runtime. Completion requires running the qualification commands above on a
properly provisioned Debian 13/Majoris environment and recording their logs.
Do not treat availability of a benchmark registration as a benchmark result.

### Optional physical GPU qualification

On a real GTK/Wayland or X11 session using NVIDIA hardware with `nvidia-smi`
and a native renderer (not Cairo/Xvfb), run:

```sh
python3 tests/benchmarks/measure_gpu_memory.py \
  /tmp/majoris-image-performance-qualify/build-glycin-enabled/tests/benchmarks/benchmark-viewer-lifecycle \
  tests/fixtures/rgb16.png \
  --output /tmp/majoris-image-gpu-memory.json
```

The tool samples per-process VmRSS and GPU memory as exposed by the installed
NVIDIA driver. On unsupported hardware or when the driver reports memory only
under the compositor process, the GPU result is `not_measured` and does not
qualify GPU reclamation. Physical GPU memory measurements require representative
Majoris hardware and an appropriate vendor profiler; do not generalize these
results to AMD or Intel hardware.
