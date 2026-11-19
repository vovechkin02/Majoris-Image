# Majoris Image 2.0.1 — Source Precision and Evidence Contract Closure

**Input source:** `Majoris-Image-2.0.1-performance-evidence-v1.8(1).zip`  
**Standard:** Majoris Cross-Project Engineering Standards v1.8  
**Scope:** One internal precision policy, its isolated unit tests, and the Python
performance-evidence envelope; no published API, worker protocol, decoder
selection, scheduler, or rendering algorithm is intentionally changed.

## 1. Source sample precision

`src/core/decoding/glycin-fallback.vala` previously checked the color
worker's source-depth value against the Glycin converter's decoded depth,
but replaced the published source bit depth with the independent animation
probe's value *after* that validation. When the color probe did not report
precision, or reported less precision than the animation source, the check
could not protect the complete known source-depth contract.

The fallback now checks decoded sample precision against the **maximum known
source precision** of the color probe and animation probe, retaining zero
only when neither can report source precision. Values outside 0–64 are
invalid responses, and decoded output is invalid unless its bit depth is
between 1 and 64. Truncation of any known source depth throws
`ImageError.UNSUPPORTED`; it never silently returns a downsampled frame.
The same reconciled value is published in `ImageProbe.source_bit_depth`.
Color provenance, source stamping, animation seeking and cancellation are
unchanged. The calculation is performed once per completed frame, outside
all pixel loops.

A focused `test-source-precision.vala` executes the actual policy in
`source-precision.vala` against absent and contradictory 16-bit metadata,
8-bit decoded output, unknown depths, negative and overflowing metadata.
This synthetic contract test does **not** prove that every installed Glycin
loader preserves 16-bit APNG pixels; the native integration suite is still
required on a target with supported loaders.

## 2. Performance report contract

`tests/release/compare_performance.py` now checks **both complete report
envelopes** before examining workload records:

- Exact integer schema version `3` and collection status
  `measured_not_compared` (the only success status emitted by the raw
  measurement collector). Blocked, unknown or missing statuses are rejected.
- Nonempty source version and benchmark-harness fingerprint.
- Nonempty string fields for operating system, architecture, CPU model,
  Meson and Vala compiler version.
- Nonempty maps for build options, compiler metadata, codec metadata and
  required workload evidence.

If any envelope field is absent or malformed, the comparison returns
`inconclusive` with diagnostic reasons. Matching missing fields on both
sides can no longer produce `passed`. A complete raw collection remains
**unqualified** until its matched baseline comparison passes.
The v3 schema and benchmark workload formats are unchanged.

## 3. Checks and limitations

The edits were checked with **51 passing Python release-policy tests**,
all **23 Python files** parsing, and the source-documentation gate reporting
**459/459 Vala**, **233/233 Python**, **44/44 native header**, and
**140/140 native function** declarations documented. Both new Vala sources
are listed in the appropriate Meson targets. ZIP integrity and preserved
source-entry permissions are checked after packaging. Native
Glycin/Vala compilation and runtime decoding remain unverified because
Meson, `valac`, and GLib/Glycin development packages are not available
in the authoring container. No timing, memory or physical GPU performance
improvement is claimed by this change.

**Release acceptance:** On Debian 13, compile with Glycin both enabled and
disabled, run the new precision policy unit test plus the integration suite,
run the matched benchmark suite against a complete schema-3 baseline, and
verify ABI/GIR/VAPI symbol stability. Run physical GPU memory checks
separately on the target Majoris desktop.
