# Majoris-Image 2.0.1 — bounded performance closure

**Source:** `Majoris-Image(20261119-025234).zip`  
**Engineering standard:** Majoris Cross-Project Engineering Standards v1.8  
**Scope:** APNG forward-frame decoding, worker admission scaling, and GTK disposal.

## Changes and invariants

| Component | Targeted correction | Preserved contract |
| --- | --- | --- |
| `src/core/decoding/glycin-fallback.vala` | One document-owned forward decoder cursor; reuse advances from the last validated frame | Freshness stamp, BWRAP sandbox, color/precision checks, per-operation cancellation and timeout, bounded pixel conversion, independent fallback for backward/concurrent requests |
| `src/core/image-document.vala` | Carries the private cursor across frame documents | Public class API, `open_frame()` signature, source verification, native sequence behavior |
| `src/core/execution/job-scheduler.vala` | Stable priority heap, FIFO by submission sequence | Four admissions, one reserved interactive slot, priorities, cancellation wakeups and releases |
| `src/gtk/image-view.vala` | Releases metadata and presentation state at disposal | GTK object identity, cancel-on-dispose, tile eviction, animation suspension and public properties |

The session is limited to **one retained Glycin image handle per document**.
It does not retain every decoded frame, modify Glycin's sandbox selector, or
add a process-wide cache. The first frame still uses the original decoder path;
subsequent forward frames can reuse the verified session. Rewinds and concurrent
requests reopen independently instead of seeking an already-advanced handle.
Invalid or canceled sessions are not reused.

The scheduler no longer performs a full sort on every `acquire()`. The
heap comparator orders tickets by `priority`, then by submission sequence, and
retains the high-priority reserved-slot policy.

## Added verification

- The existing APNG integration test now covers sequential frame one/two,
  reverse seek to frame zero, replay, continued access from a returned frame,
  precancellation, and pixel-byte equality against an independent frame decode.
- The scheduler unit executable adds a seeded 1,200-request mixed-priority
  burst with canceled tickets. It reports submission elapsed time for comparison
  without imposing environment-dependent latency thresholds.
- GTK integration adds repeated load/remove/dispose operations and asserts that
  document and metadata references are released after each view is disposed.
- A new `animation-apng` public-API workload is included when Glycin is
  explicitly enabled, using the existing generated APNG fixture. The existing
  GIF animation workload and registration remain unchanged.
- The versioned source documentation checker validates Valadoc/gtk-doc/Doxygen
  comments for the modified and new declarations.

## Build and performance qualification

On the Debian 13/Majoris development environment, install the documented
Meson/Vala/Glycin/GTK development dependencies, then run:

```sh
meson setup build -Dglycin=enabled -Dgtk=true -Dtests=true
meson compile -C build
meson test -C build --print-errorlogs
meson test -C build --suite performance --print-errorlogs
python3 tests/release/check_source_docs.py
```

Build a second profile with `-Dglycin=disabled` to check the optional-codec
build condition. Compare native/optional export ABI, the installed GIR/VAPI,
worker protocol, APNG frame pixels, scheduler cancellation and the renderer's
multi-viewer memory behavior against the unchanged pre-edit archive.

**Status:** Source edits and scanner checks only. The native Vala/Meson
compiler toolchain and physical GPU/HDR qualification environment were not
available in the editing container, so no native compile or runtime performance
measurements are claimed. The modified source is **pending full compilation,
release regression and matched benchmarks** on the target system. In particular,
there is no independent measured result for large APNG playback or GPU-memory
release yet; this document must not be treated as a completed performance signoff.
