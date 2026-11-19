# Majoris Image 2.0.1 — Performance Gate Corrections

**Source baseline:** `Majoris-Image(20261119-045151).zip`  
**Standard:** Majoris Cross-Project Engineering Standards v1.8  
**Scope:** Benchmark, measurement and release-acceptance tooling only; no production decoder, scheduler, native codec, public API or worker protocol changes.

## Corrections

| Area | Change | Required acceptance property |
|---|---|---|
| Live GTK memory | `compare_performance.py` requires both preview and tiled live RSS | Live memory growth must stay within 15% of the matched baseline **and** 128 MiB; neither an omitted measurement nor a huge live-only regression can pass |
| Settled GTK memory | Preview and tiled retained RSS are both mandatory | Additional retained RSS must not exceed 64 MiB per workload |
| Tiled viewport memory | The GTK lifecycle benchmark now loads eight 20,000 × 10,000 TIFF viewers in a 4 × 2 grid, invokes actual-size presentation and verifies at least one decoded tile in every viewer in each of six cycles | A preview-only run cannot masquerade as a tiled-memory measurement; failure to display tiles fails collection |
| APNG frame equivalence | A streaming SHA-256 digest incorporates every frame index, pixel dimensions, stride, timestamp, duration and decoded RGBA float payload | A changed intermediate frame causes baseline comparison to become inconclusive, even when the last frame is unchanged |
| APNG composition | Added a 24-frame 512 × 384 APNG with alpha blending, partial frame regions and disposal operations | The long-sequence fixtures remain for scaling; the larger one covers different work |
| Methodology compatibility | The hash now covers the collector, benchmark sources, Meson registrations and fixture-generator sources | Old and new production builds must use the **same** measurement code; mismatched methodologies fail closed |
| Units and budgets | Every required workload carries `seconds` or `KiB` units; added configurable `--max-live-growth-kib` | Mismatched units and missing fixture identities are inconclusive, never passing |
| Physical GPU telemetry | The NVIDIA-only helper forwards `preview` or `tiled` to the updated benchmark | Unavailable or unsupported GPU measurements are reported `not_measured`, not passed |

The earlier `GlycinFrameCursor` concurrent-publication ordering and overlapping-request correctness test are **unchanged**. Forcing reversed decoder-completion order deterministically would require an additional test-only hook in production Glycin code. That hook was deliberately not introduced without a verified Vala/Glycin build environment. No source-version-bound color-probe caching was added because no native profile demonstrated a measurable bottleneck.

## Matched baseline workflow

1. Install the project dependencies, `valac`, Meson, Ninja and Xvfb on Debian 13/Majoris.
2. Prepare an earlier source from the exact previous ZIP with the benchmark-only harness synchronized using `tests/release/prepare_baseline.py`.
3. Capture at least seven independent samples using `tests/release/qualify_performance.py --record-baseline ...`.
4. Run the candidate on the same machine using `--baseline ...`; a release passes only if both Glycin build profiles and tests pass, all workload results exist, output fingerprints match and every acceptance budget passes.
5. On a physical desktop, separately record GPU-memory and compositor evidence. Host RSS under Cairo/Xvfb does not qualify GPU allocations.

The measurement schema is **2** to prevent accepting pre-correction reports. Each memory workload retains six simultaneous-viewer cycles per independent run; the qualified comparison uses the median across independent runs. A benchmark timeout, missing tile presentation, missing stage data, codec mismatch, high variance or unavailable native toolchain is a failure or **inconclusive/blocked**, not a performance pass.

## Verification performed in the editing environment

- The Python performance-comparator tests passed, including live-memory growth, missing large-viewer data, measurement-unit mismatch and collector-hash sensitivity.
- The declaration-documentation gate passed for handwritten Vala, Python and native headers/definitions.
- The 10-, 50-, 100- and 24-frame composited APNG fixtures were generated and decoded using Pillow, including frame-count and geometry checks.
- The baseline-preparation tool was exercised against the uploaded previous archive, and source preservation and identical benchmark-harness hashes were checked.
- ZIP and source-file preservation are checked at packaging.
- **Not performed:** native C/Vala compilation; runtime ImageKit/APNG or GTK benchmarks; physical GPU-memory profiling. Meson and `valac` are unavailable in the editing environment. No performance improvement or GPU qualification is claimed.

## Freeze decision

The production performance architecture remains unchanged. The only remaining required gate is a real Debian/Majoris native build, complete correctness tests, matched performance measurements and any applicable GPU evidence. Reopen implementation only for a reproducible problem found by those checks.
