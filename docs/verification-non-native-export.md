# Non-native export verification

## Native sources

Core captures the native source's identity and opens an `O_PATH | O_CLOEXEC`
descriptor before launching the worker. The export coroutine owns the pin
until completion. The worker launcher duplicates it for Bubblewrap's
read-only `/source` binding, so it can encode the selected inode even after
the pathname moves. Core compares the descriptor and original pathname
against the preflight metadata after reading and validating the worker's
output. A detected change fails before GIO opens a replacement stream.

The pre-publication check uses regular-file type, device, inode, size, and
mtime and ctime at the filesystem's available resolution. Concurrent writes
with indistinguishable metadata can escape detection. Source bytes can also
change after the check, including while the backend writes or closes its
stream. A check immediately before close would run after bytes have already
been submitted to some backends. GIO has no universal way to abort or roll
back those writes. Core retains the validated full buffer and awaits close
to preserve compatibility and report the backend's final result.

The optional loopback probe exercises the public encoder with native and
WebDAV sources. It captures descriptor identity and timestamps, worker exit
status, client error details, and the server's request history and stored
output. See [Testing](TESTING.md) for commands.

### Source protection results

| Source | Destination | Native pin and freshness check | Observed result |
|---|---|---|---|
| Native | Native | Yes | Public PNG and TIFF exports and the native mutation regressions passed. |
| Native | WebDAV | Yes | Creation and replacement passed. Four source-change scenarios returned `G_IO_ERROR_BUSY` with zero WebDAV requests. |
| Native | Other non-native `GFile` | Yes | A `resource:///` test double returned a validated payload for unchanged input and an error for changed input. The public API rejected a source change before calling GIO. |
| WebDAV | Native | No | Public PNG export passed using remote staging. |
| WebDAV | WebDAV | No | Public PNG export passed using remote staging and GIO publication. |
| Native | SMB | Same Core path | Live backend untested. |

| Native source or operation | Previous WebDAV path | Updated regression result |
|---|---|---|
| Unchanged | Encoded and published without a pin | Published to new and existing WebDAV targets through the pin. |
| Deleted or renamed | No post-encoding source check | `G_IO_ERROR_BUSY` with a source-path diagnostic. The real worker encoded through the pin after rename. |
| Replaced by another inode | No post-encoding source check | `G_IO_ERROR_BUSY`; the existing WebDAV bytes survived. |
| Modified in place, including a same-size overwrite, growth, or shrinkage | No post-encoding source check | `G_IO_ERROR_BUSY` in transfer tests. WebDAV rejected growth with the sandbox initialized and after encoding. |
| Subsecond mtime changed or mtime restored after a write | No post-encoding source check | `G_IO_ERROR_BUSY` from the mtime or ctime comparison. |
| Descriptor `EIO` or pathname `EACCES` | No post-encoding source check | Source diagnostics and mapped GIO codes survived. |
| Worker failure or cancellation | Worker error or cancellation propagated | Both propagated with zero WebDAV requests and clean job directories. |
| WebDAV rejected upload | Backend error propagated | HTTP 403, HTTP 507, and connection fault cases passed with explicit close. |

The source mutation inside the sandbox lifecycle is synchronized with
Bubblewrap's readiness and block descriptors. The change happens after
sandbox setup, before worker exec. The other mutation happens after real
encoding, before publication. The launcher checks the inherited source
descriptor against host device, inode, size, mtime, and ctime. All four
WebDAV source-change cases confirmed worker success, zero requests during
the attempt, zero remote leftovers, and the previous destination SHA-256.
An equivalent post-worker mutation on the baseline succeeded and issued one
PUT to replace the previous destination.

### Performance comparison

Debian WSL2, GLib 2.84.4, disposable GVfs WebDAV over IPv4 loopback, baseline
`0bbfc72`. Five fresh processes per successful workload. Times are median
wall milliseconds with the observed range. RSS is the median client process
peak reported by `wait4` in KiB.

| Workload | Before ms [range] | After ms [range] | Client RSS before / after |
|---|---:|---:|---:|
| Small PNG, native replacement | 71.4 [70.9, 71.6] | 71.0 [66.2, 71.4] | 28,712 / 28,428 |
| Small PNG, WebDAV replacement | 76.4 [76.3, 76.6] | 71.3 [70.4, 76.6] | 28,712 / 28,428 |
| Small PNG, WebDAV creation | 76.7 [76.3, 77.0] | 75.8 [70.9, 76.5] | 28,712 / 28,428 |
| Large TIFF fixture to PNG, native replacement | 241.9 [227.9, 262.6] | 254.6 [241.6, 279.3] | 30,136 / 30,720 |
| Large TIFF fixture to PNG, WebDAV replacement | 241.5 [228.3, 260.8] | 263.6 [242.0, 327.9] | 30,136 / 30,720 |
| Large TIFF fixture to PNG, WebDAV creation | 235.3 [233.5, 264.9] | 272.3 [239.6, 337.6] | 30,136 / 30,720 |
| Large TIFF fixture to TIFF, WebDAV replacement | 256.1 [230.0, 292.1] | 254.7 [234.0, 322.5] | 30,136 / 30,720 |
| Large TIFF fixture to JPEG XL, WebDAV replacement | 492.7 [491.4, 512.1] | 530.8 [497.7, 569.5] | 30,136 / 30,720 |
| Large TIFF fixture to Ultra HDR, WebDAV replacement | — | 479.4 [475.6, 585.0] | — / 30,720 |
| WebDAV source growth at sandbox readiness | No rejection check | 403.4 [401.6, 406.7] | — / 31,976 |
| WebDAV source growth after encoding | Published in the baseline probe | 403.1 [402.5, 403.2] | — / 31,976 |

Small PNG WebDAV replacement added a median 1,338 µs preflight and removed
the 1,545 µs worker pathname query. The large PNG workload added 245 µs
preflight and removed the 214 µs query. Median source descriptor duplication
took 2 µs for both. Final descriptor and pathname checks took 140 and 134 µs
on the small fixture on the Windows-mounted drive, and 2 and 6 µs on the
large fixture on Linux storage. One source capture query and two final
metadata queries replaced one GIO source query. There was one sandbox launch
and one PUT per successful WebDAV export on both revisions.

Median sandbox launch was 616 → 677 µs for small PNG and 615 → 718 µs
for large PNG. Median aggregate transfer, including publication, was
3,850 → 4,213 µs and 4,530 → 4,511 µs respectively. The summed GIO open,
write, and close phases took 3,703 → 3,648 µs and 4,312 → 4,252 µs.
Subtracting each sample's GIO phases from its aggregate transfer gives a
median of 148 → 460 µs for small PNG and 247 → 271 µs for large PNG. This
includes output validation, source revalidation where applicable, and
asynchronous dispatch. End-to-end timings varied across samples and overlapped the
baseline ranges. These runs do not isolate host scheduling or encoding
variance, and the RSS samples include process startup and the full payload.
The rejection rows use three runs with the sandbox probe launcher. The
WebDAV-source controls took 250.9 ms to native and 250.5 ms to WebDAV
(three-run medians) with three and four requests respectively.

The three-run isolated native transfer medians were 1.065 → 0.995 ms at
1 MiB and 118.656 → 117.699 ms at 256 MiB. Both revisions used a 256 KiB
transfer buffer and reported a median peak RSS of 24,924 KiB. The 16 MiB
WebDAV payload probe confirmed complete readback and one PUT per sample.
Its explicit-close path used a 16 MiB client payload on both creation and
replacement.

### Reproduction on Debian WSL2

The baseline tree was extracted with `git archive 0bbfc72`. Both comparison
builds enabled `-Dworker_profiling=true`. Use the same large TIFF fixture for
both builds. The small run uses `tests/fixtures/rgb8.png` as the source and
`--formats png`. The large baseline uses `--formats png tiff jxl`.

```sh
b=/tmp/imagekit-pin-after
before=/tmp/imagekit-pin-before
headless=/tmp/imagekit-pin-headless
native=/tmp/imagekit-pin-native
san=/tmp/imagekit-pin-san
strict=/tmp/imagekit-pin-strict
release=/tmp/imagekit-pin-release
meson setup "$b" -Dworker_profiling=true
meson setup "$before" /tmp/imagekit-pin-baseline-tree -Dworker_profiling=true
meson compile -C "$b" -j 6
meson compile -C "$before" -j 6
meson test -C "$before" --print-errorlogs --suite unit --suite integration --suite security
meson test -C "$b" --print-errorlogs \
  'Encoded export transfer validation and publication' 'Headless Core API'
python3 -B tests/release/qualify.py "$b" --variant full --jobs 4
meson setup "$headless" -Dgtk=false -Dglycin=disabled
python3 -B tests/release/qualify.py "$headless" --variant headless --jobs 4
meson setup "$native" -Dglycin=disabled -Dsequence=disabled -Dultrahdr=disabled
python3 -B tests/release/qualify.py "$native" --variant native --jobs 3
meson setup "$san" -Dgtk=false -Dglycin=disabled -Db_sanitize=address,undefined
meson test -C "$san" --print-errorlogs 'Encoded export transfer validation and publication'
meson setup "$strict" -Dwarning_level=3 -Dwerror=true
meson compile -C "$strict" -j 3
cc -std=gnu11 -Wall -Wextra -Werror -Wformat=2 -fsyntax-only \
  -I src/core -I "$b/src/core" $(pkg-config --cflags gio-2.0 gee-0.8 glycin-1) \
  src/core/export-transfer.c tests/unit/test-export-transfer.c
python3 -B tests/benchmarks/benchmark_export_transfer.py \
  "$b/tests/benchmarks/benchmark-export-transfer" \
  --previous-executable "$before/tests/benchmarks/benchmark-export-transfer" \
  --samples 3 --output /tmp/imagekit-pin-native-transfer.json
dbus-run-session -- python3 -B tests/benchmarks/benchmark_remote_export.py \
  "$b/tests/benchmarks/benchmark-export-transfer" tests/fixtures/rgb8.png \
  --sizes 1024 --samples 3 --protected-source-cases --formats png tiff jxl uhdr \
  --public-benchmark "$b/tests/benchmarks/majoris-image-benchmark" \
  --worker "$b/src/worker/majoris-image-worker" \
  --fault-worker "$b/tests/security/worker-fault" --source "$b/tests/large.tiff" \
  --output /tmp/imagekit-pin-dav-final.json
dbus-run-session -- python3 -B tests/benchmarks/benchmark_remote_export.py \
  "$b/tests/benchmarks/benchmark-export-transfer" tests/fixtures/rgb8.png \
  --sizes 16777216 --samples 3 --skip-faults --output /tmp/imagekit-pin-transfer-16m.json
```

Use `--samples 5 --skip-faults` for performance-only comparisons, selecting
each revision's built executables. For release qualification, the configured
build uses the manifest-pinned RAW corpus and enabled codecs. These are the
release and Debian package commands.

```sh
meson setup "$release" --prefix=/tmp/imagekit-pin-installed --libdir=lib \
  -Dextended_tests=true -Dglycin=enabled -Dsequence=enabled -Dultrahdr=enabled \
  -Dintrospection=enabled -Draw_fixtures=/tmp/majoris-image-raw.dD1oId
meson compile -C "$release" -j 5
dpkg-buildpackage -us -uc -b -j4
dpkg-buildpackage -us -uc -b -Ppkg.majoris-image.gainmap -j4
python3 -B tests/release/qualify.py "$release" --variant release --jobs 4 --installed
```

The package commands ran in disposable source trees with Git-tracked modes.
The release runner used the arguments above. Only its
`installed_consumers.py` subprocess ran in a Bubblewrap view of the extracted
Debian packages with the packaged worker and thumbnailer bound at their
system paths. The gain-map package view separately ran
`python3 -B tests/release/installed_consumers.py --gainmap`.

The full profile passed 25 tests, headless passed 14, and the Glycin-disabled
GTK profile passed 24, with zero failures or skips in all three. Meson
registered the same 26 tests with the same names, suites, and timeouts on
both comparison builds. The export-transfer suite increased from 79 to
101 cases and passed with ASan and UBSan enabled. The strict C diagnostic
and Ruff checks passed. Both repository-wide strict builds stopped at the
same five existing Vala warnings. The full Core and GTK ABI baselines
matched 285 and 77 symbols. Both GIR and VAPI files matched across the
comparison builds. The headless and Glycin-disabled profiles matched
the 280-symbol Core baseline.

Full release qualification passed 28 functional tests, including all 25
mandatory tests, and 35 performance benchmarks, with zero failures or
skips. Both Debian package profiles built successfully. Each package build
passed 25 tests and skipped the external RAW corpus check. Release
qualification ran the RAW corpus check successfully. The extracted package
views passed installed Core, GTK, GtkBuilder, introspection, Photos export,
thumbnail, and gain-map consumers.

The updated WebDAV run passed 44 transport fault cases, five deterministic
stream faults, 22 public publication cases, and three repetitions of ten
protected-source and compatibility cases. The connection fault run emitted
GLib warnings and critical diagnostics on both revisions. The assertions
for client status, server state, and cleanup passed on both revisions.

## A. Backend investigation

**Core keeps the validated full-buffer compatibility path and now checks
explicit stream close.** The WebDAV
investigation found a bounded upload mechanism, but did not establish a
compatible transaction for safely publishing it to arbitrary destinations.
The chunked and staged variants run in the benchmark executable.

| Backend or path | Client and backend memory bounded | Existing file protected during writing | Incomplete upload discard | Atomic final publication | Remote cleanup | Compatible and eligible |
| --- | --- | --- | --- | --- | --- | --- |
| GVfs WebDAV, chunked direct replacement probe | Core yes; GVfs no | Yes before PUT in this fixture | No; canceled close can publish buffered bytes | Loopback fixture uses a same-directory rename; general server behavior unknown | No client-created staging object in direct mode | No |
| GVfs WebDAV, stream to sibling and MOVE | Core yes; GVfs no | Yes in this fixture until MOVE | Sibling can be deleted when permitted | Same-directory rename on this fixture | Limited; DELETE can fail | Different operations and permissions; no |
| GVfs WebDAV, local file copy (GVfs push) to sibling and MOVE | Bounded upload observed in the GVfs push path | Yes in this fixture until MOVE | Sibling can be deleted when permitted | Same-directory rename on this fixture | Limited; DELETE can fail | Different operations, pathname reopen, and permissions; no |
| GVfs SMB | Not measured | Unknown in deployment; the source includes an in-place truncate fallback | Unknown | Source includes unlink and rename as separate operations | Unknown | No |
| Other or unknown GIO implementations | Untested | Unknown | Unknown | Unknown | Unknown | No |

The loopback runs used the real GVfs client and `gvfsd-dav` on D-Bus. The
server was a disposable HTTP fixture. Its PUT reads a body into server-local
scratch and renames only after the full request body is received. Its MOVE
uses a same-directory rename. These choices describe this server, including
its visibility and failure behavior. DAV offers no capability here that would
let Core infer those properties for a destination from the URI or mount.
[WebDAV MOVE specification](https://www.rfc-editor.org/rfc/rfc4918.html#section-9.9)

- GIO `replace_async()` delegates to the backend. Generic `write_all_async()`
  handles short writes. `splice_async()` and flush can transfer bytes or
  drain buffers, but provide no abort or rollback contract. Close can flush
  outstanding writes, and GIO may close a stream on final unref. GVfs
  1.57.2's daemon-backed `GFile` has no read/write replacement vfunc.
  [GIO replacement](https://docs.gtk.org/gio/method.File.replace.html)
  [GIO close](https://docs.gtk.org/gio/method.OutputStream.close.html)
  [GVfs GFile implementation](https://github.com/GNOME/gvfs/blob/1.57.2/client/gdaemonfile.c)
- WebDAV stream creation and replacement allocate a resizable memory stream
  in the daemon. Writes accumulate there. The daemon builds its PUT body from
  that buffer at close. A direct stream cannot safely recover if validation
  discovers a truncated, unreadable, or modified worker output after a write.
  [GVfs WebDAV write and close implementation](https://github.com/GNOME/gvfs/blob/1.57.2/daemon/gvfsbackenddav.c#L2721)
- The WebDAV backend's local-file push can send a GIO input stream as the
  HTTP request body. The backend opens that file by pathname. Calling it with
  the worker-output path would discard the descriptor-based validation
  guarantee. It also implements a copy rather than ordinary GIO replacement.
  A separate protected snapshot could address input ownership, but would
  leave promotion, metadata, and cleanup guarantees unresolved.
  [GVfs WebDAV push implementation](https://github.com/GNOME/gvfs/blob/1.57.2/daemon/gvfsbackenddav.c#L3740)
- The staged probes require `NO_FALLBACK_FOR_MOVE`. The server logged DAV
  MOVE rather than an upload to the final URI. Staging adds a second remotely
  visible name and requires permission to create it, promote it, and delete
  it. GIO's PRIVATE flag depends on filesystem support. A failed DELETE can
  leave a complete sibling behind.
  [GIO move](https://docs.gtk.org/gio/method.File.move.html)
  [GIO replacement flags](https://docs.gtk.org/gio/method.File.replace.html)
- SMB source review found local temporary-handle and in-place truncation
  branches. With a temporary handle and no backup, close may unlink the old
  destination before renaming. The installed SMB client backend had no
  disposable SMB server to test.
  [GVfs SMB implementation](https://github.com/GNOME/gvfs/blob/1.57.2/daemon/gvfsbackendsmb.c#L1070)

## B. Implementation and ownership

The transfer contract in `src/core/export-transfer.h` specifies that a
successful NULL completes
native publication and a non-NULL result transfers a validated owned
`GBytes` reference. The transfer task opens the completed worker output
safely, bounds it to 268,435,456 bytes, checks the input descriptor and the
complete read, and closes the descriptor before returning the bytes. The
compatibility path allocates one payload of the exact output size.
`g_bytes_new_take()` takes ownership of that allocation without copying it.

The encoder checks cancellation before opening a replacement stream with no
expected ETag, no backup, and `FileCreateFlags.NONE`. The publication coroutine
holds the bytes and lends their backing storage to `write_all_async()`. It
awaits `close_async()` with the same caller cancellable after a complete write.
The encoder cleans the worker job directory and releases the source handle
after publication returns. Native publication still completes inside the
transfer task.

After a write error, the coroutine awaits one cleanup close with an already
canceled token and preserves the original error. GLib's stream disposal can
close an open stream, and GIO offers no generic abort. Even a canceled close
can publish buffered bytes, so cleanup does not establish rollback. Core
releases the stream after close completes and reports secondary cleanup
errors at debug level.

The other source changes are confined to tests and benchmarks.

- `tests/unit/test-export-transfer.c` now verifies small, exact-chunk, and
  multi-chunk compatibility payloads with short reads. It also checks that
  invalid worker outputs, truncation, growth, same-size modification, I/O
  error, and cancellation return errors without a publishable payload.
- `tests/benchmarks/benchmark-export-transfer.vala` probes fallback,
  direct 256 KiB writes, staged 256 KiB writes, and file push with staged
  promotion. It records the results of GIO calls and accepts a test signal to
  cancel while the server holds a request.
- `tests/benchmarks/benchmark-export-bytes.c` and
  `tests/benchmarks/meson.build` time the benchmark's linked
  `g_bytes_new_take()` call. The wrapper only links into the benchmark.
- `tests/benchmarks/benchmark.vala` accepts an optional destination URI for
  public encoder workloads, including Ultra HDR when the codec is enabled.
- `tests/benchmarks/benchmark_remote_export.py` starts the loopback server,
  injects failures, checks SHA-256 by readback, and records client, backend,
  and worker data. It checks independently encoded image pixels and
  separately republishes the same encoded artifact to compare exact bytes.

## C. Replacement and cancellation guarantees

The fallback validates the entire local output before calling GIO. On this
WebDAV backend, the PUT begins on close. The loopback server keeps the old
contents until it consumes the complete body and renames its scratch file.
A successful server response and byte-for-byte readback establish that
publication completed in the success cases. A client cancellation arriving
before the server response is distinct from a cancellation delivered after
the caller already received success. The former can coincide with publication.

The tested GLib 2.84.4 asynchronous bytes helper discards the output-stream
close error. Its result can therefore be successful when a DAV PUT failed
at close. This limitation is present in the project's supported minimum
GLib 2.76 source as well. The current encoder observes the close result and
preserves the underlying error. A successful close is the completion boundary.
Transport errors and cancellation can leave the server outcome indeterminate.
The client makes one replacement attempt and leaves the destination for the
backend to resolve.
The probe checks the server status and retrieved bytes independently of the
client's return. [GLib 2.84.4 bytes replacement](https://github.com/GNOME/glib/blob/2.84.4/gio/gfile.c#L8117)
[GLib 2.76 bytes replacement](https://github.com/GNOME/glib/blob/2.76.0/gio/gfile.c#L7769)

The production fallback does not impose a metadata preservation policy on
remote filesystems or require an ETag check. DAV can pass an ETag as
`If-Match` for replacements, but this export supplies no expected ETag.
Successful preservation of encoded image metadata follows from exact byte
readback of the same artifact. Remote filesystem ownership, permissions,
ETag evolution, and server-side resource properties are separate and were
not verified across servers.

## D. Historical streaming investigation performance

Baseline revision `dd3f5f9ace61203708e7dde02887182ca7e18f33`.
Debian 13.5 on Linux 6.18.58.1 WSL2 x86-64, GCC 14.2.0, Meson 1.7.0,
GLib/GIO 2.84.4, GVfs 1.57.2-2+deb13u1, libvips 8.16.1.
The Meson release build enabled worker profiling, GTK, Glycin, timed
AVIF and HEIF sequences, and Ultra HDR. Fixtures and server storage used
`/tmp` on tmpfs, with HTTP over IPv4 loopback. Each table row uses three
fresh client processes per mode and a fresh mount per payload size, with
the mount reused across samples and warmed fixture pages. The intervals
exclude fixture setup, mounting, and readback.

### Completed-output publication

At the time of this investigation, the production path was the bytes helper
for both the baseline and
this revision. The direct columns represent the experimental stream.
Median times and ranges are in milliseconds. RSS values are in MiB.

| Payload and operation | Fallback time (range) | Fallback Core peak | Direct probe time | Direct probe Core peak |
| --- | ---: | ---: | ---: | ---: |
| 1 KiB create | 3.73 (3.51–4.06) | 27.9 | 3.73 | 27.9 |
| 1 KiB replace | 3.52 (3.49–3.82) | 27.9 | 3.57 | 27.9 |
| 1 MiB create | 6.19 (5.93–6.25) | 28.8 | 5.89 | 28.8 |
| 1 MiB replace | 5.99 (5.77–6.68) | 28.8 | 6.04 | 28.8 |
| 16 MiB create | 56.05 (53.09–56.62) | 28.8 | 59.00 | 28.8 |
| 16 MiB replace | 54.86 (54.47–56.23) | 28.9 | 57.07 | 28.9 |
| 64 MiB create | 195.18 (193.85–206.70) | 71.9 | 218.86 | 28.8 |
| 64 MiB replace | 203.23 (198.40–216.21) | 72.0 | 210.00 | 28.8 |
| 128 MiB create | 406.93 (399.49–410.42) | 135.9 | 453.29 | 28.7 |
| 128 MiB replace | 425.64 (422.09–480.15) | 135.9 | 439.05 | 28.8 |
| 256 MiB create | 819.62 (723.04–887.85) | 263.9 | 880.64 | 28.6 |
| 256 MiB replace | 924.18 (879.40–1,030.50) | 263.9 | 888.41 | 29.2 |

`wait4` supplies the client peak and CPU time. For small workloads its peak
can include launcher overhead. The fallback validated the maximum-size
replacement in a median 139.84 ms, including payload allocation and the
2 µs median `GBytes` construction. GIO replacement took 784.33 ms.
Client CPU time was 205.78 ms and throughput was 277 MiB/s. The unchanged
fallback retains the exact 268,435,456-byte allocation at the maximum.
No client-side transfer scratch was observed. At 256 MiB the loopback server
held up to one full-sized scratch payload plus the existing 15-byte target.
Each staged success used one named remote sibling and left zero.

| 256 MiB replacement | Median time (ms) | Median client CPU (ms) | Core peak (MiB) | Sampled daemon peak (MiB) | Throughput (MiB/s) |
| --- | ---: | ---: | ---: | ---: | ---: |
| GIO fallback | 924.18 | 205.78 | 263.9 | 3,353.9 | 277.0 |
| Direct GIO stream | 888.41 | 191.58 | 29.2 | 3,609.9 | 288.2 |
| GIO stream and MOVE | 948.53 | 193.40 | 29.2 | 3,865.9 | 269.9 |
| Local file push and MOVE | 2,947.76 | 7.42 | 29.2 | 3,865.9 | 86.8 |

Daemon RSS is sampled separately from Core and includes retained memory
from earlier samples in the mount. The equal absolute peaks for the staged
stream and file push reflect the high starting RSS for push. Changes from
the per-sample idle RSS distinguish upload behavior. At 256 MiB replacement,
the median increase was about 256 MiB for each of the fallback, direct stream,
and staged stream, and 0.0 MiB at the sampling resolution for file push.
No worker runs during these measurements.

### EncoderRegistry end to end

One native and one WebDAV replacement of a 2500 × 1250 crop of the
generated large TIFF. Times cover `EncoderRegistry.encode()`, including
worker launch, encoding, publication, and cleanup. Core peaks are
29.4 MiB for both destinations in all rows. Worker and daemon peaks are
sampled independently. These native results provide an end-to-end
reference. The historical WebDAV path used the bytes helper after the
investigation as before it.

| Format | Native time (ms) | WebDAV time (ms) | Worker peak native / WebDAV (MiB) | WebDAV daemon peak (MiB) |
| --- | ---: | ---: | ---: | ---: |
| PNG | 288.74 | 209.99 | 68.9 / 69.2 | 18.0 |
| TIFF | 201.78 | 213.01 | 70.6 / 70.3 | 18.7 |
| JPEG XL | 479.61 | 458.77 | 179.2 / 179.3 | 18.7 |
| Ultra HDR | 374.64 | 393.40 | 159.1 / 159.1 | 19.7 |

## E. Historical streaming investigation results

The transfer matrix completed 144 successful WebDAV publications across
creation and replacement. Each sample read back the expected size and SHA-256
from the server and found zero leftover remote siblings. The public API
round trips reopened PNG, TIFF, JPEG XL, and Ultra HDR, checked loader,
dimensions, band count, sample precision, and decoded pixel hashes.
ICC profile presence and lengths matched for each native/remote pair, as
did the selected metadata fields. The Ultra HDR comparison decoded the 8-bit
JPEG base layer. Independent PNG, TIFF, and JPEG XL encodes sometimes produced
different container hashes. Publishing the same encoded artifact through the
compatibility transfer produced an exact hash match for all four formats,
including their encoded metadata and profiles. The worker job directories
were removed. An additional public PNG export to a read-only `resource:///`
destination surfaced the GIO “Operation not supported” message and removed
its worker job directory.

| WebDAV fault | Observed destination and client result |
| --- | --- |
| Pre-canceled fallback | Canceled without a PUT. No new file appeared, and old contents survived. |
| Direct stream canceled after first chunk, midway, or before close | Client returned canceled. Server stored 256 KiB, 8 MiB, or the complete 16 MiB respectively. This applied to both creation and replacement. |
| Fallback and explicit stream canceled during close before server commit | Server rejected the held PUT and preserved the previous state. Cancellation to result was about 100–106 ms with a deliberate 100 ms response hold. |
| Fallback canceled after server commit, with response held | Client returned canceled after about 105 ms. Readback showed the complete replacement in both cases. |
| Fallback canceled after acknowledgement | Client success and the complete output were preserved. |
| Abrupt connection drop | The GVfs WebDAV daemon exited and the probe remounted. Fallback returned success with the previous state intact. Explicit close reported a stream-protocol error. |
| PUT rejected with HTTP 403 or 507 | The fallback returned success after the server rejected the close-time PUT. Old contents survived and no new file appeared. Explicit close propagated the 403 as a GIO permission error. |
| Connection dropped after successful PUT, before response | The fallback returned success. Readback proved that the full new bytes were present. The daemon exited and the probe remounted. |
| MOVE denied or canceled before promotion | The previous state survived. DELETE removed the sibling when allowed. The suspended MOVE cases returned canceled after about 3–4 ms, including cleanup. |
| MOVE and cleanup denied | The previous state survived. Both staged methods left their complete sibling. The probe kept the MOVE error and recorded the DELETE error. |

Focused and qualification commands (build directories shown as variables)

```sh
meson test -C "$standard" --print-errorlogs 'Encoded export transfer validation and publication'
python3 -B tests/release/qualify.py "$standard" --variant full
python3 -B tests/release/qualify.py "$headless" --variant headless
python3 -B tests/release/qualify.py "$native" --variant native
python3 -B tests/release/qualify.py "$release" --installed
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  meson test -C "$sanitizers" --print-errorlogs 'Encoded export transfer validation and publication'
cc -std=gnu11 -g -Wall -Wextra -Wpedantic -Werror -fsyntax-only \
  $(pkg-config --cflags gio-2.0) -I"$standard/src/core" -Isrc/core \
  src/core/export-transfer.c tests/unit/test-export-transfer.c \
  tests/benchmarks/benchmark-export-bytes.c
dpkg-buildpackage -us -uc -b -j6
dpkg-buildpackage -us -uc -b -Ppkg.majoris-image.gainmap -j4
python3 -B tests/release/installed_consumers.py "$standard/tests/large.tiff"
python3 -B tests/release/installed_consumers.py --gainmap
```

The full suite had 25 passed, 0 failed, and 0 skipped, including all
22 mandatory tests, with 285 Core and 77 GTK ABI exports verified.
The headless suite had 14 passed, 0 failed, and 0 skipped, including
12 mandatory tests, with 280 Core ABI exports verified without Glycin.
The Glycin-disabled full GTK profile had 24 passed, 0 failed, and 0 skipped,
including 22 mandatory tests, with 280 Core and 77 GTK ABI exports verified.
The focused export-transfer suite passed all 79 cases, including 12 new
non-native cases. The ASan/UBSan command and the strict C diagnostic passed.
The repository-wide `-Dwarning_level=3 -Dwerror=true` build stopped on
existing Vala warnings for an unhandled error in `decoder-registry.vala`
and unused methods in `image-probe.vala`, `worker-request.vala`, and
`worker-failure.vala`.

Both Debian profiles built the five ABI 2 packages. The installed
headless, Vala GTK, C GtkBuilder, introspection, Photos, and thumbnail
consumers passed in isolated views of the extracted package trees. The
thumbnail descriptor was generated using the package registration
executable and its system arguments. The vendor worker also exported and
decoded an 8 × 8 JPEG gain map.

The full release profile passed 28 tests, including 25 mandatory tests,
and 35 performance benchmarks. Both stages reported zero failures and
zero skips. The qualification included ABI 2, worker protocol, complete
8K tile coverage, GTK lifecycle stress, and manifest-pinned real-camera RAW
decoding. It ran on the host, with only its installed-consumer step routed
through the extracted gain-map package view. An initial attempt to run
the whole qualification in that view failed the supplementary-group test
because the isolation changed the group mapping. The same test passed
when rerun on the host.

The [WebDAV runner commands](TESTING.md) reproduce the transfer, fault, and
public API checks. The six measured sizes used `--samples 3 --skip-faults`
with one size per session. The fault and public API run used
`--samples 1 --sizes 1024 1048576 --formats png tiff jxl uhdr`, with
the public benchmark, worker, and generated large TIFF paths. All 36
fault assertions completed. The assertions check remote bytes and capture
the upstream caller result, including the false-success results above.

## F. Deferred backend limitations

There is no enabled streaming backend. SMB and additional backends lack a
disposable server verification in this run. Server filesystem metadata,
authentication expiration, races with competing remote writers, and a full
matrix of server faults were unverified. The network faults exercise HTTP
publication and promotion. They do not inject failures into the daemon's
first write, later writes, or flush while it is filling its memory buffer.
More specific GIO failures are
observable with explicit stream close but the tested direct stream can
publish a partial image after cancellation. A future staged path needs
equivalent publication permissions and metadata, reliable staging cleanup,
an abortable finalization contract, and a protected verified input handle.

## G. Explicit-close verification

The pre-change WebDAV probe and the corrected public PNG export ran on Debian
13 with GLib 2.84.4 and GVfs over a private D-Bus session. Each fault was
tested with a new and an existing destination. “Previous state” means an
existing destination retained its bytes, or a new destination stayed absent.

| Scenario | Previous client | Explicit-close client | Fixture outcome |
| --- | --- | --- | --- |
| Successful PUT | Success | Success | Complete output |
| HTTP 403 | False success | Permission denied | Previous state |
| HTTP 507 | False success | Insufficient Storage error | Previous state |
| Connection dropped before commit | False success | Stream protocol error | Previous state |
| Connection dropped after commit | False success | Stream protocol error | Complete output |
| Cancellation before upload | Canceled | Canceled | Previous state, zero PUTs |
| Cancellation while writing | Canceled in direct-stream probe | Canceled in deterministic stream probe | Direct WebDAV close can still publish a prefix |
| Cancellation while close was pending | Canceled | Canceled | Previous state after the fixture rejected the PUT |
| Cancellation after server commit | Canceled | Canceled | Complete output |
| Cancellation after client success | Success | Success | Complete output |

The connection-loss tests also caused the GVfs daemon to exit on this host.
The probe remounted between samples and sampled server state independently.
A backend error alone cannot establish which connection-loss outcome occurred.

| Fault | Domain and code | Message |
| --- | --- | --- |
| 403 | `g-io-error-quark:14` | HTTP Client Error: Forbidden |
| 507 | `g-io-error-quark:0` | HTTP Error: Insufficient Storage |
| 500 | `g-io-error-quark:0` | HTTP Error: Internal Server Error |
| Authentication rejected on PUT | `g-io-error-quark:14` | HTTP Client Error: Unauthorized |
| Missing parent rejected on PUT | `g-io-error-quark:0` | HTTP Error: Conflict |
| Unsupported destination | `g-io-error-quark:15` | Operation not supported |
| Injected replacement open rejection | `g-io-error-quark:14` | Injected replacement open failure |
| Injected first or later write rejection | `g-io-error-quark:12` | Injected write failure |
| Disconnect and lost response | `g-io-error-quark:0` | Error in stream protocol: End of stream |
| Caller cancellation | `g-io-error-quark:19` | Operation was cancelled |
| Missing worker output after successful worker exit | `image-kit-image-error-quark:11` | Decoder did not produce an output file |

These are observed mappings on this host. The WebDAV regression checks failure
independently of the numeric code. The benchmark also links a disposable
stream double to the same private publication helper. It can reject open,
return short writes, fail the first or a later write, and cancel during writing.
The close handler checks the cleanup token and injects a second failure so the
test can verify that the original write error survives.

The final run completed 44 WebDAV probe cases, five deterministic stream
cases, and 22 fault-matrix calls through the public encoder. It also ran four
public format comparisons. GVfs deferred the authentication and missing
parent responses to PUT in this fixture. The synthetic open failure checks
the earlier replacement error path independently.

### Explicit-close performance

One sample per mode and destination on Debian 13. Times are milliseconds and
RSS values are MiB. Total time includes validation. The old GLib helper only
exposes its aggregate replacement time. Open, write, and close timings are
for explicit publication.

| Payload | Destination | Total before / after | Core peak RSS before / after | GVfs sampled peak before / after | Open / write / close |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 MiB | New | 6.39 / 6.19 | 29.1 / 29.1 | 19.0 / 21.0 | 2.50 / 1.06 / 1.81 |
| 1 MiB | Existing | 6.10 / 6.02 | 29.2 / 29.2 | 25.2 / 26.2 | 2.58 / 0.72 / 1.89 |
| 16 MiB | New | 55.83 / 55.36 | 29.7 / 29.7 | 37.9 / 53.9 | 2.72 / 12.66 / 31.34 |
| 16 MiB | Existing | 57.32 / 54.69 | 29.7 / 29.7 | 102.2 / 118.2 | 2.82 / 11.86 / 31.39 |
| 64 MiB | New | 218.35 / 213.82 | 71.9 / 71.9 | 85.9 / 154.0 | 3.17 / 40.65 / 142.96 |
| 64 MiB | Existing | 206.57 / 212.83 | 71.9 / 71.9 | 346.1 / 410.1 | 2.80 / 38.52 / 142.60 |
| 128 MiB | New | 417.34 / 417.18 | 136.1 / 135.9 | 149.9 / 277.9 | 2.75 / 68.50 / 288.96 |
| 128 MiB | Existing | 409.90 / 375.17 | 136.0 / 135.9 | 662.2 / 790.2 | 3.08 / 103.41 / 213.54 |
| 256 MiB | New | 876.89 / 840.19 | 264.1 / 264.0 | 277.9 / 537.9 | 3.38 / 130.45 / 596.55 |
| 256 MiB | Existing | 725.19 / 711.17 | 263.9 / 263.9 | 1302.2 / 1558.2 | 2.84 / 128.57 / 475.69 |

GVfs peak figures include allocator retention across modes on the same mount.
They cannot isolate the memory cost of an individual publication. The Core
peak and the allocation probe show one full-sized payload at the 256 MiB
limit. Exact size and SHA-256 were verified by readback on success.

These 16 MiB fault samples used a new destination. Wall time includes process
startup and any fixture response hold. All timings are milliseconds.

| Fault | Wall time before / after | Explicit open / write / close |
| --- | ---: | ---: |
| HTTP 403 | 86.11 / 74.75 | 2.80 / 12.66 / 41.22 |
| HTTP 507 | 80.89 / 75.10 | 2.60 / 13.02 / 38.32 |
| Disconnect before commit | 42.27 / 47.68 | 2.83 / 12.62 / 15.28 |
| Lost response after commit | 79.70 / 85.03 | 2.53 / 11.47 / 54.95 |
| Cancellation before commit | 142.45 / 142.63 | 2.58 / 14.47 / 105.65 |
| Cancellation after commit | 173.70 / 180.35 | 3.01 / 12.72 / 143.80 |

| Public worker export | Total before / after | Core peak RSS before / after | Open / write / close |
| --- | ---: | ---: | ---: |
| PNG | 208.53 / 249.84 | 28.4 / 30.2 | 2.81 / 0.30 / 1.10 |
| TIFF | 209.77 / 220.26 | 28.4 / 30.2 | 2.75 / 0.21 / 1.47 |
| JPEG XL | 458.36 / 663.84 | 28.4 / 30.2 | 2.74 / 0.13 / 1.40 |
| Ultra HDR | 356.68 / 426.30 | 28.9 / 30.2 | 2.97 / 1.32 / 1.74 |

The real exports used the same 2500 × 1250 crop. Native and WebDAV outputs
matched decoded dimensions, sample format, pixel SHA-256, and inspected
metadata fields. Libvips reads the 8-bit base image in Ultra HDR. The other
three formats decoded to 16-bit unsigned samples. Republishing the same
encoded artifact independently verified exact file bytes. The public
before-and-after measurements came from separate runs. Total latency
includes worker execution and varies with host load.

### Regression commands

```sh
profiles=/tmp/majoris-remote.qoIImS
b="$profiles/build"
meson compile -C "$b" -j 4
dbus-run-session -- python3 -B tests/benchmarks/benchmark_remote_export.py \
  "$b/tests/benchmarks/benchmark-export-transfer" tests/fixtures/rgb8.png \
  --sizes 1024 1048576 16777216 67108864 134217728 268435456 --samples 1 \
  --public-benchmark "$b/tests/benchmarks/majoris-image-benchmark" \
  --worker "$b/src/worker/majoris-image-worker" \
  --fault-worker "$b/tests/security/worker-fault" --source "$b/tests/large.tiff" \
  --formats png tiff jxl uhdr --output /tmp/majoris-close-final.json
python3 tests/release/qualify.py "$profiles/headless" --variant headless --jobs 3
python3 tests/release/qualify.py "$profiles/native" --variant native --jobs 3
python3 tests/release/qualify.py "$profiles/release" --variant release --jobs 5 --installed
meson compile -C "$profiles/strict" -j 2
meson compile -C "$profiles/sanitizers" -j 3
meson test -C "$profiles/sanitizers" --suite unit --print-errorlogs
meson test -C "$profiles/sanitizers" \
  'Encoded export transfer validation and publication' --print-errorlogs
cc -std=gnu11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only \
  $(pkg-config --cflags gio-2.0) tests/benchmarks/benchmark-export-bytes.c
DEB_BUILD_OPTIONS="parallel=4 noclean" dpkg-buildpackage -us -uc -b
```

Headless qualification passed 14 tests and verified 280 Core exports.
Glycin-disabled GTK qualification passed 24 tests and verified 280 Core and
77 GTK exports. Both profiles had zero failures or skips. Core and GTK
GIR and VAPI files matched the pre-change artifacts byte for byte.

The focused export-transfer regression and all five deterministic GIO stream
cases passed under ASan/UBSan. In the full sanitized unit run, six tests passed
and the separate job scheduler test reported a 40-byte LeakSanitizer finding
from `g_rec_mutex_lock()`. The strict C fixture compile passed. The repository
strict build stopped on existing Vala warnings in decoder and worker sources.
The generated C for the Vala `Bytes.get_data()` binding also emits a
const-qualification warning for the borrowed payload.

The standard Debian profile built the binary packages in a disposable source
copy with Git-tracked file modes. Release qualification passed all 25 required
tests. Its functional suite passed 28 tests and its performance suite passed
35 benchmarks, both with zero failures or skips. It verified 285 Core and 77
GTK exports.
The installed headless, Vala GTK, C GtkBuilder, introspection, Photos export,
and thumbnail checks passed in an isolated view of the extracted packages.
SMB, other live remote backends, and a Wayland compositor were outside this
run. The validated full-buffer path continues to support arbitrary GIO
destinations. Remote streaming still needs a compatible validation and
publication contract that addresses partial writes across backends.
