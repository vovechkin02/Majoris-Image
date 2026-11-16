# Bounded export transfer verification

## A. Implementation

The baseline was revision `0b6f2b4`. Core checked the worker output's pathname
with `NOFOLLOW_SYMLINKS`, checked that it was a regular file of 1 to
268,435,456 bytes, and loaded the whole file before calling
`replace_contents_bytes_async()`. A missing output or invalid initial type
or size produced `ImageError.INVALID_RESPONSE`. Source and destination
`GFile.equal()` produced `IOError.INVALID_ARGUMENT`. Source read and GIO
replacement errors propagated to the caller. The caller's cancellable
covered worker execution, reading, and replacement.

The worker still accepts `jpg`, `jpeg`, `png`, `tif`, `tiff`, `webp`,
`avif`, `heif`, `heic`, `jxl`, `exr`, and `uhdr` subject to the existing
codec and option checks. The output suffix matches the format, with `uhdr`
producing `.jpg`. The existing source checks, worker protocol, and
Bubblewrap invocation are used for every format.

| Changed file | Purpose |
|---|---|
| `src/core/encoder-registry.vala` | Calls the private transfer and retains GIO replacement for non-native destinations and worker cleanup in `finally`. |
| `src/core/export-transfer.c` and `src/core/export-transfer.h` | Validate and own the source handle, stream and commit native output, and supply the verified payload for other backends. |
| `src/core/meson.build` | Compiles the helper into Core with hidden visibility. |
| `tests/unit/test-export-transfer.c` and `tests/unit/meson.build` | Exercise the private transfer with deterministic payloads and injected I/O faults. |
| `tests/integration/test-core-api.vala` | Checks identical-source rejection and reopens a TIFF that replaced a PNG, checking dimensions, depth, and representative pixels. |
| `tests/release/qualify.py` | Requires the transfer regression test during qualification. |
| `tests/benchmarks/benchmark-export-transfer.vala` and `tests/benchmarks/benchmark_export_transfer.py` | Compare the original and bounded transfer with generated fixtures, hashes, and cancellation measurements. |
| `tests/benchmarks/benchmark.vala`, `tests/benchmarks/run_benchmarks.py`, and `tests/benchmarks/meson.build` | Register isolated and full-export benchmarks and report output sizes, hashes, and peak RSS. |
| `docs/api-2.md` and `docs/verification-export-transfer.md` | Document publication guarantees, compatibility, and verification. |

`WorkerClient` owns the private job while it runs. After a successful
worker exit, `EncoderRegistry` owns the returned job and encoded artifact
until the asynchronous transfer finishes. The transfer task owns its
source descriptor, one buffer of up to 256 KiB on the native path, and
any staging descriptors and files. It closes them before finishing.
Staging cleanup is independent of the canceled operation. A primary
transfer error survives cleanup, while an otherwise successful copy
still fails if its final sync or close fails. After successful
publication, ownership of the destination remains with the caller.

## B. Trust and atomicity

Core opens the worker job directory with `O_NOFOLLOW`, opens the final
component with `O_PATH | O_NOFOLLOW`, and checks its type and size with
`fstat()`. This classifies symlinks, FIFOs, and devices before opening
for I/O. Linux procfs reopens the validated inode for reading. A swap
of the worker pathname cannot redirect the read. The transfer stops at
the initial size and probes for EOF with a single extra byte. It rejects
unexpected EOF, growth, or changes to size, mtime, or ctime on the open
descriptor. This detects changes observable through the file and its
metadata. It is a file integrity check for the completed worker output.

The original GIO replacement requested no expected ETag, no backup,
and `FileCreateFlags.NONE`. On local files, GIO can create a new
destination up front or overwrite an existing inode in place when
staging is unavailable or the target is linked. The native path now
stages on the destination filesystem, finalizes permissions, syncs and
closes the staged file, and checks cancellation immediately before
`renameat()`. The rename is the commit boundary. Once it succeeds,
the export succeeds even if cancellation arrives before the callback.
The final filename is visible atomically on filesystems that provide
atomic rename. [GLib 2.84.4 local replacement implementation](https://raw.githubusercontent.com/GNOME/glib/2.84.4/gio/glocalfileoutputstream.c)

Native destination symlinks retain their referent behavior, including
creation through a dangling link. Existing regular destinations retain
owner, group, and ordinary permission bits when replaced. New files
respect the process umask. Other hard links retain their old inode.
Native destinations require directory write permission and the ability
to stage with appropriate ownership. These are deliberate consequences
of atomic publication where GLib previously could overwrite in place.

For non-native destinations, the compatibility path uses one validated
allocation of up to 256 MiB and calls the original asynchronous GIO
replacement. Backend publication, failure recovery, and cancellation
semantics apply once that operation begins. A canceled backend commit
may have reached the destination. Loopback GVfs WebDAV was exercised
through its GIO backend for creation and overwrite. The server handled
both as PUT operations, and both files matched the corresponding native
PNG. This test establishes compatibility with that backend.

## C. Correctness

The focused test suite passed all 24 cases.

| Case | Observed result |
|---|---|
| 1 KiB, exactly 256 KiB, and multiple chunks plus 17 bytes | Complete output matched the deterministic SHA-256. The exact-chunk overwrite preserved mode `0640` while handling forced short reads and writes. |
| Exact 256 MiB | Published the full payload with the expected SHA-256. |
| 256 MiB plus one byte, missing, empty, symlink, and FIFO | Returned `INVALID_RESPONSE` with no publication. |
| Truncation, growth past the limit, and substitution of the source name with a symlink during copy | Returned `INVALID_RESPONSE`. Existing output was preserved where present. An incomplete new export was absent. |
| Read and write errors after the first chunk | Returned the respective I/O errors and preserved the old destination. |
| Sync, close, and rename errors | Returned errors and preserved the old destination. |
| Cancellation during transfer or before commit | Returned `CANCELLED`, with no incomplete new destination and the existing destination preserved in the pre-commit case. |
| Cancellation immediately after rename | Returned success and retained the complete output. |
| Existing and dangling destination symlinks | Preserved the symlink and wrote the complete result to its referent. A 40-link chain succeeded. A 41-link chain preserved the existing referent and returned an error. |
| Temporary staging | Each case checked that no destination staging directory remained. Integration and benchmark runs checked worker cleanup. |
| Non-native payload | The validated multichunk payload matched the source SHA-256 before the GIO backend call. |

The Core API and Photos workflows reopened 16-bit PNG, including a
rotated export and representative decoded pixel checks. The Core API
also passed a lossless HDR JPEG XL round trip with four-channel pixel
checks, and reopened the TIFF overwrite with matching dimensions,
precision, and representative pixels. In a separate check with the
same worker executable, Core published the exact worker-produced bytes
for PNG, JPEG XL, and TIFF in both builds. The six files reopened as
2500 × 1250, 16-bit RGB images. SHA-256 comparisons were made against
each run's worker artifact because the encoder can generate ICC
timestamps across independent runs.

A separate PNG round trip preserved the embedded Display P3 ICC bytes
under both `preserve` and `remove-all`. It kept the test EXIF Artist
tag with `preserve` and removed it with `remove-all`. Both results
reopened at 2 × 1 and 16-bit depth. The packaged Ultra HDR worker also
exported and reopened an 8 × 8 JPEG gain map.

## D. Performance

Five repetitions per mode on Debian 13, WSL2 x86-64, Linux 6.18.58.1,
GIO 2.84.4, and ext4. Each sample used a fresh process and replaced a
small existing destination. Fixtures were warm in the operating-system
cache and the fixture SHA-256 was checked after each completed transfer.
The previous path used the same GIO calls and size validation as
`0b6f2b4`. RSS for the isolated transfer uses the process wait high-water
mark, which can include a floor from process startup. CPU covers the
benchmark process and children accounted for by wait.

| Transfer | Previous time (ms) | New time (ms) | Previous peak RSS (KiB) | New peak RSS (KiB) | Throughput before / after (MiB/s) | CPU before / after (ms) |
|---|---:|---:|---:|---:|---:|---:|
| 1 KiB | 5.38 | 5.66 | 23,548 | 23,548 | 0.18 / 0.17 | 6.59 / 6.32 |
| 1 MiB | 13.99 | 6.25 | 24,232 | 24,232 | 71 / 160 | 14.02 / 6.99 |
| 16 MiB | 135.67 | 14.69 | 24,392 | 24,232 | 118 / 1,089 | 126.55 / 12.50 |
| 64 MiB | 520.18 | 41.61 | 73,540 | 24,232 | 123 / 1,538 | 476.37 / 32.04 |
| 128 MiB | 1,049.05 | 84.59 | 139,024 | 24,272 | 122 / 1,513 | 964.48 / 47.64 |
| 256 MiB | 2,127.42 | 172.53 | 270,208 | 24,288 | 120 / 1,484 | 1,957.81 / 99.31 |

At 256 MiB the previous read and replacement phases took medians of
1,957.88 ms and 157.68 ms. The new copy and commit phases took 84.03 ms
and 88.28 ms. The prior path retained a 256 MiB payload allocation.
The native path allocated 256 KiB for the transfer buffer. This
describes payload storage and excludes library and allocator overhead.
Both paths held the source fixture and one extra destination-sized
payload on disk during replacement. The 256 MiB cancellation benchmark
canceled 5 ms after starting and verified that the old output remained
and staging was empty. Median time from cancellation to completion was
111 µs before and 812 µs after.

The full exports used one worker binary for both builds and the same
2500 × 1250 crop from the generated TIFF. RSS below is Core's sampled
peak. Worker time includes sandbox processing and encoding.

| Export | Output bytes | Previous time (ms) | New time (ms) | Previous peak RSS (KiB) | New peak RSS (KiB) | CPU before / after (ms) |
|---|---:|---:|---:|---:|---:|---:|
| PNG | 131,086 | 232.33 | 231.20 | 9,264 | 9,196 | 11.71 / 10.71 |
| JPEG XL | 13,034 | 485.99 | 479.89 | 9,196 | 9,164 | 10.41 / 10.03 |
| TIFF | 129,490 | 227.33 | 222.64 | 9,264 | 9,312 | 11.60 / 9.81 |

| Export | Worker before / after (ms) | Previous read / replacement (ms) | New copy / commit (ms) | Worker cleanup before / after (ms) | Median sampled temporary bytes before / after |
|---|---:|---:|---:|---:|---:|
| PNG | 222.47 / 222.14 | 1.69 / 6.47 | 0.14 / 5.78 | 0.113 / 0.115 | 131,383 / 131,383 |
| JPEG XL | 478.09 / 471.46 | 0.55 / 6.92 | 0.04 / 6.03 | 0.094 / 0.084 | 13,331 / 13,331 |
| TIFF | 217.63 / 215.06 | 1.47 / 6.68 | 0.11 / 5.93 | 0.089 / 0.083 | 259,278 / 259,278 |

Run the isolated comparison from a configured build using the same
`TMPDIR` filesystem for both modes.

```sh
python3 -B tests/benchmarks/benchmark_export_transfer.py \
  build/tests/benchmarks/benchmark-export-transfer --samples 5 --output transfer.json
```

Run full exports using the existing `run_benchmarks.py` runner with
`export-png=FIXTURE`, `export-jxl=FIXTURE`, and `export-tiff=FIXTURE`.
Supply the same generated `large.tiff` and worker executable to both
builds. The registered `performance` suite includes both benchmark
entry points. Cold filesystem caches and allocator-specific heap
instrumentation were unavailable in this comparison.

## E. Compatibility

Headless qualification passed 15 tests, including 12 required tests.
Full GTK qualification passed 25 tests, including 22 required tests.
The build enabled Glycin, timed AVIF and HEIF sequences, Ultra HDR,
and GIR. Release qualification passed all 28 functional tests,
including 25 required tests, and all 19 performance workloads. The
functional run included extended GTK and camera RAW checks. The
transfer tests passed under AddressSanitizer and UndefinedBehaviorSanitizer,
including the final symlink boundary cases.

ABI qualification matched the 285 Core and 77 GTK baseline exports.
Generated Core and GTK C headers, VAPI, and GIR were byte-identical to
the baseline.
The Core and GTK SONAMEs are both 2. Encoder implementations, the worker
protocol and sandbox, and ABI manifests require no changes for this
transfer.

The warning-level-3 diagnostic build and focused test passed. Treating
all warnings as errors stopped the full build on pre-existing Vala
warnings. The changed handwritten C code compiled with
`-Wall -Wextra -Wpedantic -Werror`.

The Debian gain-map profile produced all five ABI 2 binary packages.
Their extracted files passed the headless and Vala GTK consumers,
C GtkBuilder, introspection, Photos export, thumbnail, and gain-map
checks in an isolated package view. The thumbnail descriptor was
generated with the package's registration tool and system arguments.

## F. Deferred issues

Wrapping a native GIO replacement stream was considered. GIO can
truncate or create the final path before the copy finishes, so the
implementation uses a separate staging transaction. A bounded-memory
non-native transfer remains deferred until the backend can provide an
abortable replacement with equivalent publication semantics. Portable
backend-specific atomicity and behavior on a failed remote commit
remain dependent on GIO and the server.
