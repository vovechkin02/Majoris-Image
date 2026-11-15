# Majoris Image

Majoris Image is a Vala and C/C++ image framework for Debian 13 and Majoris OS.
Use the headless core to inspect, process, export, and thumbnail images. Embed
`MajorisImageView` in a GTK4 application to display them.

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
library requires the worker, Bubblewrap, and `prlimit`. Debian supplies
`prlimit` through its Essential util-linux package.

Build the five stock Debian 13 packages with `dpkg-buildpackage -us -uc -b`.
That package configuration uses Debian's runtime codecs, including libavif.
Add `-Ppkg.majoris-image.nosequence` for a still-codec build without that dependency.
Libheif track playback also requires its detected visual-track and loop-control APIs. For Majoris gain-map
decoding and encoding, provide the Majoris libuhdr development and runtime
packages and build with `dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b`.
Both builds use the same package names and SONAMEs. Choose the set appropriate
for the target system. Use a Linux filesystem for Debian packaging so the
executable mode on `debian/rules` and regular modes on debhelper files are
preserved.

Run `sh tests/installed-consumers.sh` with the installed development package,
Vala, a C compiler, Xvfb and Python GObject bindings to verify independent
consumer builds. To exercise licensed camera inputs, configure
`-Draw_fixtures=/absolute/path/to/raw-only-directory`.

See the [integration examples and ABI 1 capability matrix](docs/api-1.md) for formats,
sandbox requirements and integration examples. The [1.1.2 verification report](docs/verification-1.1.2.md) records the build, package and test results.

On Debian, the thumbnailer package generates its desktop entry from the
installed system codecs. Package triggers refresh it when loader packages
change. A standalone Meson installation registers the mandatory native
baseline. Query `DecoderRegistry.thumbnail_mime_types()` for direct thumbnail
requests, including codecs from the user's loader configuration.

Create a source release with Git metadata so Debian rules retain their
executable mode. From a committed release tree, use:

```sh
git -c tar.umask=0022 archive --format=tar.gz --prefix=majoris-image-1.1.2/ HEAD > majoris-image-1.1.2.tar.gz
tar -tvf majoris-image-1.1.2.tar.gz | grep 'debian/rules$'
```

Extract and build on a Linux filesystem that preserves executable permissions.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE) and [debian/copyright](debian/copyright).
The codec, colour, scheduling, worker, rendering, and test code adapted from
Majoris Photos retains its GPL-3.0-or-later licence and contributor attribution.
The generated regression images identified in
[tests/fixtures/README.md](tests/fixtures/README.md) are CC0-1.0.
Applications that distribute this library with their software should account
for the GPL terms when deciding how to distribute the combination.
