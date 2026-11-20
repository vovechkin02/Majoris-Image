# Majoris-Image 2.0.1 — Tiled-Viewer Memory and Qualification Hardening

**Base:** `Majoris-Image(20261119-221926).zip`  
**Standard:** `docs/engineering-standards-v1.8.md`  
**Purpose:** Implement the follow-up to reported tiled-viewer RSS retention without
assuming the observed retention is an application leak.

## Source findings from the uploaded baseline

The baseline's Debian 13.5/WSL2 verification report records 32/32 native tests
and 40/40 benchmark registrations completing. Its six-cycle tiled-viewer run
reports an initial RSS of 104,468 KiB and final RSS of 713,304 KiB (increase:
608,836 KiB). All eight viewers reportedly finalized in every cycle. In the
first five cycles, several `VmHWM` measurements are lower than contemporaneous
`VmRSS` readings. These observations **do not identify the allocation owner**
or produce a valid matched peak-memory performance result.

## Implemented changes

1. **Extended diagnosis:** Optional `IMAGEKIT_MEMORY_DIAGNOSTICS=1` on
   `benchmark-viewer-lifecycle.vala`. The native diagnostic reads an initial,
   live, settled and final memory snapshot; it uses `/proc/self/smaps_rollup`
   (RSS/PSS/private dirty/swap), `/proc/self/status` (RSS/high-water/anonymous/
   file-backed), GSK texture-node counts, existing tile-rendered evidence and
   weak references confirming normal viewer finalization. Default six-cycle
   collector output remains compatible. Anomalous `VmHWM` values are reported
   but not corrected or substituted.
2. **20-cycle runner:** `diagnose_viewer_memory.py` runs an isolated benchmark
   with an execution deadline and saves ordered cycle results, a five-cycle
   settled-RSS delta, high-water consistency, and optional detailed memory
   observations. It does **not** conclude that retained RSS is a proven leak,
   or that absent GPU counters are zero.
3. **Optional matched-workflow hook:** `qualify_matched.py` can run
   `--diagnose-viewer-memory` only after successful matched CPU acceptance.
   Compilation, reference and candidate measurements, and Glycin-disabled
   coverage are still performed by the existing qualification workflow.
4. **GPU profiler robustness:** Replaced undrained stdout pipe with bounded
   temporary-file capture; added an explicit execution deadline, an output
   limit, process-group termination/reaping, bounded JSON tail, and distinct
   `failed`, `not_measured`, and `measured` outcomes. Corrected DRM fdinfo
   formatting. GPU measurements remain per-process, not compositor-wide.
5. **Tests and packaging:** Registered Python tests for diagnostic parsing,
   incomplete viewer/tile evidence, anomalous high-water values, stalled
   children, excessive output, bounded log tails and lack of GPU telemetry.
   Baseline preparation synchronizes these new test helpers while preserving
   original production sources and ABI.

## What the evidence can establish

- Positive weak-reference release demonstrates `ImageView` objects are no
  longer strongly retained; it does not prove all third-party allocation
  arenas have been returned to the operating system.
- Host `VmRSS` vs `smaps_rollup` anonymous/private dirty/PSS trends help
  distinguish retained heap from file-backed residency. Per-process counters
  do not supply complete allocation call stacks, nor physical GPU residency.
- A 20-cycle plateau or continuing increase is a **diagnostic observation**,
  not a universal release acceptance threshold or proof of a memory leak.
- If `VmHWM` remains lower than observed `VmRSS`, matched memory acceptance
  must remain blocked while the platform/kernel counter mismatch is diagnosed.
- APNG metadata caching remains **unimplemented** pending stage-level native
  profiling. The Glycin source-freshness and precision contracts are unchanged.

## Checks performed in this artifact environment

- Python release tests: 68 passed, including six memory-diagnostic policy
  cases and four subprocess/GPU-profiler safety cases.
- Declaration-documentation checker: Vala 459/459; Python 277/277;
  native headers 44/44; native function definitions 140/140.
- ZIP integrity and source-diff checks: see final delivery metadata.
- Native Meson/Vala compilation: **not run**; toolchain unavailable.
- Native 20-cycle diagnosis, seven-sample matched benchmarks and physical GPU
  profiling: **not run**; require the Debian/Majoris target environment.

## Reproduction

```sh
python3 -m unittest discover -s tests/release -p 'test_*.py'
python3 tests/release/check_source_docs.py --source-root .
python3 tests/release/qualify_matched.py \
  --baseline-archive /path/to/earlier-source.zip \
  --source . --workdir /tmp/imagekit-matched-new \
  --output /tmp/imagekit-matched-report.json --samples 7 \
  --diagnose-viewer-memory
```

Do not declare complete performance certification until a valid matched
baseline, both native build profiles, and any required physical GPU or licensed
RAW qualification have actually passed.
