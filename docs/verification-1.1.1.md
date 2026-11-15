# Majoris Image 1.1.1 verification

| Finding | Implementation and verification | Main files |
| --- | --- | --- |
| HDR output | The output detector distinguishes unrealized surfaces, known SDR renderers, unknown output and verified HDR. GTK 4.18 supplies surface, monitor and renderer information. Public GDK through the examined 4.24 API supplies no surface HDR negotiation query. Xvfb tested the actual SDR fallback. A policy test exercised every output classification, including simulated verified output. | `src/gtk/presentation.vala`, `src/gtk/image-texture.c`, `tests/presentation.vala` |
| Presentation regeneration | Independent presentation generations cancel stale conversions. Base previews, tiles and the active animation frame use the same tone-map decision. Equivalent policies preserve existing textures. The viewer retains navigation and playback state. Headless tests tone-mapped retained pixels after deleting the staged source and compared preview, full decode and tile luminance and alpha. GTK tested policy changes across remapping. | `src/gtk/image-view.vala`, `src/gtk/tile-cache.vala`, `src/core/{image-document,worker-client}.vala`, `src/worker/main.vala`, `tests/{viewer,headless}.vala` |
| Glycin colour | The worker validates colour declarations. A validated PNG sRGB declaration without competing ICC or CICP metadata establishes the Glycin 1 PNG output contract and satisfies strict colour requests. ICC conversion, assumed defaults and missing evidence carry distinct provenance. RGB and grayscale conversion use the working float buffer and keep alpha independent. A numeric test forced Glycin for sRGB, Display P3, Adobe RGB and grayscale ICC PNG. Tests reject malformed ICC, unsupported CICP and strict requests without an established contract. | `src/core/glycin-fallback.vala`, `src/worker/metadata-reader.vala`, `tests/{decoders.vala,make-formats.py}` |
| HEIF timeline | The worker selects an image track before a general video track, starts with libheif's presentation classification and factors out recognized whole-cycle repeats for GTK. It rejects unsupported edit lists, playback rates and composition offsets. Tests used finite and indefinite repeats, two visual tracks, malformed duration, non-unit rate, media offset and a nonzero composition offset. The AVIF and HEIC fixtures retained 70 ms and 160 ms frame durations. The 10-bit PQ AVIF test continued to verify HDR and SDR pixels. | `native/sequences.c`, `tests/{decoders.vala,make-formats.py,make-heif-sequence.c}`, `tests/fixtures/` |
| Desktop registration | Packages consistently register JPEG, PNG, TIFF and OpenEXR from the native catalogue. Runtime API discovery includes applicable optional loaders. The desktop test checks the baseline against the runtime catalogue and exercises generated sample thumbnails. | `native/{bridge.h,codec-catalogue.c}`, `src/thumbnail/{mime-registration.c,meson.build}`, `debian/control`, `tests/thumbnail-registration.py` |
| Consumers | Files tests add a bounded 200 MP TIFF preview, ICC PNG and AVIF/HEIC seeks with remapping. Photos tests add ICC PNG, indexed animations and RAW development in the configured real-camera run. Both examples use public Vala interfaces and XML GtkBuilder construction. | `examples/{files-preview,photos-viewer}.vala`, `tests/{meson.build,installed-consumers.sh,installed-gainmap.vala}` |

The additive public API is `ImageDocument.render_preview(bool tone_map, Cancellable?)`.
It returns retained pixels directly when the requested representation is
available and uses the sandboxed tone mapper on the in-memory preview
otherwise. Metadata from Glycin now carries decoder-output colour, working
colour and provenance. Both libraries retain ABI 1. `nm -D --defined-only`
on the stock 1.1.0 and 1.1.1 libraries confirmed that all existing exports
are present. Release metadata and deployment guidance changed in
`meson.build`, `debian/changelog`, `README.md` and `docs/api-1.md`.
[ABI and format guide](api-1.md)

### Security and platform boundaries

- Sequence traversal stays in Bubblewrap. The HEIF reader bounds pixels and
  samples before decoding; file offsets, box counts and timing metadata are
  checked before seeking or allocating. The worker retains its memory, file,
  process and time limits. Sequence fixture mutations exercise the explicit
  error paths. The installed Debian 13 security build is
  `1.23.6-2~deb13u1`. Upstream 1.23.7 contains additional security fixes
  and was published in Debian unstable at the time of this validation.
  [Debian libheif tracker](https://tracker.debian.org/pkg/libheif/news/) ·
  [Upstream security release](https://github.com/strukturag/libheif/releases/tag/v1.23.7)
- Nonzero composition offsets, non-unit rates, fractional repetitions and
  edit-list arrangements that libheif cannot interpret are unsupported.
  HEVC sequences and primary image track selection were tested. Other track
  codecs, arbitrary edit arrangements and HEIC alpha/HDR combinations remain
  independently unverified.
- The Glycin 2.2 colour interfaces belong to a separate ABI and are
  documented as unstable. The Debian 13 build uses Glycin 1.2.1. ICC output
  from that ABI remains unverifiable through its C interface. The tested
  strict fallback case is explicitly tagged sRGB PNG. [Glycin C API](https://gnome.pages.gitlab.gnome.org/glycin/libglycin/index.html)
- SDR and extended preview textures both use straight-alpha float data in
  linear sRGB primaries. PQ, HLG and wide-gamut conversions happen in the
  core before creation of the GDK texture. Physical HDR is unverified.
  GTK uses SDR tone mapping for HDR sources on the tested platform.
  [GDK surface API](https://docs.gtk.org/gdk4/class.Surface.html)

### Build matrix

Commands ran from the source directory in Debian WSL with GTK 4.18.6,
Glycin 1.2.1, libheif 1.23.6 and libavif 1.2.1.

| Profile | Commands | Result |
| --- | --- | --- |
| Full | `meson setup --reconfigure /tmp/majoris-image-build "$PWD"`, `meson compile -C /tmp/majoris-image-build -j 6`, `meson test -C /tmp/majoris-image-build --print-errorlogs` | 13 passed, including the nine camera RAW inputs and both independent workflow tests. |
| Headless | Existing build configured with `-Dgtk=false -Dglycin=enabled -Dultrahdr=disabled -Dsequence=enabled`; `meson compile -C /tmp/majoris-image-headless-110 -j 5`, `meson test -C /tmp/majoris-image-headless-110 --print-errorlogs` | 7 passed. Core's `ldd` output showed no GTK or GDK. |
| Native | Existing build configured with `-Dglycin=disabled -Dultrahdr=enabled -Dsequence=disabled`; `meson compile -C /tmp/majoris-image-native-110 -j 5`, `meson test -C /tmp/majoris-image-native-110 --print-errorlogs` | 12 passed. Native animation and still-image paths were exercised. |
| Stock Debian | `dpkg-buildpackage -us -uc -b` | Five amd64 packages, 12 tests passed. |
| Majoris gain-map | `dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b` | Five amd64 packages, 12 tests passed. |

Both package builds enabled introspection. Each produced
`libmajoris-image-1`, `libmajoris-image-gtk4-1`,
`majoris-image-worker`, `majoris-image-thumbnailer`, and
`libmajoris-image-dev`. Artifacts are under `build-packages/1.1.1/`.

### Installed verification

The stock packages passed install, removal and reinstall in isolated
Debian 13.7, running `sh tests/installed-consumers.sh` as an unprivileged
user against the public packages. That script exercised headless Vala, C
GtkBuilder, Vala GTK examples, independent workflows, Python introspection
and the desktop thumbnailer.

A second installed run removed `glycin-loaders` and masked the libheif plugin
directory in the test namespace. Debian's libheif package depends on the
plugins, so masking tests runtime absence while keeping the dependency
graph installed. The consumer suite passed with the plugins masked and
again after restoring the loaders. The freedesktop descriptor had the same
SHA-256 before and after both changes.

The Majoris profile was installed separately in the same isolated Debian
release. The vendor runtime `libuhdr2 2.0.2+majoris1` was repacked from its
existing Debian WSL installation for this test. The public consumer suite
passed, including a JPEG gain-map export and decode against the installed
worker. The round trip used the opaque 10-bit HDR AVIF fixture. The procedure
compiled `valac --pkg majoris-image-1 tests/installed-gainmap.vala -o gainmap-check`
and ran `./gainmap-check avif-hdr-sequence.avif vendor-gainmap.jpg`. This validates
the vendor binary on Debian 13.7. A production Majoris OS deployment remains
external validation.

`lintian --fail-on error --tag-display-limit 0` reported zero errors and
nine warnings per profile. They concern package and GIR naming, typelib
sections, and GIR dependency metadata.

[Previous release verification](verification-1.1.0.md)
