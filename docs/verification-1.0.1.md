# Majoris Image 1.0.1 implementation and verification

| Finding | Change | Main files |
| --- | --- | --- |
| GTK lifecycle | Loads can complete while hidden. Remapping restores the viewport and resumes requested playback. Replacement and disposal invalidate generations, including selections made from a property notification. | `src/gtk/image-view.vala`, `src/gtk/tile-cache.vala`, `tests/viewer.vala` |
| HDR presentation | Retains an extended-range document. The view and its tiles use consistent SDR tone mapping when native HDR cannot be verified. Policy and surface changes update the presentation status and invalidate affected tiles. | `src/gtk/image-view.vala`, `src/gtk/tile-cache.vala`, `src/core/image-document.vala` |
| Glycin fallback | The registry negotiates fallback for compatible SDR images. It uses Glycin's Bubblewrap mode directly and checks the reported layout, precision, alpha, and bounds before converting to float RGBA. | `src/core/decoder-registry.vala`, `src/core/glycin-fallback.vala`, `src/core/glycin-pixels.c`, `tests/decoders.vala`, `tests/worker-fault.c` |
| Thumbnail concurrency | Cancellable advisory locks coordinate cache misses. A private staging file is flushed and atomically renamed, with source stamp checks, cache rechecks, and lock-aware eviction. | `src/core/thumbnail-service.vala`, `src/core/thumbnail-png.c`, `src/thumbnail/main.vala`, `tests/cache-processes.py` |
| Desktop registration | Generates the MIME list from native loaders and applicable installed Glycin configurations. HEIF/AVIF also check the decoder plugins. | `src/thumbnail/mime-registration.c`, `src/thumbnail/majoris-image.thumbnailer.in`, `src/thumbnail/meson.build`, `tests/thumbnail-registration.py` |
| RAW | Reports white-level-derived sensor precision separately from preview, developed, and buffer precision. Exposes LibRaw and EXIF orientation and keeps thumbnails, camera-WB processing, and development sandboxed. | `native/raw.c`, `src/worker/image-inspector.vala`, `src/worker/main.vala`, `src/core/decoder-backend.vala`, `src/core/image-document.vala`, `tests/raw-samples.vala` |
| Animation | Preserves playback intent and frame position through unmap/remap. GIF, WebP, and APNG exercise timing, loops, frame composition, transparency, and replacement. Timed AVIF sequences with only a still-image decode path carry a warning. | `src/gtk/image-view.vala`, `src/worker/image-inspector.vala`, `src/core/image-document.vala`, `tests/viewer.vala`, `tests/decoders.vala`, `tests/make-fixtures.c`, `tests/make-formats.py`, `tests/fixtures/avif-sequence.avif` |
| Debian | Produces the same five packages in both stock Debian and Majoris gain-map profiles. The latter requires vendor libuhdr. The installed-consumer test uses only public files and tools. | `debian/control`, `debian/rules`, `debian/changelog`, `tests/installed-consumers.sh` |

Build wiring changed in `meson.build`, `meson.options`, `src/core/meson.build`,
`src/gtk/meson.build` and `tests/meson.build`. Documentation and fixture handling
changed in `README.md`, `docs/api-1.md`, `tests/fixtures/README.md` and
`.gitattributes`.

## Public API

- `ImageDocument` adds `source_bit_depth`, `pixel_buffer_bit_depth`, `has_alpha`
  and `timed_sequence`.
- `ImageProbe` adds `source_bit_depth`, `has_alpha`, `embedded_preview` and
  `timed_sequence`.
- `ImageView` adds `current_frame` and `frame_duration_ms`.
- `PresentationStatus` appends `NATIVE_HDR` and `HDR_UNAVAILABLE`. Existing
  numeric values and the GtkBuilder GType are preserved.

Both SONAMEs remain at ABI 1. A dynamic symbol comparison against the 1.0.0
packages confirmed that both libraries still export every previous symbol.

## Verification

The complete Meson run passed 10 suites, including the optional camera suite.
The headless build with Glycin enabled and Ultra HDR disabled passed seven
suites. The GTK build with Glycin disabled and Ultra HDR enabled passed nine.
Introspection was enabled in both variants. The headless library linked
Glycin without linking GTK or GDK.

The RAW suite developed nine CC0 samples and extracted nine embedded previews.
It checked source checksums after preview, development and thumbnailing. The
samples covered Canon PowerShot G1 CRW, Canon EOS 350D CR2, Nikon D2H NEF,
Sony ILCE-7S ARW, Fujifilm FinePix S5000 RAF, Panasonic DMC-LX1 DNG,
Olympus E-10 ORF, Pentax K10D PEF and Ricoh GR DNG. The fixtures remain outside
the repository and were verified against [raw.pixls.us](https://raw.pixls.us/)
catalogue checksums. Synthetic TIFF fixtures separately checked all eight EXIF
orientations, including mirrored cases.

Both Debian `amd64` package profiles built with all nine default tests passing.
Each profile contains `libmajoris-image-1`, `libmajoris-image-gtk4-1`,
`majoris-image-worker`, `majoris-image-thumbnailer`, and
`libmajoris-image-dev`.
The stock Debian 13.7 chroot resolved dependencies from Debian, installed all
five packages, removed them and reinstalled them. The installed C GtkBuilder
consumer, Vala GTK demo, headless Vala consumer, introspection and thumbnailer
passed with the packaged Bubblewrap worker and an unprivileged consumer.
Lintian reported zero errors and nine advisory naming or GIR/typelib layout
warnings for each profile. The five requested package names and their
typelib layout were retained.

## Remaining external validation

Debian's GTK 4.18 surface APIs do not establish physical HDR scanout. Automatic
presentation uses SDR tone mapping, and HDR Preferred reports
`HDR_UNAVAILABLE` with the tone-mapped image. Physical HDR remains unverified.

The generated AVIF sequence decoded as a still preview through the installed
native fallback. The installed Glycin HEIF loader rejected the timed
sequence. Full AVIF/HEIF playback requires a decoder with sequence support.

The stock Debian package set omits gain maps. The full Majoris build passed
with the installed Majoris libuhdr 2.0.2 runtime and headers. Deploying that
profile on a clean target requires the Majoris repository to provide
`libuhdr2 (>= 2.0.2+majoris1)`. The stock Debian chroot confirmed that its
configured repositories could not resolve that vendor dependency.
