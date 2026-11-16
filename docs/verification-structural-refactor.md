# Majoris-Image structural refactor

## Component map

| Component | Before | After |
| --- | --- | --- |
| Core source types | `image-source.vala` also defined the pixel and metadata objects. | `ImageSource` remains in place. `pixel-buffer.vala` owns float RGBA storage and validation. `image-metadata.vala` owns the metadata value. |
| Decoder contract | `worker-request.vala` contained public decode options. `decoder-backend.vala` contained the probe manifest. | `decode-options.vala` owns defaults and copying. `image-probe.vala` owns capability derivation, manifest validation, and legacy field translation. Core, worker, and the protocol tests use the same source definitions. |
| Worker | `main.vala` handled startup and every processing path. A private archive supplied the entry point with `link_whole`. | `main.vala` handles startup and exit status. `worker-dispatcher.vala` reads requests, selects operations, and assembles their outputs. `ImageInspector` opens source pixels and `MetadataReader` updates probe metadata. Meson compiles the Vala sources and contracts directly into the worker executable and links the private native archive. |
| GTK | `ImageView` managed document loading, presentation, playback, input, and drawing. `presentation.vala` also defined `AnimationFrame`. | `animation-controller.vala` owns playback timing, pending work, prefetch, seeks, and loop progress. `ImageView` owns the published properties and texture, output subscriptions, presentation requests, GTK input and lifecycle, snapshot, and tiles. `Presentation` evaluates output policy and renders the selected presentation. |
| Native sequences | `sequences.c` validated HEIF boxes and edit lists while also selecting tracks and decoding AVIF and HEIF. | `sequence-timeline.c` parses the selected HEIF track's timing and edit list. Its private header declares the shared frame budget and timeline result. `sequences.c` selects codecs and tracks, consumes the validated timeline, and publishes decoded frames. |

The worker retains preparation order, the thumbnail RAW preview fallback, the
SVG region transform, and the decode metadata sidecar. The thumbnail output
helper retains an owned reference to a tone-mapped image through color
conversion and PNG writing. The parent still owns process isolation, staging,
response validation, cancellation, and temporary-file cleanup.

Each viewer owns its animation controller. Frame requests retain their source
document and cancellable across asynchronous work. Document and playback
generations gate completion. The controller holds a weak link to the widget
and the widget clears that link during disposal after stopping the timer and
canceling frame work. Frame publication still updates the existing properties
inside the widget's notification freeze. Monitor changes can suspend frame
advancement until the widget publishes its selected presentation.

HEIF validation retains the existing file, box, iteration, allocation, and
timing checks. The decoder passes the selected track ID and timing parameters
to the parser, which is compiled only with HEIF sequence support. The smoke and
libFuzzer harnesses both invoke that production function on HEIF input, even
when libheif rejects the file before reaching timeline validation.

## Internal naming

- `native/exr.cpp` became `native/exr-encoder.cpp`, alongside the existing EXR source reader.
- `tone-operations.vala` and `color-operations.vala` became `tone-adjustments.vala` and `color-adjustments.vala`. Their types are `ToneAdjustments` and `ColorAdjustments`.
- `FloatOps` became a namespace of private worker functions. `linear`, `band`, and `join` became `scale_offset`, `extract_bands`, and `join_bands`.
- `ImageInspector.inspect_with_pixels` became `inspect_and_prepare_pixels`. `Presentation.pixels` became `render_pixels`. `WorkerClient.executable` became `resolve_executable`.
- Playback duration fields now identify microseconds explicitly.

The remaining worker helper classes use the existing static-method pattern.
The native bridge header remains the shared Vala/C binding boundary. The
timeline header carries only the declarations shared by its decoder and fuzz
callers. GTK retains event-controller setup with widget focus and event
propagation. The fit and pan calculations share rotated dimensions while
keeping their existing redraw and notification paths.

## Public compatibility

The generated headers, VAPIs, GIRs, typelibs, and installed pkg-config
descriptors are byte-identical to the pre-refactor build. The complete dynamic
export sets and symbol types match, with **285 Core** and **77 GTK** symbols.
ELF SONAMEs and needed dependencies match. The repository ABI checker passed
against both [Core](../tests/release/abi2-core.symbols) and
[GTK](../tests/release/abi2-gtk.symbols) manifests.

Core exposes `ImageSource`, `PixelBuffer`, `ImageMetadata`,
`ImageDocument`, `ImageProbe`, `DecodeOptions`, `RenderOptions`,
`ExportOptions`, `DecoderBackend`, `DecoderRegistry`, `EncoderRegistry`,
`ThumbnailService`, and `PreloadManager`, with `ImageCapabilities`,
`RawState`, and `ImageError`. GTK exposes `ImageView` and the
`ViewState`, `FitMode`, `ScrollZoomMode`, `PresentationPolicy`, and
`PresentationStatus` enums. `ImageKit-2.0`, `ImageKitGtk-2.0`,
`ImageKitView`, and the existing `transformed` signal and property
notifications remain the public interfaces. The [API guide](api-2.md)
describes the consumer contract. The compared generated files enumerate its
methods, properties, signal signatures, and GType identities.

The worker CLI continues to take `OPERATION SOURCE OUTPUT REQUEST EDGE`.
Exit status 2 covers invocation or startup failure, 1 covers operation
failure with a JSON diagnostic, and 0 covers success. Protocol version 1,
the `transform` and `options` request objects, export options, thumbnail
metadata, probe fields, and `MIF1` pixels retain their representations.
The legacy `colour-probe`, `colour-managed`, `colour-diagnostic`, and
`colour-profile` spellings remain part of the wire contract. `WorkerFailure`
still converts the `kind` and `message` diagnostic into Core errors, with
separate parent-side handling of timeout, cancellation, sandbox failure,
resource exhaustion, crashes, and invalid responses.

The baseline and new worker produced byte-identical responses for `codecs`,
`colour-probe`, `probe`, `decode`, `render-buffer`, `tile`, `export`,
and an OpenEXR HDR `thumbnail` under the isolated mount layout. Wrong
argument count, invalid edge, invalid JSON, and unknown operation also
matched in exit status and diagnostics.

## Verification

The initial unchanged default build registered 22 tests and reported
21 passes and one skip for external RAW fixtures. With manifest-pinned RAW
inputs and extended suites enabled, both the original HEAD and the refactor
registered the same 24 tests with matching names, suites, timeouts, priority,
parallel settings, and result protocols. Both builds passed all 24.

| Profile or check | Command or configuration | Result |
| --- | --- | --- |
| Full GTK, Glycin, AVIF and HEIF sequences, Ultra HDR, GIR | `meson setup BUILD -Dglycin=enabled -Dsequence=enabled -Dultrahdr=enabled -Dintrospection=enabled -Dextended_tests=true -Draw_fixtures=RAW`, then `meson test -C BUILD -j4 --print-errorlogs` | 24 passed, including RAW, 8K coverage, and lifecycle stress |
| Headless with Glycin and sequences | `meson setup BUILD -Dgtk=false -Dglycin=enabled -Dsequence=enabled -Dintrospection=enabled`, then `python3 tests/release/qualify.py BUILD --variant headless` | 11 mandatory tests and ABI passed |
| GTK with optional decoders disabled | `meson setup BUILD -Dglycin=disabled -Dsequence=disabled -Dultrahdr=disabled -Dintrospection=enabled`, then `python3 tests/release/qualify.py BUILD --variant native` | 21 mandatory tests and ABI passed |
| HEIF-only and AVIF-only | Headless builds with `-Dsequence=auto`. The HEIF-only build hid libavif from pkg-config. The AVIF-only build simulated an unavailable libheif repeat symbol at configure time. | Native fuzz smoke and decoder-selection tests passed in each build |
| Wayland | `meson configure BUILD -Dwayland_tests=true`, then `meson test -C BUILD --suite platform --print-errorlogs` under WSLg | One lifecycle test passed |
| Native ASan and UBSan | GCC, `-Dgtk=false -Dglycin=disabled -Dsequence=enabled -Dultrahdr=disabled -Dwarning_level=3 -Db_sanitize=address,undefined -Db_lundef=false` | Five passes covering OpenEXR, imaging references, PNG validation, native fuzz smoke, and worker protocol |
| Strict warnings | `meson setup BUILD -Dwarning_level=3 -Dwerror=true`, then `meson compile -C BUILD -j4` | Both the original HEAD and the refactor failed on existing codec-catalog initializer warnings. The compiler also promotes Vala unused-method warnings to errors. |
| Clang 19 libFuzzer | `-Dlibfuzzer=true -Dc_args=-fsanitize=fuzzer-no-link -Db_sanitize=address,undefined`. Built `fuzz-imagekit` and seeded it with `tests/security/seed_fuzz.py`. | 10,000 executions without sanitizer findings |
| Debian packages | `dpkg-buildpackage -us -uc -b` and `dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b` | Stock and gain-map binary packages built from a temporary staging directory with Git file modes |
| Release qualification | `python3 tests/release/qualify.py BUILD --installed` using extracted gain-map Debian packages, then `python3 tests/release/installed_consumers.py --gainmap` | 24 required tests, ABI, installed C/Vala/GI/GTK/thumbnail consumers, the performance benchmark, and the installed JPEG gain-map round trip passed |

Installed tests used the built packages in a temporary directory, with their
worker and thumbnailer paths mounted inside an isolated environment. Package
manager installation, removal, and reinstallation were outside that run.

The inventory below lists the full-profile test names and timeouts. All are
parallel Meson tests at priority zero using exit codes. The default build
registers the RAW case with a 30-second skip placeholder when the fixture
directory is unspecified. The extended tests require `extended_tests=true`.
X11 input tests require Xvfb and XTest. The optional Wayland lifecycle test
requires `wayland_tests=true` and an active compositor.
Fixture provenance is recorded in the [fixture guide](../tests/fixtures/README.md).

| Suite | Registered tests with timeouts in seconds |
| --- | --- |
| unit | GDK floating-point texture color state (30), HDR presentation policy (30), Job scheduling and priorities (10), Native image processing references (150), OpenEXR region decoding (30), Thumbnail PNG decoding and validation (15) |
| integration | Decoder selection and composited APNG frames (30), Desktop thumbnail registration and output (30), Files public preview workflow (90), GTK input and accessibility at scale 1 (30), GTK input and accessibility at scale 2 (30), GTK view lifecycle and navigation (45), GTK viewer readiness and load errors (30), GtkBuilder view loading (30), Headless Core API (90), Photos public full-quality workflow (90), Viewport tile coverage and cancellation (45) |
| security | Cross-process thumbnail publication (40), Native parser fuzz smoke (40), Worker protocol round trips and validation (15), Worker timeout, cancellation, crash and response validation (15) |
| extended | Complete 8K tile coverage (180), GTK lifecycle stress (160) |
| interoperability | Real-camera RAW decoding (180) |
| platform | Wayland view lifecycle (60) |

## Deferred work

Fit-mode notifications and explicit transformation calls can both invoke
`update_transform()` during one public operation. Measure that path before
considering coalescing because clients can observe redraw and signal ordering.
The existing benchmark harness can also measure buffer materialization and
tile planning in a subsequent performance pass. Physical HDR output and
native touch require hardware validation.
