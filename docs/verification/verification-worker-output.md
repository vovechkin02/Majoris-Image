# Bounded worker output verification

## A. Implementation

`WorkerClient.read_output()` still returns owned `GLib.Bytes` and records the
`output-read` phase. The reader runs in GLib's task thread pool. It reads
directly into one fallible output allocation in chunks of at most 256 KiB and
transfers that allocation to `GBytes` after validation.

| Changed file | Purpose |
| --- | --- |
| `src/core/worker-output-reader.c`, `src/core/worker-output-reader.h` | Private descriptor reader, cancellation, and ownership. |
| `src/core/worker-client.vala`, `src/core/meson.build` | Bind, profile, and compile the reader in headless Core. |
| `tests/unit/test-worker-output-reader.c`, `tests/unit/meson.build` | Exercise the production reader with syscall faults, real filesystem objects, and cancellation checkpoints. |
| `tests/security/worker-fault.c`, `tests/security/test-worker-security.vala`, `tests/security/worker-output-fault.c`, `tests/security/worker-output-fault.h`, `tests/security/meson.build` | Exercise rejected output from the sandboxed worker and inject faults while Core consumes it. |
| `tests/security/cache_processes.py` | Verify symlink cache repair, target preservation, and subsequent concurrent cache hits. |
| `tests/benchmarks/benchmark-worker-output.c`, `tests/benchmarks/benchmark_worker_output.py`, `tests/benchmarks/meson.build` | Generate isolated fixtures and compare output reading with the previous GIO sequence. |
| `tests/release/qualify.py` | Make the private reader test mandatory by exact name. |
| `.github/workflows/test.yml` | Include the direct reader cases in the sanitizer job. |
| `docs/TESTING.md`, `docs/verification-core-worker.md`, `docs/verification-worker-output.md` | Document the test commands, contract, and results. |
| `docs/benchmarks/worker-output-reader.json` | Preserve the paired ext4 benchmark samples and fixture hashes. |

The text path covers the codec catalog, probe, decode and render manifests,
and Glycin metadata. Its 1,048,576-byte cap precedes UTF-8, JSON, and schema
validation. The MIF1 path covers previews, pages and frames, buffer and
source rendering, and regions. It retains its 268,435,468-byte cap,
12-byte header, and exact pixel geometry checks. The pixel payload remains
a slice of the same allocation. Fresh and cached thumbnails share the
8,388,608-byte cap before PNG validation. A rejected cache entry takes the
existing regeneration path. Encoder output continues through the separate
export transfer path.

## B. Validation comparison

Previous behavior follows the original GIO sequence. The benchmark also
reproduced its behavior for regular files, growth, truncation, and cancellation.

| Scenario | Previous | Bounded | Verification |
| --- | --- | --- | --- |
| Valid regular output | Loaded using the pathname | Returns byte-identical output from the pinned inode | Direct tests and benchmark digests |
| Missing or empty output | Invalid response at metadata query | Invalid response from the open or descriptor size check | Direct tests |
| Symlink or FIFO | Initial type query rejected it | Pin rejects the object before data access | Direct and sandbox tests |
| Exact size limit | Initial query accepted it | Reads exactly the limit and rechecks the descriptor | Direct tests and benchmark |
| One byte above the limit | Initial query rejected it | Descriptor size check rejects it before allocation or data reads | Direct and sandbox tests |
| File grows during reading | Could return bytes beyond the limit | Rejects extra bytes or a changed final size | Direct tests and benchmark |
| File is truncated during reading | Could return a shorter buffer | Rejects premature EOF | Direct and sandbox tests, benchmark |
| Same-size rewrite or timestamp change | No post-read check | Rejects changed modification or change timestamps | Direct tests |
| Path replaced after opening | The pathname could be changed between query and load | Data still comes from the pinned inode. An observed metadata change discards the buffer. | Direct tests with symlink and inode replacement |
| Read or metadata failure | GIO error | GIO error mapped from the failing system call | Direct tests |
| Cancellation | Asynchronous GIO cancellation | GIO cancellation before opening, during consumption, or before completion | Direct and sandbox tests, benchmark |

## C. Memory and performance

Measurements use Debian 13 on x86-64 WSL2, Linux 6.18.58, GLib 2.84.4,
and ext4 fixtures under `/var/tmp`. Each cell is a median of five
fresh-process samples per reader and cache condition. The benchmark
alternates the reader order and compares with the previous GIO metadata
query and asynchronous load sequence. It measures output consumption
independently of sandbox startup, image decoding, and hashing. Cold
samples use `POSIX_FADV_DONTNEED` after flushing the fixture. Eviction is
advisory and does not control filesystem or host caches. Warm samples
read the fixture before launching the benchmark process. Peak RSS is the
Linux process `VmHWM` measured before hashing.

The [raw benchmark samples](../benchmarks/worker-output-reader.json) include
every measured CPU, allocation, byte-count, read-count, metadata, cancellation,
and error-outcome field.

| Output | Bytes | Warm previous ms | Warm bounded ms | Warm previous KiB RSS | Warm bounded KiB RSS | Cold previous ms | Cold bounded ms | Cold previous KiB RSS | Cold bounded KiB RSS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 KiB JSON | 1,024 | 0.551 | 0.209 | 8,316 | 8,356 | 0.704 | 0.368 | 8,340 | 8,336 |
| 1 MiB JSON | 1,048,576 | 7.958 | 0.758 | 9,404 | 9,392 | 8.378 | 1.422 | 9,420 | 9,312 |
| 1 MiB PNG | 1,048,576 | 8.094 | 0.700 | 9,424 | 9,296 | 8.178 | 1.498 | 9,436 | 9,312 |
| 8 MiB PNG | 8,388,608 | 59.511 | 3.793 | 16,532 | 16,508 | 60.772 | 4.976 | 16,644 | 16,472 |
| 16 MiB MIF1 | 16,777,228 | 119.047 | 7.200 | 24,784 | 24,700 | 121.471 | 9.476 | 24,804 | 24,680 |
| 64 MiB MIF1 | 67,108,876 | 475.716 | 26.653 | 73,968 | 73,860 | 477.287 | 32.092 | 73,996 | 73,860 |
| 128 MiB MIF1 | 134,217,740 | 942.546 | 52.528 | 139,464 | 139,368 | 1,001.161 | 66.415 | 139,436 | 139,396 |
| 256 MiB MIF1 | 268,435,468 | 1,917.412 | 102.995 | 270,556 | 270,468 | 1,962.115 | 123.431 | 270,528 | 270,428 |

Both implementations consumed and returned the file size shown for every
successful sample. Every returned SHA-256 matched its fixture. The bounded
reader made one output-sized allocation in every sample, including the
fault cases below. Baseline allocations inside GLib were uninstrumented.
RSS was similar because both readers retain the complete payload. The
previous reader issued 8 KiB reads in this GLib build. A read-call count
includes any EOF check.

| Warm output | CPU µs previous → bounded | Read calls previous → bounded | Initial metadata µs previous → bounded | Bounded final stat µs |
| --- | ---: | ---: | ---: | ---: |
| 1 KiB JSON | 663 → 321 | 2 → 2 | 228 → 17 | <1 |
| 1 MiB JSON | 7,602 → 900 | 129 → 4 | 251 → 19 | 1 |
| 1 MiB PNG | 7,692 → 826 | 129 → 5 | 278 → 18 | 1 |
| 8 MiB PNG | 56,492 → 3,884 | 1,025 → 32 | 243 → 23 | 1 |
| 16 MiB MIF1 | 111,210 → 7,288 | 2,050 → 66 | 247 → 21 | 2 |
| 64 MiB MIF1 | 447,137 → 26,778 | 8,194 → 258 | 246 → 19 | 3 |
| 128 MiB MIF1 | 885,674 → 52,663 | 16,386 → 514 | 252 → 20 | 3 |
| 256 MiB MIF1 | 1,785,697 → 103,128 | 32,770 → 1,025 | 256 → 20 | 3 |

The previous metadata timing includes asynchronous query dispatch and
completion. Bounded metadata timing covers the pin, initial stat, and
readable open syscalls. The previous sequence had no final metadata
verification.

Fault workloads started at 1,048,588 bytes and changed the file or canceled
immediately after the first successful read.

| Fault | Duration µs previous → bounded | CPU µs previous → bounded | RSS KiB previous → bounded | Consumed bytes / read calls previous → bounded | Outcome previous → bounded |
| --- | ---: | ---: | ---: | --- | --- |
| Cancel | 445 → 386 | 562 → 514 | 8,340 → 8,328 | 8,192 / 1 → 262,144 / 1 | Both canceled. Callback latency 59 → 62 µs. |
| Growth | 8,183 → 728 | 7,873 → 848 | 9,396 → 9,036 | 1,048,589 / 130 → 1,048,588 / 5 | Previous returned one byte over the limit. Bounded returned invalid response. |
| Truncation | 633 → 413 | 720 → 530 | 8,360 → 8,348 | 8,192 / 2 → 262,144 / 2 | Previous returned 8,192 bytes. Bounded returned invalid response. |

The injected truncation leaves the bytes from the first read in the file,
so its offset differs with the readers' chunk sizes. Canceled and rejected
reads returned zero bytes from the bounded implementation.

## D. Security and resource ownership

Linux `O_PATH | O_NOFOLLOW | O_CLOEXEC` pins the final component without
opening it for data I/O. The first `fstat()` validates the pinned type and
size. Symlinks are inspected as symlinks. FIFOs, sockets, directories, and
devices are rejected before they can be opened for reading. A readable
close-on-exec descriptor is opened through the process's own
`/proc/self/fd` entry while the pin is live. This procfs entry refers to
the pinned inode even when another file has taken its pathname.

The requested byte count is the smaller of 256 KiB and the bytes remaining
in the validated size. Short reads advance by the actual count, and an
interrupted read retries after a cancellation check. Once the expected bytes
have arrived, an EOF probe can consume at most one byte of the remaining
allowance. At the limit, the final size check detects growth without requesting
an additional byte. Final `fstat()` checks type, device, inode, size, and the
nanosecond modification and change timestamps against the initial descriptor
snapshot.

Cancellation is checked before opening, between chunks and retries, and
around final verification. A single filesystem syscall can complete before
cancellation takes effect. It runs off the caller's main context. GTask
delivers once and propagates cancellation if it arrives before the result is
collected. The pin and readable descriptor close before delivery. Failure
frees the partial allocation and leaves the caller's established job cleanup
in place. Missing, invalid, and changed output map to
`ImageError.INVALID_RESPONSE`. Ordinary operating-system errors retain
their GIO classification and errno diagnostic.

Descriptor identity stabilizes which object supplies the bytes. The metadata
check detects observable changes during the read. It cannot guarantee an
immutable snapshot if the filesystem reports indistinguishable metadata or
a write occurs after final verification.

## E. Regression and release verification

| Check | Result |
| --- | --- |
| Direct reader | 40 cases including size limits, descriptor stability, read and allocation failures, metadata changes, and cancellation. |
| ASan and UBSan | The direct suite passed 25 consecutive runs under both sanitizers. All six tests in the CI sanitizer selection passed. |
| Worker security | Passed Bubblewrap isolation and resource limits, crash, timeout, missing codec, malformed response, invalid UTF-8, oversized JSON and MIF1, symlink, FIFO, truncation, extra bytes, cancellation during consumption, and job and child cleanup. |
| Core and GTK integration | Passed still and HDR decode, pages and regions, rendered pixels, Glycin fallback, thumbnail generation and cache hits, corrupt and symlink entry repair, concurrent reads, GTK at X11 scales 1 and 2, animation, and tile rendering. |
| Full GTK qualification | 26 passed, 0 failed, 0 skipped. Core ABI 2 matched 285 exports and GTK matched 77. |
| Headless with Glycin | 16 passed, 0 failed, 0 skipped. Core ABI 2 matched 285 exports. |
| GTK with Glycin, timed sequences, and Ultra HDR disabled | 25 passed, 0 failed, 0 skipped. Core matched 280 exports and GTK matched 77. |
| Extended and interoperability | All 3 passed, including complete 8K tile coverage, GTK lifecycle stress, and the full real-camera RAW corpus. |
| Generated interfaces | Both Core and GTK GIR and VAPI pairs matched the unmodified revision byte for byte. |
| Stock and gain-map Debian packages | Both built. Each package build passed 26 tests and skipped the external RAW case. The RAW case passed in the full profile. |
| Installed consumers | Passed C, Vala, GI, GtkBuilder, Files, Photos export, and thumbnail consumers on the extracted stock packages. The installed gain-map worker exported and decoded a JPEG gain map. |
| Full release qualification | Passed all 29 functional tests, all 26 mandatory cases, installed consumers, ABI checks, and all 36 performance benchmarks, with zero failures or skips. Final focused reader, sandbox, and cache tests passed again after the final test-harness edits. |
| Strict warnings | Changed C sources passed the focused compiler command below, also with `-Wpedantic`. Both baseline and updated repository-wide strict builds stopped at the same five existing Vala warnings in `decoder-registry.vala`, `image-probe.vala`, `worker-request.vala`, and `worker-failure.vala`. |
| Static checks | Changed C sources passed clang-format 19.1.7. Changed Python passed Ruff lint and formatting. `git diff --check` passed. |
| Environment scope | The GTK checks ran under Xvfb. Wayland and physical-display checks were outside this run. Package checks used disposable extracted packages with relocated pkg-config prefixes and worker binaries bound to their installed paths. Package-manager install and removal transactions were outside this run. |

The previous test inventory at `d8f520f` had 28 functional registrations and 35
performance registrations in the full profile. It gained the mandatory
unit test `Bounded worker output validation` with a 15-second timeout and
the performance benchmark `Bounded worker output memory and throughput`
with a 900-second timeout. Every preexisting registration retained its
name, suite, and timeout. The baseline passed all 28 functional tests.

```sh
meson setup /tmp/imagekit-output-full -Dglycin=enabled -Dsequence=enabled \
  -Dintrospection=enabled -Dextended_tests=true
meson compile -C /tmp/imagekit-output-full -j 6
meson setup --reconfigure /tmp/imagekit-output-full -Dultrahdr=enabled \
  -Draw_fixtures=/tmp/majoris-raw-fixtures.sAAnY8
python3 tests/release/qualify.py /tmp/imagekit-output-full --variant full --jobs 4
meson test -C /tmp/imagekit-output-full --print-errorlogs --suite extended --suite interoperability

meson setup /tmp/imagekit-output-headless -Dgtk=false -Dglycin=enabled -Dintrospection=enabled
python3 tests/release/qualify.py /tmp/imagekit-output-headless --variant headless --jobs 3
meson setup /tmp/imagekit-output-native -Dglycin=disabled -Dsequence=disabled \
  -Dultrahdr=disabled -Dintrospection=enabled
python3 tests/release/qualify.py /tmp/imagekit-output-native --variant native --jobs 3

meson setup /tmp/imagekit-output-sanitize -Dgtk=false -Dglycin=disabled \
  -Db_sanitize=address,undefined -Db_lundef=false
meson test -C /tmp/imagekit-output-sanitize --print-errorlogs 'Bounded worker output validation'
meson test -C /tmp/imagekit-output-sanitize --no-rebuild --print-errorlogs \
  --repeat 25 'Bounded worker output validation'
meson setup --reconfigure /tmp/imagekit-output-sanitize -Dsequence=enabled \
  -Dultrahdr=disabled -Dintrospection=enabled -Dwarning_level=3
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  meson test -C /tmp/imagekit-output-sanitize --print-errorlogs \
  'OpenEXR region decoding' 'Native image processing references' 'Thumbnail PNG decoding and validation' \
  'Native parser fuzz smoke' 'Worker protocol round trips and validation' 'Bounded worker output validation'
meson setup /tmp/imagekit-output-strict -Dwarning_level=3 -Dwerror=true
meson compile -C /tmp/imagekit-output-strict -j 3
cc -std=gnu11 -D_FILE_OFFSET_BITS=64 -Wall -Wextra -Werror -Wformat=2 \
  -Wmissing-prototypes -fsyntax-only -I /tmp/imagekit-output-full/src/core \
  $(pkg-config --cflags gio-2.0 gee-0.8 glycin-1) \
  src/core/worker-output-reader.c tests/unit/test-worker-output-reader.c \
  tests/security/worker-fault.c tests/security/worker-output-fault.c \
  tests/benchmarks/benchmark-worker-output.c

meson compile -C /tmp/imagekit-output-full benchmark-worker-output
TMPDIR=/var/tmp python3 tests/benchmarks/benchmark_worker_output.py \
  /tmp/imagekit-output-full/tests/benchmarks/benchmark-worker-output --samples 5 \
  --output /tmp/imagekit-output-readings-ext4.json

dpkg-buildpackage -us -uc -b -j4
dpkg-buildpackage -us -uc -b -Ppkg.majoris-image.gainmap -j4
```

The two package builds used separate Linux staging directories populated
from Git-tracked source modes and the working changes. In the extracted
package views, the consumer commands were
`python3 -B tests/release/installed_consumers.py /tmp/imagekit-output-full/tests/large.tiff`
for stock and `python3 -B tests/release/installed_consumers.py --gainmap`
for gain-map. Running all release tests in the package namespace removed the
supplementary groups needed by an existing export test. The retry ran the
qualification runner on the host and used the package view only for the
installed consumer subprocess.

```sh
dbus-run-session -- python3 -B /tmp/imagekit-output-release-runner.py \
  /tmp/imagekit-output-full --variant release --jobs 4 --installed
```

On a disposable Debian system with the packages installed at their canonical
paths, the equivalent command is `python3 -B tests/release/qualify.py
/tmp/imagekit-output-full --variant release --jobs 4 --installed`.

## F. Scope

- A memory mapping could still observe writes or fault after truncation.
  The reader owns the bytes it returns.
- Shared-memory transport would change the worker protocol and ownership.
  The current descriptor path fits the existing sandbox and cache.
- Streaming MIF1 parsing and alternate pixel backing would change caller
  interfaces. The existing pixel slice already shares the validated
  allocation.
- Persistent output caches would change lifetime and retention. Cached
  thumbnail reads already use the validated path.
- Full-file hashing requires additional work on every read and would still
  need a trusted reference to establish a snapshot. Hashing is used in the
  benchmarks to compare fixture content.
