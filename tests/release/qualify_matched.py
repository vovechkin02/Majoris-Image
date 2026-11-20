#!/usr/bin/env python3
"""Run matched reference/current Debian qualification without inventing measurements.

Reference and current revisions use the candidate's identical benchmark harness,
while retaining their original production implementations and public APIs.
This tool does not execute in the background or mark unavailable GPU/RAW coverage
as passed.
"""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

from prepare_baseline import prepare
from qualify_performance import qualify


def project_source_digest(root: Path) -> str:
    """Fingerprint the C, C++, and Vala source files being compared."""
    checksum = hashlib.sha256()
    for directory in ("src", "native"):
        for path in sorted((root / directory).rglob("*")):
            if path.is_file() and path.suffix in (".vala", ".c", ".h", ".cpp", ".hpp"):
                checksum.update(path.relative_to(root).as_posix().encode("utf-8"))
                checksum.update(path.read_bytes())
    return checksum.hexdigest()


def run_matched(archive: Path, source: Path, workdir: Path, samples: int,
                raw_fixtures: Path | None, require_gpu: bool) -> dict:
    """Build and compare two sources, retaining explicit coverage status per stage."""
    result = {
        "status": "blocked", "baseline_archive": str(archive),
        "baseline_archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "candidate_source": str(source),
        "candidate_source_sha256": project_source_digest(source),
        "raw_qualification": "not_measured",
        "physical_gpu_qualification": "not_measured",
    }
    base = workdir / "reference-source"
    result["baseline_preparation"] = prepare(archive, source, base)
    result["reference_source_sha256"] = project_source_digest(base)
    if result["candidate_source_sha256"] == result["reference_source_sha256"]:
        result["comparison_note"] = "No executable source delta: regression check only"

    evidence = workdir / "reference-evidence.json"
    reference = qualify(base, workdir / "reference-build", None, evidence, samples,
                        raw_fixtures=raw_fixtures,
                        raw_manifest=source / "tests/fixtures/raw-qualification.tsv")
    result["reference"] = reference
    result["raw_qualification"] = reference.get("raw_qualification", "not_measured")
    if reference["status"] != "baseline_recorded":
        result["reason"] = "Reference native qualification did not complete"
        return result

    current = qualify(source, workdir / "candidate-build", evidence, None, samples,
                      raw_fixtures=raw_fixtures)
    result["candidate"] = current
    result["raw_qualification"] = current.get("raw_qualification", "not_measured")
    result["cpu_performance_qualification"] = current.get(
        "performance_status", "not_measured"
    )
    if current["status"] != "passed":
        result["reason"] = "Current native build, tests, or matched comparison failed"
        result["status"] = "failed" if current["status"] == "failed" else "blocked"
        return result

    result["status"] = "passed_cpu_only"
    return result


def qualify_gpu(result: dict, benchmark: Path, fixture: Path, output: Path) -> None:
    """Collect physical-display telemetry, rejecting unavailable GPU counters."""
    command = [sys.executable, str(Path(__file__).resolve().parents[1] /
               "benchmarks/measure_gpu_memory.py"), str(benchmark), str(fixture),
               "--output", str(output), "--mode", "tiled"]
    completed = subprocess.run(command, capture_output=True, text=True,
                               check=False, timeout=1200)
    if output.exists():
        telemetry = json.loads(output.read_text(encoding="utf-8"))
        result["physical_gpu_qualification"] = telemetry
    else:
        result["physical_gpu_qualification"] = {
            "qualification": "not_measured", "reason": completed.stderr.strip()
        }
    if completed.returncode != 0:
        if result.get("cpu_performance_qualification") == "passed":
            result["status"] = "blocked"
    elif result.get("cpu_performance_qualification") == "passed":
        result["status"] = "passed_cpu_and_gpu"


def qualify_viewer_diagnostics(result: dict, build: Path, workdir: Path,
                               cycles: int) -> None:
    """Record optional long-cycle GTK evidence without calling it a leak test."""
    from collect_performance import artifact

    benchmark = artifact(build, "benchmark-viewer-lifecycle")
    fixture = build / "tests/large.tiff"
    output = workdir / "viewer-memory-diagnosis.json"
    command = [
        sys.executable,
        str(Path(__file__).resolve().parents[1] /
            "benchmarks/diagnose_viewer_memory.py"),
        str(benchmark), str(fixture), "--cycles", str(cycles),
        "--viewers", "8", "--mode", "tiled", "--xvfb",
        "--output", str(output),
        "--log", str(workdir / "viewer-memory-diagnosis.log"),
        "--timeout-seconds", "2400",
    ]
    completed = subprocess.run(
        command, capture_output=True, text=True, check=False, timeout=2500,
    )
    if output.exists():
        result["viewer_memory_diagnosis"] = json.loads(output.read_text())
    else:
        result["viewer_memory_diagnosis"] = {
            "status": "blocked", "reason": completed.stderr.strip(),
        }
    if completed.returncode != 0:
        result["status"] = "blocked"
        result["reason"] = "Requested long-cycle viewer diagnosis is incomplete"


def main() -> int:
    """Run a fresh matched qualification and save a complete, truthful report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-archive", type=Path, required=True)
    parser.add_argument("--source", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--raw-fixtures", type=Path,
                        help="Licensed RAW samples matching raw-qualification.tsv")
    parser.add_argument("--gpu-executable", type=Path,
                        help="Built GTK viewer-lifecycle executable on a real display")
    parser.add_argument("--gpu-fixture", type=Path,
                        help="Large TIFF image for physical GPU measurements")
    parser.add_argument("--require-gpu", action="store_true",
                        help="Only succeed if GPU per-process telemetry was measured")
    parser.add_argument("--diagnose-viewer-memory", action="store_true",
                        help="Record optional 20-cycle tiled-viewer memory diagnostics")
    parser.add_argument("--diagnostic-cycles", type=int, default=20,
                        help="Diagnostic cycles, between 5 and 50 (default: 20)")
    args = parser.parse_args()
    if not 3 <= args.samples <= 30:
        parser.error("3–30 samples required")
    if not 5 <= args.diagnostic_cycles <= 50:
        parser.error("Diagnostic cycles must be between 5 and 50")
    if (args.gpu_executable is None) != (args.gpu_fixture is None):
        parser.error("--gpu-executable and --gpu-fixture must be supplied together")
    source, archive, workdir = (value.resolve() for value in
                                (args.source, args.baseline_archive, args.workdir))
    if not archive.is_file() or not (source / "meson.build").is_file():
        parser.error("Provide an existing earlier archive and Majoris-Image source")
    if workdir.exists() or workdir == source or source in workdir.parents:
        parser.error("Use a new workdir outside the source checkout")
    workdir.mkdir(parents=True)
    try:
        result = run_matched(
            archive, source, workdir, args.samples,
            args.raw_fixtures.resolve() if args.raw_fixtures else None,
            args.require_gpu,
        )
        if result["status"] == "passed_cpu_only":
            benchmark = args.gpu_executable.resolve() if args.gpu_executable else None
            fixture = args.gpu_fixture.resolve() if args.gpu_fixture else None
            if args.require_gpu and benchmark is None:
                # Locate the actual compiled binary rather than assuming a build path.
                from collect_performance import artifact
                build = workdir / "candidate-build/build-glycin-enabled"
                benchmark = artifact(build, "benchmark-viewer-lifecycle")
                fixture = build / "tests/large.tiff"
            if benchmark is not None and fixture is not None:
                qualify_gpu(result, benchmark, fixture, workdir / "gpu-telemetry.json")
        if (args.diagnose_viewer_memory and
                result["status"] in ("passed_cpu_only", "passed_cpu_and_gpu")):
            qualify_viewer_diagnostics(
                result, workdir / "candidate-build/build-glycin-enabled",
                workdir, args.diagnostic_cycles,
            )
        if args.require_gpu and result["status"] != "passed_cpu_and_gpu":
            result["status"] = "blocked"
            result["reason"] = "Physical GPU telemetry unavailable or not qualified"
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired,
            json.JSONDecodeError) as error:
        result = {"status": "blocked", "reason": str(error),
                  "baseline_archive": str(archive), "source": str(source)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(json.dumps({"status": result["status"],
                      "report": str(args.output), "reason": result.get("reason", "")},
                     sort_keys=True))
    return 0 if result["status"] in ("passed_cpu_only", "passed_cpu_and_gpu") else 1


if __name__ == "__main__":
    raise SystemExit(main())
