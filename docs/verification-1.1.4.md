# Majoris Image 1.1.4 verification

| Area | Changes | Main files |
| --- | --- | --- |
| Tiled viewport | The planner covers the full clipped rectangle, orders visible work by distance from its center, and admits two jobs per view. It accounts for alignment and tile overlap before allocating. Large viewports receive up to 704 MiB of tile-cache reservations. The planner coarsens the sampling level when necessary to retain complete coverage. Unused textures are evicted before visible work, and canceled requests occupy their slots until completion. | `src/gtk/{tile-cache,image-view}.vala`, `tests/tile-coverage.vala` |
| Preloading | Ordinary and background opening use one decoder-selection implementation. A manager can share a custom registry with its foreground consumer. Native decoding, metadata probes and Glycin fallback all use background admission for preloads. Entries retain the source stamp, decode options and preview-memory bounds. | `src/core/{preload-manager,decoder-registry,glycin-fallback,worker-client,job-scheduler}.vala`, `tests/{headless,decoders,raw-samples,scheduler}.vala` |
| Integration | Photos-style navigation opens JPEG, preloads TIFF and installed QOI, reuses cached documents, and tests a different preview size. Existing HDR, RAW, tile, image-switching and Files-style consumer tests continue running. | `examples/photos-viewer.vala`, `tests/{meson.build,make-fixtures.c,installed-consumers.sh}` |
| Source distribution | The repository index records mode 100755 for Debian rules and the installed-consumer script. The release checker verifies both entries in the tarball and extracts them to check the resulting mode on Linux. | `debian/rules`, `tests/{installed-consumers.sh,check-release-modes.py}`, `README.md` |

The additive API is `PreloadManager.with_registry(DecoderRegistry)`. The
default constructor, existing APIs and SONAMEs remain available. See the
[ABI 1 guide](api-2.md) for the tile and preload budgets, and
[1.1.3 verification](verification-1.1.3.md) for the previous release.

The coverage test enumerates 1080p, 4K, 5K, 8K, shifted 8K and ultrawide
geometry. It also checks fractional sampling, rotation, mirroring, invalid
and extreme bounds, scheduler priority, cancellation across a generation
reset and texture reuse during repeated viewport changes. A tone-mapped
transparent HDR OpenEXR tile matched the corresponding preview samples.
The separately run real 200 MP TIFF test decoded and retained all 160
tiles in a shifted 7,680 × 4,320 viewport and reused them on the next
request. It completed in 21.30 seconds with maximum process RSS of 714,564
KiB as reported by `/usr/bin/time -v`. This test measured the render
working set with actual memory textures. Physical scanout and GPU driver
allocations were outside the measurement.

Preload tests verified native PNG, OpenEXR and RAW embedded previews,
forced Glycin QOI and strict-color requests, cache identity, source
replacement, superseded batches, independent managers, the four-source
limit and the 64 MiB preview budget. The scheduler test asserted queued
foreground work runs ahead of tiles, with preloads admitted afterward.
The unchanged worker sandbox and cross-process thumbnail tests also ran.

## Validation

| Configuration | Commands | Result |
| --- | --- | --- |
| Full GTK and nine camera RAW fixtures | `meson setup --reconfigure /tmp/majoris-image-build "$PWD"`, `meson compile -C /tmp/majoris-image-build -j 8`, `meson test -C /tmp/majoris-image-build --print-errorlogs` | 18 passed. |
| Headless, Glycin and timed sequences enabled, Ultra HDR disabled | `meson compile -C /tmp/majoris-image-headless-110 -j 5`, `meson test -C /tmp/majoris-image-headless-110 --print-errorlogs` | 8 passed. Core had no GTK or GDK dependency according to `ldd`. |
| GTK, Glycin and timed sequences disabled, Ultra HDR enabled | `meson compile -C /tmp/majoris-image-native-110 -j 5`, `meson test -C /tmp/majoris-image-native-110 --print-errorlogs` | 17 passed. |
| Stock Debian | `dpkg-buildpackage -us -uc -b` from extracted source tarball | 17 passed and five amd64 packages built. |
| Gain-map profile | `dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b` from extracted source tarball | 17 passed and five amd64 packages built. |

The complete tile test ran from the full build directory with
`MAJORIS_IMAGE_WORKER` set to its built worker:

```sh
/usr/bin/time -v tests/test-tile-coverage tests/large.tiff "$source/tests/fixtures/multipart.exr" --complete
```

The Git source snapshot was exported with `git -c tar.umask=0022 archive`.
`python3 tests/check-release-modes.py build-packages/1.1.4/majoris-image-1.1.4.tar.gz`
verified both scripts. A deliberately corrupted archive with mode 0644
for `debian/rules` failed the checker. Both binary package builds started
from Linux tar extractions with both scripts at 0755, without a permission
repair step.

An exported-symbol comparison against the 1.1.3 packages with
`nm -D --defined-only` found every previous symbol in both libraries
and the new registry-aware constructor. Stock installation, removal
and reinstallation passed in isolated Debian 13.7. Both package profiles
passed `sh tests/installed-consumers.sh` with independent Vala and C
builds, Python introspection, GtkBuilder and mixed JPEG/QOI/TIFF
navigation. The installed Majoris profile also exported and reopened
an 8×8 JPEG gain map with the vendor `libuhdr2` runtime.

Lintian reported zero errors and nine existing package naming, GIR and
typelib warnings for each profile. The source tarball and package sets
are under `build-packages/1.1.4/`.
