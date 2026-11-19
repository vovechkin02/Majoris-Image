# Contributing

Apply Majoris Cross-Project Engineering Standards v1.8 using the
[versioned repository copy](docs/engineering-standards-v1.8.md) (SHA-256
`ac5bd2d3ac2d877603090dfaec315c8f5d6263dddf8fb480201948c3bd5b8979`).
This pinned copy permits offline development and source-archive review; the
organization's approved master remains authoritative for future revisions.
The [current project architecture decisions](docs/architecture.md)
and [historical adoption evidence](docs/verification/verification-gof-vala.md) record its application.
README files also follow the standards v1.8 §15.5 organization-wide template;
keep format and public API claims sourced from the current implementation.

Prefer `var` for clearly inferred Vala locals and typed collection iteration.
Keep explicit API signatures, fields, numeric widths/precision, nullable state,
base/interface contracts, and ownership qualifiers. Review declarations using
the supported Vala compiler and compare generated C when ownership could differ.
Keep declaration-only edits separate from structural changes. Do not rewrite
locals with a regex or enforce a percentage of `var` declarations.

Apply GoF patterns only where their participant contracts solve an actual
variation problem. Preserve ABI 2, worker protocol 1, and native GLib/GTK
lifecycle and signal facilities. Keep implementation labels at the private
`DecoderRoute.from_label` boundary instead of adding label comparisons to
consumers. Do not add an abstraction to pixel, tile, or frame hot loops without
matched correctness, latency, and memory evidence.

Use Valadoc, gtk-doc/gi-docgen, Doxygen, and PEP 257 summaries for every handwritten
class, method, property, signal, and function (§15.2). Verify coverage with
`python3 tests/release/check_source_docs.py` before submitting documentation changes.

Use four spaces in C, C++, Vala, and Python, and two spaces in Meson.
Keep control-flow bodies multiline and braced in C, C++, and Vala.
Use American English for new internal names and diagnostics. Keep existing
external spellings and identifiers, including the version 1 worker keys
`colour-managed`, `colour-diagnostic`, the `colour-probe` operation, and the
`colour` metadata key. See [the API contract](docs/api-2.md).

On Linux, use the formatter versions from the test workflow. The pinned Meson
formatter is a development tool. Building the project still requires Meson 1.4
or later. The Ruff 0.17.5 result in the Core worker verification report records
that run. The workflow pin below is the supported contributor version.

```sh
python3 -m pip install clang-format==21.1.8 ruff==0.15.0 meson==1.12.1
git ls-files -z '*.c' '*.cpp' '*.h' '*.hpp' | xargs -0 clang-format -i
git ls-files -z '*.py' | xargs -0 ruff check
git ls-files -z '*.py' | xargs -0 ruff format
git ls-files -z '*meson.build' meson.options | xargs -0 meson format -e -i
```

Use `--dry-run --Werror` for the C and C++ check, `--check` with
`ruff format`, and `-e -q` with `meson format`. Review inserted braces, macro
expansions, and include groups after running clang-format. Edit Vala and its
handwritten bindings directly. Formatters should receive only tracked
project-owned sources. Preserve binary fixtures, generated C and interfaces,
upstream API spelling in the VAPI bindings, and historical release records.
Keep repository-wide formatting changes in their own commit.

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
meson setup build-headless -Dgtk=false
meson test -C build-headless --print-errorlogs
meson setup build-diagnostic -Dwarning_level=2 -Dintrospection=enabled
meson test -C build-diagnostic --print-errorlogs
dpkg-buildpackage -us -uc -b
```

Meson compiles the worker entry point directly into the worker executable.
The versioned public libraries and introspection interfaces are built
independently.

See [test and release qualification](docs/TESTING.md) for required fixture
sets, installed-consumer checks, release profiles, and environment setup.
