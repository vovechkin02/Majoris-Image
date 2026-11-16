# Contributing

Use four spaces in C, C++, Vala, and Python, and two spaces in Meson.
Keep control-flow bodies multiline and braced in C, C++, and Vala.
Use American English for new internal names and diagnostics. Keep existing
external spellings and identifiers, including the version 1 worker keys
`colour-managed`, `colour-diagnostic`, the `colour-probe` operation, and the
`colour` metadata key. See [the API contract](docs/api-2.md).

On Linux, use the formatter versions from the test workflow. The pinned Meson
formatter is a development tool. Building the project still requires Meson 1.4
or later.

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

The worker entry point is built into an uninstalled static archive and linked
whole into the worker executable. The archive is private to that executable.
The versioned public libraries and introspection interfaces are built
independently.

See [test and release qualification](docs/TESTING.md) for required fixture
sets, installed-consumer checks, release profiles, and environment setup.
