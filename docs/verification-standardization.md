# Majoris Image engineering standard verification

The baseline is commit `c5a4f2e` (Majoris Image 2.0.0). The work uses the
Majoris Cross-Project Engineering Standards v1.5. This report covers the
behavior-preserving migration of the existing 2.0.0 source.

## Baseline inventory

Run `git ls-tree -r --name-only c5a4f2e` for the source file inventory.
`native/` provides the private C/C++ codecs and codec discovery.
`src/core/` owns the headless library, worker client, and thumbnail service.
`src/gtk/` owns the GTK4 widget and presentation code. `src/worker/` owns the
sandboxed worker's executable and imaging operations. `src/thumbnail/` owns
the thumbnailer and desktop registration. The repository also has public API
examples, private Vala bindings under `vapi/`, test code and fixtures, Debian
manifests and rules, and release documentation. The GTK example UI and
resource manifest live under `examples/`.

The Core and GTK libraries expose `ImageKit` and `ImageKitGtk` and use
`libmajoris-image.so.2` and `libmajoris-image-gtk4.so.2`. The versioned
GIRs and typelib namespaces are `ImageKit-2.0` and `ImageKitGtk-2.0`.
The public pkg-config and VAPI names are `majoris-image-1` and
`majoris-image-gtk4-1`. The tracked ABI manifests contain 285 Core exports
(280 without Glycin) and 77 GTK exports.

The default dependency set on this Debian 13 runner registered 37 Meson
targets, 22 tests, and one benchmark. Test categories include unit,
integration, security, and interoperability. The existing options add
extended GTK and Wayland suites. The tests use the checked-in fixtures,
37 generated files, and a separate manifest-pinned camera RAW corpus.
[The test inventory](test-inventory-2.0.0.md) describes the individual cases.
The baseline default build passed 21 tests and skipped the RAW test because
it had no configured external fixtures.

Before the changes, Meson reported 1.6-only test arguments against the
declared 1.4 minimum. Debian runtime package names still ended in `-1` even
though those packages installed the ABI 2 libraries. Git stored
`debian/rules` and `tests/release/installed_consumers.py` with mode 100644.
Native and Vala code contained inline control flow and mixed declaration
spacing. Generated interfaces, historical release records, binary fixtures,
and serialized protocol and metadata identifiers were excluded from
automatic transformations.

## Source and build changes

- C/C++ source uses four spaces, consistently grouped includes, multiline
  control flow, and expanded adjacent declarations. Vala methods, callbacks,
  branches, and switch cases use the same expanded layout. The private VAPI
  bindings retain their upstream declarations and annotations. Python utility
  changes add parameter and return annotations. Ruff already accepted their
  imports and layout.
- Private catalog helpers use `add_format_capabilities`,
  `add_decoder_backend`, and `collect_codec_operation`.
  Native color and profile errors use the `ImageKit` diagnostic domain.
  The EXIF reader documents why unreadable optional tags and GPS data are
  omitted. Version 1 worker keys, operations, error kinds, metadata keys,
  and persistent recipe keys retain spellings such as `colour-probe`,
  `colour-profile`, `colour-space`, and `nearest-neighbour`.
- Meson now passes explicit paths when Python and Xvfb are nested test
  arguments. Named generated fixture references preserve the generators'
  positional output contract. Optional codec, GTK, and typelib availability
  appear in the summary. Existing option defaults and test registrations
  are preserved. The private worker archive retains the Vala entry point
  and links whole into the executable.
- The native capability constants in `native/capability-flags.h` are checked
  against all 13 members of the Vala-generated enum by static assertions
  compiled into Core. A deliberately changed bit in a scratch copy caused a
  compiler error. The worker's emitted codec catalog was byte-identical to
  the baseline with the same dependencies.
- The existing CI workflow checks clang-format, Ruff, and Meson formatting.
  Its independent diagnostics variant uses `warning_level=2`. The local
  diagnostic build emitted 710 warnings versus 713 on the same baseline
  configuration. Comparing warning messages and their counts found zero
  additions.

## Debian and public compatibility

The new runtime package names are `libmajoris-image2` and
`libmajoris-image-gtk4-2`. The development package depends on both at its
exact package version. The Core package still pins the matching worker.
The package builds generated shlibs records mapping both SONAMEs to the
new runtimes, and the GTK and thumbnailer dependencies resolved to the new
Core package. The manifests preserve installed library and typelib paths.
Versioned `Breaks` and `Replaces` cover the file overlap with ABI 2 packages
built under the old names beginning with 2.0.0. The README describes the
coordinated worker upgrade required for earlier ABI 1 consumers.

The exact exported dynamic symbol sets, two public headers, two public
VAPIs, two GIRs, and two typelibs matched the baseline. The public namespaces,
properties, and signals are represented in the identical generated
interfaces. The existing ABI checker passed for full, headless, and
Glycin-disabled builds, including its headless GTK dependency check.
Names, suite assignments, timeouts, protocols, and priorities matched for
all 22 default tests and the benchmark. The 37 fixture files matched after
normalizing the Little CMS creation timestamp in five ICC profiles.

## Build and test commands

Each Meson profile used a separate directory. `RAW_FIXTURES` points to the
external directory containing all nine inputs checked against the manifest's
SHA-256 values.

```sh
meson setup build-default
meson setup build-headless -Dgtk=false -Dglycin=enabled -Dsequence=enabled -Dintrospection=enabled
meson setup build-native -Dglycin=disabled -Dsequence=disabled -Dultrahdr=disabled -Dintrospection=disabled
meson setup build-diagnostic -Dwarning_level=2 -Dglycin=enabled -Dsequence=enabled \
  -Dultrahdr=enabled -Dintrospection=enabled -Dextended_tests=true \
  -Dwayland_tests=true -Draw_fixtures="$RAW_FIXTURES"
meson compile -C "$BUILD"
meson test -C "$BUILD" --print-errorlogs
python3 tests/release/check_abi.py build-default/src/core/libmajoris-image.so.2 \
  build-default/src/gtk/libmajoris-image-gtk4.so.2
python3 tests/release/check_abi.py build-headless/src/core/libmajoris-image.so.2
python3 tests/release/check_abi.py --without-glycin build-native/src/core/libmajoris-image.so.2 \
  build-native/src/gtk/libmajoris-image-gtk4.so.2
dpkg-buildpackage -us -uc -b
dpkg-buildpackage -Ppkg.majoris-image.gainmap -us -uc -b
python3 tests/release/check_release_modes.py source.tar.gz
python3 tests/release/installed_consumers.py build-default/tests/large.tiff
python3 tests/release/installed_consumers.py --gainmap
python3 tests/release/qualify.py build-diagnostic --variant release --installed --jobs 4
```

| Profile | Passed | Failed | Skipped |
| --- | ---: | ---: | ---: |
| Baseline default | 21 | 0 | 1 |
| Default GTK4 | 21 | 0 | 1 |
| Final default with Meson 1.4.2 | 21 | 0 | 1 |
| Headless | 11 | 0 | 1 |
| Native-only codecs | 21 | 0 | 1 |
| Diagnostics, all codecs, extended, RAW, and Wayland | 25 | 0 | 0 |
| Debian stock package build | 21 | 0 | 1 |
| Debian gain-map package build | 21 | 0 | 1 |
| Release qualification | 24 | 0 | 0 |
| Release benchmark | 1 | 0 | 0 |

The single skip in each applicable profile was the deliberately unconfigured
RAW test. The diagnostics profile passed that test with all nine inputs,
GTK input at X11 scales 1 and 2, the Wayland lifecycle case, the extended
lifecycle test, and complete 8K tile coverage.

Both Debian builds produced all five expected binary packages. The installed
consumer checks used the package payloads in an isolated Linux namespace
with the thumbnailer post-install step executed there. This covered
independent Core, Vala GTK, C GtkBuilder, GI, and thumbnail consumers and
an installed JPEG gain-map round trip. CI contains the clean APT install,
removal, and reinstall checks. An APT upgrade from a previously published
ABI 1 or old-name ABI 2 package set was not exercised locally.

The pinned style commands in [CONTRIBUTING.md](../CONTRIBUTING.md), Python
syntax compilation with `python3 -m compileall -q tests`, `sh -n` for test
and Debian shell scripts, and ElementTree parsing of the GTK UI and resource
XML passed. Meson 1.4.2 configured and compiled successfully.

## Exceptions and follow-up

- The Vala build emits GIR source as part of the existing library interface
  even with introspection disabled. The option controls typelib compilation.
  The worker's uninstalled archive boundary and test-specific window sizes
  are intentional. Existing public member order, automatic property
  accessors, and compatibility-sensitive strings retain their established
  layout where changes could affect generated output or obscure the contract.
- A monitored maintainer email remains a release blocker. Replace
  `packages@majoris.invalid` in the active Debian control and changelog
  entries before publishing.
- WSLg covered the Wayland lifecycle. Physical HDR display output and native
  touch input require hardware verification. The CI jobs were edited and
  their local equivalents run in Debian WSL.
- Follow-up optimization candidates include measuring worker-launch cost,
  decoder I/O and memory use, and tile-cache reuse under the existing
  performance workloads. This pass changes source layout, build checks,
  diagnostics, and package metadata.
