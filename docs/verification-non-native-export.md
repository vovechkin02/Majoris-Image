# Non-native export verification

## A. Backend investigation

**The current GIO compatibility path remains in use.** The WebDAV
investigation found a bounded upload mechanism, but did not establish a
compatible transaction for safely publishing it to arbitrary destinations.
The stream and staging experiments run only in the test executable.

| Backend or path | Client and backend memory bounded | Existing file protected during writing | Incomplete upload discard | Atomic final publication | Remote cleanup | Compatible and eligible |
| --- | --- | --- | --- | --- | --- | --- |
| GVfs WebDAV, direct replacement stream | Core yes; GVfs no | Yes before PUT in this fixture | No; canceled close can publish buffered bytes | Loopback fixture uses a same-directory rename; general server behavior unknown | No client-created staging object in direct mode | No |
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

No production transfer or encoder code changed. The ownership comment in
`src/core/export-transfer.h` now specifies that a successful NULL completes
native publication and a non-NULL result transfers a validated owned
`GBytes` reference. The transfer task opens the completed worker output
safely, bounds it to 268,435,456 bytes, checks the input descriptor and the
complete read, and closes the descriptor before returning the bytes. The
compatibility path allocates one payload of the exact output size.
`g_bytes_new_take()` takes ownership of that allocation without copying it.

The encoder checks cancellation before it starts GIO publication. GIO
retains a reference to the bytes for asynchronous replacement with no
expected ETag, no backup, `FileCreateFlags.NONE`, and the caller's
`Cancellable`. The encoder releases its reference and cleans the worker
job directory and source handle when the operation returns. GIO's
reference covers stream cleanup even if a write error completes the
caller before close finishes.

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
GLib 2.76 source as well. The fallback preserves that upstream behavior.
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

## D. Performance

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

The production path is the measured fallback for both the baseline and
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
reference. The production WebDAV path uses the same fallback after the
investigation as before it.

| Format | Native time (ms) | WebDAV time (ms) | Worker peak native / WebDAV (MiB) | WebDAV daemon peak (MiB) |
| --- | ---: | ---: | ---: | ---: |
| PNG | 288.74 | 209.99 | 68.9 / 69.2 | 18.0 |
| TIFF | 201.78 | 213.01 | 70.6 / 70.3 | 18.7 |
| JPEG XL | 479.61 | 458.77 | 179.2 / 179.3 | 18.7 |
| Ultra HDR | 374.64 | 393.40 | 159.1 / 159.1 | 19.7 |

## E. Regression results

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
