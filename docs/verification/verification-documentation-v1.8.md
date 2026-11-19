# Majoris-Image 2.0.1 — declaration documentation migration

## Scope and source

This documentation migration was based on `Majoris-Image(20261119-014656).zip`
and `Majoris_Engineering_Standards_v1.7(1).md`. It updates the organization-wide
standards to v1.8 and the repository-pinned copy, preserving the existing
architecture, file layout, design-pattern roles, wire protocol and public names.

The changes are documentation-only in existing Vala, C, C++ and Python sources:
Valadoc declaration comments, gtk-doc/Doxygen-compatible native declarations and
function definitions, and PEP 257 module/class/function docstrings. Source
comments explain purposes, numeric units, error and cancellation behavior, source
freshness, worker isolation and ownership where relevant. They do **not** change
decoding algorithms, memory limits, formatting of executable statements or public
API signatures. A standalone source-coverage checker and Meson unit-suite
registration have been added; the testing guide now describes that checker.

## Source coverage results

| Handwritten code category | Documentation comments present | Scanner-detected declarations |
|---|---:|---:|
| Vala source classes, interfaces, enums, methods, properties and signals | 449 | 449 |
| Python modules, classes and functions | 135 | 135 |
| Native C/C++ header declarations | 44 | 44 |
| Native C/C++ function definitions | 140 | 140 |

These are reproducible **source-scanner** measurements, not a claim that every
comment's technical wording has been verified by Valadoc, gi-docgen or a C/C++
compiler. The Vala and C/C++ scanners conservatively recognize common source
forms, while Python uses `ast`. A human reviewer must still assess semantic
accuracy, field ownership, units and optional-feature behavior. Generated and
vendored code is excluded.

Run without building:

```sh
python3 tests/release/check_source_docs.py
```

On environments with Meson and the supported toolchain, the same check is
registered as `Source declaration documentation` in the `unit` suite.

## Source and compatibility verification

- Existing Vala and native source files were compared against the uploaded
  archive with lexical tokens retained and documentation comments ignored.
  **65 Vala and 53 C/C++/header files** had unchanged executable tokens.
- Existing Python source was parsed, and executable ASTs were compared after
  discarding module, class and function docstrings. **14 existing Python files**
  had identical executable ASTs.
- The repository's v1.7 pinned standards copy is intentionally replaced with
  the exact standalone v1.8 document; its SHA-256 is recorded in
  `CONTRIBUTING.md`.
- The source documentation check is registered in `tests/meson.build`; the
  changes do not alter worker or library source lists.
- The project structure and previously documented GoF responsibilities are
  retained. There is no new pattern hierarchy or runtime dependency.

## Limitations

**No native build, GIR/VAPI generation or runtime tests were executed** in the
available environment because the complete Meson/Vala/GTK development toolchain
was unavailable. The source-only comparison and coverage checker cannot replace
those release checks. The existing long executable lines were deliberately
left unchanged: cosmetic reformatting should be performed separately, if desired,
to keep a documentation-only patch easy to audit.
