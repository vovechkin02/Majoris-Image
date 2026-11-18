# Majoris Image

Majoris Image is a Vala and C/C++ image framework for Debian 13 and Majoris OS.
Use the headless core to inspect, process, export, and thumbnail images. Embed
`ImageKitView` in a GTK4 application to display them.

## Build and test

Install the build dependencies in [debian/control](debian/control), then run:

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

Build without GTK using `-Dgtk=false`. Use `-Dultrahdr=enabled` to require
libuhdr, `-Dglycin=disabled` to omit Glycin fallback and APNG compositing,
`-Dsequence=disabled` to omit optional AVIF/HEIF track decoding, and
`-Dintrospection=enabled` to require typelib compilation.

The libraries use a Bubblewrap worker. Running an uninstalled consumer requires
the absolute path to its worker:

```sh
MAJORIS_IMAGE_WORKER="$PWD/build/src/worker/majoris-image-worker" \
  build/examples/majoris-image-demo
```

On an installed system, the core finds the worker in Meson's configured
`libexecdir`. Use `meson install -C build` for a local install. The headless
library requires the worker, Bubblewrap 0.10.0 or later, and `prlimit`. Debian supplies
`prlimit` through its Essential util-linux package.

Build the five stock Debian 13 packages with `dpkg-buildpackage -us -uc -b`.

The ABI 2 runtimes are `libmajoris-image2` and `libmajoris-image-gtk4-2`.
Install these with the matching `libmajoris-image-dev`, `majoris-image-worker`,
and `majoris-image-thumbnailer` packages. Reinstall the development package
from this build if an earlier 2.0.0 build used the old runtime package names.
The new runtimes replace the overlapping files in old-name 2.0.0 packages.

ABI 1 applications need the original ABI 1 runtimes. Their SONAMEs and
typelibs use distinct paths, but the ABI 1 Core package pins the single
worker package to its matching version. A normal installation therefore
requires a coordinated upgrade of ABI 1 consumers and the worker. For
applications that must still use ABI 1, keep the matching package set in
a separate installation.

Publishing Debian packages is blocked until the Majoris release owner
supplies a monitored maintainer address to replace
`packages@majoris.invalid` in `debian/control` and the current changelog
entry.
That package configuration uses Debian's runtime codecs, including libavif.
Add `-Ppkg.majoris-image.nosequence` for a still-codec build without that dependency.
Libheif track playback also requires its detected visual-track and loop-control APIs. For Majoris gain-map
decoding and encoding, provide the Majoris libuhdr development and runtime
packages and build with `dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b`.
Both builds use the same package names and SONAMEs. Choose the set appropriate
for the target system. Use a Linux filesystem for Debian packaging so the
executable mode on `debian/rules` and regular modes on debhelper files are
preserved.

Run `python3 tests/release/installed_consumers.py /absolute/build/tests/large.tiff` with the installed development package,
Vala, a C compiler, Xvfb and Python GObject bindings to verify independent
consumer builds. To exercise licensed camera inputs, configure
`-Draw_fixtures=/absolute/path/to/raw-only-directory`.

See the [integration examples and ABI 2 capability matrix](docs/api-2.md) for formats,
sandbox requirements and integration examples. Version 2.0.1 adds lossless JPEG
quarter-turn export; see the [2.0.1 feature verification](docs/verification-files-lossless-jpeg.md).
The [2.0.0 verification report](docs/verification-2.0.0.md) remains the historical
baseline for that release.

On Debian, the thumbnailer package generates its desktop entry from the
installed system codecs. Package triggers refresh it when loader packages
change. A standalone Meson installation registers the mandatory native
baseline. Query `DecoderRegistry.thumbnail_mime_types()` for direct thumbnail
requests, including codecs from the user's loader configuration.

Create a source release with Git metadata so Debian rules retain their
executable mode. From a committed release tree, use:

```sh
git -c tar.umask=0022 archive --format=tar.gz --prefix=majoris-image-2.0.1/ HEAD > majoris-image-2.0.1.tar.gz
python3 tests/release/check_release_modes.py majoris-image-2.0.1.tar.gz
```

The check verifies both executable scripts in Git and after tar extraction.
Extract and build on a Linux filesystem that preserves executable permissions.

The view requests the entire visible image, starting at the center, within
a bounded tile cache. Adjacent image preloads use the same decoder selection
as foreground opens, including Glycin, at background priority.

The GtkBuilder demo uses ordinary wheel zoom. The Files-style example embeds
the view in a scrolling pane and assigns Ctrl+wheel zoom. The Photos-style
example uses the default wheel policy. The [widget guide](docs/api-2.md)
documents shortcuts, per-image accessible descriptions, animations and
external texture reporting.

See the [test guide](docs/TESTING.md) for isolated, extended, sanitizer, benchmark,
and release qualification workflows.
The [final closure report](docs/verification-release-2.0.md) records the current
source-validation fix, strict-build blockers, and release qualification status.
The [GoF and Vala v1.6 adoption report](docs/verification-gof-vala.md) records
private backend routing, declaration policy, compatibility evidence, and bounded
verification exceptions.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [debian/copyright](debian/copyright).
The codec, color, scheduling, worker, rendering, and test code adapted from
Majoris Photos retains its GPL-3.0-or-later license and contributor attribution.
The generated regression images identified in
[tests/fixtures/README.md](tests/fixtures/README.md) are CC0-1.0.
Applications that distribute this library with their software should account
for the GPL terms when deciding how to distribute the combination.
