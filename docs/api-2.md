# Majoris Image API 2

The Vala namespace is `ImageKit`, the public C prefix is `image_kit_`,
and the GtkBuilder class is `ImageKitView`. Import `ImageKit-2.0` or
`ImageKitGtk-2.0` when using GObject Introspection.

The core and GTK4 libraries have SONAMEs `libmajoris-image.so.2` and
`libmajoris-image-gtk4.so.2`. Update and recompile API 1 consumers against
the API 2 development package before upgrading the runtime. The Debian
package names, pkg-config modules, Vala package IDs, and include paths
retain their distribution names.

Existing edit recipes and version 1 worker requests, manifests, errors, and
metadata retain their existing JSON field names and operation names. For
example, the API uses `nearest_neighbor` and `color_managed`, while saved
recipes and worker manifests use `nearest-neighbour` and `colour-managed`.

## Components

- `majoris-image-1` links the headless GObject API. Native format parsing,
  metadata extraction, orientation, ICC conversion, RAW processing and export
  run in the worker. The core receives bounded manifests and straight-alpha
  float32 RGBA buffers in linear sRGB. RGB values can exceed one or be negative.
  The GTK library gives that buffer to a GDK memory texture with a linear sRGB
  color state.
- `majoris-image-gtk4-1` links the GtkBuilder-compatible image view. It uses a
  scaled base preview and requests overlapped tiles at power-of-two resolutions
  for region-capable sources. The tile cache is scoped to the current document,
  page, and presentation. Temporary unmapping allows the active image request
  to complete while clearing viewport tiles and suspending timers. Mapping
  restores visible tiles, view transforms, and requested playback. Explicit
  pause, selection replacement, cancellation and disposal have independent
  effects. Glycin supplies composited animation frames through Bubblewrap.
- `majoris-image-thumbnailer` is a desktop thumbnail command. It accepts an
  input URI, caller-owned output file, and maximum pixel edge. The headless
  `ThumbnailService` is also available independently.

## Core usage

```vala
using ImageKit;

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
separate RAW state. `bit_depth` reports effective decoded precision, including
8-bit JPEG previews and 16-bit developed RAW. `source_bit_depth` is the
source precision when known. For RAW, LibRaw's reported white level provides
an estimate; the raw metadata includes that estimate, sensor dimensions,
LibRaw flip and EXIF orientation. `pixel_buffer_bit_depth` is 32 for float
RGBA. Screen precision depends on the presentation environment.
`RenderOptions` describes non-destructive rotation,
crop, exposure and color adjustments. `EncoderRegistry.encode()` writes a
completed export to a separate destination using the chosen container,
quality, bit depth, color profile and metadata policy.

Use `DecoderRegistry.inspect()` for the actual source format and
capabilities. `installed_codecs()` returns the runtime libvips operation
catalog. Its suffixes are discovery information. Decoder availability for
an individual input still depends on signatures and installed plugins. To
extend the worker stack, install an appropriate libvips plugin, or register
an implementation of `DecoderBackend` with an absolute worker executable and
a signature predicate. The core checks required capability flags when
selecting registered workers and again against the decoded manifest.

When a native decoder reports an unavailable or unsupported format, a
headless Glycin adapter negotiates the source signature and MIME aliases against
executable loader configurations across the XDG data directories. Glycin
starts its own Bubblewrap sandbox. The worker probes source metadata separately
and the fallback validates geometry, stride, precision and alpha. Tagged RGB
can use Glycin 1's Little CMS conversion, whose output color state is
unavailable through its public C API. An explicit source transfer the adapter
cannot interpret or an invalid ICC profile yields `COLOR_PROFILE`. Untagged
integer previews use an sRGB default. Applications can request
`ImageCapabilities.COLOR_MANAGED` to require a verified interpretation.
Glycin 1 can satisfy it for PNG carrying a validated sRGB declaration and
no competing ICC or CICP declaration, and for QOI that declares sRGB.
Linear QOI preserves the stored transfer and assumes sRGB primaries. Its
metadata reports that assumption and strict requests require declared
primaries. The PNG samples follow the loader's
sRGB contract. Tagged ICC previews rely on Glycin's internal Little CMS
conversion and carry an unverified provenance. The framework never
re-applies the source profile to those pixels. Assumed and unknown source
states remain separate in the metadata. Glycin 2.2 publishes frame color
metadata under a separate, unstable API, while this build targets Glycin 1.
Inspect `color_managed` and `color_diagnostic` on probes or documents.

Fallback previews use linear-light area filtering with premultiplied alpha.
`RenderOptions.nearest_neighbor` selects pixel sampling explicitly.
`render_region()` can re-decode a fallback input and crop at the requested
edge. The `REGION` flag identifies decoders that can read spatial regions
directly. `DecoderRegistry.render()` uses the same decoder negotiation.

`PreloadManager` considers up to four adjacent sources and retains at most
64 MiB of preview pixels. It uses ordinary decoder selection, including
registered workers, capability checks and Glycin fallback. Use
`new PreloadManager.with_registry(registry)` when foreground navigation
uses a configured registry. Both native and Glycin preload jobs enter
the shared scheduler below foreground opens, visible tiles and
thumbnails.

Use one manager per navigation session on the consumer's main context.
Starting a new batch or calling `clear()` cancels superseded work and
releases cached documents. Cache identity includes the source stamp and
serialized decode options, including page, preview size, HDR and required
capabilities. Foreground `open()` checks the current identity and uses
ordinary decoding on a miss. A failed preload is never cached.

## GtkBuilder

Register the type before loading a builder file or preparing a composite
template:

```xml
<object class="ImageKitView" id="viewer">
  <property name="fit-mode">contain</property>
  <property name="allow-zoom">true</property>
  <property name="allow-pan">true</property>
</object>
```

In Vala, call `ImageKit.ImageView.register_type()` and use
`yield view.load_file(file, options, cancellable)`. `load_file()` reports
observable loading, preview, ready and failure states. Stage a stream or
bytes with `ImageSource.from_stream()` or `from_bytes()` and pass it to
`load_source()`. Bounded consumers can set `DecodeOptions.allow_regions=false`
to suppress automatic GTK tile decoding and choose a small `preview_size`. A newer load cancels
an earlier one. Use `set_document()` or `set_texture()` when an application
already has suitable content. The viewer owns references to its current
document and texture until cleared or destroyed.

In C, include `majoris-image-gtk4.h`, call
`g_type_ensure (IMAGE_KIT_TYPE_IMAGE_VIEW)`, and use
`image_kit_image_view_load_file()` with
`image_kit_image_view_load_file_finish()`. Build the standalone C example:

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

### Input and accessibility

The wheel policy is local to each view. `ScrollZoomMode.ALWAYS` is the
default. Use `CONTROL` inside a scrolling pane, or `DISABLED` to give
the surrounding UI every wheel event. Plain and horizontal scroll,
unrelated modifier combinations, and attempts to zoom past a limit propagate
when the viewer does not handle them. GTK pinch gestures remain available
in all three modes. `allow_zoom` controls interactive wheel, pinch, and
keyboard zoom.

```xml
<object class="ImageKitView" id="preview">
  <property name="scroll-zoom-mode">control</property>
  <accessibility>
    <property name="label" translatable="yes">Image preview</property>
  </accessibility>
</object>
```

The view exposes GTK's image accessibility role and the standard busy
state during loading. The host can supply a persistent label using GTK
accessibility properties, and a per-image description using
`accessible_description`. Source replacement and `clear()` retire the
old description. A description configured while the viewer is empty
applies to its first image. Page changes retain the description.
The generic description is “Image preview” when content is displayed.
The view does not derive descriptions from file paths or EXIF fields.

```vala
view.scroll_zoom_mode = ImageKit.ScrollZoomMode.CONTROL;
yield view.load_file (selected_file);
view.accessible_description = description_for_selected_image;
```

Use Tab or click to focus the viewport. `+` and `=` zoom in, `-` zooms out,
`0` fits the image, and the arrows pan. The keypad zoom and fit equivalents
also work. Disabled actions, arrows at a pan limit, and unrelated shortcuts
propagate. Applications can observe `state`, `warning` and `error-message`
to provide status UI. Test screen-reader announcements and physical pinch
gestures with the target desktop session.

### High-resolution viewport tiles

The view draws its base preview while tiles arrive. It enumerates the
complete visible source rectangle, including rotated and flipped views
at fractional zoom or device scale. It queues visible tiles nearest the
center first, followed by up to two neighboring tiles. Each view runs
at most two tile jobs, including jobs that are still winding down after
cancellation. Replaced sources and presentation generations cannot
publish stale tiles.

The cache reserves a square float32 RGBA allocation for every tile,
including overlap. It charges up to 256 MiB for the usual warm cache
and expands its visible working set to a 704 MiB limit for large
viewports. It evicts unused textures before evicting visible work.
The workspace admits up to 256 tile entries. When estimated padding
and the requested pyramid level would exceed either budget, the
planner first tries display-scale sampling, then coarsens the level
until the entire viewport fits. The limits apply to cache allocations.
Worker buffers, the base preview and renderer-owned resources are
accounted for separately. Ordinary 5K and 8K raster viewports fit the
full-resolution grid. Overlap, clipping and the existing preview mask
preserve transparent compositing during progressive rendering.

### Animation and external textures

Playback requires a confirmed multi-frame document. The viewer decodes
GIF, WebP, AVIF and HEIF frames with `ImageDocument.open_frame()`. APNG uses
Glycin's composited buffers through that same API, from its first frame.
When Glycin cannot provide a compatible frame, the view retains the still
preview with an animation diagnostic. The viewer has no separate streaming
GDK texture path. Frame presentation uses the same float buffer, HDR policy,
texture factory, and generation checks as still images. Indexed APNG
requests may replay earlier source frames inside Glycin. The viewer keeps
one prefetched frame and cancels obsolete requests.

`set_texture()` retains the original GDK texture and its declared color
state. An sRGB texture reports SDR. Linear sRGB, Rec. 2100 PQ, linear Rec. 2100,
and other color states are reported as `EXTERNAL_UNVERIFIED`. Rec. 2100 tags
set `hdr_source` and describe the input encoding. The actual HDR range and
monitor output remain unverified. GTK draws the original texture using its
color state. The view needs an `ImageDocument` for its own tone mapping and
source-resolution tiles.

The internal explicit-state constructor accepts prepared straight-alpha
float32 RGBA and a GDK color state that describes those samples. Document
previews, tiles and animation frames currently provide linear sRGB.
The constructor validates the layout. It does not perform a color conversion.
A future verified HDR backend must supply pixels converted to its chosen
encoding and separately establish compositor output support.

## HDR and format coverage

The worker converts source ICC or known CICP color descriptions into linear
sRGB. It uses 203 nits as reference white for PQ and the reference 1,000 nit
HLG OOTF. Reinhard luminance compression and gamut mapping produce SDR
presentation. For native HDR, a host must establish a verified end-to-end
presentation path. Debian 13's GTK 4.18 does not expose a public GDK signal
that proves HDR scanout for a surface. `AUTOMATIC` therefore selects
tone-mapped SDR for HDR sources. `SDR` explicitly selects that same
color-managed path. `HDR_PREFERRED` reports `HDR_UNAVAILABLE` and displays
the same SDR fallback. `NATIVE_HDR` is reserved for a verified path; a
floating-point texture alone is insufficient evidence. The view retains
the extended-range document. `ImageDocument.render_preview()` can tone-map
the retained float pixels in the worker without reopening the source.
Base preview, current animation frame and tiles share presentation decisions.
A material mode change cancels obsolete conversions and publishes the new
preview and tile policy together. Equivalent SDR policies reuse existing
textures. Monitor and surface changes re-evaluate the active output. Physical
HDR output remains unverified.

Use `open_frame()` or `ImageView.seek_frame()` for indexed frames.
`frame_timestamp_us` and `frame_duration_us` expose the native track timeline.
A loop count of zero means indefinite playback. A count of one plays once. GIF and WebP use the existing native decoders, and APNG compositing
requires Glycin. AVIF uses libavif 1.1+ when enabled. HEIF visual tracks require
the libheif sequence and edit-list APIs detected by Meson, available in the
tested Debian 13 security build (1.23.6). A compatible decoder plugin is
required for the track codec. HEIF chooses an image-sequence track before a
general video track. The public frame count and timestamps describe one
presentation cycle, and the GTK scheduler applies whole-cycle repeats.
Bounded unit-rate edit lists can trim and reassemble media intervals,
including transparent empty intervals. A finite repeat of the complete
media timeline can end in a clipped cycle when movie and track timescales
agree. Nonzero composition offsets, non-unit rates and incompatible repeat
patterns return a timeline diagnostic. Libheif handles frame emission and
duration within the supported cycle. Track scans, frame sizes and queued
previews are bounded. An HEIF input without a compatible track backend may
supply a still preview with a diagnostic.

| Format | Decode backend | Still | Animation / pages | Precision and HDR | ICC | Region | Thumbnail | Encode |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| JPEG | libvips JPEG | Yes | — | SDR; optional libuhdr gain map | Yes | — | Yes | JPEG, optional gain map |
| PNG / APNG | libvips PNG, optional Glycin | Yes | APNG with Glycin | 8/16-bit; decoder-specific color tags | Native ICC | — | Yes | PNG |
| GIF / WebP | libvips and optional Glycin | Yes | GIF and WebP | SDR | WebP where present | — | Yes | Installed GIF/WebP savers |
| TIFF | libvips TIFF | Yes | Pages | 16-bit and float HDR | Yes | Tiled | Yes | TIFF |
| JPEG 2000 | libvips JPEG 2000 loader | Conditional | — | High depth | Loader-dependent | — | Conditional | Installed saver |
| JPEG XL | libvips and libjxl | Conditional | Unverified | Float-aware HDR | Native ICC | — | Conditional | JPEG XL |
| HEIF / HEIC / AVIF | libvips, libheif plugins, optional libavif and libheif sequences | Conditional | Timed tracks; still collections where decoded | High depth, PQ/HLG and optional vendor gain maps | ICC or supported CICP | — | Conditional | Installed saver; gain maps depend on libuhdr |
| OpenEXR | Native OpenEXR | Yes | Multipart | Float HDR | Native chromaticities | Yes | Yes | OpenEXR |
| SVG / SVGZ | librsvg | Yes | — | Rasterized SDR | — | Yes | Yes | — |
| RAW families | LibRaw | Camera-dependent | — | Embedded preview or 16-bit development | Camera profile and native workflow | — | Camera-dependent | Developed image to installed savers |
| Radiance HDR | libvips Radiance loader | Conditional | — | Float HDR | — | — | Conditional | Installed saver |
| BMP, ICO, QOI, TGA, DDS, PNM and other loader families | Installed libvips/Glycin | Conditional | Per-file, unverified | Integer fallback; higher depth negotiated | Fallback color diagnostic | Crop via re-decode | Conditional | Installed savers only |

Conditional entries require the named installed backend and a compatible input.
`installed_codecs()` reports backend potential and `thumbnail_mime_types()`
lists configured thumbnail families. Inspect each file for actual capabilities.

### Physical HDR validation

On a Wayland compositor and monitor with HDR enabled, open the 10-bit PQ AVIF
and OpenEXR fixtures. Compare automatic, SDR and HDR-preferred policies, zoom
into tiles, move the window to an SDR monitor and back, and remap the window.
Check the diagnostics and compare measured output against known luminance
patches. For native HDR qualification, confirm the compositor's active HDR
surface color description, transfer function, monitor mode and measured
extended-range values. On GTK 4.18 the expected software result is
tone-mapped SDR in every mode for these sources, with `HDR_UNAVAILABLE` under
HDR-preferred. The hardware portion requires an HDR-capable Wayland session.

### Independent consumers

`examples/files-preview.vala` loads staged streams and bounded previews while
independently reading metadata and requesting thumbnails. It tests rapid
selection changes, cancellation, remapping and an unsupported selection.
`examples/photos-viewer.vala` exercises full-quality document loading,
zoom/pan, regions from a 200 MP TIFF, HDR page navigation and PNG export.
Both compile using the installed public VAPI:

```sh
valac --pkg majoris-image-gtk4-1 files-preview.vala -o files-preview
valac --pkg majoris-image-gtk4-1 photos-viewer.vala -o photos-viewer
./files-preview rgb16.png multipart.exr [camera.raw]
./photos-viewer large.tiff multipart.exr exported.png [camera.raw]
```

Run `tests/installed-consumers.sh` with the five installed packages, Xvfb,
Vala and Python GI. For the Photos workflow, place the generated
`build/tests/large.tiff` in the external fixture directory beside the other
inputs. The optional RAW input uses the same public viewer API.

## Thumbnail cache

Request 1–1024 pixels on the long edge, using the desired device-pixel size for
high-DPI clients. `generate_page()` includes the page in the cache key.
All applications share `$XDG_CACHE_HOME/majoris-image/thumbnails-v2`. Keys
use URI, file identity, ETag, size, modification and change times. Writes
use a private, same-directory staging file, flush it, rename it atomically,
and flush the directory. Corrupt PNG chunk CRCs trigger regeneration.
A cancellable advisory lock coordinates cache misses across processes.
4,096 stable lock stripes bound lock-file growth, with unrelated hashes
normally able to proceed in parallel. A lock holder rechecks the cache and
source stamp before generation and publication. Lock release on process exit
allows retries, and eviction respects active locks and reaps unpublished
staging files.

The desktop entry invokes the same service with `%u %o %s`. For sources that
provide modification timestamps, generated PNGs include the URI and mtime.
The desktop thumbnail manager owns placement and validation in its shared
thumbnail directories. The Debian package generates one system descriptor from the native
catalog plus the installed system Glycin loaders. File triggers on loader
configurations, decoder plugin directories and shared MIME definitions
refresh it during package transactions. Removal cleans up the generated
file. Standalone Meson installs ship a reproducible JPEG, PNG, TIFF and
OpenEXR baseline. The direct API also considers the current user's
Glycin configurations. A MIME entry describes a decoder family. The
thumbnailer validates the actual input, camera and color profile before
publication.
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
processing, and keep its edit history, thumbnail display, and catalog in the
application. Files can embed the view for its preview pane, pass a cancellable
per selection, and use the headless thumbnail and probe APIs from list views.
Headless consumers can link just the core and install the worker.

## Regression tests and measurements

`meson test -C build --print-errorlogs` runs native numerical color and EXR
checks, public headless and GTK consumers, and worker fault injection.
The 200 MP TIFF is generated at build time from an equivalent Photos
regression and exercises preview memory limits and a corner-region read.
The GUI tests use Xvfb when available. Native orientation tests use
asymmetric images with all eight EXIF orientations. For RAW coverage,
configure `-Draw_fixtures=/absolute/path/to/raw-only-directory` with licensed
camera inputs. The optional test checks preview and developed precision,
process options, thumbnails and the original file checksum. The
[raw.pixls.us catalog](https://raw.pixls.us/) supplies CC0 samples.
Physical HDR output and additional HEIF track codecs require external validation.

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
