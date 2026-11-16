"""Check ABI 2 SONAMEs, dependencies and the approved symbol baseline."""

import argparse
import re
import subprocess
from pathlib import Path

GLYCIN_ONLY = {
    "image_discover_glycin_loaders",
    "image_glycin_pixels_async",
    "image_glycin_pixels_finish",
    "image_glycin_pixels_profile_async",
    "image_mime_matches",
}


def command(*args: str | Path) -> str:
    return subprocess.check_output(args, text=True, timeout=15)


def check(
    library: Path,
    soname: str,
    baseline: Path,
    optional: set[str] | frozenset[str] = frozenset(),
) -> set[str]:
    dynamic = command("readelf", "-d", library)
    if re.findall(r"\(SONAME\).*\[(.*?)\]", dynamic) != [soname]:
        raise SystemExit(f"{library}: expected SONAME {soname}")
    actual = {
        line.split()[-1]
        for line in command("nm", "-D", "--defined-only", library).splitlines()
    }
    expected = set(baseline.read_text().splitlines()) - optional
    if not expected:
        raise SystemExit(f"Empty ABI baseline: {baseline}")
    if missing := expected - actual:
        raise SystemExit(
            f"{library}: missing ABI 2 exports: {', '.join(sorted(missing))}"
        )
    needed = set(re.findall(r"\(NEEDED\).*\[(.*?)\]", dynamic))
    print(f"{soname}: verified {len(expected)} baseline exports")
    return needed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("core", type=Path)
    parser.add_argument("gtk", type=Path, nargs="?")
    parser.add_argument("--without-glycin", action="store_true")
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    core_soname = "libmajoris-image.so.2"
    optional = GLYCIN_ONLY if args.without_glycin else frozenset()
    dependencies = check(args.core, core_soname, tests / "abi2-core.symbols", optional)
    if args.without_glycin and any(
        name.startswith("libglycin") for name in dependencies
    ):
        raise SystemExit("The core unexpectedly links Glycin")
    if any(name.startswith(("libgtk", "libgdk-")) for name in dependencies):
        raise SystemExit("GTK or GDK leaked into the headless library")
    if args.gtk:
        needed = check(
            args.gtk, "libmajoris-image-gtk4.so.2", tests / "abi2-gtk.symbols"
        )
        if core_soname not in needed:
            raise SystemExit("The GTK4 library does not link ABI 2 of the core")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
