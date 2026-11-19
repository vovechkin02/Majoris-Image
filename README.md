# Majoris Image

**Version:** 2.0.1 · **Public API/ABI:** 2 · **Worker protocol:** 1  
**Platform:** Debian 13 (Trixie) / Majoris OS · **License:** GPL-3.0-or-later

Majoris Image is an embeddable, sandboxed image-processing framework for
applications such as Majoris Photos and Files. **ImageKit** is its headless
GObject Core for probing, decoding, rendering, exporting and thumbnails;
**ImageKitGtk** supplies a reusable GTK4 image viewer (`ImageKitView`).
Applications do not need to implement their own image decode pipelines.

## Contents

- [Features](#features)
- [Supported image formats](#supported-image-formats)
- [Requirements and dependencies](#requirements-and-dependencies)
- [Building and installing](#building-and-installing)
- [Quick start](#quick-start)
- [Public API reference](#public-api-reference)
- [Architecture and source layout](#architecture-and-source-layout)
- [Security, resources and compatibility](#security-resources-and-compatibility)
- [Testing and release qualification](#testing-and-release-qualification)
- [Documentation and contribution](#documentation-and-contribution)
- [License](#license)

## Features

- Still images, multipage documents, animated images and supported RAW
  cameras, with capability negotiation per input file.
- High-depth and float RGBA processing; HDR input, ICC/CICP interpretation
  where supported, and color-managed SDR tone mapping.
- Region rendering, zoom, pan, rotation, flipping, progressive viewport
  tiles and a bounded cache in the optional GTK4 viewer.
- Built-in image thumbnailing and a freedesktop desktop thumbnailer.
- Non-destructive render recipes and image export, including optional Ultra
  HDR gain maps and **lossless JPEG quarter-turn export** in 2.0.1.
- Read-only isolated workers using Bubblewrap, bounded input/output and
  cancellation; compatible headless operation.

**Important:** Decoder availability depends on the installed codec libraries,
Glycin loaders, and optional build features. An advertised format family does
not guarantee that every file variant or animation track can be decoded.

## Supported image formats

The following matrix summarizes the format families recorded in the worker
codec catalog and [the detailed API/format matrix](docs/api-2.md#hdr-and-format-coverage).
Here, *conditional* means that an appropriate installed loader, encoder or
optional build feature is required. Inspect each file for actual capabilities.

| Format / family | Decode | Animation / pages | Encoding and notes |
| --- | --- | --- | --- |
| JPEG | Native libvips | Still | JPEG; optional gain-map JPEG; lossless 90° turns under strict constraints |
| PNG / APNG | Native PNG, optional Glycin | APNG with Glycin | PNG; 8/16-bit input and ICC where available |
| GIF / WebP | libvips, optional Glycin | GIF and WebP animation | Installed savers determine export formats |
| TIFF | libvips | Multipage; regions | TIFF, high depth and floating-point HDR |
| JPEG 2000 (JP2) | Conditional libvips loader | Still | Saver-dependent |
| JPEG XL (JXL) | libvips / libjxl | Animation not verified | JPEG XL output and HDR handling where supported |
| HEIF / HEIC / AVIF | libvips, libheif plugins, optional sequence codecs | Collections; timed tracks when enabled | Encoder and gain-map support depend on installed components |
| OpenEXR | Native OpenEXR | Multipart; regions | OpenEXR floating-point HDR |
| SVG / SVGZ | librsvg | Rasterized still, scalable | No SVG encoder contract |
| Camera RAW | LibRaw (camera-dependent) | Embedded preview or development | Export developed pixels through supported encoders |
| Radiance HDR | Conditional libvips loader | Still | HDR input, saver-dependent output |
| PNM/PBM/PGM/PPM | Native libvips family | Still | Saver-dependent |
| BMP / ICO / QOI / TGA / DDS and others | Installed libvips or Glycin | Decoder-specific | Conditional; consult live codec discovery |

- **RAW family examples:** DNG, CR2/CR3/CRW, NEF/NRW, ARW/SR2, RAF, RW2,
  ORF, PEF, SRW and MRW; decoding is contingent on camera support.
- **Color/HDR:** Source precision, actual color management, region support
  and gain maps are input-specific. `ImageCapabilities` and `ImageProbe`
  describe what was established for the file.
- **HDR output:** A floating-point texture does not prove HDR scanout. On
  Debian 13 GTK, native HDR display verification is not available; the
  documented default is tone-mapped SDR presentation.
- **Runtime discovery:** `DecoderRegistry.installed_codecs()` exposes a JSON
  catalog, `inspect()` probes a particular source, and
  `thumbnail_mime_types()` lists configured thumbnail families.

## Requirements and dependencies

**Build target:** Debian 13, Meson **1.4 or later**, Vala, a C/C++ toolchain,
GLib/GIO, Gee, JSON-GLib and the dependencies declared in
[`debian/control`](debian/control) and [`meson.build`](meson.build).
Typical codec dependencies include libvips, OpenEXR, libjxl, LibRaw, libheif,
libturbojpeg, gexiv2 and librsvg; exact requirements are defined by Meson and
the Debian build profile. Optional features may require Glycin 1, libavif,
compatible libheif sequence APIs, and libuhdr.

**Runtime requirements:** Bubblewrap **0.10.0+**, `prlimit` (util-linux) and
the matching installed Majoris Image worker. GTK4 is needed only when
building or using `ImageKitGtk`; the headless Core does not depend on GTK.

The authoritative package dependency list is [`debian/control`](debian/control).
Do not assume the presence of an optional decoder merely because the Core
library has been installed.

## Building and installing

### Full build (Core, worker, GTK viewer and examples)

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

For installation into a chosen prefix:

```sh
meson setup build --prefix=/usr/local
meson compile -C build
meson install -C build
```

The commands use the project's feature defaults; Meson reports which optional
libraries were detected. `meson install` may require suitable permissions for
the chosen prefix. Build on a Linux filesystem when packaging for Debian.

### Headless Core

```sh
meson setup build-headless -Dgtk=false
meson compile -C build-headless
```

### Build options

| Option | Meaning |
| --- | --- |
| `-Dgtk=false` | Omit GTK viewer and examples |
| `-Dglycin=enabled\|disabled\|auto` | Require, omit or auto-detect Glycin fallback / APNG composition |
| `-Dsequence=enabled\|disabled\|auto` | Optional AVIF/HEIF timed sequences |
| `-Dultrahdr=enabled\|disabled\|auto` | Optional libuhdr gain-map support |
| `-Dintrospection=enabled\|disabled\|auto` | GI typelib generation |
| `-Dtests=true\|false` | Build verification targets |
| `-Dextended_tests=true` | Register longer lifecycle and scale checks |
| `-Dwayland_tests=true` | Register checks requiring a Wayland compositor |
| `-Draw_fixtures=/absolute/path` | External licensed camera RAW samples |
| `-Dworker_profiling=true` | Worker phase diagnostics |
| `-Dlibfuzzer=true` | Optional Clang libFuzzer harness |

### Debian packages

```sh
dpkg-buildpackage -us -uc -b
```

The packages include `libmajoris-image2`, `libmajoris-image-gtk4-2`,
`libmajoris-image-dev`, `majoris-image-worker` and
`majoris-image-thumbnailer`. Stock Debian codecs are used by default.
For a still-codec profile without the sequence dependency, use
`-Ppkg.majoris-image.nosequence`. For the Majoris gain-map package profile,
provide libuhdr development/runtime packages and use:

```sh
dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b
```

**Publication blocker:** The release owner must replace
`packages@majoris.invalid` with a monitored, authorized maintainer address
in `debian/control` and the current changelog entry before publishing.
The source release and package install/upgrade procedure are described in
[the test and release guide](docs/TESTING.md).

## Quick start

### Open an image from Vala (headless)

```vala
using GLib;
using ImageKit;

async ImageDocument open_image (File file, Cancellable? cancellable = null) throws Error {
    var registry = new DecoderRegistry ();
    var source = new ImageSource (file);
    return yield registry.open (source, null, cancellable);
}
```

Compile applications using the installed `majoris-image-1` Vala package and
`pkg-config` metadata. For input without a filename, use
`yield ImageSource.from_stream (...)` or `yield ImageSource.from_bytes (...)`.

### Embed the GTK4 viewer

Register the type before loading a GtkBuilder XML definition:

```vala
ImageKit.ImageView.register_type ();
```

```xml
<object class="ImageKitView" id="viewer">
  <property name="fit-mode">contain</property>
  <property name="scroll-zoom-mode">control</property>
  <property name="allow-zoom">true</property>
  <property name="allow-pan">true</property>
</object>
```

Load through `yield view.load_file (file)` on the GTK main context. Files-like
scroll containers should use `ScrollZoomMode.CONTROL`; Photos-like viewing
can use the `ALWAYS` default. Use `set_document()` or `set_texture()` when
content has already been prepared.

**Uninstalled demo:** The worker is resolved from Meson's configured libexec
directory when installed. For an in-tree run, supply its absolute path:

```sh
MAJORIS_IMAGE_WORKER="$PWD/build/src/worker/majoris-image-worker" \
  build/examples/majoris-image-demo
```

Consult [`examples/`](examples/) and [the complete widget guide](docs/api-2.md)
for C, GtkBuilder, GI and Vala integration details, gestures and accessibility.

## Public API reference

The **Vala namespace is `ImageKit`**, the public C symbol prefix is
`image_kit_`, the Core GI namespace is `ImageKit-2.0`, and GTK introspection
uses `ImageKitGtk-2.0`. Public GObject types are exported through the
versioned development package; **do not use worker/internal classes** as an
application API. This is a human-oriented index of the current declared
public methods. For parameter and error contracts, consult
[the API 2 guide](docs/api-2.md) and installed generated headers, VAPI and GIR.

### Core: sources, decoding and documents

| Public type | Public operations | Role |
| --- | --- | --- |
| `ImageSource` | `ImageSource(File)`, `from_stream(InputStream, Cancellable?)`, `from_bytes(Bytes, Cancellable?)` | Own or stage image input (stream limit: 1 GiB) |
| `DecoderRegistry` | `register_backend(DecoderBackend)`, `installed_codecs(Cancellable?)`, `thumbnail_mime_types(Cancellable?)`, `inspect(ImageSource, Cancellable?)`, `open(ImageSource, DecodeOptions?, Cancellable?)`, `render(ImageSource, RenderOptions, int edge, ...)` | Select a decoder and inspect, open or directly render a source |
| `DecoderBackend` | `id`, `executable`, `capabilities`, `mime_types`, `accepts(string mime, Bytes signature)` | Extensible registered-worker selection contract (Strategy) |
| `ImageDocument` | `open_page(int, Cancellable?)`, `open_frame(int, Cancellable?)`, `render_preview(bool tone_map, Cancellable?)`, `render_region(int x, int y, int width, int height, int edge, ...)` | Image/page/frame navigation and float pixel processing |
| `ImageProbe` | Public properties, including dimensions, depth, `mime`, `loader`, `capabilities` and metadata flags | Result of `inspect()`; supports per-input capability negotiation |
| `PixelBuffer` | `PixelBuffer(int width, int height, Bytes bytes)`; `width`, `height`, `stride`, `bytes` | Linear-sRGB, straight-alpha float32 RGBA storage |
| `ImageMetadata` | `json` | Structured image metadata |

`ImageDocument` additionally exposes `preview`, `metadata`, frame and page
counts, RAW status, decoder identity, HDR/color-management diagnostics and
capabilities. `PixelBuffer` has **16 bytes per pixel** (four 32-bit floats),
so its tightly packed stride is `width × 16`.

### Core: options, export, thumbnails and preloading

| Public type | Public operations | Role |
| --- | --- | --- |
| `DecodeOptions` | `copy()`; page, preview, RAW, region and required-capability properties | Request decoder behavior |
| `RenderOptions` | `validate()`, `serialize()`, `deserialize(string)`, `copy()` | Non-destructive image recipe and transformations |
| `ExportOptions` | `validate()`; format, dimensions, bit depth, quality, profile, metadata and lossless properties | Export policy |
| `EncoderRegistry` | `encode(ImageSource, File destination, ExportOptions, RenderOptions?, Cancellable?)` | Validate and publish an encoded output to a separate destination |
| `ThumbnailService` | `generate(ImageSource, int edge, Cancellable?)`, `generate_page(ImageSource, int edge, int page, Cancellable?)` | Cached PNG thumbnails, maximum edge 1024 |
| `PreloadManager` | Constructors (default or `with_registry`), `open(ImageSource, DecodeOptions?, Cancellable?)`, `preload(ImageSource[], DecodeOptions?)`, `clear()` | Bounded adjacent-image previews |

The public `ImageCapabilities` flags include `STILL`, `ANIMATION`,
`MULTI_IMAGE`, `HIGH_DEPTH`, `HDR`, `REGION`, `THUMBNAIL`,
`EMBEDDED_PREVIEW`, `METADATA`, `ENCODE`, `VECTOR`, `GAIN_MAP` and
`COLOR_MANAGED`. `RawState` describes RAW preview/development states.

**Lossless JPEG:** `ExportOptions.format = "jpeg"` and `lossless = true`
request a TurboJPEG coefficient-domain quarter-turn. Only compatible
`RenderOptions.turns` transformations are accepted; incompatible options,
untransformable edge blocks and metadata changes are rejected rather than
silently using a lossy fallback. See
[2.0.1 lossless-JPEG verification](docs/verification/verification-files-lossless-jpeg.md).

### GTK: image viewer

| Public type | Public operations | Role |
| --- | --- | --- |
| `ImageView` | `register_type()`, `load_file()`, `load_source()`, `set_document()`, `set_texture()`, `clear()` | Create and update the reusable image widget |
| `ImageView` | `set_page()`, `play()`, `pause()`, `step_frame()`, `seek_frame()` | Pages and animations |
| `ImageView` | `zoom_to()`, `zoom_in()`, `zoom_out()`, `fit_to_view()`, `actual_size()`, `pan_by()` | Viewport and scale |
| `ImageView` | `rotate_clockwise()`, `rotate_counterclockwise()`, `flip_horizontal()`, `flip_vertical()`, `reset_view()` | Non-destructive view transforms |
| `ImageView` | `transformed` signal | Notifies hosts of transform changes |

Key properties include `state`, `document`, dimensions, current page/frame,
`metadata`, `decoder`, `warning`, `error_message`, `hdr_source`,
`presentation_policy`, `presentation_status`, `presentation_diagnostic`,
`animated`, `playing`, `fit_mode`, `scroll_zoom_mode`, `zoom`,
`accessible_description`, `allow_zoom`, `allow_pan`, `nearest_neighbor`,
`minimum_zoom` and `maximum_zoom`.

Relevant enum types are `ViewState`, `FitMode`, `ScrollZoomMode`,
`PresentationPolicy` and `PresentationStatus`. The public type names and enum
values remain stable despite declaration-file reorganizations.

**Compatibility:** Core and GTK use SONAME 2. ABI 1 consumers need their
original matching worker/package set or a coordinated upgrade. Worker
requests retain protocol version 1 and existing wire keys, including legacy
British-spelled identifiers where already standardized in the protocol.

## Performance and animation

Frame-based APNG playback uses an optional document-owned Glycin forward cursor
when available; rewinds, concurrent requests and source changes use a safe
independent decode. Worker admissions use a stable priority queue while retaining
the four-job cap and interactive reservation. See
[`docs/architecture.md`](docs/architecture.md) and the
[performance closure report](docs/verification/verification-performance-closure-v1.8.md)
for design constraints and validation requirements.

## Architecture and source layout

| Directory | Purpose |
| --- | --- |
| `src/core/` | Headless image model and shared policy |
| `src/core/decoding/` | Decoder selection, probing and Glycin conversion |
| `src/core/execution/` | Worker IPC, scheduling and validated outputs |
| `src/core/export/` | Encoding and secure native/remote publication |
| `src/core/thumbnail/` | Thumbnail generation and validation |
| `src/gtk/` | GTK view, presentation, tile cache and animations |
| `src/worker/` | Sandboxed worker operations |
| `native/` | Native codec and pixel/color bridge |
| `src/thumbnail/` | Desktop thumbnailer executable |
| `tests/` | Isolated unit, integration, security, benchmarks and release qualification |
| `docs/verification/` | Historical verification evidence |

See [architecture and GoF design decisions](docs/architecture.md). The
architecture uses Strategy for registered worker selection, Facade for
consumer operations, GObject Observer signals and component composition. It
does not introduce additional GoF object hierarchies where they would add no
substitutability or lifecycle benefit.

## Security, resources and compatibility

- Source files and remote inputs are treated as untrusted. Decoding runs in
  short-lived, Bubblewrap-isolated workers with bounded execution and output.
- Images are decoded into linear-sRGB float32 RGBA; the GTK widget retains
  references to documents and textures until replaced, cleared or destroyed.
- Worker output uses validated descriptor-based bounded reads; native exports
  protect source/destination identity. Remote publication uses a complete,
  validated encoded buffer for compatibility with GIO backends.
- Per-instance tile caches, preloads, jobs and thumbnails have documented
  resource budgets. Multiple viewers have separate widget state.
- Cancellation, source freshness and error domains are described in
  [the API guide](docs/api-2.md); see actual API and tests before relying on
  a backend-specific guarantee such as remote rollback.

## Testing and release qualification

```sh
meson test -C build --print-errorlogs
python3 tests/release/qualify.py build --variant full
```

Additional profiles include headless Core, optional codecs disabled, and
release/installed-consumer checks. Real RAW fixtures use `-Draw_fixtures`.
For independent installed-consumer verification, see
[`tests/release/installed_consumers.py`](tests/release/installed_consumers.py).
Follow [docs/TESTING.md](docs/TESTING.md) for supported commands and suite
registration, including sanitizer, GTK/Wayland and benchmark requirements.

**Do not interpret included historical verification reports as passing tests
for a newly reorganized source archive.** Build, ABI baseline comparison,
GIR/VAPI checks and installed-consumer verification must be repeated against
this revision. Formal release still requires an authorized Debian maintainer
contact and any applicable target-platform qualification.

## Documentation and contribution

- [Detailed public API and format capabilities](docs/api-2.md)
- [Architecture and GoF design record](docs/architecture.md)
- [Tests, fixtures, benchmarks and release procedures](docs/TESTING.md)
- [Historical verification reports](docs/verification/README.md)
- [Majoris engineering standards v1.8](docs/engineering-standards-v1.8.md)
- [Contributing guidelines](CONTRIBUTING.md)

Contribute according to the shared standards. Follow the Vala `var` inference
policy, keep public ABI and protocol stable, avoid stylistic rewrites of
unrelated code, and verify any source moves in all Meson targets.

## License

**GPL-3.0-or-later**; see [LICENSE](LICENSE) and
[Debian copyright](debian/copyright). Code adapted from Majoris Photos retains
its recorded author and licensing history. Generated regression image fixtures
identified in [tests/fixtures/README.md](tests/fixtures/README.md) are
CC0-1.0. Consumers should assess GPL distribution obligations for their
particular combination and distribution model.
