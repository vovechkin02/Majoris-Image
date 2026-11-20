#!/usr/bin/env python3
"""Run a 20-cycle GTK memory-retention diagnosis without claiming leak status.

The benchmark runs in its own process. Per-cycle RSS, process high-water and
optional /proc/self/smaps_rollup values are diagnostic evidence, not a matched
performance release pass or proof that a retained allocation remains live.
"""

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
from pathlib import Path


CYCLE = re.compile(
    r"Viewer-memory cycle=(\d+) live_rss_kib=(\d+) "
    r"settled_rss_kib=(\d+) peak_rss_kib=(\d+)"
)
INITIAL = re.compile(r"Viewer-memory initial_rss_kib=(\d+)")
FINAL = re.compile(r"Viewer-memory final_rss_kib=(\d+) retained_delta_kib=(-?\d+)")
TILED = re.compile(r"Viewer-tiles cycle=(\d+) ready_viewers=(\d+)")
RELEASED = re.compile(r"Viewer-lifetime cycle=(\d+) released_viewers=(\d+)")
DIAGNOSTIC = re.compile(r"Viewer-diagnostic stage=(\w+) cycle=(\d+) (.*)")


def parse_diagnostics(output: str, cycles: int, viewers: int,
                      tiled: bool) -> dict:
    """Extract ordered lifecycle and optional Linux memory evidence."""
    first, last = INITIAL.search(output), FINAL.search(output)
    entries = [dict(zip(("cycle", "live_kib", "settled_kib", "hwm_kib"),
                        map(int, match))) for match in CYCLE.findall(output)]
    ordered = [entry["cycle"] for entry in entries]
    released = [(int(cycle), int(count)) for cycle, count in RELEASED.findall(output)]
    tiles = [(int(cycle), int(count)) for cycle, count in TILED.findall(output)]
    expected = list(range(1, cycles + 1))
    result = {"status": "inconclusive", "cycles": entries, "viewers": viewers,
              "diagnostics": [], "high_water_consistent": False}
    if (first is None or last is None or ordered != expected or
            released != [(cycle, viewers) for cycle in expected] or
            (tiled and tiles != [(cycle, viewers) for cycle in expected])):
        result["reason"] = "Incomplete viewer, tile or finalization evidence"
        return result
    for stage, cycle, values in DIAGNOSTIC.findall(output):
        fields = {"stage": stage, "cycle": int(cycle)}
        for field in values.split():
            if "=" not in field:
                continue
            key, value = field.split("=", 1)
            try:
                fields[key] = int(value)
            except ValueError:
                fields[key] = value
        result["diagnostics"].append(fields)
    complete_stages = {
        (entry["stage"], entry["cycle"]) for entry in result["diagnostics"]
        if "smaps_rss_kib" in entry and "smaps_pss_kib" in entry
    }
    expected_stages = {
        (stage, cycle) for cycle in expected for stage in ("live", "settled")
    }
    expected_stages.update({("initial", 0), ("final", cycles)})
    result["smaps_qualification"] = (
        "measured" if expected_stages.issubset(complete_stages)
        else "not_measured"
    )
    initial = int(first.group(1))
    final, reported_delta = int(last.group(1)), int(last.group(2))
    if final - initial != reported_delta:
        result["reason"] = "Post-release RSS delta does not match raw counters"
        return result
    peaks = [entry["hwm_kib"] for entry in entries]
    consistent = all(
        entry["hwm_kib"] >= max(entry["live_kib"], entry["settled_kib"])
        and (index == 0 or entry["hwm_kib"] >= peaks[index - 1])
        for index, entry in enumerate(entries)
    )
    result.update({
        "status": "diagnostic_complete", "initial_rss_kib": initial,
        "final_rss_kib": final, "retained_delta_kib": reported_delta,
        "last_five_settled_delta_kib": (
            entries[-1]["settled_kib"] - entries[-5]["settled_kib"]
        ) if cycles >= 5 else None,
        "high_water_consistent": consistent,
        "high_water_note": (
            "Linux VmHWM disagrees with sampled RSS; the peak cannot be "
            "accepted for performance qualification"
        ) if not consistent else (
            "Counters appear consistent; GPU memory is not measured"
        ),
        "interpretation": (
            "Retained RSS may include allocator, libvips, GTK/GDK and kernel "
            "caches; this result alone does not establish a memory leak"
        ),
    })
    return result


def run_diagnosis(executable: Path, fixture: Path, log: Path, cycles: int,
                  viewers: int, mode: str, timeout_seconds: int,
                  xvfb: bool) -> dict:
    """Run the benchmark with a deadline and preserve its diagnostic log."""
    command = [str(executable), str(fixture), str(cycles), str(viewers), mode]
    if xvfb:
        launcher = shutil.which("xvfb-run")
        if launcher is None:
            return {"status": "blocked", "reason": "xvfb-run is unavailable"}
        command = [launcher, "-a", "-s", "-screen 0 1600x900x24"] + command
    environment = dict(os.environ, IMAGEKIT_MEMORY_DIAGNOSTICS="1")
    if xvfb:
        environment.update(GDK_BACKEND="x11", GSK_RENDERER="cairo", GDK_SCALE="1")
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("wb") as stream:
        process = subprocess.Popen(
            command, stdout=stream, stderr=subprocess.STDOUT,
            env=environment, start_new_session=True,
        )
        try:
            process.wait(timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=2)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                pass
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            return {"status": "blocked", "reason": "Diagnostic execution timed out"}
        if process.returncode != 0:
            return {"status": "blocked", "reason": (
                f"Benchmark exited with status {process.returncode}")}
    return parse_diagnostics(log.read_text(encoding="utf-8", errors="replace"),
                             cycles, viewers, mode == "tiled")


def main() -> int:
    """Write diagnostic JSON without describing an unmeasured leak as proven."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--cycles", type=int, default=20)
    parser.add_argument("--viewers", type=int, default=8)
    parser.add_argument("--mode", choices=("preview", "tiled"), default="tiled")
    parser.add_argument("--timeout-seconds", type=int, default=1800)
    parser.add_argument("--xvfb", action="store_true")
    args = parser.parse_args()
    if (not 5 <= args.cycles <= 50 or not 1 <= args.viewers <= 16
            or args.timeout_seconds < 1):
        parser.error("Cycles must be 5–50, viewers 1–16 and timeout positive")
    if not args.executable.is_file() or not args.fixture.is_file():
        parser.error("Provide an existing benchmark executable and image fixture")
    if not args.xvfb and not (os.environ.get("DISPLAY") or
                             os.environ.get("WAYLAND_DISPLAY")):
        parser.error("Provide an active display or use --xvfb for host-only evidence")
    log = args.log or args.output.with_suffix(".log")
    result = run_diagnosis(args.executable.resolve(), args.fixture.resolve(), log,
                           args.cycles, args.viewers, args.mode,
                           args.timeout_seconds, args.xvfb)
    result["log"] = str(log)
    result["gpu_memory_qualification"] = "not_measured"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n",
                           encoding="utf-8")
    print(json.dumps({"status": result["status"], "report": str(args.output)}))
    return 0 if result["status"] == "diagnostic_complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
