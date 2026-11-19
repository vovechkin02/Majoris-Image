# Majoris Image 2.0.1 — Structural migration inventory

**Scope:** Source-file relocation, public declaration-file organization,
documentation grouping and README/standards updates. **No image-processing
algorithm was intentionally changed.**

## Source changes

- Moved 22 original Core files into `decoding/`, `execution/`, `export/` and
  `thumbnail/`; retained one Core Meson entry point and the original source
  files for shared worker contracts and test sources.
- Extracted five existing GTK enums without changing names or members:
  `ViewState`, `FitMode`, `ScrollZoomMode` to `image-view-types.vala` and
  `PresentationPolicy`, `PresentationStatus` to `presentation-types.vala`.
- Extracted `ImageCapabilities` and `DecodeFailure` from
  `decoder-backend.vala` into `decoder-types.vala`; located `RawState` beside
  the public `ImageProbe`. `DecoderBackend` retains the same public methods.
- Updated Core/GTK Meson sources, cross-target file references, private header
  search paths, direct test includes and benchmark registrations.
- Moved 24 historical verification/report files under `docs/verification/`.
  Added one stable [architecture reference](../architecture.md) instead of
  relying on older milestone reports for current architecture.
- Replaced the root README with the current API, format, build and usage
  guide. Added shared README policy to engineering standards v1.7 and pinned
  its hash in `CONTRIBUTING.md`.
- Restored the required executable source-archive file modes on Debian rules
  and standalone release scripts, since the incoming archive had no Unix
  executable-mode metadata.

## Structural validation performed

- Confirmed every Meson source path referring to a real source file resolves,
  with generated build outputs excluded from source-tree existence checks.
- Confirmed unchanged production C/C++/Vala source bytes match the input
  archive. The only intentionally changed production Vala files are the two
  files from which public type declarations were moved and the file receiving
  `RawState`; extracted type bodies retain their original values.
- Verified that each moved public enum and `DecoderBackend` declaration has
  exactly one definition in the current source tree.
- Checked local Markdown links, Python parseability, XML well-formedness,
  shell syntax and the embedded standards SHA-256 pin.

## Validation NOT performed

The environment used for this structural migration does not provide Meson,
`valac`, GLib/GIO headers, GTK development headers or the other native
codec dependencies. **No Core or GTK compilation, GIR/VAPI generation,
ABI comparison, runtime test suite, package build or benchmark was run.**

Before publishing this new archive, compile the full and headless profiles,
compare the ABI 2 baseline symbol files, check GIR and VAPI, run installed
consumers and execute the repository's established qualification matrix.
See [the test guide](../TESTING.md) for the supported commands.

## Release policy

This relocation does not raise the library version, change the worker
protocol, rename exported types or alter feature flags. Any native compilation
or ABI mismatch found on a release toolchain must be resolved before claiming
a behavior- and ABI-preserving release.
