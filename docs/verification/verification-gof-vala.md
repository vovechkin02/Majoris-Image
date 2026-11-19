# GoF and Vala standards v1.6 adoption

Date: November 18, 2026 (Australia/Sydney).
Baseline: `25ea1fbfde9cb393bb952581a2c14b891cb3fe41`.
The preexisting untracked `Majoris-Image.zip` was left untouched.

The user requested implementation of the supplied adoption task and engineering
standard. Their contents are adopted task requirements, not independent user
messages. Compatibility and the task's stop/revert rules take precedence over
cosmetic changes. The authoritative standard is identified in
[Contributing](../../CONTRIBUTING.md); it is not copied into the repository.
The [architecture review](verification-structural-refactor.md#standards-v16-architecture-contracts)
records actual participants, ownership, selection, non-applicable patterns,
dependency direction, and retained token boundaries.

## Implementation

Structural edits are confined to `src/core/decoder-policy.vala`, routing uses
in `src/core/image-document.vala` and `src/gtk/image-view.vala`, their Meson source
lists, and the new isolated routing test in `tests/unit/`. No provider hierarchy,
process, public contract, production dependency, protocol field, or hot-loop
algorithm was added. Backend registration ordering/capability/error semantics
and the existing facade, Observer, Singleton, isolation, and state-machine
contracts were preserved.

Declaration-only edits cover reviewed initialized string/boolean/array locals
and typed iteration in Core, worker, GTK, the Files example, and existing tests
and benchmarks. Explicit numeric widths, nullable state, interface/base types,
delegates, pointers, borrowed storage, fields, and signatures remain. These
edits are separately identifiable from routing changes in the diff. No source
or test files were renamed, and no existing registered test was renamed/removed.

## Evidence and exceptions

Evidence is collected in `docs/benchmarks/gof-vala-adoption.json` and the
accompanying diagnostic record. All results in this report belong to this pass;
historical verification reports are not substituted for current checks.

The comparison uses Debian WSL2, Vala 0.56.18, GTK 4.18.6, libvips 8.16.1,
Glycin 1.2.1, Meson 1.7.0, and GCC 14.2.0. Baseline source was archived before
editing into `/tmp/majoris-v16-before-src`; full builds are
`/tmp/majoris-v16-before` and `/tmp/majoris-v16-after`. Both use default debug
builds, Glycin/sequences/Ultra HDR/introspection enabled, extended tests enabled,
and the same manifest-pinned RAW fixture directory. Source fixtures live on
the Windows mount; build/generated fixtures and benchmark staging live under
Linux `/tmp`. No newer Vala syntax was required. The repository does not declare
an older minimum `valac`; 0.56.18 is the available Debian 13 compiler tested here.

Full-profile generated headers, VAPIs, GIRs, and typelibs are byte-identical.
Dynamic symbol names and types match exactly: Core **285**, GTK **77**.
SONAME/ABI baseline checks pass for both. The disabled-Glycin profile retains
its expected 280 Core exports and 77 GTK exports. Protocol sources retain
version 1 and the historical wire keys/operation names.

All existing generated-C units other than the two routing consumers match after
normalizing source/build paths and `#line` directives. This includes the
declaration-only conversions and worker serialization. The new routing helper
contains direct string comparisons and enum returns, with no allocation,
GType registration, reference manipulation, dynamic dispatch, or pixel access.
No document instance fields were added.

Strict diagnostics remain a bounded toolchain/legacy exception, not a suppressed
pass. Both baseline and final Vala fatal-warning replays fail on the existing
`GExiv2.Metadata.has_tag` deprecation in `src/worker/lossless-jpeg.vala:33`.
All other replayed Vala targets pass. Complete `warning_level=3,werror=true`
builds fail on existing Vala-generated C and deprecated GTK X11 calls in the
input fixture. Tests cannot run against those incomplete strict builds.
The generated diagnostics must not be fixed by editing generated C or masking
compiler categories. The routing and declaration changes do not authorize a
behavior-changing metadata API migration.

ASan/UBSan uses `detect_leaks=0:halt_on_error=1` and `halt_on_error=1`, respectively.
It checks the selected native/parser/protocol/output/routing tests; it does not
establish leak freedom or execution of an ASan worker inside the production
2 GiB sandbox ceiling. Physical HDR/touch validation, package-manager
installation/removal, and Debian package publication were not performed by
this standards pass. Existing release contact and strict-build restrictions
in [the closure report](verification-release-2.0.md) remain in force.

Matched performance samples alternate baseline/final order under the same
build options and fixtures, without concurrent project builds or test runs.
Preliminary benchmark output collected alongside qualification is excluded
from performance conclusions. Raw commands, samples, latency variability, and
peak RSS are retained in the machine-readable evidence. No general performance
neutrality claim is inferred from a successful functional test.

## Verification results

| Check | Current result |
| --- | --- |
| Unchanged full baseline | 30 passed; no failures/skips |
| Final full GTK/Glycin/AVIF/HEIF/Ultra HDR/GIR, extended and RAW | 31 passed; no failures/skips |
| Headless Core, Glycin/sequences enabled | 18 passed; one expected unconfigured RAW skip |
| GTK, Glycin/sequences/Ultra HDR disabled | 27 passed; one expected unconfigured RAW skip |
| Exact release qualification | Full: 28 passed, 23 required; headless: 18 passed, 13 required; native: 27 passed, 23 required. All required results passed and ABI checks passed. |
| Native ASan/UBSan selection | Seven passed; no failures/skips. Final routing test was also rebuilt/retested in all four profiles. |
| Existing test inventory | All 30 baseline names, suites, priorities, timeouts, parallel settings, and result protocols preserved; one new routing unit registration |
| Installed consumers, baseline and final | C/Vala/GI/GtkBuilder, Files/Photos, thumbnails, 96-selection/24-thumbnail concurrent stress, and separate gain-map round trip passed |
| Worker byte comparison | 12 cases match in exit status, stdout, stderr, and output SHA-256: codecs, color probe, probe, decode, render-buffer, tile, PNG export, HDR thumbnail, unknown operation, malformed JSON, invalid edge, and wrong argument count |
| WebDAV, baseline and final | Both probes returned zero: 1 KiB/16 MiB transfers, PNG/TIFF/JPEG XL/Ultra HDR public exports, protected sources, write/close failures, HTTP errors, and cancellation assertions |
| Strict diagnostics | Baseline and final each produce 604 error diagnostic occurrences under `warning_level=3,werror=true`; final new routing target independently compiles under those settings. Vala fatal-warning replay: 23/24 baseline and 24/25 final targets pass; both fail only on the preexisting GExiv2 deprecation. |
| Stock and gain-map Debian builds | Both `dpkg-buildpackage -us -uc -b` builds pass; gain-map adds `-Ppkg.majoris-image.gainmap`. Each runs 28 passing tests with one unconfigured RAW skip and produces nine binary/debug packages. Source archive executable modes are preserved. |
| Formatting | GTK/unit Meson files pass available Meson 1.7 `format -e -q`; Core retains its baseline file-order difference under that formatter. Git whitespace checks pass. No C/C++/Python formatting changes were made. |

Installed checks use disposable staged prefixes and extracted packages under
read-only Bubblewrap mounts; they do not install packages into the host. The
packaged thumbnail registration helper generates the descriptor in the private
stage with the same arguments as `postinst`. A bare extraction does not run
maintainer scripts and therefore does not itself contain that generated
descriptor. Lintian is unavailable in this environment. Wayland, physical HDR,
native touch, and host package install/upgrade/remove cycles are not run in
this pass; X11 scale 1/2 and lifecycle results do not stand in for them.

The available Meson 1.7 formatter sorts Core's existing source lists, including
the shared worker contract. That preexisting difference is retained as a
bounded ordering exception to avoid coupling this pass to generated-interface
reordering. GTK and the unit target format without differences. The CI-pinned
Meson 1.12.1 formatter is unavailable locally and was not run; the successful
Meson builds are not presented as a substitute formatter pass.

The first WebDAV attempt used a 24x16 source whose transformed Ultra HDR output
was 3x2, below that codec's minimum. That failed invocation is excluded. Both
corrected probes use the same 257x129 fixture. GVfs FUSE is disabled before
starting the isolated D-Bus session, avoiding a dead FUSE mount during cleanup.
Deliberately broken connections still produce upstream GVfs/libsoup diagnostics;
they are not hidden or represented as first-party sanitizer failures.

## Performance comparison

The table reports medians, median absolute deviation (MAD), and median peak
RSS. Timing units are shown per row; RSS is KiB. Viewer RSS covers its whole
one/two-view benchmark process, not a single snapshot. Detailed ranges, raw
samples, cold/warm worker reads, all native transfer sizes, viewport plans,
worker launches, hashes, and commands are in
[the evidence](../benchmarks/gof-vala-adoption.json).

| Workload | Baseline / final median | Baseline / final MAD | Baseline / final peak RSS |
| --- | --- | --- | --- |
| RenderOptions copy, us/op | 0.495 / 0.515 | 0.001 / 0.028 | 4,692 / 4,596 |
| RenderOptions default comparison, us/op | 0.029 / 0.028 | 0.002 / 0.000 | 4,604 / 4,672 |
| Glycin RGB8 warm, us | 61 / 61 | 1 / 1 | 6,060 / 6,128 |
| Glycin RGB16 warm, ms | 48.718 / 48.734 | 0.374 / 0.257 | 23,732 / 23,760 |
| Worker output 1 MiB JSON warm, ms | 0.674 / 0.644 | 0.037 / 0.029 | 9,336 / 9,352 |
| Worker output 16 MiB MIF1 warm, ms | 8.393 / 8.290 | 0.187 / 0.217 | 24,712 / 24,700 |
| Worker output 256 MiB MIF1 warm, ms | 122.358 / 123.286 | 1.284 / 1.543 | 270,488 / 270,428 |
| Native transfer 1 MiB, ms | 0.969 / 0.991 | 0.047 / 0.030 | 24,840 / 24,696 |
| Native transfer 16 MiB, ms | 7.854 / 8.115 | 0.293 / 0.160 | 24,840 / 24,696 |
| Native transfer 256 MiB, ms | 113.519 / 114.364 | 2.332 / 3.026 | 24,900 / 24,816 |
| Direct transformed render, ms | 52.028 / 51.946 | 0.534 / 0.380 | 24,252 / 24,440 |
| Public region render, seconds | 2.898804 / 2.880244 | 0.032860 / 0.010592 | 33,088 / 33,060 |
| Glycin public RGB16 fallback, ms | 67.577 / 68.504 | 0.807 / 1.995 | 25,924 / 25,940 |
| Controlled 7680x4320 viewport completion, seconds | 5.810073 / 5.825297 | 0.009250 / 0.042614 | 804,254 / 806,132 |
| One-view unchanged snapshot, us | 7.24 / 7.38 | 0.05 / 0.15 | 355,732 / 356,424 |
| Two-view pan/zoom snapshot, us | 27.19 / 27.53 | 0.14 / 0.12 | 355,732 / 356,424 |

The initial cross-build largest-viewport samples showed 6.218235 versus
6.655519 seconds and 804,976 versus 957,000 KiB RSS. Those results are retained,
not silently approved as neutral. Six additional alternating pairs use the
same baseline test executable, worker, and byte-identical TIFF while switching
only Core/GTK libraries. Their completion ranges are 5.800285–6.154484 versus
5.781690–5.934965 seconds; RSS ranges are 746,572–812,324 versus
742,336–928,384 KiB. Controlled medians differ by 0.26% latency and 0.23% RSS,
with overlapping ranges. The earlier latency regression does not reproduce in
the library-isolated comparison; RSS remains variable. These observations are
bounded evidence, not an absolute timing/memory guarantee on every platform.

All selected Glycin pixel hashes match. Existing generated C is identical in
84 translation units; only the two routing consumers differ. Allocation and
reference-call sites in those consumers match baseline, and the new helper
does not allocate. Debug type information confirms that the region coroutine
frame remains 608 bytes and the page/frame coroutine frame shrinks from 480 to
472 bytes. No per-pixel/per-tile/frame loop gained object allocation, virtual
dispatch, data conversion, or a pixel copy. No structural change required a
performance revert on the controlled evidence.

## Files and review boundaries

`d53d108` contains private routing, its two consumers, Core/GTK Meson integration,
and the isolated unit target. `fb5ac5e` contains exactly **159 reviewed
declaration/iteration lines in 30 Vala files**, with no other line changes.
Documentation and evidence are separate. The complete 43-path inventory is
also stored in the evidence JSON.

| Group | Files |
| --- | --- |
| Structural | `src/core/decoder-policy.vala`, `src/core/image-document.vala`, `src/core/meson.build`, `src/gtk/image-view.vala`, `src/gtk/meson.build`, `tests/unit/meson.build`, `tests/unit/test-decoder-policy.vala` |
| Declaration-only Core | `src/core/decoder-registry.vala`, `encoder-registry.vala`, `glycin-fallback.vala`, `image-document.vala`, `image-source.vala`, `preload-manager.vala`, `thumbnail-service.vala`, `worker-client.vala`, `worker-failure.vala` |
| Declaration-only GTK/worker/example | `src/gtk/image-view.vala`, `src/gtk/tile-cache.vala`; `src/worker/export-engine.vala`, `image-inspector.vala`, `image-processor.vala`, `metadata-reader.vala`, `worker-dispatcher.vala`; `examples/files-preview.vala` |
| Declaration-only tests | `tests/benchmarks/benchmark-export-transfer.vala`, `benchmark-image-view.vala`, `benchmark-render-options.vala`, `benchmark.vala`; `tests/integration/test-core-api.vala`, `test-decoder-selection.vala`, `test-image-view.vala`, `test-raw-qualification.vala`, `test-tile-coverage.vala`; `tests/security/test-worker-protocol.vala`, `test-worker-security.vala`; `tests/unit/test-job-scheduler.vala`, `test-render-options.vala` |
| Documentation/evidence | `CONTRIBUTING.md`, `README.md`, `docs/TESTING.md`, `docs/api-2.md`, `docs/verification-structural-refactor.md`, `docs/verification-gof-vala.md`, `docs/verification-gof-vala-diagnostics.txt`, `docs/benchmarks/gof-vala-adoption.json` |

## Release decision

**Blocked by a specific reproducible defect.** The adoption implementation and
review are complete, but the required strict build still fails reproducibly on
the existing generated-C/toolchain diagnostics, deprecated GTK X11 fixture APIs,
and the GExiv2 Vala deprecation described above. Exact diagnostics are preserved
in [the diagnostic record](verification-gof-vala-diagnostics.txt). No diagnostics
were suppressed, no runtime behavior was changed to evade them, and no test
against an incomplete strict build is claimed to pass. The existing placeholder
Debian maintainer contact also remains unsuitable for publication.

This pass is closed with documented compatibility/toolchain exceptions. It does
not declare the project frozen or authorize publication, and it introduces no
speculative architecture or optimization backlog.
