# Majoris Image 1.1.2 verification

| Area | Changes | Main files |
| --- | --- | --- |
| Desktop thumbnails | Debian creates the system descriptor from the installed native catalogue and system Glycin loaders. Codec package transactions refresh it atomically. Package removal cleans it up. The build installs a reproducible baseline for standalone Meson deployments. The thumbnailer continues to validate each source and share the existing cache. | `src/thumbnail/{meson.build,mime-registration.c}`, `native/codec-catalogue.c`, `debian/majoris-image-thumbnailer.*`, `debian/rules`, `tests/thumbnail-registration.py` |
| Colour | The sandboxed header probe validates the QOI colour declaration. Glycin fallback satisfies strict colour requests for sRGB QOI and PNG. Linear QOI preserves its numerical transfer and reports the default primaries assumption. Alpha remains independent of colour conversion. | `src/worker/metadata-reader.vala`, `src/core/{glycin-fallback.vala,glycin-pixels.c}`, `tests/decoders.vala` |
| HEIF | A bounded timeline maps unit-rate edit segments to the frames emitted by libheif. It supports clipped frames, multiple media segments, transparent empty intervals and a clipped final cycle for finite whole-media repeats with matching timescales. Existing complete and infinite repeats remain delegated to libheif and the viewer. | `native/sequences.c`, `tests/{decoders.vala,make-formats.py}` |
| HDR | The existing output probe keeps HDR display unavailable until the compositor path can be verified. A GDK regression downloads a floating-point texture in the same colour state and verifies extended samples and straight alpha. | `tests/image-texture.c`, `tests/meson.build`, `docs/api-1.md` |
| Integration and security | The existing public consumers retain their independent viewer and worker lifetimes. The installed stress test alternates sources while a separate process requests thumbnails and checks the private temporary directory after both finish. Timeline arithmetic and allocations have fixed budgets and remain in Bubblewrap. | `examples/files-preview.vala`, `tests/installed-consumers.sh`, `native/sequences.c` |
| Distribution | Git records executable Debian rules. Source tarballs retain the mode through extraction. Both package profiles keep the same ABI and package names. | `debian/{rules,changelog,control}`, `meson.build`, `README.md` |

The [ABI 1 guide](api-1.md) includes the format matrix and a physical HDR
procedure. The [1.1.1 verification](verification-1.1.1.md) gives background
on ICC, animation, and presentation policies. This release makes no public
API or SONAME changes.

## Compatibility boundaries

- System thumbnail registration follows installed decoder families. On the
  tested stack these include the mandatory JPEG, PNG, TIFF and OpenEXR families
  and optional native or Glycin WebP, GIF, AVIF, HEIF, JPEG XL, JPEG 2000, SVG,
  BMP, ICO, QOI, TGA, PNM and RAW families. Consult the installed descriptor for
  exact MIME identifiers. Runtime validation still checks individual inputs.
  [Debian trigger semantics](https://manpages.debian.org/trixie/dpkg-dev/deb-triggers.5.en.html)
- QOI 8-bit sRGB/RGBA and linear RGBA were checked numerically. The earlier
  ICC fixtures continue covering Display P3, Adobe RGB, grayscale, 16-bit
  PNG, TIFF, invalid profiles and transparency. Glycin 1.2.1 does not expose
  independently attestable ICC output. ABI 2.2 is separate and its development
  library is unavailable in the tested Debian 13 environment.
  [QOI specification](https://qoiformat.org/qoi-specification.pdf) ·
  [Glycin ABI 2 documentation](https://gnome.pages.gitlab.gnome.org/glycin/libglycin/index.html)
- HEIF composition offsets and playback rates other than one still produce
  explicit errors. The bounded mapper uses sample durations only when the
  container declares no nonzero composition offsets. Other HEIF codecs and
  combined HDR/auxiliary-alpha cases require independent media validation.
  Debian provided security build `1.23.6-2~deb13u1` for this test.
- Public GDK through the examined 4.24 API offers surface and monitor information
  without a verified surface HDR negotiation query. The tested GTK 4.18 path
  presents colour-managed SDR. Physical HDR and the production Majoris OS
  image require external validation. [GDK surface API](https://docs.gtk.org/gdk4/class.Surface.html)

## Validation

The build ran in Debian WSL with GTK 4.18.6, Glycin 1.2.1, libheif
1.23.6 and libavif 1.2.1.

| Configuration | Commands | Result |
| --- | --- | --- |
| Full, with nine camera RAW fixtures | `meson setup --reconfigure /tmp/majoris-image-build "$PWD"`, `meson compile -C /tmp/majoris-image-build -j 6`, `meson test -C /tmp/majoris-image-build --print-errorlogs` | 14 passed. |
| Headless with Glycin and sequences, Ultra HDR disabled | `meson compile -C /tmp/majoris-image-headless-110 -j 6`, `meson test -C /tmp/majoris-image-headless-110 --print-errorlogs` | 7 passed. `ldd` found no GTK/GDK dependency in Core. |
| GTK with Glycin and sequences disabled, Ultra HDR enabled | `meson compile -C /tmp/majoris-image-native-110 -j 6`, `meson test -C /tmp/majoris-image-native-110 --print-errorlogs` | 13 passed. |
| Stock Debian source extraction | `dpkg-buildpackage -us -uc -b` | Five amd64 packages, 13 tests passed. |
| Majoris gain-map source extraction | `dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b` | Five amd64 packages, 13 tests passed. |

Package builds used `-Dintrospection=enabled`. Dynamic symbol comparisons
of the 1.1.1 and 1.1.2 packages with `nm -D --defined-only` found every
previous export in both libraries.

The HEIF tests compare a 20 ms transparent interval, a 35 ms clipped red
frame, and a 60 ms clipped green frame. A separate finite repeat test checks
a 40 ms red frame at 230 ms, after one full cycle. Whole-cycle and infinite
repeat, AVIF PQ and malformed timing tests also passed.

`git ls-files -s -- debian/rules` reported mode `100755`. The source archive
was made with `git -c tar.umask=0022 archive --format=tar.gz` using an isolated
Git index. `tar -tvf` reported executable rules, and `stat -c '%a' debian/rules`
reported `755` in both independent Linux extractions before package builds.
The stock binaries, vendor binaries and source tarball are under
`build-packages/1.1.2/`.

The stock packages passed clean dependency resolution, installation,
removal and reinstallation in isolated Debian 13.7. The public installed
suite exercised GtkBuilder, Vala and C consumers, Python introspection,
the Bubblewrap worker and desktop thumbnail generation. With
`MAJORIS_TEST_STRESS=1 sh tests/installed-consumers.sh`, an independent
Files preview process ran 96 selections while another process made 24
thumbnail requests. Both completed and the dedicated temporary directory
was empty afterward. Removing and reinstalling `glycin-loaders` through APT
removed and restored QOI desktop registration automatically. The restored
descriptor was byte-identical.

The vendor profile passed the same installed consumer suite. It used the
existing Majoris `libuhdr2 2.0.2+majoris1` binary, repacked from WSL into the
isolated Debian 13.7 environment. The installed encoder exported an HDR
JPEG gain map from the opaque 10-bit AVIF fixture, and the installed worker
reopened and tone-mapped it. This test did not boot a Majoris OS image.

`lintian --fail-on error --tag-display-limit 0` returned zero errors and
nine warnings per profile. The warnings concern the existing SONAME and
GIR package names, typelib sections and Lintian's expected typelib package
relationships. The development package depends on the version-matched
runtime packages that ship those typelibs. Installed Python GI imports
passed. The GTK development dependency now records its minimum supported
version, 4.16.
