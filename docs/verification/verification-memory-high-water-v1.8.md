# Majoris Image 2.0.1 — Memory High-Water Qualification Hardening

**Baseline source:** `Majoris-Image(20261119-051853).zip`  
**Engineering standard:** Majoris Cross-Project Engineering Standards v1.8  
**Scope:** Release-performance comparison, GTK viewer lifecycle benchmark, APNG integration test, and documentation; no changes to image decoding, scheduling, worker protocols, native codecs, or public API.

## Changes

| Area | Implementation | Acceptance behavior |
|---|---|---|
| Viewer memory statistics | `compare_performance.py` recomputes median, MAD, minimum, 95th percentile and maximum from each raw sample list | Incorrect summary fields, absent samples, nonfinite or negative data, and mismatched counts are inconclusive |
| High-water gate | Both preview and large-image viewer memory require median, p95 and maximum comparisons | Live RSS must satisfy **both** 15% relative growth and 128 MiB absolute growth; retained RSS must not grow more than 64 MiB; any exceeded limit fails |
| Memory stability | Relative MAD and peak-to-minimum spread are limited to 20% of the median, with the existing absolute memory allowance as a floor for spread | Excessively noisy baseline is inconclusive; noisy candidate is inconclusive unless a validated regression is already demonstrated |
| Viewer lifetime | `benchmark-viewer-lifecycle.vala` holds non-owning weak references to each `ImageView`, loads all eight using a dedicated async helper, then requires the weak references to clear after GTK unparenting and normal reference release | A retained `ImageView` fails the benchmark instead of silently reporting stable RSS |
| Collector contract | `collect_performance.py` requires six successful lifecycle probes per mode | Missing release evidence cannot be converted to a passing memory sample |
| APNG overlap | `test-decoder-selection.vala` exercises both request initiation orders using a shared helper | Decoded pixels and follow-on seeking are checked under both initiation orders; **completion order is not artificially forced** |
| Matched baseline | Existing `prepare_baseline.py` synchronizes the collector and GTK benchmark instrumentation into the earlier-source reference | Baseline and candidate run identical measurement logic; existing runtime code remains separate |

No metadata caching, persistent sandbox worker, extra GoF hierarchy or replacement scheduler was added. The existing APNG stage-level instrumentation remains available; a production optimization would need a native, matched performance result to justify it.

## Verification in this editing environment

- Source archive inspected and verified against the earlier performance-gates ZIP.
- `python3 -B tests/release/test_compare_performance.py` — **23 tests passed**, including the synthetic 600,000-KiB median / 2,000,000-KiB maximum regression that previously passed.
- `python3 -B tests/release/check_source_docs.py --source-root .` — documented declarations covered by the scanner.
- Python syntax, source diff scope, ZIP integrity and preserved production source hashes checked during packaging.
- **Not run:** native Meson/Vala compilation, compiled decoder/GTK tests, APNG or memory benchmarks, and physical GPU telemetry. Meson and `valac` are not installed in the editing environment. The new weak-reference assertions require native runtime confirmation and may reveal lifetime defects needing a subsequent focused fix.

## Debian 13 verification on 20 November 2026

The full build succeeded on Debian 13.5 under WSL2 with Meson 1.7.0,
Vala 0.56.18, GTK 4.18.6, and Glycin 1.2.1. GTK, Glycin, timed sequences,
Ultra HDR, and introspection were enabled. All 31 unit, integration, and
security tests passed on the final build.

The initial Meson benchmark run passed 38 of 40 entries. Both GTK lifecycle
entries failed the viewer release check. The scroll and pinch handlers held
reference cycles in `ImageView`, and the benchmark needed `GLib.WeakRef` to
observe object release. Both lifecycle entries passed after those fixes,
with all eight viewers released in each of six cycles per entry. The tiled
entry also confirmed that all eight viewers presented decoded tiles per cycle.
The other two GTK benchmarks passed again on the final build.

| Meson benchmark | Passing entries | Elapsed time per entry (s) |
|---|---:|---:|
| Scheduler cancellation-heavy queues (120, 1200, 4800) | 3 | 0.01, 0.01, 0.09 |
| Bounded worker output | 1 | 37.76 |
| Encoded export transfer | 1 | 25.20 |
| Public API performance, including APNG | 1 | 110.21 |
| Native pixel output and JPEG XL encoding | 1 | 248.70 |
| GTK tile planning and progressive rendering | 1 | 27.01 |
| GTK ImageView snapshots and progressive presentation | 1 | 10.20 |
| GTK ImageView repeated disposal and RSS | 1 | 9.65 |
| GTK ImageView eight large tiled images and RSS | 1 | 143.00 |
| Glycin pixel conversion | 21 | 0.01–1.36 |
| Sandboxed Glycin loading and conversion | 6 | 0.05–0.06 |
| Render recipe copy and default comparison | 2 | 0.01, 0.00 |
| **Total** | **40** | |

The timings are Meson entry wall times, rounded to two decimal places.
Matched baseline comparison, publication of raw RSS samples, and physical
GPU measurements remain open. No external RAW fixture directory was configured.

## Release decisions

1. On Debian 13, build with Glycin enabled and disabled; run every registered unit and integration test.
2. Prepare a matched baseline tree with the current benchmark harness and capture independent measurements on the same machine.
3. Run candidate qualification; missing workload, weak-reference failure, changed output, noncomparable host, unstable memory evidence or memory-regression budget exceedance must not return `passed`.
4. Measure real GPU/compositor memory separately on supported hardware. Host-RSS and weak-pointer checks cannot establish full GPU-resource reclamation.
5. If a native test finds a concrete failure, change only the affected production subsystem. Otherwise freeze the current performance implementation.
