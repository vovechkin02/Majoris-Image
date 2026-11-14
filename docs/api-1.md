# Majoris Image API 1

## Components

- `majoris-image-1` links the headless GObject API. Native format parsing,
  metadata extraction, orientation, ICC conversion, RAW processing and export
  run in the worker. The core receives bounded manifests and straight-alpha
  float32 RGBA buffers in linear sRGB. RGB values can exceed one or be negative.
  The GTK library gives that buffer to a GDK memory texture with a linear sRGB
  colour state.
- `majoris-image-gtk4-1` links the GtkBuilder-compatible image view. It uses a
  scaled base preview and requests overlapped tiles at power-of-two resolutions
  for region-capable sources. The tile cache is scoped to the current document,
  page, and presentation. Glycin provides composited animation frames through
  its Bubblewrap sandbox when enabled.
- `majoris-image-thumbnailer` is a desktop thumbnail command. It accepts an
  input URI, caller-owned output file, and maximum pixel edge. The headless
  `ThumbnailService` is also available independently.

## Core usage

```vala
using MajorisImage;

async ImageDocument open_file (File file, Cancellable cancel) throws Error {
    var registry = new DecoderRegistry ();
    return yield registry.open (new ImageSource (file), null, cancel);
}
```

Use `ImageSource.from_stream()` or `from_bytes()` for input without a
persistent filename. The core stages up to 1 GiB of input and retains the
staged file for the lifetime of the source. Use `ImageDocument.open_page()`
for an independent page and `render_region()` for pixel rectangles in the
EXIF-oriented source coordinate system. `PixelBuffer.bytes` owns its
storage and can outlive the originating document. A buffer's stride is
`width * 16`. Host-endian floats are suitable for direct GDK upload on the
machine that requested the decode.

`DecodeOptions` controls page selection, preview dimensions, required
capabilities, RAW quality and camera white balance. `raw_preview_only`
selects an embedded RAW preview explicitly. Full development returns a
separate RAW state. `RenderOptions` describes non-destructive rotation,
crop, exposure and colour adjustments. `EncoderRegistry.encode()` writes a
completed export to a separate destination using the chosen container,
quality, bit depth, colour profile and metadata policy.

Use `DecoderRegistry.inspect()` for the actual source format and
capabilities. `installed_codecs()` returns the runtime libvips operation
catalogue. Its suffixes are discovery information. Decoder availability for
an individual input still depends on signatures and installed plugins. To
extend the worker stack, install an appropriate libvips plugin, or register
an implementation of `DecoderBackend` with an absolute worker executable and
a signature predicate. The core checks required capability flags when
selecting registered workers and again against the decoded manifest.

`PreloadManager` retains up to four adjacent previews and a maximum of 64 MiB.
Use an instance per navigation session and call `clear()` when its source list
changes. Call it from the same main context as the consumer.

## GtkBuilder

Register the type before loading a builder file or preparing a composite
template:

```xml
<object class="MajorisImageView" id="viewer">
  <property name="fit-mode">contain</property>
  <property name="allow-zoom">true</property>
  <property name="allow-pan">true</property>
</object>
```

In Vala, call `MajorisImage.ImageView.register_type()` and use
`yield view.load_file(file, options, cancellable)`. `load_file()` reports
observable loading, preview, ready and failure states. A newer load cancels
an earlier one. Use `set_document()` or `set_texture()` when an application
already has suitable content. The viewer owns references to its current
document and texture until cleared or destroyed.

In C, include `majoris-image-gtk4.h`, call
`g_type_ensure (MAJORIS_IMAGE_TYPE_IMAGE_VIEW)`, and use
`majoris_image_image_view_load_file()` with
`majoris_image_image_view_load_file_finish()`. Build the standalone C example:

```sh
cc builder.c $(pkg-config --cflags --libs majoris-image-gtk4-1) -o builder
```

For Vala, use the installed `majoris-image-gtk4-1.vapi` and
`majoris-image-gtk4-1.deps`. The XML demo can be built after installation:

```sh
glib-compile-resources --generate-source --target=demo-resources.c demo.gresource.xml
valac --pkg majoris-image-gtk4-1 demo.vala demo-resources.c -o majoris-image-demo
```

Observe `state`, `error-message`, `warning`, `current-page`,
`presentation-status` and `presentation-diagnostic` to provide surrounding
application controls. Use the UI thread for GTK properties and operations.
`actual_size()` targets one source pixel per surface pixel, including
fractional device scaling. The widget uses the application's GTK theme.

## HDR and format coverage

The worker converts source ICC or known CICP colour descriptions into linear
sRGB. It uses 203 nits as reference white for PQ and the reference 1,000 nit
HLG OOTF. Reinhard luminance compression and gamut mapping produce SDR
presentation. For native HDR, a host must establish a verified end-to-end
presentation path. This GTK implementation reports SDR tone mapping for HDR
sources across all three requested policies. Extended-range buffers remain
available through the core. Physical display output has not been verified.

| Path | Coverage |
| --- | --- |
| PNG and libvips raster | 8-bit and 16-bit PNG covered by public API tests. JPEG, TIFF, WebP, PNM, JPEG 2000 and other libvips formats depend on installed loaders. |
| TIFF | Multipage navigation and a generated 200 MP tiled TIFF regression. TIFF strips may require more I/O than tiled inputs. |
| OpenEXR | Native multipart scanline and tiled samples, float32, display/data windows and region tests. Deep parts are unsupported. |
| SVG and SVGZ | librsvg rasterization at requested size and region. The SVG fixture exercises the worker. |
| HEIF, HEIC and AVIF | libvips/libheif with the relevant decoder plugins. Numeric PQ/HLG samples are generated by the native test. |
| JPEG XL | libvips decode and native libjxl float-aware export. Numeric round-trip tests cover the installed codec. |
| Radiance HDR and other float rasters | Runtime libvips loaders and linear colour pipeline. Independent interoperability fixtures are needed for release qualification. |
| RAW | LibRaw reports recognized inputs. The worker supports embedded previews and 16-bit full development. The supplied archive has no licensed camera samples. |
| Gain maps | Optional libuhdr. The Majoris codec build supports JPEG, HEIF and AVIF gain maps. Container support depends on the libuhdr build. |
| Animation | Glycin provides composited frames and frame durations. GIF playback is exercised with a generated fixture. APNG and additional loader support depend on the local Glycin version; complete loop and disposal validation requires additional fixtures. |
| Legacy BMP, DIB, ICO/CUR, QOI, TGA, DDS | Runtime backend-dependent. Some require a compatible library plugin. The framework does not publish an unconditional support guarantee for them. |
| Physical HDR scanout | Hardware-dependent. No verified HDR presentation path is enabled. |

## Thumbnail cache

Request 1–1024 pixels on the long edge, using the desired device-pixel size for
high-DPI clients. `generate_page()` includes the page in the cache key.
All applications share `$XDG_CACHE_HOME/majoris-image/thumbnails-v2`. Keys
use URI, file identity, ETag, size, modification and change times. Writes
replace entries atomically, corrupt PNG chunk CRCs trigger regeneration, and
periodic eviction removes old entries and enforces a byte/count budget.
Each process may compute the same cache miss concurrently.

The desktop entry invokes the same service with `%u %o %s`. For sources that
provide modification timestamps, generated PNGs include the URI and mtime.
The desktop thumbnail manager owns placement and validation in its shared
thumbnail directories. This allows arbitrary sizes and page-specific entries
in the framework cache while desktop callers use the standard size classes.
See the [Freedesktop Thumbnail Managing Standard](https://specifications.freedesktop.org/thumbnail/latest/).

## Worker protocol and security

Only register worker executables from trusted software. Format parsing is
untrusted and always takes place in Bubblewrap. The parent mounts the source
read-only, isolates the network and process namespaces, limits address space,
CPU time, file size and open descriptors, and kills the sandbox on timeout or
cancellation. It stages remote input privately, validates fixed pixel geometry
and byte count, and removes work directories. Sandbox startup errors are
observable and fail closed.

The executable receives `OPERATION SOURCE OUTPUT REQUEST EDGE`. The JSON
request contains `version: 1`, `transform`, and `options`. Relevant operations
include `probe`, `decode`, `tile`, `thumbnail`, `export` and `codecs`. Probe
writes a UTF-8 manifest with `protocol-version: 1`; decode also writes
`/job/image.json`. Float output has a four-byte `MIF1` magic, big-endian
32-bit width and height, then exact-length native-endian float32 RGBA. The
parent bounds output size and accepts regular files only. Worker errors
are one JSON diagnostic with `kind` and `message` on standard error.
Run `meson test` for failure, cancellation, response validation and
source-protection coverage.

## Consumer migrations

Photos can use `RenderOptions` and `EncoderRegistry` for view and export
processing, and keep its edit history, thumbnail display, and catalogue in the
application. Files can embed the view for its preview pane, pass a cancellable
per selection, and use the headless thumbnail and probe APIs from list views.
Headless consumers can link just the core and install the worker.

## Regression tests and measurements

`meson test -C build --print-errorlogs` runs native numerical colour and EXR
checks, public headless and GTK consumers, and worker fault injection.
The 200 MP TIFF is generated at build time from an equivalent Photos
regression and exercises preview memory limits and a corner-region read.
The GUI tests use Xvfb when available. Hardware HDR, broader file
interoperability, and real-camera RAW need separately licensed fixtures and
hardware.

For repeatable local measurements, build the benchmark and use a local image:

```sh
export MAJORIS_IMAGE_WORKER="$PWD/build/src/worker/majoris-image-worker"
/usr/bin/time -v build/tests/majoris-image-benchmark open tests/fixtures/rgb16.png
/usr/bin/time -v build/tests/majoris-image-benchmark tile build/tests/large.tiff
```

Benchmark modes are `open`, `full` (up to a 4096-pixel edge), `tile`,
`thumbnail`, `hdr-sdr`, `raw-preview` and `raw-develop`.
Use a fresh `XDG_CACHE_HOME` for uncached thumbnail measurement. For zoom and
pan, use the GTK demo with the generated 200 MP TIFF and profile compositor
frame times through Sysprof on the target desktop.
