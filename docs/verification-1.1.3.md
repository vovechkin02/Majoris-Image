# Majoris Image 1.1.3 widget verification

| Area | Change | Main files |
| --- | --- | --- |
| Wheel input | Per-view default, Ctrl-only and disabled modes. Parent controllers receive unhandled events, including horizontal scrolling and attempts to zoom at a limit. Pointer anchoring uses the same transform as pinch and keyboard zoom. | `src/gtk/image-view.vala`, `tests/widget-interaction.c` |
| Animation | Playback starts only for confirmed multi-frame documents. GIF, WebP, AVIF, HEIF and APNG use indexed document frames, the common presentation policy and one-frame prefetch. Glycin composites APNG from frame zero. With an unavailable backend or incompatible colour requirement, the widget presents a still image and a diagnostic. | `src/core/{image-document,glycin-fallback}.vala`, `src/gtk/image-view.vala`, `tests/{viewer.vala,make-fixtures.c,make-formats.py}` |
| External textures | The original texture and its GDK colour state stay intact. The view distinguishes sRGB, linear sRGB, Rec. 2100 PQ, linear Rec. 2100 and other states. It uses an unverified presentation status where the external state cannot establish SDR. | `src/gtk/{image-view.vala,presentation.vala,image-texture.c}`, `tests/{image-texture.c,widget-interaction.c}` |
| Accessibility | GTK image role, loading busy state, application-provided per-image description, and standard persistent GTK labels. Keyboard actions respect interaction permissions, with focus styling left to GTK. | `src/gtk/image-view.vala`, `tests/widget-interaction.c`, `examples/{builder.c,demo.ui}` |
| HDR readiness | The internal factory takes prepared straight-alpha float32 samples and their explicit colour state. It checks dimensions, stride and size. The current preview, tile and animation path continues to provide linear sRGB. | `src/gtk/{image-texture.c,image-texture.h,presentation.vala}`, `tests/image-texture.c` |
| Consumer integration | Files embeds a Ctrl+wheel view in a scrolling pane. Photos uses default wheel zoom. The examples exercise image replacement, descriptions, rotation, external textures, wide-gamut content, animation and the existing public workflow. Installed C, Vala and Python GI checks cover the public interfaces. | `examples/{files-preview.vala,photos-viewer.vala,builder.c,demo.ui}`, `tests/{meson.build,installed-consumers.sh,viewer.vala}` |

Public additions are `ScrollZoomMode`, `ImageView.scroll_zoom_mode`,
`ImageView.accessible_description` and
`PresentationStatus.EXTERNAL_UNVERIFIED`. The new presentation value is
appended after the existing ones. GtkBuilder uses the enum nicknames
`always`, `control` and `disabled`. Existing SONAMEs remain ABI 1.
The GTK library now delegates Glycin frame handling to Core and no longer
links the Glycin GTK bridge. XTest is used solely by the test executable.
See the [ABI 1 guide](api-1.md) for XML and Vala usage,
shortcuts, texture contracts and a physical HDR procedure.

The two Xvfb tests send synthetic X11 events through GTK at monitor scale
factors 1 and 2. They measure a parent scroll adjustment, verify focus
and parent key delivery, compare pointer-anchored pixels after panning
and rotating/flipping, and exercise independent Ctrl-only, default and
disabled viewers. The desktop accessibility backend used GTK's test
interface. Physical touchpad and screen-reader behaviour still require
testing in the target desktop session.

GTK 4.18 uses colour-managed SDR presentation for HDR documents. The PQ,
linear Rec. 2100, linear sRGB and Display P3 texture checks compare the
declared state and downloaded sample values. They do not measure monitor
output. A verified native HDR path still requires platform support.
[GTK surface API](https://docs.gtk.org/gdk4/class.Surface.html)

Build metadata was adjusted in `meson.build`, `src/gtk/meson.build`,
`debian/control` and `debian/changelog`. The [1.1.2 report](verification-1.1.2.md)
documents the earlier decoder and deployment work.

## Validation

The builds ran in Debian WSL with GTK 4.18.6 and Glycin 1.2.1.

| Configuration | Commands | Result |
| --- | --- | --- |
| Full GTK, optional codecs, nine camera RAW inputs | `meson setup --reconfigure /tmp/majoris-image-build "$PWD"`, `meson compile -C /tmp/majoris-image-build -j 8`, `meson test -C /tmp/majoris-image-build --print-errorlogs` | 16 passed. |
| Headless with Glycin and sequences enabled, Ultra HDR disabled | `meson compile -C /tmp/majoris-image-headless-110 -j 5`, `meson test -C /tmp/majoris-image-headless-110 --print-errorlogs` | 7 passed. `ldd` verified that Core does not link GTK or GDK. |
| GTK with Glycin and sequences disabled, Ultra HDR enabled | `meson compile -C /tmp/majoris-image-native-110 -j 5`, `meson test -C /tmp/majoris-image-native-110 --print-errorlogs` | 15 passed. |
| Stock Debian | `dpkg-buildpackage -us -uc -b` | Five amd64 packages, 15 tests passed. |
| Majoris gain-map | `dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b` | Five amd64 packages, 15 tests passed. |

The GTK suite verifies static PNG and WebP, GIF, WebP animation, APNG
with and without a Display P3 ICC profile, indexed HEIC and AVIF, and
10-bit HDR AVIF animation through the SDR policy. It also checks source
replacement, finite/infinite playback, cancellation, seeking, unmap/remap,
tiled rendering, and the Files and Photos public workflows. The isolated
input tests verify the view can accept its retained external texture again
and releases the texture when replaced.

An exported-symbol comparison with the 1.1.2 packages using
`nm -D --defined-only` found every previous symbol in both libraries.
The GTK package records its GTK 4.16 minimum and has no Glycin GTK bridge
runtime dependency.

The stock packages passed installation, removal and reinstallation in
isolated Debian 13.7. The unprivileged installed check ran
`sh tests/installed-consumers.sh`, compiling C and Vala against the installed
public interfaces and checking the new enum, properties and image role in
Python GI. The synthetic input test was also compiled outside the source
build with `pkg-config --cflags --libs majoris-image-gtk4-1 gtk4-x11 xtst x11`.
Both `GDK_SCALE=1` and `GDK_SCALE=2` passed under Xvfb.

The vendor profile passed the installed C, Vala and Python GI checks with
the Majoris `libuhdr2` binary in the same isolated Debian environment.
An installed public-API round trip exported and reopened an 8×8 JPEG gain map.
Lintian returned zero errors and nine package naming, GIR and typelib
warnings per profile. Artifacts are under `build-packages/1.1.3/`.
