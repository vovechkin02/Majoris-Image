# Majoris Image 1.1.0 implementation and verification

See [1.1.1 verification](verification-1.1.1.md) for the current release.

| Area | Change | Main files |
| --- | --- | --- |
| Decoder selection and color | Discover compatible Glycin 1 loaders in XDG data roots. Negotiate by content, installed MIME and requested capabilities. Probe ICC, CICP and source precision in the native sandbox. Use linear-light area filtering, alpha-weighted accumulation and re-decode for larger crops. General rendering uses the same fallback. | `native/glycin-discovery.{c,h}`, `src/core/{decoder-registry,glycin-fallback,image-document,render-options}.vala`, `src/core/glycin-pixels.c`, `src/worker/{metadata-reader,image-processor,main}.vala`, `native/{metadata,pixels}.c` |
| Timed images | Read AVIF tracks with libavif and compatible visual tracks with libheif. Preserve effective sample depth, ICC or supported CICP, alpha, frame timings and loop count. The same widget scheduler handles native and Glycin frames. | `native/sequences.c`, `native/color.c`, `src/worker/image-inspector.vala`, `src/core/{image-document,worker-request,worker-client,worker-failure}.vala`, `src/gtk/image-view.vala` |
| Thumbnail registration | Share decoder discovery between the registry and the desktop generator. Test actual sample PNG output, runtime catalog agreement, disabled Glycin and simulated loader and plugin availability. | `native/codec-catalog.c`, `src/core/decoder-registry.vala`, `src/thumbnail/{meson.build,mime-registration.c}`, `tests/integration/thumbnail_registration.py` |
| HDR presentation | Select the presentation policy through a dedicated backend using realized renderer and mapped surface state. Preserve the HDR document. Use color-managed SDR for GTK 4.18 previews, animation and tiles. Test policy decisions with simulated output states and the visible widget in Xvfb. | `src/gtk/{presentation.vala,image-view.vala,image-texture.c,image-texture.h}`, `tests/unit/presentation.vala`, `tests/integration/viewer.vala` |
| Independent consumers | Exercise bounded Files selections from streams, concurrent thumbnails, metadata, cancel/remap and error handling. Exercise Photos regions from a 200 MP TIFF, zoom, pan, HDR page navigation and export. The C example opens asynchronously through GtkBuilder and requests metadata, a full-quality region and a thumbnail. | `examples/{files-preview.vala,photos-viewer.vala,builder.c,meson.build}`, `tests/release/installed_consumers.py`, `debian/libmajoris-image-dev.examples` |

Public additions include `COLOR_MANAGED`, probe and document color diagnostics,
microsecond frame timestamps and durations, `DecodeOptions.allow_regions`,
`RenderOptions.nearest_neighbor`, `DecoderRegistry.thumbnail_mime_types()`,
`ImageView.load_source()` and `ImageView.seek_frame()`. Both libraries retain
ABI 1. The development package still provides headers, VAPI, GIR and typelibs.
A dynamic symbol comparison used the 1.0.1 Debian packages as the baseline.

Build and bridge changes are in `meson.build`, `meson.options`,
`native/{bridge.h,meson.build}`, `vapi/{image-native,image-vips}.vapi`,
`src/{core,gtk}/meson.build`, and `debian/{control,rules,changelog}`.
Test and fixture changes are in `tests/integration/decoders.vala`, `tests/support/make-fixtures.c`, `tests/support/make_formats.py`, `tests/support/make-heif-sequence.c`, `tests/meson.build`, `tests/security/worker-fault.c` and
`tests/fixtures/`. The [ABI 1 guide](api-2.md) describes format availability,
processing limits, integration examples and the physical HDR procedure.
[Verification for 1.0.1](verification-1.0.1.md) records the prior release.

## Tests and commands

Run Meson from the source directory. These validation commands used Debian 13
under WSL with GTK 4.18.6, Glycin 1.2.1, libavif 1.2.1 and Debian's security
libheif 1.23.6. The generated fixture tests ran in Bubblewrap.

| Configuration | Commands | Result |
| --- | --- | --- |
| Full build and nine licensed camera inputs | `meson setup --reconfigure /tmp/majoris-image-build "$PWD"`, `meson compile -C /tmp/majoris-image-build -j 6`, `meson test -C /tmp/majoris-image-build --print-errorlogs`, with `raw_fixtures=/tmp/majoris-raw-fixtures.sAAnY8` configured | 13 passed |
| Headless, Glycin and sequences enabled, Ultra HDR disabled | `meson setup /tmp/majoris-image-headless-110 -Dgtk=false -Dglycin=enabled -Dultrahdr=disabled -Dsequence=enabled`, `meson compile -C /tmp/majoris-image-headless-110 -j 5`, `meson test -C /tmp/majoris-image-headless-110 --print-errorlogs` | 7 passed. `ldd` showed no GTK or GDK dependency for the core. |
| GTK, Glycin and sequences disabled, Ultra HDR enabled | `meson setup /tmp/majoris-image-native-110 -Dglycin=disabled -Dultrahdr=enabled -Dsequence=disabled`, `meson compile -C /tmp/majoris-image-native-110 -j 5`, `meson test -C /tmp/majoris-image-native-110 --print-errorlogs` | 12 passed. This includes native GIF/WebP playback and the optional AVIF still path. |
| Stock Debian package build | `dpkg-buildpackage -us -uc -b` | 12 passed and five amd64 packages built. |
| Majoris gain-map package build | `dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b` | 12 passed and five amd64 packages built. |

The sequence regressions decoded indexed red and green frames and asserted
70 ms and 160 ms durations. They checked indefinite AVIF playback, finite
HEIC playback, the second frame's timestamp, 10-bit BT.2020 PQ output above
SDR reference white, and a tone-mapped version of that frame. GTK tests
covered seeking, pause/resume, remap and source replacement.

Decoder tests forced Glycin for BMP, ICO, TGA, DDS, PGM and QOI. They checked
decoded dimensions and values, alpha-weighted QOI reduction, nearest sampling,
crop re-decode and the transformed fallback path. Native and fallback 16-bit
Display P3 PNG results were compared with linear reference values. A 16-bit
TIFF exercised the native ICC path. The malformed ICC fixture and strict
color requests exercised explicit errors. JPEG 2000 used the native
thumbnail path. The Files workflow repeated selection and thumbnail requests
24 times and the cross-process thumbnail tests exercised shared publication.

## Installed packages

Each profile produced `libmajoris-image-1`, `libmajoris-image-gtk4-1`,
`majoris-image-worker`, `majoris-image-thumbnailer` and
`libmajoris-image-dev`. Artifacts are in `build-packages/1.1.0/debian-13`
and `build-packages/1.1.0/majoris-gainmap`.

The stock profile was installed, removed and reinstalled in an isolated
Debian 13.7 filesystem using Debian's runtime packages. The unprivileged
runner checked the installed libraries after each installation. It ran the
C and Vala GTK examples,
both independent workflows, the headless Vala program, Python introspection,
and the desktop thumbnailer. The installed tests used generated fixture
files and public development interfaces.

`lintian --fail-on error --tag-display-limit 0` returned zero errors and nine
warnings for each package set. The warnings concern the required package
names, GIR naming and dependencies, and the typelib package sections.

## Remaining external validation

- Physical HDR output still requires a compositor, monitor and a public
  presentation path that can verify HDR for the active surface. The software
  tests use Xvfb and simulated policy inputs. GTK 4.18 uses tone-mapped SDR
  for HDR sources. [GDK surface API](https://docs.gtk.org/gdk4/class.Surface.html)
- The clean install covered Debian's stock codec profile. The Majoris
  packages require the Majoris vendor `libuhdr2` runtime and were built and
  tested in a vendor-library build environment. A clean target-distribution
  install with that runtime remains to be verified.
- HEIC playback was tested with an HEVC track and libde265. Additional track
  codecs, edit-list patterns, and hardware-specific configurations need
  independent media and output validation.
- Glycin 1 can apply source ICC internally. Its C API does not expose the
  resulting color state. The fallback explicitly rejects strict color
  requests and detected unsupported transfers. Some installed loaders have
  only representative fixtures, so family registration remains conditional
  on the individual input and loader.
