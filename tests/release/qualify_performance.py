"""Qualify ImageKit with and without Glycin; do not fabricate passing results."""

import argparse
import json
import platform
import shutil
import subprocess
import time
from pathlib import Path


def command_record(argv: list[str], working_dir: Path, log: Path) -> dict:
    """Execute a build/qualification stage and save its complete output to a log."""
    began = time.monotonic()
    try:
        with log.open("w", encoding="utf-8") as stream:
            outcome = subprocess.run(
                argv, cwd=working_dir, stdout=stream, stderr=subprocess.STDOUT,
                check=False, timeout=3600,
            )
        return {
            "command": argv,
            "status": "passed" if outcome.returncode == 0 else "failed",
            "exit_code": outcome.returncode,
            "seconds": round(time.monotonic() - began, 2),
            "log": str(log),
        }
    except (OSError, subprocess.TimeoutExpired) as error:
        with log.open("a", encoding="utf-8") as stream:
            stream.write(f"\nQualification could not complete: {error}\n")
        return {
            "command": argv, "status": "blocked", "reason": str(error),
            "seconds": round(time.monotonic() - began, 2), "log": str(log),
        }


def compiler_version(command: str) -> str | None:
    """Read a compiler version without treating missing tools as successful checks."""
    if shutil.which(command) is None:
        return None
    try:
        return subprocess.check_output(
            [command, "--version"], text=True, stderr=subprocess.STDOUT,
            timeout=10,
        ).strip().splitlines()[0]
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return "unavailable"


def qualify(root: Path, workdir: Path, baseline: Path | None,
            record_baseline: Path | None, samples: int,
            raw_fixtures: Path | None = None,
            raw_manifest: Path | None = None) -> dict:
    """Qualify both builds and enforce matched benchmark acceptance when requested."""
    result = {
        "source": str(root), "platform": platform.platform(),
        "tools": {name: compiler_version(name) for name in ("meson", "valac", "ninja")},
        "profiles": {},
        "gpu_memory_qualification": (
            "not_measured: requires a supported physical GPU/compositor"
        ),
        "performance_status": "not_measured",
        "raw_qualification": "not_measured: external licensed RAW samples not supplied",
    }
    if raw_fixtures is not None:
        from verify_raw_fixtures import verify
        manifest = raw_manifest or root / "tests/fixtures/raw-qualification.tsv"
        checked = verify(raw_fixtures, manifest)
        result["raw_qualification"] = checked
        if checked["status"] != "verified":
            result["status"] = "blocked"
            result["reason"] = checked["reason"]
            return result
    if any(result["tools"][name] is None for name in ("meson", "valac", "ninja")):
        result["status"] = "blocked"
        result["reason"] = "The Meson, Vala, and Ninja native toolchain is required"
        return result

    for name in ("enabled", "disabled"):
        build = workdir / f"build-glycin-{name}"
        build.mkdir(parents=True, exist_ok=True)
        profile = {"build_directory": str(build), "stages": []}
        result["profiles"][name] = profile
        options = [f"-Draw_fixtures={raw_fixtures}"] if raw_fixtures is not None else []
        steps = [
            ["meson", "setup", str(build), str(root), "-Dgtk=true", "-Dtests=true",
             f"-Dglycin={name}", "-Dworker_profiling=true"] + options,
            ["meson", "compile", "-C", str(build)],
            ["meson", "test", "-C", str(build), "--print-errorlogs"],
        ]
        if raw_fixtures is not None:
            steps.append([
                "meson", "test", "-C", str(build), "--no-rebuild",
                "--suite", "interoperability", "--print-errorlogs",
            ])
        if name == "enabled":
            # Meson benchmarks execute only after a successful test-qualified build.
            steps.append([
                "meson", "test", "-C", str(build), "--benchmark",
                "--suite", "performance", "--print-errorlogs",
            ])
        for index, command in enumerate(steps):
            stage = command_record(command, root, workdir / f"{name}-{index:02d}.log")
            profile["stages"].append(stage)
            if stage["status"] != "passed":
                profile["status"] = stage["status"]
                break
        else:
            profile["status"] = "passed"
    if not all(p["status"] == "passed" for p in result["profiles"].values()):
        result["status"] = "failed"
        return result
    if raw_fixtures is not None:
        result["raw_qualification"]["native_tests"] = "passed_in_both_profiles"

    # Meson's benchmark suite is a correctness/execution check. The explicit
    # collector supplies independent timings to the fail-closed comparison gate.
    measurement = workdir / "candidate-performance.json"
    # Use the same qualification tooling for both production revisions; the
    # baseline checkout may precede the addition of these release scripts.
    tools_root = Path(__file__).resolve().parents[2]
    collector = [
        "python3", str(tools_root / "tests/release/collect_performance.py"),
        "--source", str(root),
        "--build", str(workdir / "build-glycin-enabled"),
        "--workdir", str(workdir / "measurement-logs"),
        "--output", str(measurement), "--samples", str(samples),
    ]
    stage = command_record(collector, root, workdir / "measurement-collection.log")
    result["measurements"] = stage
    result["measurement_file"] = str(measurement)
    if stage["status"] != "passed":
        result["status"] = "blocked"
        result["performance_status"] = "not_measured"
        return result

    if record_baseline is not None:
        # Capturing a baseline is a preparatory operation, NEVER a release pass.
        record_baseline.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(measurement, record_baseline)
        result["baseline_file"] = str(record_baseline)
        result["performance_status"] = "baseline_recorded_not_compared"
        result["status"] = "baseline_recorded"
        return result

    if baseline is None:
        result["performance_status"] = "blocked_missing_matched_baseline"
        result["status"] = "blocked"
        return result
    if not baseline.is_file():
        result["performance_status"] = "blocked_missing_matched_baseline"
        result["status"] = "blocked"
        result["reason"] = f"Baseline file does not exist: {baseline}"
        return result

    comparison = workdir / "performance-comparison.json"
    command = [
        "python3", str(tools_root / "tests/release/compare_performance.py"),
        "--baseline", str(baseline), "--current", str(measurement),
        "--output", str(comparison),
    ]
    stage = command_record(command, root, workdir / "performance-comparison.log")
    result["comparison"] = stage
    result["comparison_file"] = str(comparison)
    if comparison.is_file():
        evaluated = json.loads(comparison.read_text(encoding="utf-8"))
        result["performance_status"] = evaluated["status"]
    else:
        result["performance_status"] = "blocked"
    result["status"] = (
        "passed" if stage["status"] == "passed" and
        result["performance_status"] == "passed" else "failed"
    )
    return result


def main() -> int:
    """Write results and fail when required qualification is incomplete."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source", type=Path, default=Path(__file__).resolve().parents[2]
    )
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    policy = parser.add_mutually_exclusive_group()
    policy.add_argument("--baseline", type=Path,
                        help="Matched baseline JSON from the earlier source")
    policy.add_argument("--record-baseline", type=Path,
                        help="Capture reference results; does NOT qualify a release")
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--raw-fixtures", type=Path,
                        help="External licensed RAW samples; verified against manifest")
    args = parser.parse_args()
    if not 3 <= args.samples <= 30:
        parser.error("3–30 independent samples required")
    source = args.source.resolve()
    if not (source / "meson.build").is_file():
        parser.error(f"Not a Majoris-Image project: {source}")
    workdir = args.workdir.resolve()
    if workdir == source or source in workdir.parents:
        parser.error("Keep qualification build trees outside the source checkout")
    workdir.mkdir(parents=True, exist_ok=True)
    baseline = args.baseline.resolve() if args.baseline is not None else None
    recorded = (args.record_baseline.resolve()
                if args.record_baseline is not None else None)
    raw = args.raw_fixtures.resolve() if args.raw_fixtures else None
    result = qualify(source, workdir, baseline, recorded, args.samples,
                     raw_fixtures=raw)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    # Baseline capture is permitted as a preparatory success but never marked passed.
    return 0 if result["status"] in ("passed", "baseline_recorded") else 1


if __name__ == "__main__":
    raise SystemExit(main())
