# Majoris Image 2.0.0 final closure verification

Date: November 18, 2026 (Australia/Sydney). Baseline:
`aa1cca57f450826983a8e2765303e5347b4bd792`. This is a finite correctness and
release-qualification pass. No transport, export architecture, codec algorithm,
Glycin conversion, scheduler, or viewport optimization was introduced.

## Release decision

**BLOCKED.** The direct-render correction and first-party Vala warning fixes
are complete. Repository-wide `warning_level=3,werror=true` still fails on
Vala-generated C and deprecated GTK X11 APIs used by the XTest fixture.
Debian publication also requires an authorized monitored maintainer address.
The existing `packages@majoris.invalid` was preserved; no contact was invented.
The implementation is not declared frozen or ready for publication.

## Implementation changes

| File | Reason |
| --- | --- |
| `src/core/decoder-registry.vala` | Capture the existing source stamp before direct native decoding; verify after manifest and pixel validation; reject changes and post-decode deletion with `SOURCE_CHANGED` and the existing profiling event. Move duplicate-registration errors outside the lock while retaining atomic checking/insertion and the public `throws IOError` contract. |
| `src/core/image-probe.vala` | Compile manifest serialization for worker/protocol targets and deserialization for Core/protocol targets. |
| `src/core/worker-request.vala` | Compile request serialization for Core/protocol targets and deserialization for worker/protocol targets. |
| `src/core/worker-failure.vala` | Compile failure serialization/deserialization only where consumed; retain classification in the protocol target and test it. |
| `src/core/meson.build` | Define the private Core contract-consumer compilation mode. |
| `src/worker/meson.build` | Define the private worker contract-consumer compilation mode. |
| `src/core/operation-profile.vala` | Omit the unused event method only in the standalone timing-only export benchmark. |
| `src/gtk/presentation.vala` | Permit the policy unit target to compile only its policy decision logic. The production GTK target retains all methods. |
| `tests/unit/meson.build` | Select policy-only compilation for that unit target. |
| `tests/security/test-worker-protocol.vala` | Add typed diagnostic round trips, message checks, classification checks, and malformed-diagnostic rejection using the existing shared contract. |
| `tests/integration/test-core-api.vala` | Extend the existing Bubblewrap launcher with deterministic direct-render mutation, malformed-manifest, cancellation, cleanup, recovery, and transformed-pixel checks. |
| `tests/benchmarks/benchmark.vala` | Add a transformed direct-render workload to the existing public API benchmark. |
| `tests/benchmarks/meson.build` | Include direct rendering in the existing performance registration; select timing-only compilation for the standalone export benchmark. |
| `native/sequences.c` | Compare the signed track count with the fixed 64-element capacity using a matching type. |
| `native/ultra.c` | Name the existing GNU11 binary16 type with a narrowly scoped `__extension__` typedef; preserve libuhdr sample layout and conversion. |
| `src/core/thumbnail-png.c` | Explicitly mark the unused GTask callback source argument. |
| `tests/security/fuzz-native.c` | Explicitly mark the unused libFuzzer initialization argument. |
| `tests/security/cache-publish-gate.c` | Copy the POSIX `dlsym` pointer representation into the function pointer with a compile-time size check. |
| `README.md` | Link this closure report. |
| `docs/api-2.md` | Document direct-render freshness and error behavior. |
| `docs/TESTING.md` | Document regression coverage, the render workload, and strict-build status. |
| `docs/verification-release-2.0.md` | Consolidate this pass and its release decision. |
| `docs/verification-release-2.0-diagnostics.txt` | Preserve the original warnings and final strict diagnostic inventory. |
| `docs/benchmarks/release-closure.json` | Preserve measured rendering, benchmark, remote publication, and viewer-probe evidence. |

The returned `PixelBuffer` is read once and retained locally until the source
check succeeds. No hashing, snapshot, extra pixel copy, protocol field, public
API, or dependency was added. `WorkerClient.cleanup(output)` remains in
`finally`. Worker and malformed-output errors retain their original
classification; cancellation propagates as GIO cancellation. Only missing or
unsupported codecs are eligible for Glycin fallback. Glycin's own freshness
checks were not modified.

## Baseline and build diagnostics

The source tree was clean apart from the preexisting untracked
`Majoris-Image.zip`, which was left untouched. Compiler diagnostics were captured
before source edits. Baseline and final builds are separate directories under
`/tmp/imagekit-closure-*` in Debian WSL2, on Linux
`6.18.58.1-microsoft-standard-WSL2`, x86-64, Debian 13, GLib 2.84.4,
GTK 4.18.6, Glycin 1.2.1, Vala 0.56.18, GCC 14.2.0, and Meson 1.7.0.
Builds use the project's default debug configuration. Small fixture paths are
on the Windows-mounted source tree; build artifacts and the large TIFF are on
Linux tmpfs. The installed Bubblewrap is 0.12.0.

Original Core Vala diagnostics under fatal warnings:

| Location in baseline | Diagnostic | Correction |
| --- | --- | --- |
| `decoder-registry.vala:11` | Unhandled `GLib.Error` at the lock containing an error exit | Keep the error exit outside the lock without widening the installed method contract. |
| `image-probe.vala:67` | `ImageProbe.serialize` never used | Target-specific contract compilation. |
| `worker-request.vala:20` | `WorkerRequest.deserialize` never used | Target-specific contract compilation. |
| `worker-failure.vala:3` | `WorkerFailure.classify` never used | Retain and test classification in the protocol target. |
| `worker-failure.vala:28` | `WorkerFailure.serialize` never used | Target-specific contract compilation. |

Checking every Vala compiler command exposed reciprocal unused methods in the
worker, unused failure methods in the protocol target, presentation methods in
the isolated policy target, and the profile event in the timing-only benchmark.
Those were addressed with the same narrow target separation and meaningful
protocol assertions. **All 22 Vala compilation targets pass `--fatal-warnings`.**
No warning category was globally disabled.

The complete strict build was also run with Ninja `-k 0` to collect failures
beyond the first target. The handwritten signed comparison, unused callback
parameters, GNU binary16 declaration, and POSIX symbol conversion were corrected.
The remaining handwritten C diagnostics are GTK 4.18 deprecations in
`test-widget-interaction.c:128,365,418`: `gdk_x11_surface_get_xid` and
`gdk_x11_display_get_xdisplay`. These X11 handles are required by the existing
XTest fixture. No blanket deprecation suppression was added.

The remaining generated C diagnostics include unused generated parameters,
variables and labels, generated closure-function casts, `const` mismatches in
GBytes bindings, `volatile` GOnce storage passed to GLib atomic macros, and
private extern declarations reported as used but not defined. These originate
in generated translation units, including diagnostics mapped back to `.vala`
lines. Hand-editing generated output or suppressing whole compiler categories
would not establish strict compliance. The exact target and diagnostic inventory
is in [the compiler evidence](verification-release-2.0-diagnostics.txt).

## Correctness and release matrix

The baseline full build passed 28 tests, failed none, and skipped the unconfigured
external RAW case. Baseline performance passed all 35 registrations. Final
configuration enables the existing RAW corpus and Wayland option, so those
registration differences are intentional. No existing name, suite, parallel
policy, or timeout changed except the RAW placeholder becoming its real
180-second test. Direct-render coverage extends the already mandatory
`Headless Core API` registration. Qualification required-test sets were not
changed.

| Configuration | Built | Passed | Failed | Skipped |
| --- | --- | --- | --- | --- |
| Full GTK, Glycin, sequence, introspection | Yes | 26 | 0 | 0 |
| Headless Core with Glycin | Yes | 15 | 0 | 0 |
| Glycin and sequence disabled | Yes | 25 | 0 | 0 |
| Strict warnings | Vala: yes; complete C build: no | 22 Vala compilations | Repository-wide strict build | Tests not run in incomplete build |
| ASan/UBSan native selection | Yes | 7 registered tests | 0 | 0 |
| ASan/UBSan Core integration with regular sandbox worker | Yes | 1 integration command | 0 | 0 |
| Extended GTK and complete 8K coverage | Yes | 2 | 0 | 0 |
| RAW interoperability | Yes | 1 | 0 | 0 |
| Debian stock | Yes, clean package build | 26 | 0 | 1 external RAW placeholder |
| Debian gain-map | Yes, clean package build | 26 | 0 | 1 external RAW placeholder |
| Installed consumers | Yes, disposable extracted packages | C, Vala, GI, GtkBuilder, Files, Photos, thumbnails, gain map, and installed stress | 0 | 0 |
| Wayland | Yes, WSLg compositor | 1 | 0 | 0 |
| Physical HDR | Unavailable | Not run | Not run | Not a simulated pass |

The complete release runner passed all **29 functional registrations**, all
**26 exact mandatory results**, installed consumers, ABI checks, and all
**36 performance registrations**, with no failures or skips. Its exit status
was zero. This runner does not include the separate repository-wide strict
build, maintainer-contact requirement, or physical-hardware qualification;
its successful result therefore does not override the blocked release decision.

The new regression fails against the unmodified baseline at the first mutation
assertion, then passes with the correction. Coverage includes transformed
dimensions, native-endian float32 RGBA samples including alpha, unchanged source
bytes, metadata change, same-size content rewrite, appended data, inode
replacement, deletion, malformed JSON, cancellation before launch and while a
controlled launcher is pending, job cleanup, and successful rendering after
failures. Mutation cases each observe exactly one sandbox launch, so a source
change does not trigger another decoder. The existing decoder-selection suite
continues to exercise legitimate transformed Glycin fallback. Synchronization
uses worker completion and a file monitor/FIFO gate, not arbitrary sleeps.

Security qualification retains the production sandbox and existing fault
infrastructure. The separate WebDAV pass covers native protected-source cases,
source and destination mutations, descriptor pinning, close-time errors, HTTP
403/507, cancellation/finalization, bounded native transfer, and remote cleanup.
The probe completed its assertions and wrote its JSON result. GVfs emitted
GError warnings during deliberately terminated HTTP connections; these are
recorded backend diagnostics, not evidence of a first-party sanitizer defect.

ASan workers cannot reserve shadow memory inside the worker's 2 GiB address-space
limit. The seven established native tests exercise parsers directly. The extra
Core integration command instruments Core but uses the regular sandbox worker.
The established sanitizer settings use `detect_leaks=0`; these runs establish
address/undefined-behavior coverage, not a blanket leak-free claim.

## Performance and memory

The alternating direct-render comparison uses the same final benchmark executable
and worker with baseline/final Core selected by `LD_LIBRARY_PATH`. Three batches
of three samples per phase alternate order. Each render launches one worker.
The initial 20-sample runs, including reported P95, CPU, host/worker RSS, phase
timing, range and variability, are retained in the
[machine-readable evidence](../benchmarks/release-closure.json). Their large-image
wall times were affected by concurrent qualification; they are not attributed
to source-stamp cost.

| Controlled direct render | Baseline median | Final median | Baseline / final range | Baseline / final MAD |
| --- | ---: | ---: | --- | --- |
| Small 24 × 16 PNG, rotated | 71.706 ms | 74.545 ms | 71.370–76.476 / 72.069–76.130 ms | 0.336 / 1.382 ms |
| 200 MP TIFF, rotated to 1024-pixel edge | 2.936587 s | 2.956019 s | 2.919005–3.031342 / 2.909674–3.022797 s | 0.008479 / 0.017482 s |

Median total source-query time rises from 1,397 to 3,394 microseconds for the
Windows-mounted small fixture and from 238 to 394 microseconds for the Linux
large fixture. This does not demonstrate disproportionate verification overhead.
The source-stamp safety checks were retained.

The existing performance suite checks worker-output consumption, native export,
non-native publication, Glycin RGB8/RGB16 conversion, recipe copying/comparison,
worker startup, still/HDR decoding, tiles, navigation and animation. Representative
measurements and numerical-output comparisons are consolidated in the evidence
file. Every Glycin synthetic conversion hash matches baseline, including mixed
concurrent layouts. Small fluctuations alone do not justify algorithm changes.

| Representative workload | Baseline | Final |
| --- | ---: | ---: |
| Bounded 256 MiB MIF1 warm read, median | 118.728 ms | 124.469 ms |
| Same reader peak RSS, median | 270,376 KiB | 270,496 KiB |
| Glycin small RGB8 warm conversion, median | 60 us | 59 us |
| Glycin RGB16 warm conversion, median | 48.744 ms | 48.989 ms |
| Native 256 MiB transfer, median | 103.971 ms | 116.012 ms |
| Same transfer process RSS, median | 24,568 KiB | 24,696 KiB |
| Same transfer payload buffer | 262,144 bytes | 262,144 bytes |
| Public PNG export, median | 217.959 ms | 224.122 ms |
| Public JPEG XL export, median | 465.833 ms | 467.597 ms |
| Non-native 16 MiB create with validated explicit-close path, median | 59.560 ms | 58.867 ms |
| Non-native 16 MiB replacement with that path, median | 60.013 ms | 56.462 ms |
| Public TIFF tile workload, median | 2.865469 s | 2.883144 s |
| Two-viewer unchanged snapshot, median per iteration | 10.13 us | 9.96 us |
| Two-viewer pan/zoom snapshot, median per iteration | 26.57 us | 26.27 us |
| GTK snapshot benchmark host peak RSS | 360,432 KiB | 357,324 KiB |

These are process/runner measurements on a shared WSL host, not controlled
cross-machine speed targets. Native transfer remains bounded and its production
algorithm is unchanged. No repeated meaningful regression was established that
would justify reopening the completed implementations. The raw summaries retain
CPU, RSS, range and variability rather than concealing slower samples.
The non-native comparison uses three samples per phase against the same
loopback GVfs WebDAV backend. Experimental streaming/staging probe modes were
measured by the existing script but were not adopted into production.

The existing installed stress passed 96 preview selections concurrently with
24 thumbnail requests and found no worker artifacts. The existing GTK snapshot
benchmark was also adapted outside the repository to counts `1,4,8,1,4,8`,
with Cairo/Xvfb. It completed both baseline and final probes. Its actual first
allocations, snapshot timings, and retained RSS are saved in the evidence.
Final retained host RSS after each group was 211,144; 353,256; 611,888;
660,492; 782,516; and 918,780 KiB, with peak 942,192 KiB. The baseline also
grew and ended at 909,868 KiB. This probe does not distinguish allocator,
renderer, or fixture retention from leaks and does not establish stable
post-destruction memory. No new growth defect was attributed to this patch.
Worker RSS is separately sampled in the public API/direct-render benchmarks;
the multi-viewer probe itself records host RSS only. Reliable GPU/texture-memory
accounting and representative physical-GPU pressure testing were unavailable.
Post-run inspection found no surviving closure worker. Two Bubblewrap processes
from unrelated November 16–17 build paths predated this pass and were left
untouched; they are not counted as successful cleanup by this run.

## ABI, packaging, and platform limits

Actual exported symbol sets match exactly, including absence of extra exports:
Core **285**, GTK **77**. Native-only Core matches its expected **280** exports.
Public headers, VAPI, and GIR compare byte for byte with baseline. SONAMEs remain
`libmajoris-image.so.2` and `libmajoris-image-gtk4.so.2`; public namespaces,
worker identity, protocol version **1**, and JSON field spelling are unchanged.
The headless dependency checker confirms no GTK/GDK dependency.

Both Debian profiles use isolated Linux staging copies with Git modes preserved.
Final artifacts are under `/tmp/imagekit-closure-debs-stock` and
`/tmp/imagekit-closure-debs-gainmap`. Consumer checks use extracted package
views, relocated pkg-config prefixes, and canonical worker paths inside the
existing disposable Bubblewrap package namespace. Thumbnail registration is
generated with the installed helper, matching post-install behavior.
Package-manager install/remove/upgrade transactions on a disposable Debian
system were not performed. Lintian was unavailable. Tracked source archive and
extraction modes passed for `debian/rules` and the installed-consumer script;
this checks the current committed archive's modes, not a newly committed release.

X11 input/accessibility tests passed at scales 1 and 2. The registered Wayland
lifecycle test passed at scales 1 and 2 using the real WSLg Wayland socket. This is virtual-display
WSLg coverage, not certification of the intended Majoris OS compositor, physical
touch gestures, physical HDR/color management, or representative GPU hardware.
These platform checks remain explicit validation gaps.

Changed C files passed the available clang-format 19.1.7 check except
`native/sequences.c`, whose unrelated preexisting line-274 wrapping violation
was reproduced on the baseline. No formatting sweep was performed.
`git diff --check` passes.

## Remaining release work

1. Resolve the documented generated-C/toolchain strict-build failures and the
   XTest API deprecation policy, then rerun the unchanged strict settings.
2. Supply an authorized monitored maintainer address for `debian/control` and
   the current changelog entry before publication.
3. Complete the named target-platform, physical-hardware, package-transaction,
   and memory-retention qualification gaps under the release owner's policy.

The existing full-buffer non-native publication path, MIF1 staging, JPEG XL
materialization, tile cache, preload architecture, worker isolation and bounded
native transfer remain accepted compatibility decisions. No new optimization
or structural-refactoring milestone is proposed by this closure pass.

**BLOCKED**
