# Bounded export transfer verification

## Native source freshness

The preceding transaction pinned the source and checked only the device and
inode reached by the original pathname before publication. Same-inode writes
could pass that check. Now, after the staged output is synchronized and closed
and destination revalidation passes, Core checks both `fstat()` on the pinned
descriptor and `stat()` on the original pathname. Both must match the original
regular-file type, device, inode, size, and nanosecond mtime and ctime captured
by preflight. Mode, ownership, and link count are outside the explicit source
comparison. Their usual ctime effects can still invalidate an export. Atime is
ignored because encoding itself can advance it.

Source changes, a missing path, a broken parent component, and symlink loops
return `G_IO_ERROR_BUSY`. Other query errors retain the errno-to-GIO mapping
and identify which check failed. Source rejection leaves the destination
untouched and uses the existing staging and worker cleanup paths. The
cancellation check still runs before rename.

Documents and thumbnails use GIO source stamps based on backend-provided URI,
identity, ETag, size, and microsecond timestamps. Native-to-native export uses
the pinned Linux descriptor and native stat fields directly. The read-only
sandbox mount restricts the worker's access. Other processes can write to the
same inode, and checks can miss coarse timestamps, restored or manipulated
metadata, unreported filesystem changes, and writes between the final checks
and rename. Export uses metadata queries only. The optional earlier check
after worker completion was deferred to avoid repeating queries on success.

### Implementation files

| Files | Change |
|---|---|
| `src/core/export-transfer.c` | Compares pinned and pathname metadata immediately before rename and records the source capture and validation phases. |
| `src/core/encoder-registry.vala` | Records complete native preflight time using the existing profiler. |
| `tests/unit/test-export-transfer.c`, `tests/unit/meson.build` | Add source mutation and metadata-query faults to the existing transfer suite and link the pathname-stat wrapper. |
| `tests/integration/test-core-api.vala` | Checks normal PNG and TIFF exports and changes a disposable source after the real worker encodes it, before the launcher returns. |
| `tests/benchmarks/benchmark-export-transfer.vala`, `tests/benchmarks/benchmark_export_transfer.py` | Measure native transfer and rejection, and compare two native builds on the same fixtures. |
| `docs/api-2.md`, `docs/verification-export-transfer.md` | Document the native publication contract, limitations, and verification. |

### Freshness and identity checks

| Scenario | Previous behavior | Updated behavior | Regression result |
|---|---|---|---|
| Unchanged source | Published | Publishes | Native transfer, PNG, and TIFF passed |
| Source inode replaced | Rejected | Rejects | Pinned descriptor retained the original inode and source SHA-256 |
| Source deleted | Rejected | Rejects | `G_IO_ERROR_BUSY` with a source-path diagnostic |
| Same-inode size increase | Could publish | Rejects when size or timestamps differ | `G_IO_ERROR_BUSY` |
| Same-inode size decrease | Could publish | Rejects when size or timestamps differ | `G_IO_ERROR_BUSY` |
| Same-size overwrite | Could publish | Rejects when mtime or ctime differs | Baseline published the test fixture. Updated build returned `G_IO_ERROR_BUSY` |
| Modification time changed | Could publish | Compares seconds and nanoseconds | Same-second mtime change rejected |
| Modification time restored after write | Could publish | Rejects when ctime differs | Original mtime restored and changed ctime observed before rejection |
| Access time changed only | Published | Publishes | Read advanced atime and the source SHA-256 matched |
| Source alias through hard link | Rejected | Rejects | `G_IO_ERROR_INVALID_ARGUMENT` |
| Source alias through symlink | Rejected | Rejects | `G_IO_ERROR_INVALID_ARGUMENT` |

Fault injection covered mutation during copy and after synchronization, an
invalid source symlink, descriptor `EIO` mapped to `G_IO_ERROR_FAILED`, and
pathname `EACCES` mapped to `G_IO_ERROR_PERMISSION_DENIED`. The tests checked
the existing destination's bytes and inode, no new destination on rejection,
staging removal, and descriptor closure via `EBADF`. The real worker tests
checked job cleanup and both existing and absent destinations. The existing
cancellation cases verified `G_IO_ERROR_CANCELLED` before publication and
successful completion after rename. The existing non-native payload test
verified the bytes passed to GIO. Loopback GVfs WebDAV creation and
overwrite also produced bytes identical to the native PNG, confirmed by
SHA-256 and the server's HTTP 201 and 204 responses.
In both builds the published PNG and TIFF matched their respective worker
artifacts byte for byte, and the source TIFF SHA-256 stayed the same.
Independent encoder runs generated different hashes, so each comparison
used the artifact and destination from the same export.

### Performance measurements

For a same-host comparison, build the preceding and updated revisions with
the same Meson options and use the updated runner. It retains the historical
whole-payload mode, compares the two native executables, checks payload hashes,
and measures rejection with the prior destination preserved.

Baseline `ca16fb9` and the updated build ran on Debian 13 under WSL2
(Linux 6.18.58.1, GIO 2.84.4), with warm fixtures and destination staging
on ext4. The baseline carried equivalent timers for capture, pathname
validation, preflight, and transfer. Synthetic
workloads used nine fresh processes per mode with rotating run order. The
real exports used five fresh processes per build, the same TIFF fixture and
the same worker executable. Synthetic RSS is the wait high-water mark. Real
export RSS is the sampled Core-process peak.

| Workload or phase | Previous time | Updated time | Previous peak RSS (KiB) | Updated peak RSS (KiB) |
|---|---:|---:|---:|---:|
| 1 MiB preflight | 220 µs | 235 µs | 24,704 | 24,704 |
| 1 MiB source capture | 2 µs | 3 µs | 24,704 | 24,704 |
| 1 MiB source validation | 5 µs | 4 µs | 24,704 | 24,704 |
| 1 MiB complete native transfer | 5.717 ms | 5.846 ms | 24,704 | 24,704 |
| 1 KiB export | 5.051 ms | 5.197 ms | 24,704 | 24,704 |
| 1 MiB export | 6.006 ms | 6.177 ms | 24,704 | 24,704 |
| 16 MiB export | 13.289 ms | 13.398 ms | 24,756 | 24,756 |
| 64 MiB export | 34.437 ms | 34.378 ms | 24,788 | 24,788 |
| 128 MiB export | 64.054 ms | 67.929 ms | 24,924 | 24,924 |
| 256 MiB export | 131.894 ms | 142.644 ms | 24,924 | 24,924 |
| 1 MiB new destination | 6.234 ms | 6.498 ms | 24,544 | 24,544 |
| 1 MiB symlink destination | 6.011 ms | 5.954 ms | 24,704 | 24,704 |
| 1 MiB source change rejection | — | 6.184 ms | — | 25,716 |
| Real PNG export | 234.488 ms | 234.962 ms | 9,228 | 9,296 |
| Real TIFF export | 232.351 ms | 232.438 ms | 9,296 | 9,340 |

The phase rows report the enclosing process's RSS. The 1 MiB freshness check
measured 1 µs for the pinned descriptor and 3 µs for the original pathname in
the updated build. The baseline measured only pathname lookup and inode
comparison. At 256 MiB the updated run took about 11 ms longer overall.
The copy phase showed the largest difference, with a median of 57.4 ms before
and 74.6 ms after. Final metadata validation accounted for 12 µs in the
updated run. Native payload buffer size remained 256 KiB.

```sh
python3 -B tests/benchmarks/benchmark_export_transfer.py \
  build-after/tests/benchmarks/benchmark-export-transfer \
  --previous-executable build-before/tests/benchmarks/benchmark-export-transfer \
  --samples 9 --output transfer.json
```

### Build and compatibility

- Focused transfer coverage passed all 67 cases, including the 12 new cases,
  with no skips. AddressSanitizer and UndefinedBehaviorSanitizer passed the
  focused transfer suite.
- Glycin-disabled headless qualification passed all 14 tests, including 12
  required cases. Full GTK qualification passed all 25 tests, including 22
  required cases. Both runs had zero failures and skips.
- Core and GTK matched their ABI 2 baselines of 285 and 77 symbols.
  Generated public C headers, GIR, and VAPI were byte-identical to the
  preceding revision. Worker protocol v1 and worker security tests passed.
- The warning-level-3 headless build and strict handwritten C checks passed.
  The project-wide warnings-as-errors build stopped on five existing Vala
  warnings in Core.
- The Debian gain-map build produced the five primary binary packages and
  four debug-symbol packages. A read-only private package view passed the
  installed headless and GTK consumers, C GtkBuilder, introspection, Photos
  export, thumbnail, and gain-map checks.
- Release qualification passed all 28 functional tests, including the 25
  mandatory tests, and all 35 performance benchmarks with zero failures and
  skips. A matching baseline configuration had identical registered names,
  suites, and timeouts.

## Destination safety hardening

The comparison baseline is `ec51c52`. Its native transaction validated and
pinned the completed worker output, resolved final destination links, checked
write access, and staged a bounded copy in a private directory. It preserved
owner, group, and ordinary mode bits, synced and closed the output, and then
renamed it into place. The only original-source check was `GFile.equal()`.
The destination checked before the copy could change before the rename. A
failed staging privacy check could leave the directory behind.

### Changed files

| Files | Change |
|---|---|
| `src/core/encoder-registry.vala`, `src/core/export-transfer.c`, `src/core/export-transfer.h` | Check aliases before the worker starts, retain the source and destination identities needed for publication, preserve the access ACL, and clean up staging on failure. The async transfer's two successful result forms are documented in the private header and at the Vala call site. |
| `src/core/worker-client.vala` | Pass the pinned source descriptor to Bubblewrap as the read-only `/source` mount. Keep the existing source query, worker arguments, and protocol. |
| `tests/unit/test-export-transfer.c`, `tests/unit/meson.build`, `tests/integration/test-core-api.vala` | Extend the existing suites with identity, mutation, metadata, and staging-fault assertions. |
| `tests/benchmarks/benchmark-export-transfer.vala`, `tests/benchmarks/benchmark_export_transfer.py`, `tests/benchmarks/run_benchmarks.py` | Measure the preflight and transfer together, and add new-file and symlink fixtures with byte and link validation. |
| `README.md`, `debian/control`, `docs/api-2.md`, `docs/verification-export-transfer.md` | Record the existing Bubblewrap dependency's minimum version, the native replacement policy, and verification. |

### Publication and cleanup boundaries

- For two native files, preflight opens the original source with `O_PATH` and
  compares its device and inode to the resolved destination. The open
  descriptor prevents inode reuse and pins the object mounted by Bubblewrap.
  Preflight runs off the caller's main context.
- After encoding, the native copy records the effective destination directory,
  final basename, existing inode and metadata, and identities of the final
  symlinks traversed. This snapshot governs the copy and commit. Alias checks
  run again against it. Parent paths follow normal kernel lookup. Core
  verifies the effective parents by inode.
- After file sync and close, Core verifies that the original source path
  still refers to its pinned inode, repeats destination resolution, and uses
  `fstatat(AT_SYMLINK_NOFOLLOW)` against the directory descriptor that will
  receive the rename. It checks the resolved path and link identities,
  existence, inode, permissions, ownership, size, link count, and timestamps.
  A detected alias gives `INVALID_ARGUMENT` and a detected transaction change
  gives `BUSY`.
- A new destination uses `renameat2(RENAME_NOREPLACE)`, which checks absence
  inside the publication operation. An existing destination uses
  `renameat()`. It atomically publishes complete bytes on supporting
  filesystems, but Linux provides no inode-conditioned replacement. A
  competing entry change after the check can affect the rename. Parent
  descriptors keep the commit anchored even if an ancestor moves in that
  interval. The final checks detect changes visible at inspection time.
  [Linux rename documentation](https://man7.org/linux/man-pages/man2/rename.2.html)
- Core removes its staged file through the private directory descriptor, then
  removes the empty staging directory through the original parent descriptor
  after checking its captured inode. It preserves the primary error and
  leaves a substituted entry alone. Directory creation and removal use
  separate identity checks. A change before the first identity capture or
  after the last cleanup check remains possible in a writable namespace.
  If the initial identity cannot be read, the empty directory may remain.
  Staging cleanup never recurses.
- Data synchronization covers the staging file. A successful close completes
  the staged stream, and rename supplies visibility. The directory entry
  remains subject to loss after sudden power failure. A canceled operation
  after successful rename reports success.

### Filesystem compatibility

The public alias comparison used disposable copies of the same 16-bit PNG and
the same worker with both Core builds. The GIO column summarizes a separate
unprivileged GLib/GIO 2.84.4 matrix using
`replace_contents_bytes_async()` and `FileCreateFlags.NONE`. The native
columns include the regression results and prior-policy observations.

| Scenario | Earlier GIO path | Previous bounded native path | Hardened native path |
|---|---|---|---|
| Identical source and destination | Core rejected it | Rejected | Rejected |
| Symlink or parent-link alias of the source | GIO followed the link to its referent | Public export succeeded and replaced the original inode and bytes | Rejected with source SHA-256, device, inode, and links preserved |
| Source hard-link alias | GIO wrote linked files in place | Public export succeeded and detached the alias from the original inode | Rejected with both names still linked to the original inode |
| Distinct file, ordinary mode, and ownership | Replaced an ordinary file with a new inode and kept its mode and owner/group | Preserved ordinary mode, owner, and group | Same policy, including a tested replacement with a different supplementary group |
| Existing and dangling destination symlinks | Updated the existing referent in place or created the missing referent | Preserved the link and published to the referent | Same referent behavior, with chain revalidation and a 40-link limit |
| Previously absent destination | Created mode `0640` under umask `0027` | Staged and published mode `0640` under that umask | Same mode with atomic no-clobber creation |
| Destination changed after resolution | No expected ETag was supplied | Published to the captured basename even if its entry changed | Observed inode, deletion, creation, symlink, parent, mode, and source-alias changes abort before commit |
| Read-only file in writable directory | Permission denied | Permission denied | Permission denied, with the access check applied to the pinned inode |
| Writable file in read-only directory | Wrote in place | Staging failed | Staging fails and the previous bytes remain |
| Existing POSIX access ACL | Dropped on an ordinary staged replacement | Dropped on replacement | Preserved byte for byte, or publication fails |
| Existing `user.*` attribute | Dropped on an ordinary staged replacement | Dropped on replacement | Dropped. Only the POSIX access ACL is carried over from the old inode |
| Set-user-ID, set-group-ID, and sticky bits on mode `07750` | Published mode `01750` | Preserved ordinary mode bits only | Publishes mode `0750` |
| Other hard links to a distinct destination | Kept linking the updated inode | Retained the old inode and old bytes | Retain the old inode and old bytes |
| Pre-commit cancellation or commit error | An already canceled call and a directory rejection preserved the fixtures in the GIO matrix | Native fault tests preserved the prior destination | Preserves competing or prior data and removes owned staging |

New files use the usual process umask or inherited default ACL. Replacements
keep ordinary mode bits and the previous access ACL, including removing a
staging-inherited access ACL if the old file has none. Capabilities and other
`security.*` attributes from the old inode are discarded. The filesystem
provides metadata for the new inode. Configurations with security labeling,
non-POSIX ACLs, and ownership transitions to another user require separate
filesystem verification.

### Performance

Five fresh processes per case on Debian 13, WSL2, GIO 2.84.4, with
destination staging on ext4. Fixtures were warm in the operating-system cache.
The same fixture generator and measurement runner checked output SHA-256 after
each transfer. The comparison uses the bounded native transfer from
`ec51c52`, with the same new-file and symlink fixture setup, and the hardened
native transfer with source preflight included. RSS is the process wait
high-water mark in KiB and includes the measurement overhead.

| Workload | Previous time (ms) | Hardened time (ms) | Previous peak RSS (KiB) | Hardened peak RSS (KiB) | Hardened preflight (µs) |
|---|---:|---:|---:|---:|---:|
| 1 KiB, existing | 5.044 | 5.151 | 24,692 | 24,480 | 222 |
| 1 MiB, existing | 5.798 | 6.349 | 24,692 | 24,480 | 224 |
| 16 MiB, existing | 14.843 | 13.358 | 24,692 | 24,480 | 213 |
| 64 MiB, existing | 40.222 | 34.164 | 24,692 | 24,480 | 223 |
| 128 MiB, existing | 78.720 | 68.850 | 24,692 | 24,520 | 223 |
| 256 MiB, existing | 160.733 | 158.103 | 24,692 | 24,520 | 214 |
| 1 MiB, new destination | 6.520 | 6.414 | 24,396 | 24,348 | 214 |
| 1 MiB, existing symlink destination | 5.913 | 6.105 | 24,692 | 24,480 | 232 |

The 1 MiB existing-file median increased by 0.55 ms. Preflight took about
0.22 ms, and the commit path now checks metadata and resolves the effective
destination again. At 256 MiB, median throughput was 1,593 MiB/s before and
1,619 MiB/s after. The native payload buffer remains at most 256 KiB and
the 256 MiB limit remains in place. Median cleanup time after the benchmark
canceled a 256 MiB transfer was 754 µs before and 622 µs after. Both paths
preserved the previous destination in those cancellation samples.

The end-to-end comparison used the same worker executable and 2500 × 1250
TIFF crop with both builds. The reported RSS is Core's sampled peak.
Independent runs can produce different ICC timestamps and small variations
in encoded byte counts.

| Export | Previous time (ms) | Hardened time (ms) | Previous peak RSS (KiB) | Hardened peak RSS (KiB) |
|---|---:|---:|---:|---:|
| PNG | 226.63 | 230.71 | 9,024 | 8,916 |
| JPEG XL | 476.27 | 481.50 | 9,028 | 9,040 |
| TIFF | 231.84 | 230.37 | 9,016 | 9,124 |

Use the [benchmark commands below](#d-performance) to repeat the measurements.
The isolated runner now reports new and symlink cases under `destinations`
and the preflight timings under `export-identity`.

### Regression and compatibility verification

- All 55 focused transfer cases passed under strict C compiler warnings,
  AddressSanitizer with leak detection, and UndefinedBehaviorSanitizer on
  ext4. They verify source hashes and inode identity after alias rejection,
  the pinned descriptor after pathname replacement, competing destination
  bytes at controlled mutation points, permissions and metadata, and cleanup
  with the original error intact. Cancellation at the rename boundary
  leaves the committed bytes intact and reports success.
- The default full GTK profile passed 25 tests, including 22 required tests.
  The headless profile passed 15 tests, including 12 required tests. The
  release-profile inventories matched across revisions. They contain 28
  functional tests and 19 benchmarks with the same names, suites, timeouts,
  parallel flags, and test protocols.
- The ABI checker matched 285 Core and 77 GTK exports. The generated headers,
  VAPI files, and GIR declarations matched byte for byte. The SONAMEs remain
  2, and the worker protocol remains version 1. The Debian gain-map build
  produced all five binary packages and passed the installed consumer checks
  in a private package filesystem view.
- PNG, lossless HDR JPEG XL, and TIFF round trips passed. In a separate
  fixed-fixture comparison, both builds published the exact bytes of their
  worker artifacts, with identical SHA-256 values across builds for all
  three formats. The installed worker passed the Ultra HDR gain-map round
  trip. Both revisions reported that the available libvips lacked an EXR
  writer, so EXR export could not be verified on this system.
- A loopback GVfs WebDAV destination returned HTTP 201 on creation and 204
  on replacement. Both outputs decoded to the same pixels as the native
  PNG. The final remote file also matched the native SHA-256. Backend
  publication and cancellation semantics continue to apply to non-native
  destinations.
- The project-wide warnings-as-errors build stopped at existing Vala
  diagnostics in decoder and worker code. The changed handwritten C and
  fault-injection tests passed `-Wall -Wextra -Wpedantic -Werror`.

## Original bounded-memory transfer

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

The [non-native export verification](verification-non-native-export.md)
measures the compatibility path and compares it with real GVfs WebDAV
streaming and staging probes.
