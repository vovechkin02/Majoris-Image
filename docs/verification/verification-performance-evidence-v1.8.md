# Majoris Image 2.0.1 — Process Peak Memory and Timing Evidence Closure

**Input source:** `Majoris-Image-2.0.1-memory-high-water-v1.8(1).zip`  
**Standard:** Majoris Cross-Project Engineering Standards v1.8  
**Scope:** Release qualification and semantic source documentation. All production executable
Vala, C, and C++ code is preserved; no worker protocol, GObject ABI or file-format behavior
is intentionally modified.

## Corrections

1. **True process peak RSS:** `collect_performance.py` now parses the benchmark's
   `peak_rss_kib` output (`/proc/self/status:VmHWM`). Six ordered cycles per independent
   subprocess are required. The process high-water counter must never decline and must
   not be smaller than either live or settled RSS. Finalization evidence remains mandatory;
   tiled-image runs must also prove that all eight viewers displayed tiles. The final
   cumulative VmHWM becomes one independent sample per run.
2. **Mandatory peak workloads:** `compare_performance.py` requires
   `viewer-peak-rss` and `viewer-large-peak-rss` in addition to both live and retained
   metrics. Peak and live RSS use the same 15% relative and 128 MiB absolute growth
   budgets; retained RSS uses its existing 64 MiB budget. The median, p95 and maximum
   of each workload are checked. Invalid/noisy evidence cannot pass.
3. **Raw timing consistency:** All APNG and scheduler timing workloads must retain
   positive finite raw samples with an exact declared count and matching baseline/candidate run counts. Median, MAD, minimum,
   maximum and p95 are recomputed and checked against their reported values before
   performance ratios are evaluated. A claimed median that contradicts the samples
   cannot pass. The memory and timing checks share a focused statistics validator.
4. **Matched methodology:** Measurement schema advances to 3. The collector remains
   in the benchmark harness fingerprint and is synchronized by `prepare_baseline.py`.
   Reference and candidate must collect the new peak workloads using the same source
   harness on the same host. Previous schema-2 JSON evidence is not a valid baseline.
5. **API comments:** Corrected type-specific descriptions in `ImageDocument`,
   `ImageView`, `PixelBuffer` and `ExportOptions`, including source/preview dimensions,
   bit depth, HDR, metadata ownership/type and output dimensions. No declaration or
   implementation signatures were modified.

## Verification

- ZIP source integrity: verify after packaging.
- Python policy and collector tests: **40 passed** on the editing host, including
  synthetic transient VmHWM peaks, inconsistent raw timings, missing peak evidence,
  malformed maxima, and normal-release/tile-coverage checks.
- Declaration documentation checker: **Vala 458/458**, **Python 221/221**,
  **native headers 44/44** and **native implementations 140/140** recognized
  declarations documented.
- All 23 Python files parsed successfully; edited Vala files contain only
  documentation changes, with the non-comment source lines unchanged.
- Native Meson/Vala build, GTK/Glycin integration, benchmark timings and
  physical GPU-memory measurements: **not run** in this environment because the
  required Meson/Vala development toolchain is unavailable.

## APNG concurrency qualification

The existing integration test runs both initiation orders and verifies frame
pixels and subsequent seeking. It does not deterministically force reversed
completion. Adding a decoder scheduler hook solely for that test would modify
production ownership/cancellation boundaries. This pass retains the test and
records this remaining native integration qualification limitation rather than
introducing an unmeasured test-only production abstraction.

## Required release steps

1. Prepare an earlier-source checkout with the same candidate benchmark harness
   using `tests/release/prepare_baseline.py`.
2. On the same Debian 13 system, run native dual Glycin builds, all unit and
   integration tests, then baseline/candidate measurements with
   `tests/release/qualify_performance.py`.
3. Require every timing, live/retained RSS and actual VmHWM peak workload to
   qualify under its budget. Treat incompatible methodology, missing probes,
   noisy samples and incorrect hashes as blocking/inconclusive.
4. Profile GPU memory separately on the target graphics drivers. Linux host
   VmHWM does not include all dedicated GPU allocations.
5. Only revisit APNG decoder metadata reuse if the measured stage cost justifies it.
