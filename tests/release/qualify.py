"""Run a fail-closed ABI 2 qualification for a configured Meson build."""

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

CORE_TESTS = {
    "OpenEXR region decoding",
    "Native image processing references",
    "Thumbnail PNG decoding and validation",
    "Bounded worker output validation",
    "Job scheduling and priorities",
    "Headless Core API",
    "Encoded export transfer validation and publication",
    "Decoder selection and composited APNG frames",
    "Desktop thumbnail registration and output",
    "Cross-process thumbnail publication",
    "Worker timeout, cancellation, crash and response validation",
    "Native parser fuzz smoke",
    "Worker protocol round trips and validation",
}
GTK_TESTS = {
    "HDR presentation policy",
    "GDK floating-point texture color state",
    "GTK input and accessibility at scale 1",
    "GTK input and accessibility at scale 2",
    "Viewport tile coverage and cancellation",
    "Files public preview workflow",
    "Photos public full-quality workflow",
    "GtkBuilder view loading",
    "GTK view lifecycle and navigation",
    "GTK viewer readiness and load errors",
}
RELEASE_TESTS = {
    "Complete 8K tile coverage",
    "GTK lifecycle stress",
    "Real-camera RAW decoding",
}


def load_json(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        raise SystemExit(f"Missing Meson qualification input: {path}")
    return json.loads(path.read_text())


def run(*args: str, timeout: int = 1800) -> None:
    if status := subprocess.run(args, check=False, timeout=timeout).returncode:
        raise SystemExit(status)


def verify_profile(
    info: Path, variant: str, installed: bool, parser: argparse.ArgumentParser
) -> dict[str, Any]:
    options = {
        option["name"]: option["value"]
        for option in load_json(info / "intro-buildoptions.json")
    }
    expected = {"gtk": variant != "headless", "tests": True}
    if variant == "native":
        expected.update(glycin="disabled", sequence="disabled")
    elif variant == "release":
        if not installed:
            parser.error(
                "release qualification requires --installed and installed ABI 2 packages"
            )
        expected.update(
            extended_tests=True,
            glycin="enabled",
            sequence="enabled",
            ultrahdr="enabled",
            introspection="enabled",
        )
        if not options.get("raw_fixtures"):
            parser.error(
                "release qualification requires -Draw_fixtures with the full manifest"
            )
        targets = load_json(info / "intro-targets.json")
        parameters = {
            item
            for target in targets
            if target["name"] == "majoris-image-codecs"
            for group in target["target_sources"]
            for item in group.get("parameters", [])
        }
        for feature in (
            "AVIF_SEQUENCE",
            "HEIF_SEQUENCE",
            "UHDR_AVAILABLE",
            "UHDR_CONTAINERS",
        ):
            if f"-DIMAGE_CODEC_{feature}=1" not in parameters:
                parser.error(
                    f"release qualification requires compatible codec support for {feature}"
                )
    for option, value in expected.items():
        if options.get(option) != value:
            parser.error(
                f"{variant} qualification requires -D{option}={str(value).lower()}"
            )

    return options


def verify_test_results(
    build: Path, gtk: bool, release: bool, jobs: int, parser: argparse.ArgumentParser
) -> int:
    tests = {
        test["name"]: test for test in load_json(build / "meson-info/intro-tests.json")
    }
    mandatory = set(CORE_TESTS)
    suites = ["unit", "integration", "security"]
    if gtk:
        mandatory |= GTK_TESTS
    if release:
        mandatory |= RELEASE_TESTS
        suites += ["extended", "interoperability"]
    if missing := mandatory - tests.keys():
        parser.error(
            "Required tests were not registered: " + ", ".join(sorted(missing))
        )

    run(
        "meson",
        "test",
        "-C",
        str(build),
        "--print-errorlogs",
        "--logbase",
        "qualification",
        "--num-processes",
        str(jobs),
        *(arg for suite in suites for arg in ("--suite", suite)),
    )
    log = build / "meson-logs" / "qualification.json"
    if not log.is_file():
        log = build / "meson-logs" / "testlog-qualification.json"
    results = {
        record["name"].split(" / ")[-1]: record["result"]
        for line in log.read_text().splitlines()
        if (record := json.loads(line))
    }
    failed = {
        name: results.get(name, "NOT EXECUTED")
        for name in mandatory
        if results.get(name) != "OK"
    }
    if failed:
        parser.error(f"Required test results: {failed}")

    return len(mandatory)


def verify_abi(build: Path, gtk: bool, without_glycin: bool) -> None:
    scripts = Path(__file__).resolve().parent
    libraries = [build / "src/core/libmajoris-image.so.2"]
    if gtk:
        libraries.append(build / "src/gtk/libmajoris-image-gtk4.so.2")
    abi_options = ["--without-glycin"] if without_glycin else []
    run(
        sys.executable,
        str(scripts / "check_abi.py"),
        *abi_options,
        *(str(path) for path in libraries),
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument(
        "--variant",
        choices=("full", "headless", "native", "release"),
        default="release",
    )
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--installed",
        action="store_true",
        help="Run installed consumer checks against packages in this environment",
    )
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    build = args.build.resolve()
    info = build / "meson-info"
    if not info.is_dir():
        parser.error(f"Configure the Meson build before qualifying it: {build}")
    run("meson", "setup", "--reconfigure", str(build))
    options = verify_profile(info, args.variant, args.installed, parser)
    gtk = args.variant != "headless"
    count = verify_test_results(
        build, gtk, args.variant == "release", args.jobs, parser
    )
    verify_abi(build, gtk, options.get("glycin") == "disabled")
    if args.installed:
        run(
            sys.executable,
            str(Path(__file__).with_name("installed_consumers.py")),
            str(build / "tests/large.tiff"),
        )
    if args.variant == "release":
        run("meson", "test", "-C", str(build), "--benchmark", "--suite", "performance")
    print(f"{args.variant} qualification passed {count} required tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
