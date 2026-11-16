# Native memory and data-transfer verification

The baseline is `9b299d2d0678c1e131cefba02971a2bc8e835740`. The native benchmark
harness was built against the original native sources before either source
changed. [Baseline JSON](benchmarks/native-memory-before.json) and
[optimized JSON](benchmarks/native-memory-after.json) contain the fixture
identity, phase, sample counts, CPU time, RSS, libvips memory counters, output
sizes, and hashes for the full matrix. The baseline benchmark executable had
SHA-256 `5ee0cf42f0ec9923067e53dc3316908bfd45d5453806a825a96b410faa970a15`.

## MIF1

The worker opens and orients the source, applies the requested crop and
transforms, and prepares a linear scRGB libvips pipeline. An HDR request for
SDR presentation adds the tone-mapping pipeline. The writer validates RGB or
RGBA float32 geometry and writes the magic and big-endian dimensions. It
converts source rows to native-endian float32 RGBA, preserving finite RGB,
replacing non-finite RGB with zero, and supplying or normalizing alpha.
The worker writes its decode manifest after the pixel file closes
successfully. Core validates and reads the file into a byte buffer and
references the payload from the `PixelBuffer`. For render-buffer input the
native reader borrows the mapped payload and keeps the mapping attached to
the `VipsImage` for its lifetime.

Previously `vips_image_write_to_memory()` materialized the entire source
into a caller-owned allocation, up to 192 MiB of RGB or 256 MiB of RGBA at
the writer's pixel limit. The writer held this allocation and its conversion
row until close. The writer now owns one converted row, bounded by 256 KiB.
The libvips disk sink manages two reusable full-width strip buffers and
evaluates the upstream graph on its workers. Its callbacks are serialized
and ordered. The callback borrows its region until it returns and uses the
region address macro for each row. The sink finishes before the caller closes
the file or releases the row. Strip height depends on libvips demand style
and concurrency. Source ownership and upstream decoder caches or working
buffers still depend on the input pipeline.
[libvips disk sink](https://www.libvips.org/API/8.16/libvips-generate.html)

An initial caller-thread region implementation passed the native checks but
hit the worker's existing address-space limit inside libvips while evaluating
an HDR render-buffer graph. The ordered sink retained libvips' worker-thread
evaluation. The captured worker request and the affected integration checks
passed with the sink.

Open, header, row, and close failures report their filesystem error.
Generation failures report the libvips diagnostic. The native error category
and worker staging and cleanup remain compatible.

## JPEG XL

`image_codec_to_linear()` supplies a referenced image or a lazy conversion
graph. The encoder materializes its float32 samples into a caller-owned
contiguous allocation, rejects non-finite samples and half-float overflow,
and scans RGB in sample order to compute the intensity target. Basic info,
linear sRGB, frame depth, alpha, distance and lossless settings precede frame
submission. The encoder drains compressed data with a 64 KiB stack buffer
and closes the output file.

`JxlEncoderAddImageFrame()` requires the full input and copies it before
returning. The caller now frees its allocation immediately after successful
submission, before processing compressed output. The encoder owns its
queued frame and codec work buffers until consumption or destruction. The
linear image reference survives until cleanup. The input allocation and the
libjxl copy can coexist during submission. At 16 MP the caller allocation
is 192 MiB for RGB or 256 MiB for RGBA. Both the original 0.10 API and the
Debian 13 libjxl 0.11.2 API document the copy contract.
[libjxl 0.10 header](https://github.com/libjxl/libjxl/blob/v0.10.0/lib/include/jxl/encode.h)
[Debian 13 libjxl package](https://packages.debian.org/trixie/libdevel/libjxl-dev)

Chunked input was deferred. The baseline 0.10 API does not provide
`JxlEncoderAddChunkedFrame()`. The 0.11 callback API requests rectangles
and can hold multiple returned buffers at once. A compatible implementation
would need a fallback and a way to validate and find the RGB peak before
frame submission, including repeatable evaluation or buffering for
sequential sources. This pass established neither equivalent output across
those paths nor a measured benefit for such a dual implementation.
[libjxl 0.11 chunked input contract](https://github.com/libjxl/libjxl/blob/v0.11.1/lib/include/jxl/encode.h)

## Measurements

Debian 13.5 on WSL2, AMD Ryzen 7 7700X, GCC/G++ 14.2.0, Meson 1.7.0,
libvips 8.16.1, libjxl 0.11.2, release build, and `VIPS_CONCURRENCY=2` for
both builds. Temporary outputs were on the same tmpfs. Each workload used
three cold and three warm processes per build. The table gives median cold
operation time and median process peak RSS. Warm samples time the second
operation and their process peak includes the first. Operation time excludes
library initialization, fixture construction, and digesting, and includes any
lazy evaluation demanded by the production operation. RSS includes setup
and library allocations. The libvips counters cover allocations tracked
by libvips. The sample size is too small to report a native P95.

| Workload | Before ms | After ms | Before peak MiB | After peak MiB |
|---|---:|---:|---:|---:|
| MIF1 RGB small | 0.26 | 0.56 | 32.3 | 32.2 |
| MIF1 RGB 4 MP | 46.00 | 34.30 | 128.4 | 86.5 |
| MIF1 RGB 16 MP | 189.32 | 119.57 | 416.3 | 236.2 |
| MIF1 RGBA small | 0.24 | 0.62 | 32.3 | 32.3 |
| MIF1 RGBA 4 MP | 54.00 | 38.26 | 160.3 | 104.4 |
| MIF1 RGBA 16 MP | 210.55 | 197.86 | 544.4 | 304.5 |
| MIF1 lazy RGBA 16 MP | 256.67 | 136.66 | 301.6 | 60.3 |
| JPEG XL RGB 4 MP, lossy half | 1,175.97 | 1,192.37 | 360.0 | 312.0 |
| JPEG XL RGB 16 MP, lossy half | 4,804.95 | 4,782.49 | 1,308.4 | 1,116.4 |
| JPEG XL RGBA 4 MP, lossy float | 1,586.91 | 1,598.85 | 408.4 | 344.3 |
| JPEG XL RGBA 16 MP, lossy float | 6,400.71 | 6,407.57 | 1,501.0 | 1,245.1 |
| JPEG XL HDR RGBA 1 MP, lossless float | 457.95 | 454.40 | 137.4 | 121.5 |

All 96 samples completed in each build. Cold and warm digests matched the
baseline for every matrix workload. Regenerating the reference files with
the unchanged baseline executable also gave direct byte matches for all
11 MIF1 and five JPEG XL workloads on this library configuration. The
small MIF1 inputs gained about 0.3–0.4 ms of sink setup overhead. The 16 MP
lazy case reduced peak RSS by 80% and its median time by 47%. JPEG XL encode
times remained within about 1.4% of the baseline in this sample. The 16 MP
RGBA JPEG XL workload also completed in both builds under the worker's
existing process limits and produced identical bytes.

## Correctness

Native tests compare the complete MIF1 byte sequence with independently
constructed headers and float values. They cover sequential PNG evaluation,
narrow and wide rows, distinct row patterns, negative and HDR RGB, signed
zero, alpha boundaries, invalid layout and geometry, injected generation
failure, denied paths, and synthetic write and close failures using
`/dev/full`. JPEG XL tests decode through libjxl and check reference values,
precision, alpha, linear sRGB, and intensity metadata. They also exercise
invalid input, encoder configuration and file output failures. The Core
test exports an HDR EXR through the sandboxed JPEG XL path and reopens it to
check dimensions and sample values with a tolerance of 0.001. The baseline
worker differed from the EXR source preview by about 0.00025 in one channel.

| Check | Result |
|---|---|
| Pre-change default build | 21 passed. The external RAW fixture test skipped. |
| Full GTK release test suite | 24 passed, including real-camera RAW, complete 8K tile coverage, lifecycle stress, Core, and worker security. |
| Complete release qualification | Passed the required tests, installed consumers, ABI checks, and both registered benchmarks. |
| Headless qualification | 11 required tests and Core ABI passed. |
| Native-only GTK qualification at warning level 3 | 21 required tests and Core/GTK ABI passed with Glycin, timed sequences, and Ultra HDR disabled. |
| ASan and UBSan | All 22 native imaging cases passed with leak detection enabled. |
| Strict warnings | The touched C sources and benchmark compiled at warning level 3 with warnings as errors. The full strict build stopped on pre-existing missing initializer warnings in `native/codec-catalog.c`. |
| Debian gain-map packages | Built all binary packages. Packaging tests passed 21 cases with the external RAW fixture unavailable to that build. |
| Installed consumers | Passed the C GtkBuilder, Vala/Core, GTK, GI, Photos export, and thumbnail checks against the fresh packages staged in a disposable mount namespace. |
| Public ABI 2 | Core and GTK SONAMEs and 285 Core and 77 GTK exports verified. Generated C headers, VAPI, GIR, and typelibs matched the baseline byte-for-byte. All 22 install destinations matched. |

Existing registered test names, suites, and timeouts were retained. The
native performance benchmark adds one registration. Package staging checked
the installed paths and regenerated the thumbnail descriptor with the
package tool. Lintian was unavailable. WSL rejected the disposable overlay
mounts needed for a package-manager removal and reinstallation check.
