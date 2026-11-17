# Core and worker overhead verification

## Opening and worker execution paths

| Consumer or source | Requests and result authority |
| --- | --- |
| Core still, vector, HDR, or first page/frame | `DecoderRegistry.open` selects a backend and sends one `decode` request on the native path. The manifest supplies capabilities, dimensions, color and HDR state, and page or frame information. Core validates the edge and identity before constructing the document. |
| GTK PNG, JPEG, other still, vector, HDR, animated, or multipage | `ImageView.load_source` creates its registry, calls `inspect` for RAW detection and early errors, then opens and presents the decoded document. Native inspection launches a separate `probe` worker. |
| GTK RAW preview-first | After `probe`, the view requests embedded-preview `decode` and presents it before starting the full `decode`. If development fails after preview presentation, the view keeps the preview and displays the existing warning. With preview-first disabled or preview-only requested, the view still inspects and issues a single decode with the requested options. |
| Existing document | A new native page or frame requests a fresh `decode` from the document's worker. A request returning the same animated frame verifies the source first. Native region reads use `tile`. APNG frame decoding can use Glycin and retains its separate animation metadata and frame checks. |
| Custom decoder | Each registry call takes a fresh ordered backend snapshot. With custom registrations, it validates the type and reads up to 65,536 bytes for MIME guessing and backend acceptance. Capability filtering determines which callbacks are eligible. Built-in selection follows when no custom worker accepts the request. |
| Glycin fallback | Eligible decode or inspection errors can trigger content sniffing and a separate `colour-probe` worker for color metadata, followed by Glycin loading. A later page request can first try APNG inspection. Error eligibility, strict capability validation, and the active decoder's result remain authoritative. |
| GVfs source | Each worker request validates the GFile and stages the bytes when there is no native path. The worker consumes the job's private copy. Inspection and decode can each stage the same URI and independently read it. |
| Stream or memory source | `ImageSource.from_stream` and `from_bytes` stage once into a private file. It has a native path, so worker requests bind it directly. A document keeps the owning source alive during later page and region reads. |

A worker job acquires its scheduler slot before creating its private job
directory. It resolves the native path, queries the source type, stages the
source if needed, serializes the request, and builds the Bubblewrap argument
vector. Executable resolution honors the registered backend, the current
`MAJORIS_IMAGE_WORKER` override, and the installed worker path in that order.
After launch, Vips initializes, the dispatcher runs the operation, and the
worker writes its output. The parent drains bounded diagnostics and reaps the
child before releasing the slot. Consumers use the
[bounded descriptor reader](verification-worker-output.md) for text, pixels,
and PNG output, then validate the format and clean the job directory. Worker errors
trigger job cleanup before they propagate. Cancellation and timeout interrupt
the diagnostic read after signaling the subprocess, then wait for the direct
child. This lets cleanup complete even when a launch helper still has the
pipe open.

## Removed work and ownership

- A native page or frame already verified the document's full identity before
  selecting its path. Previously `open_with` immediately fetched the same
  stamp again. The current call carries the just-verified, nonempty identity
  into that decode. The post-decode check still performs a fresh query, and the
  worker still validates the source type after admission. A missing stamp
  follows the existing behavior and can be queried anew.
- Custom selection needs the regular-file check before reading a header. The
  registry now requests just the type and skips calculating the unused stamp.
  Backend acceptance, MIME sniffing, and pre-decode and post-decode stamps
  retain their existing order. Worker admission also requests only the source
  type for non-thumbnail operations. Thumbnail workers additionally request
  the modification time for their existing request field.
- A remote staging job owns both streams and the partial file. It closes the
  destination on write success, closes both streams on failure, and checks
  cancellation before loading the job into the sandbox. Cleanup closes use a
  fresh, non-cancelable call and preserve a primary transfer error. The job
  removes partial files. The same cleanup policy closes an
  `ImageSource.from_stream` storage stream before unlinking a failed spool.
  The caller keeps ownership of its input stream. The final reference to a
  successful stream- or memory-backed `ImageSource` releases its file.

Identity stamps use the same URI, optional ETag and file ID, size,
modification seconds and microseconds, and change seconds and microseconds.
Inspection establishes no reusable authorization for an open. Each tile and
native page request independently checks the stamp against its document. The
decoder response remains authoritative. Thumbnail metadata and cache
publication and preload cache lookups keep their own validation scopes.

The version 1 request and response shapes still use
`OPERATION SOURCE OUTPUT REQUEST EDGE`, JSON request and manifest files, and
MIF1 pixels. Bubblewrap retains its isolated namespaces, dropped capabilities,
read-only system and source mounts, private temporary storage, cleared
environment, parent-death behavior, and a separate writable job directory.
Regular-file checks retain their existing link-following behavior. Output
validation still rejects symlinks and out-of-range sizes before loading bytes.
The worker retains its 2 GiB address-space, 90 CPU-second, 1 GiB file,
128-descriptor, and zero core-file budgets. The parent retains the 1–120 second
timeout, the bounded 64 KiB diagnostic tail, and its cancellation, timeout,
sandbox failure, crash, codec-missing, resource, and malformed-result error
paths. Admission remains limited to four jobs with a slot reserved for
foreground work.

## File changes

| Files | Responsibility |
| --- | --- |
| `image-source.vala`, `image-document.vala`, `decoder-registry.vala` | Scoped identity reuse, type-only selection validation, and owned stream cleanup. |
| `worker-client.vala` | Type and thumbnail attribute selection, remote stream lifetime, interruptible diagnostic drain, and job phase metrics. |
| `operation-profile.vala`, `src/core/meson.build`, `glycin-fallback.vala`, `preload-manager.vala`, `thumbnail-service.vala` | Internal opt-in timing and counts for source, cache, and fallback paths. |
| `src/worker/main.vala`, `src/worker/meson.build`, `meson.options` | Benchmark-build timings for initialization and dispatch. Successful profile records travel on the existing diagnostic pipe. |
| `tests/benchmarks/benchmark.vala`, `benchmark-image-view.vala`, `run_benchmarks.py`, `tests/benchmarks/meson.build` | Core, worker, GTK RAW, and GVfs workloads and reporting. |
| `test-core-api.vala`, `test-decoder-selection.vala`, `test-worker-security.vala`, `worker-fault.c` | Source validity, backend order, stream lifetime, and sandboxed source-replacement regressions. |
| `docs/TESTING.md`, `docs/benchmarks/core-worker-*.json`, `docs/benchmarks/core-worker-baseline.patch`, and this report | Reproduction commands, baseline instrumentation, call paths, measured results, and verification. |

## Performance results

The baseline is Git `911600e0f4cc40126519acf729b0b5a33e4324b2` with the same
timing hooks and benchmark drivers applied before the optimizations. Both
builds used Debian 13.5 under WSL2 x86-64, Meson 1.7.0, GCC 14.2.0,
Vala 0.56.18, GLib/GIO 2.84.4, libvips 8.16.1, and Bubblewrap 0.12.0.
The release-profile builds enabled GTK, Glycin, sequences, Ultra HDR,
GIR, and worker timing. Source fixtures and job files used Linux `/tmp`
storage. Each workload ran nine times in a fresh benchmark process and a
fresh cache/staging directory, with OS caches retained. The warm preload
case reports time per verified cache hit inside each fresh process.
Extensions used the manifest-pinned Canon 350D RAW fixture, generated
BigTIFF/JPEG/GIF fixtures, and the checked-in PNG and multipart EXR.
The Glycin workload uses the existing fault worker to return a deterministic
missing-codec error on the QOI fixture.

The benchmark runner records fixture SHA-256, worker executable SHA-256,
compiler versions, build settings, per-stage counts and times, launch
counts, byte counts, and observed peak memory. The report artifacts retain
median stage totals, end-to-end medians, median absolute deviations (MAD),
and ranges. [Baseline data](benchmarks/core-worker-before.json)
[Optimized data](benchmarks/core-worker-after.json)

To reconstruct the instrumented baseline, apply the baseline instrumentation
patch to a temporary source tree at the baseline revision. Copy the current
`tests/benchmarks/{benchmark.vala,benchmark-image-view.vala,run_benchmarks.py,meson.build}`
and `tests/security/worker-fault.c` into the corresponding paths. Configure
both trees with the same options and RAW directory, build the generated
fixtures, and run the driver with `--samples 9`. Use `--fault-worker` with the
built `tests/security/worker-fault` for `cancel` and `fallback`. Pass the build
worker and benchmark binaries from the same build. The JSON workloads record
the run modes and fixture identities. [Baseline instrumentation patch](benchmarks/core-worker-baseline.patch)
[Benchmark commands](TESTING.md#benchmarks)

| Workload | Before, median ± MAD (ms) | After, median ± MAD (ms) | Sandboxes per sample | Source queries before → after |
| --- | ---: | ---: | ---: | ---: |
| Core PNG open | 51.73 ± 1.07 | 52.76 ± 0.81 | 1 | 3 → 3 |
| Core JPEG open | 50.49 ± 0.63 | 50.76 ± 0.29 | 1 | 3 → 3 |
| Large TIFF open | 2,040.80 ± 7.36 | 2,122.41 ± 16.83 | 1 | 3 → 3 |
| Core embedded RAW preview | 375.48 ± 2.55 | 417.07 ± 2.04 | 1 | 3 → 3 |
| Core full RAW development | 777.93 ± 3.37 | 825.32 ± 10.17 | 1 | 3 → 3 |
| GTK RAW preview-first through full presentation | 1,262.67 ± 12.31 | 1,373.49 ± 15.53 | 3 | 7 → 7 |
| Multipage EXR open and next page | 104.26 ± 1.63 | 106.99 ± 1.59 | 2 | 7 → 6 |
| Animated GIF, three frames | 149.68 ± 1.73 | 154.62 ± 1.27 | 3 | 11 → 9 |
| BigTIFF open and eight tiles | 2,743.24 ± 9.00 | 2,887.43 ± 19.88 | 9 | 27 → 27 |
| Preload and repeated navigation | 154.90 ± 0.35 | 161.90 ± 1.72 | 3 | 27 → 27 |
| Warm preload hit | 0.083 ± 0.004 | 0.088 ± 0.003 | 0 for hit | 1 → 1 per hit |
| Stream staging and open | 52.61 ± 0.49 | 55.84 ± 1.01 | 1 | 3 → 3 |
| GVfs HTTP inspection and open | 110.45 ± 0.81 | 111.93 ± 1.18 | 2 | 4 → 4 |
| Custom backend inspection and open | 102.14 ± 0.51 | 108.95 ± 2.54 | 2 | 6 → 6 |
| Missing codec plus Glycin fallback | 21.57 ± 0.40 | 22.36 ± 0.76 | 2 | 5 → 5 |
| Stalled worker cancellation workload | 11.33 ± 0.14 | 10.95 ± 0.04 | 1 | 2 → 2 |
| GVfs read cancellation workload | 10,005.70 ± 0.11 | 10,005.68 ± 0.09 | 0 | 2 → 2 |
| GVfs injected short write and failure | 5.75 ± 0.08 | 5.67 ± 0.17 | 0 | 2 → 2 |

The GTK time to the first `PREVIEW_READY` presentation was **444.73 → 496.35 ms**.
The additional EXR page alone was **51.92 → 53.60 ms**,
and the eight region phases totaled **701.31 → 767.21 ms**.
Custom inspection and open still read two headers (1,634 bytes total) while
computing **four → two** stamps. One stream spool transferred **817 bytes**
in each build (0.260 → 0.299 ms). The two GVfs spools
transferred **1,634 bytes** in each build (2.618 → 3.046 ms).
The required explicit GVfs stream closes are included in that timing.

For the PNG, median observed host and worker peak RSS were **8,892 → 9,080 KiB**
and **33,500 → 32,744 KiB** respectively. The large TIFF worker was
**113,776 → 113,904 KiB**. The 20 ms polling interval can miss a short
process. For worker cancellation, the stalled worker signaled readiness from
inside the sandbox. Cancellation-to-cleanup completion was
**0.675 → 0.331 ms**. The stalled HTTP backend held
the read for roughly 10 seconds in both builds. Cancellation cleanup completed
without launching a worker. The GVfs cancellation and short-write samples were
recorded before the diagnostic-drain fix. They never enter the worker process path.

The PNG separated queue admission (0.016 → 0.020 ms),
job directory creation (0.010 → 0.009 ms), request and
Bubblewrap argument preparation (0.135 → 0.120 ms),
synchronous subprocess launch (0.633 → 0.614 ms), and output
read (0.785 → 0.916 ms). Inside the worker, Vips initialization
took 12.41 → 12.34 ms and the operation took
6.42 → 6.60 ms. Worker operation time includes
materializing the decoded output and serialization. Complete sandbox runtime
includes namespace and executable setup and the diagnostic/reaping tail.
Per-RAW worker subphases were unavailable when native diagnostics preceded the
profile records.

The EXR's source-query time was 0.630 → 0.622 ms.
Its complete sandbox runtime was 99.66 → 102.00 ms in the
same comparison. The production worker executable is byte-identical to the
baseline. The profiled worker executables also shared a SHA-256 digest.

A follow-up alternated three-sample blocks with the same worker executable and
fixture paths. Nine samples per build gave medians of **103.96 → 103.04 ms**
for the EXR and **151.72 → 151.54 ms** for the GIF. The PNG and custom
results were **51.73 → 51.68 ms** and **102.15 → 101.49 ms**. These
differences were within the observed median absolute deviations. The stable
improvements were fewer source queries and stamp calculations. The timings
and sampled memory do not establish a broader speedup.

A second control alternated six samples per build for the large fixtures.
Both builds used the same worker binary and the same source paths.
Median RAW preview times were 419.02 → 415.58 ms (MAD 3.69 → 2.13 ms).
Median RAW development times were 831.82 → 815.63 ms (MAD 18.07 → 1.27 ms).
Median large TIFF times were 2,078.59 → 2,075.81 ms (MAD 3.46 → 2.66 ms).
In the main comparison, output reading accounted for about 40 ms of the
42 ms RAW preview difference. The underlying output-read implementation and
worker binary matched across these runs.

## Regression and compatibility

The full baseline and optimized builds registered the same 24 tests, with
identical names, suites, timeouts, result protocols, priorities, and parallel
settings. The optimized full build used production worker settings for the
functional run.

| Suite | Executed tests |
| --- | --- |
| Unit | GDK floating-point texture color state, HDR presentation policy, Job scheduling and priorities, Native image processing references, OpenEXR region decoding, Thumbnail PNG decoding and validation |
| Integration | Decoder selection and composited APNG frames, Desktop thumbnail registration and output, Files public preview workflow, GTK input and accessibility at scale 1, GTK input and accessibility at scale 2, GTK view lifecycle and navigation, GTK viewer readiness and load errors, GtkBuilder view loading, Headless Core API, Photos public full-quality workflow, Viewport tile coverage and cancellation |
| Security | Cross-process thumbnail publication, Native parser fuzz smoke, Worker protocol round trips and validation, Worker timeout, cancellation, crash and response validation |
| Extended | Complete 8K tile coverage, GTK lifecycle stress |
| Interoperability | Real-camera RAW decoding |
| Platform | Wayland view lifecycle, run separately under WSLg |

The targeted cases checked unchanged reads, replacement and missing files
after inspection, invalid directory replacements, replacement while a
sandboxed decode was in progress, and same-size timestamp changes between
page and tile requests. Custom backend tests verified capability filtering,
signature bounds, and registration precedence. Stream cases checked
caller-owned input on success, read error, and cancellation, and tested
private permissions and later page/region reads of a memory-backed source.
The fault worker and existing suite exercised crash, timeout, cancellation,
malformed output, resource exhaustion, missing executable, job cleanup,
scheduler recovery, and sandbox isolation. An early cancellation during
release qualification exposed a diagnostic-drain hang. A new launcher fixture
that deliberately keeps stderr open reproduced the wait on the baseline and
passed with the interruptible drain. GTK suites covered repeated
viewer replacement, cancellation, frame/page navigation, and concurrent
tile rendering.

The GVfs HTTP success, read cancellation, and forced short-write cases
completed nine samples each per build. A separate 1 GiB plus one-byte
response hit the size cap in all three optimized runs. The cancellation,
short-write, and size-limit cases each returned the expected error, removed
the job directory, and launched zero workers. Staging cancellation while a
filesystem write was pending and non-HTTP GVfs transports were not
independently exercised.

| Verification | Result |
| --- | --- |
| Full GTK and optional codecs, including RAW and extended tests | Baseline 24 passed, 0 failed, 0 skipped. Optimized 24 passed, 0 failed, 0 skipped. |
| Headless with Glycin and sequences, `qualify.py --variant headless` | 11 passed, 0 failed, 0 skipped. Core ABI check passed. |
| GTK native-only, Glycin/sequence/Ultra HDR disabled, `qualify.py --variant native` | 21 passed, 0 failed, 0 skipped. Core and GTK ABI checks passed. |
| Wayland lifecycle | 1 passed under WSLg. X11 input ran at scales 1 and 2 in the full build. |
| Strict `-Dwarning_level=3 -Dwerror=true` | Baseline and optimized builds both stopped on existing `native/codec-catalog.c` missing-field initializer warnings. |
| Debian stock and gain-map | Both binary package sets built. Each package build ran 21 tests successfully with the external RAW case skipped in the package build profile. |
| Installed C, Vala, GI, GTK, Photos export, and thumbnail consumers | Passed against extracted stock and gain-map package files at their installed paths in an isolated namespace. The packaged thumbnailer configuration script generated the registration. |
| Full release qualification | Passed all 24 required tests, ABI checks, installed-consumer checks, and all five performance benchmarks. The gain-map package also passed its installed JPEG gain-map round trip. |
| Physical HDR and native touchscreen | Not run. This WSL environment did not provide the hardware. |

The full ABI comparison used an independently built clean Git baseline and
the optimized production build. Both shared libraries had identical SONAMEs
and dependencies, and all **285 Core** and **77 GTK** exported names and
symbol types matched. Generated headers, VAPI, GIR, and typelibs were
byte-identical. A sandboxed PNG fixture produced matching codec catalog,
color probe, probe, decode, and tile outputs. The thumbnail and export PNGs
differed only in generated ICC creation timestamps. The default production
worker binary was byte-identical.

Ruff 0.17.5 lint and formatting, ClangFormat 19 on the changed C fixture, and Git
whitespace checks passed. The available Meson 1.7 formatter reported
pre-existing layout differences in Core and benchmark source lists.
System package manager removal and reinstallation were outside the isolated
installed-consumer run.

## Deferred opportunities

- GTK retains the initial probe. Removing it would change early errors and
  RAW preview eligibility. The full decode must still use its own manifest.
- Custom header sniffing remains per selection. A previous capability request,
  registered backend snapshot, callback result, or observed source identity can
  differ at the next selection.
- GVfs staging remains job-local. Sharing a spool between operations on a
  mutable remote URI would require a distinct validation and ownership design.
  Stream- and memory-backed sources already use one private native spool.
- The scheduler wait, per-job directories, source bind, executable resolution,
  diagnostics, and environment arguments serve independent correctness and
  isolation requirements. The measured preparation cost did not justify an
  argument-template cache or a change in the sandbox lifetime.
- Independent thumbnail cache publication and preload lookups retain their
  queries. Their results outlive the decode and have different validity
  requirements from the consecutive page checks.
