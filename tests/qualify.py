"""Run a fail-closed ABI 2 qualification for a configured Meson build."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


CORE_TESTS = {
    "OpenEXR regions", "Imaging references", "thumbnail PNG decode and validation",
    "foreground, tile and preload scheduling", "public headless API",
    "decoder selection and composited APNG frames", "desktop thumbnail registration and sample output",
    "cross-process thumbnail publication", "worker timeout, cancellation, crash and response validation",
    "native parser fuzz smoke", "worker protocol properties",
}
GTK_TESTS = {
    "HDR presentation policy", "GDK floating-point texture color state",
    "GTK input and accessibility at scale 1", "GTK input and accessibility at scale 2",
    "wide viewport tile coverage and cancellation", "Files public preview workflow",
    "Photos public full-quality workflow", "public GTK Builder API", "GTK view lifecycle and navigation",
    "public GTK viewer readiness and error",
}
RELEASE_TESTS = {"tile-coverage-complete", "GTK lifecycle stress", "real camera RAW"}


def load_json(path):
    if not path.is_file():
        raise SystemExit(f"Missing Meson qualification input: {path}")
    return json.loads(path.read_text())


def run(*args):
    if status := subprocess.run(args, check=False).returncode:
        raise SystemExit(status)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--variant", choices=("full", "headless", "native", "release"), default="release")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--installed", action="store_true",
                        help="Run installed consumer checks against packages in this environment")
    args = parser.parse_args()
    build = args.build.resolve()
    info = build / "meson-info"
    if not info.is_dir():
        parser.error(f"Configure the Meson build before qualifying it: {build}")
    run("meson", "setup", "--reconfigure", str(build))
    options = {option["name"]: option["value"] for option in load_json(info / "intro-buildoptions.json")}
    expected = {"gtk": args.variant != "headless", "tests": True}
    if args.variant == "native":
        expected.update(glycin="disabled", sequence="disabled")
    elif args.variant == "release":
        if not args.installed:
            parser.error("release qualification requires --installed and installed ABI 2 packages")
        expected.update(extended_tests=True, glycin="enabled", sequence="enabled", ultrahdr="enabled",
                        introspection="enabled")
        if not options.get("raw_fixtures"):
            parser.error("release qualification requires -Draw_fixtures with the full manifest")
        targets = load_json(info / "intro-targets.json")
        parameters = {item for target in targets if target["name"] == "majoris-image-codecs"
                      for group in target["target_sources"] for item in group.get("parameters", [])}
        for feature in ("AVIF_SEQUENCE", "HEIF_SEQUENCE", "UHDR_AVAILABLE", "UHDR_CONTAINERS"):
            if f"-DIMAGE_CODEC_{feature}=1" not in parameters:
                parser.error(f"release qualification requires compatible codec support for {feature}")
    for option, value in expected.items():
        if options.get(option) != value:
            parser.error(f"{args.variant} qualification requires -D{option}={str(value).lower()}")

    tests = {test["name"]: test for test in load_json(info / "intro-tests.json")}
    mandatory = set(CORE_TESTS)
    suites = ["unit", "integration", "security"]
    if expected["gtk"]:
        mandatory |= GTK_TESTS
    if args.variant == "release":
        mandatory |= RELEASE_TESTS
        suites += ["extended", "interoperability"]
    if missing := mandatory - tests.keys():
        parser.error("Required tests were not registered: " + ", ".join(sorted(missing)))

    run("meson", "test", "-C", str(build), "--print-errorlogs", "--logbase", "qualification",
        "--num-processes", str(args.jobs), *(arg for suite in suites for arg in ("--suite", suite)))
    log = build / "meson-logs" / "qualification.json"
    if not log.is_file():
        log = build / "meson-logs" / "testlog-qualification.json"
    results = {record["name"].split(" / ")[-1]: record["result"]
               for line in log.read_text().splitlines() if (record := json.loads(line))}
    failed = {name: results.get(name, "NOT EXECUTED") for name in mandatory if results.get(name) != "OK"}
    if failed:
        parser.error(f"Required test results: {failed}")

    scripts = Path(__file__).resolve().parent
    libraries = [build / "src/core/libmajoris-image.so.2"]
    if expected["gtk"]:
        libraries.append(build / "src/gtk/libmajoris-image-gtk4.so.2")
    abi_options = ["--without-glycin"] if options.get("glycin") == "disabled" else []
    run(sys.executable, str(scripts / "check-abi.py"), *abi_options, *(str(path) for path in libraries))
    if args.installed:
        run("sh", str(scripts / "installed-consumers.sh"), str(build / "tests/large.tiff"))
    if args.variant == "release":
        run("meson", "test", "-C", str(build), "--benchmark", "--suite", "performance")
    print(f"{args.variant} qualification passed {len(mandatory)} required tests")


if __name__ == "__main__":
    main()
