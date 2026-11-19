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


def qualify(root: Path, workdir: Path) -> dict:
    """Run both native configurations, regressions, and measured performance suites."""
    result = {
        "source": str(root), "platform": platform.platform(),
        "tools": {name: compiler_version(name) for name in ("meson", "valac", "ninja")},
        "profiles": {},
        "gpu_memory_qualification": (
            "not measured: requires a supported physical GPU/compositor"
        ),
    }
    if any(result["tools"][name] is None for name in ("meson", "valac", "ninja")):
        result["status"] = "blocked"
        result["reason"] = "The Meson, Vala, and Ninja native toolchain is required"
        return result

    for name in ("enabled", "disabled"):
        build = workdir / f"build-glycin-{name}"
        build.mkdir(parents=True, exist_ok=True)
        profile = {"build_directory": str(build), "stages": []}
        result["profiles"][name] = profile
        steps = [
            ["meson", "setup", str(build), str(root), "-Dgtk=true", "-Dtests=true",
             f"-Dglycin={name}", "-Dworker_profiling=true"],
            ["meson", "compile", "-C", str(build)],
            ["meson", "test", "-C", str(build), "--no-rebuild", "--print-errorlogs"],
        ]
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
    result["status"] = (
        "passed" if all(p["status"] == "passed" for p in result["profiles"].values())
        else "failed"
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
    args = parser.parse_args()
    source = args.source.resolve()
    if not (source / "meson.build").is_file():
        parser.error(f"Not a Majoris-Image project: {source}")
    workdir = args.workdir.resolve()
    if workdir == source or source in workdir.parents:
        parser.error("Keep qualification build trees outside the source checkout")
    workdir.mkdir(parents=True, exist_ok=True)
    result = qualify(source, workdir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
