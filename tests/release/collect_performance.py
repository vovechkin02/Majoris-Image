#!/usr/bin/env python3
"""Collect reproducible APNG, queue-cancellation, and GTK memory measurements.

Run only on built Debian/Majoris trees. Results are raw evidence, never a
release pass without comparison to a matched earlier source revision.
"""

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path


SCHEDULER_PHASE = re.compile(r"ImageKit-profile scheduler-(queue|cancel|drain) (\d+) 1")
VIEWER_FINAL = re.compile(
    r"Viewer-memory final_rss_kib=(\d+) retained_delta_kib=(-?\d+)"
)
VIEWER_LIVE = re.compile(r"Viewer-memory cycle=\d+ live_rss_kib=(\d+)")
VIEWER_TILES = re.compile(r"Viewer-tiles cycle=\d+ ready_viewers=(\d+)")


def benchmark_harness_hash(root: Path) -> str:
    """Bind comparisons to collectors, workload sources and fixture definitions."""
    paths = (
        "tests/release/collect_performance.py",
        "tests/benchmarks/run_benchmarks.py",
        "tests/benchmarks/benchmark.vala",
        "tests/benchmarks/benchmark-scheduler.vala",
        "tests/benchmarks/benchmark-viewer-lifecycle.vala",
        "tests/benchmarks/meson.build",
        "tests/meson.build",
        "tests/support/make_apng_benchmarks.py",
        "tests/support/make-fixtures.c",
    )
    sha = hashlib.sha256()
    for filename in paths:
        sha.update(filename.encode("utf-8"))
        sha.update((root / filename).read_bytes())
    return sha.hexdigest()


def fingerprint(value: str) -> str:
    """Identify a deterministic request configuration, not a host-specific path."""
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def cpu_model() -> str:
    """Record the Linux CPU model so unlike hosts cannot pass comparison."""
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.partition(":")[2].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def artifact(build: Path, name: str) -> Path:
    """Locate one Meson executable using introspection rather than guessed paths."""
    metadata = subprocess.check_output(
        ["meson", "introspect", "--targets", str(build)],
        text=True, timeout=30,
    )
    matches = [target for target in json.loads(metadata)
               if target.get("name") == name]
    if len(matches) != 1:
        raise RuntimeError(f"Missing or ambiguous Meson target {name}")
    filenames = matches[0]["filename"]
    filenames = [filenames] if isinstance(filenames, str) else filenames
    paths = [
        Path(filename).resolve() for filename in filenames
        if Path(filename).is_file()
    ]
    if len(paths) != 1:
        raise RuntimeError(f"Expected one compiled executable for {name}: {filenames}")
    return paths[0]


def execute(command: list[str], cwd: Path, logfile: Path, env: dict | None = None,
            timeout: int = 1800) -> str:
    """Capture a measurement run; reject failed or incomplete executions."""
    outcome = subprocess.run(
        command, cwd=cwd, env=env, text=True, check=False,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
    )
    logfile.write_text(outcome.stdout, encoding="utf-8")
    if outcome.returncode:
        raise RuntimeError(f"Measurement failed ({outcome.returncode}): {logfile}")
    return outcome.stdout


def summarize(values: list[float], label: str, output: str | None = None) -> dict:
    """Record independent medians, absolute median deviation and output fingerprints."""
    if len(values) < 3 or any(not math.isfinite(value) for value in values):
        raise RuntimeError(f"Insufficient finite independent measurements for {label}")
    median = statistics.median(values)
    record = {
        "count": len(values), "median_value": median,
        "mad_value": statistics.median(abs(value - median) for value in values),
        "min_value": min(values), "max_value": max(values),
        "p95_value": sorted(values)[math.ceil(len(values) * .95) - 1],
        "fixture_sha256": fingerprint(label), "samples": values,
        "output_sha256": output,
    }
    return record


def record_apng(
    root: Path, build: Path, workdir: Path, samples: int
) -> tuple[dict, dict]:
    """Measure long-frame APNGs using the existing sandbox-aware benchmark runner."""
    fixtures = [build / "tests" / f"animation-{count}.apng" for count in (10, 50, 100)]
    fixtures.append(build / "tests/animation-composite.apng")
    for path in fixtures:
        if not path.is_file():
            raise RuntimeError(f"Missing generated APNG fixture: {path}")
    report_path = workdir / "apng-benchmarks.json"
    command = [
        sys.executable, str(root / "tests/benchmarks/run_benchmarks.py"),
        "--build", str(build),
        "--worker", str(artifact(build, "majoris-image-worker")),
        "--executable", str(artifact(build, "majoris-image-benchmark")),
        "--samples", str(samples), "--output", str(report_path),
    ] + [
        f"animation-apng-{count}={path}"
        for count, path in zip((10, 50, 100), fixtures[:3], strict=True)
    ] + [f"animation-apng-composite={fixtures[3]}"]
    execute(command, root, workdir / "apng-run.log", timeout=2700)
    source = json.loads(report_path.read_text(encoding="utf-8"))
    results = {}
    for name in (
        "animation-apng-10", "animation-apng-50", "animation-apng-100",
        "animation-apng-composite",
    ):
        record = source["workloads"][name]
        if not record.get("animation_all_sha256"):
            raise RuntimeError(f"Missing all-frame APNG fingerprint for {name}")
        stages = {}
        for stage in (
            "glycin-source-stamp", "glycin-loader-discovery", "glycin-color-probe",
            "glycin-scheduler-admission", "glycin-frame-advance",
            "glycin-pixel-conversion",
            "glycin-final-source-stamp",
        ):
            totals = [sample.get("phases", {}).get(stage, {}).get("us", 0)
                      for sample in record["samples"]]
            events = [sample.get("phases", {}).get(stage, {}).get("events", 0)
                      for sample in record["samples"]]
            stages[stage] = {"median_us": statistics.median(totals),
                             "median_events": statistics.median(events)}
        if not stages["glycin-frame-advance"]["median_events"]:
            raise RuntimeError(f"Missing frame-advancement profiles for {name}")
        values = [float(sample["wall_seconds"]) for sample in record["samples"]]
        if any(value <= 0 for value in values):
            raise RuntimeError(f"Invalid APNG wall time for {name}")
        results[name] = summarize(values, record["fixture_sha256"],
                                  record["animation_all_sha256"])
        results[name]["fixture_sha256"] = record["fixture_sha256"]
        results[name]["stages"] = stages
        setup_us = sum(stages[stage]["median_us"] for stage in (
            "glycin-source-stamp", "glycin-loader-discovery", "glycin-color-probe",
        ))
        results[name]["metadata_setup_median_us"] = setup_us
        results[name]["metadata_setup_percent_of_wall"] = round(
            100 * setup_us / (statistics.median(values) * 1e6), 2
        )
        results[name]["unit"] = "seconds"
    return results, source


def record_scheduler(root: Path, build: Path, workdir: Path, samples: int) -> dict:
    """Measure queued insertion, arbitrary cancellation and drain separately."""
    executable = artifact(build, "benchmark-scheduler")
    results = {}
    for count in (120, 1200, 4800):
        measures = {"queue": [], "cancel": [], "drain": []}
        for sample in range(samples):
            stdout = execute([str(executable), str(count), "75"], root,
                             workdir / f"scheduler-{count}-{sample:02d}.log",
                             timeout=120)
            found = {
                phase: int(value) for phase, value in SCHEDULER_PHASE.findall(stdout)
            }
            if set(found) != set(measures):
                raise RuntimeError(
                    f"Incomplete scheduler phase measurements at count {count}"
                )
            if f"total={count} canceled_percent=75" not in stdout:
                raise RuntimeError(
                    f"Invalid scheduler completion accounting at count {count}"
                )
            for phase, duration in found.items():
                measures[phase].append(duration / 1e6)
        for phase, values in measures.items():
            name = f"scheduler-{count}-{phase}"
            results[name] = summarize(values, f"queue:{count}:75:stable-priority-v1")
            results[name]["unit"] = "seconds"
    return results


def record_viewers(root: Path, build: Path, workdir: Path, samples: int) -> dict:
    """Compare small-preview and tiled large-image RSS under matched conditions."""
    xvfb = shutil.which("xvfb-run")
    if not xvfb:
        raise RuntimeError("xvfb-run required for matched GTK lifecycle measurements")
    executable = artifact(build, "benchmark-viewer-lifecycle")
    fixtures = (
        ("viewer", root / "tests/fixtures/rgb16.png", False),
        ("viewer-large", build / "tests/large.tiff", True),
    )
    env = dict(
        os.environ,
        MAJORIS_IMAGE_WORKER=str(artifact(build, "majoris-image-worker")),
        GDK_BACKEND="x11", GDK_SCALE="1", GSK_RENDERER="cairo",
        G_DEBUG="fatal-criticals",
    )
    result = {}
    for label, fixture, tiled in fixtures:
        if not fixture.is_file():
            raise RuntimeError(f"Missing benchmark fixture: {fixture}")
        retained, live = [], []
        for sample in range(samples):
            stdout = execute([
                xvfb, "-a", "-s", "-screen 0 1600x900x24", str(executable),
                str(fixture), "6", "8", "tiled" if tiled else "preview",
            ], root, workdir / f"{label}-{sample:02d}.log", env=env,
                timeout=900 if tiled else 600)
            final = VIEWER_FINAL.search(stdout)
            if final is None:
                raise RuntimeError(
                    f"Missing post-release GTK memory report for {label}"
                )
            views = [int(value) for value in VIEWER_LIVE.findall(stdout)]
            if len(views) != 6:
                raise RuntimeError(
                    f"Expected six simultaneous-viewer samples for {label}"
                )
            tiled_counts = [int(value) for value in VIEWER_TILES.findall(stdout)]
            if tiled and tiled_counts != [8] * 6:
                raise RuntimeError(
                    "Large-image workload did not render tiles in all viewers"
                )
            retained.append(float(max(0, int(final.group(2)))))
            live.append(float(max(views)))
        source_sha = hashlib.sha256(fixture.read_bytes()).hexdigest()
        mode = "tiled" if tiled else "preview"
        config = f"simultaneous-8-viewers:6-cycles:{mode}:{source_sha}"
        for suffix, measures in (("retained-rss", retained), ("live-rss", live)):
            result[f"{label}-{suffix}"] = summarize(measures, config)
            result[f"{label}-{suffix}"]["unit"] = "KiB"
    return result


def collect(root: Path, build: Path, workdir: Path, samples: int) -> dict:
    """Combine matching hardware and build metadata with all required measurements."""
    apng, source = record_apng(root, build, workdir, samples)
    result = {
        "schema": 2, "version": source["version"],
        "harness_sha256": benchmark_harness_hash(root),
        "environment": {
            "system": source["environment"]["system"],
            "machine": source["environment"]["cpu"],
            "cpu_model": cpu_model(),
            "meson": subprocess.check_output(["meson", "--version"], text=True).strip(),
            "valac": subprocess.check_output(["valac", "--version"], text=True).strip(),
        },
        "build_options": source["build_options"],
        "compilers": source["compilers"], "codecs": source["codecs"],
        "workloads": apng,
        "gpu_memory_qualification": "not_measured: needs physical GPU/compositor",
    }
    result["workloads"].update(record_scheduler(root, build, workdir, samples))
    result["workloads"].update(record_viewers(root, build, workdir, samples))
    return result


def main() -> int:
    """Write complete measurements; never infer success from available registrations."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--workdir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=7)
    args = parser.parse_args()
    if not 3 <= args.samples <= 30:
        parser.error("3–30 independent samples required")
    source, build, workdir = (path.resolve() for path in
                              (args.source, args.build, args.workdir))
    if source in workdir.parents or source == workdir:
        parser.error("Keep measurement outputs outside the source tree")
    if not (source / "meson.build").is_file() or not (build / "meson-info").is_dir():
        parser.error("Supply the exact source tree and completed Meson build")
    workdir.mkdir(parents=True, exist_ok=True)
    try:
        result = collect(source, build, workdir, args.samples)
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired,
            subprocess.CalledProcessError) as error:
        result = {"status": "blocked", "reason": str(error)}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        print(json.dumps(result, indent=2, sort_keys=True))
        return 1
    result["status"] = "measured_not_compared"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"Wrote {len(result['workloads'])} unqualified measurements to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
