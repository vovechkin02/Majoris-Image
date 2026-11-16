"""Measure repeatable public API workloads and emit a JSON baseline."""

import argparse
import hashlib
import json
import math
import os
import platform
import shlex
import shutil
import statistics
import subprocess
import tempfile
import time
from collections.abc import Iterator
from pathlib import Path


def descendants(pid: int) -> Iterator[int]:
    try:
        children = (
            Path("/proc") / str(pid) / "task" / str(pid) / "children"
        ).read_text()
    except OSError:
        return
    for child in map(int, children.split()):
        yield child
        yield from descendants(child)


def memory_kib(pid: int) -> tuple[int, str]:
    try:
        status = (Path("/proc") / str(pid) / "status").read_text()
    except OSError:
        return 0, ""
    lines = dict(line.split(":", 1) for line in status.splitlines() if ":" in line)
    return int(lines.get("VmHWM", "0").split()[0]), lines["Name"].strip()


def disk_usage(root: Path) -> int:
    total = 0
    for path in root.rglob("*"):
        try:
            if path.is_file():
                total += path.stat().st_size
        except FileNotFoundError:
            pass  # Worker staging files may be atomically removed while sampling.
    return total


def measure(
    executable: Path, operation: str, fixture: Path, root: Path, worker: Path
) -> dict[str, int | float | None]:
    cache = root / "sample-cache"
    cache.mkdir()
    staging = root / "sample-tmp"
    staging.mkdir()
    starts = root / "worker-starts"
    starts.write_text("")
    env = dict(
        os.environ,
        XDG_CACHE_HOME=str(cache),
        TMPDIR=str(staging),
        IMAGEKIT_WORKER_STARTS=str(starts),
    )
    host_peak = worker_peak = concurrency = temporary_bytes = 0
    with tempfile.TemporaryFile(mode="w+") as log:
        began = time.monotonic()
        with subprocess.Popen(
            [executable, operation, fixture], stdout=log, stderr=log, env=env
        ) as process:
            try:
                while True:
                    waited, status, usage = os.wait4(process.pid, os.WNOHANG)
                    if waited:
                        process.returncode = os.waitstatus_to_exitcode(status)
                        wall = time.monotonic() - began
                        break
                    if time.monotonic() - began > 90:
                        raise TimeoutError(f"{operation} exceeded 90 seconds")
                    host_peak = max(host_peak, memory_kib(process.pid)[0])
                    workers = []
                    for child in descendants(process.pid):
                        rss, name = memory_kib(child)
                        if name in {"worker", worker.name[:15]}:
                            workers.append(rss)
                    concurrency = max(concurrency, len(workers))
                    worker_peak = max(worker_peak, *workers, 0)
                    temporary_bytes = max(
                        temporary_bytes, disk_usage(cache) + disk_usage(staging)
                    )
                    time.sleep(0.02)
            except BaseException:
                process.kill()
                raise
        log.seek(0)
        output = log.read()
        if process.returncode:
            raise RuntimeError(f"{operation} failed ({process.returncode}): {output}")
    shutil.rmtree(cache)
    shutil.rmtree(staging)
    measured = next(
        (
            float(line.split()[1])
            for line in output.splitlines()
            if line.startswith("Warm-open-seconds ")
        ),
        None,
    )
    return {
        "wall_seconds": wall,
        "warm_open_seconds": measured,
        "cpu_seconds_process_and_waited_children": usage.ru_utime + usage.ru_stime,
        "sampled_host_peak_kib": host_peak or None,
        "sampled_worker_peak_kib": worker_peak or None,
        "worker_launches": len(starts.read_text().splitlines()),
        "sampled_concurrent_workers": concurrency,
        "sampled_temp_and_cache_bytes": temporary_bytes,
    }


def codec_versions() -> dict[str, str]:
    result = {}
    for name in (
        "vips",
        "lcms2",
        "libheif",
        "libavif",
        "libraw",
        "libuhdr",
        "glycin-1",
    ):
        check = subprocess.run(
            ["pkg-config", "--modversion", name],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
        if check.returncode == 0:
            result[name] = check.stdout.strip()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("workload", nargs="+", metavar="MODE=FIXTURE")
    args = parser.parse_args()
    if args.samples < 3 or args.samples > 30:
        parser.error("--samples must be between 3 and 30")
    info = args.build / "meson-info"
    project = json.loads((info / "intro-projectinfo.json").read_text())
    options = json.loads((info / "intro-buildoptions.json").read_text())
    result = {
        "version": project["version"],
        "revision": subprocess.run(
            ["git", "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        ).stdout.strip()
        or None,
        "environment": {"system": platform.platform(), "cpu": platform.machine()},
        "build_options": {
            o["name"]: o["value"]
            for o in options
            if o["name"]
            in {
                "gtk",
                "glycin",
                "introspection",
                "sequence",
                "ultrahdr",
                "buildtype",
                "b_sanitize",
            }
        },
        "compilers": json.loads((info / "intro-compilers.json").read_text()),
        "codecs": codec_versions(),
        "workloads": {},
    }
    old = json.loads(args.baseline.read_text()) if args.baseline else {}
    with tempfile.TemporaryDirectory(prefix="imagekit-benchmark-") as temporary:
        root = Path(temporary)
        # Count actual sandbox invocations while preserving Bubblewrap's arguments.
        wrapper = root / "bwrap"
        real_bwrap = shutil.which("bwrap")
        if not real_bwrap:
            parser.error("Bubblewrap is required")
        wrapper.write_text(
            "#!/bin/sh\nprintf 'start\\n' >> \"$IMAGEKIT_WORKER_STARTS\"\nexec "
            + shlex.quote(real_bwrap)
            + ' "$@"\n'
        )
        wrapper.chmod(0o700)
        os.environ["PATH"] = f"{root}:{os.environ['PATH']}"
        os.environ["MAJORIS_IMAGE_WORKER"] = str(args.worker.resolve())
        for workload in args.workload:
            mode, marker, name = workload.partition("=")
            if not mode or not marker or not Path(name).is_file():
                parser.error(f"Missing or invalid workload fixture: {workload}")
            fixture = Path(name).resolve()
            with fixture.open("rb") as source:
                digest = hashlib.file_digest(source, "sha256").hexdigest()
            samples = [
                measure(args.executable.resolve(), mode, fixture, root, args.worker)
                for _ in range(args.samples)
            ]
            key = "warm_open_seconds" if mode == "warm" else "wall_seconds"
            durations = sorted(sample[key] for sample in samples)
            median = statistics.median(durations)
            stats = {
                "fixture": fixture.name,
                "fixture_sha256": digest,
                "count": len(durations),
                "median_seconds": median,
                "p95_seconds": durations[math.ceil(len(durations) * 0.95) - 1],
                "min_seconds": durations[0],
                "max_seconds": durations[-1],
                "samples": samples,
            }
            base = old.get("workloads", {}).get(mode)
            if base and base.get("fixture_sha256") == digest:
                stats["baseline_median_ratio"] = median / base["median_seconds"]
            result["workloads"][mode] = stats
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
