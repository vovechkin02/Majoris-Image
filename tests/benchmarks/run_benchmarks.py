"""Measure repeatable public API and native workloads and emit a JSON baseline."""

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

NATIVE_WORKLOADS = {
    **{
        f"mif1-{bands}b-{size}": ("mif1", width, height, bands, 32, 0, 0)
        for bands in (3, 4)
        for size, width, height in (
            ("small", 32, 17),
            ("1mp", 1024, 1024),
            ("4mp", 2048, 2048),
            ("8mp", 4096, 2048),
            ("16mp", 4096, 4096),
        )
    },
    "mif1-lazy-16mp": ("mif1", 4096, 4096, 4, 32, 0, 1),
    "jxl-rgb-4mp": ("jxl", 2048, 2048, 3, 16, 0, 1),
    "jxl-rgb-16mp": ("jxl", 4096, 4096, 3, 16, 0, 1),
    "jxl-rgba-4mp": ("jxl", 2048, 2048, 4, 32, 0, 1),
    "jxl-rgba-16mp": ("jxl", 4096, 4096, 4, 32, 0, 1),
    "jxl-hdr-lossless": ("jxl", 1024, 1024, 4, 32, 1, 1),
}


def measure_native(
    executable: Path, workload: str, root: Path, count: int, references: Path | None
) -> dict:
    fixture = ("coordinate-ramp-v1", *NATIVE_WORKLOADS[workload])
    digest = hashlib.sha256(json.dumps(fixture).encode()).hexdigest()
    path = root / "native-output"
    stats = {"fixture": fixture, "fixture_sha256": digest, "successful": 0, "failed": 0}
    hashes = set()
    for phase, warm in (("cold", 0), ("warm", 1)):
        samples = []
        errors = []
        for _ in range(count):
            command = [executable, *map(str, fixture[1:]), str(warm), path]
            try:
                result = subprocess.run(
                    command, capture_output=True, text=True, check=True, timeout=180
                )
            except subprocess.CalledProcessError as failure:
                errors.append(failure.stderr.strip() or str(failure))
                continue
            except subprocess.TimeoutExpired as failure:
                errors.append(str(failure))
                continue
            sample = json.loads(result.stdout)
            sample["operation_cpu_seconds"] = (
                sample["operation_cpu_ticks"] / sample["cpu_ticks_per_second"]
            )
            samples.append(sample)
            with path.open("rb") as output:
                hashes.add(hashlib.file_digest(output, "sha256").hexdigest())
            if references and phase == "cold" and len(samples) == 1:
                shutil.copyfile(path, references / f"{workload}.output")
        durations = sorted(sample["operation_us"] / 1e6 for sample in samples)
        stats[phase] = {
            "count": len(samples),
            "errors": errors,
            "samples": samples,
            "median_seconds": statistics.median(durations) if durations else None,
            "p95_seconds": durations[math.ceil(len(durations) * 0.95) - 1]
            if len(durations) >= 20
            else None,
            "median_peak_rss_kib": statistics.median(
                sample["peak_rss_kib"] for sample in samples
            )
            if samples
            else None,
        }
        stats["successful"] += len(samples)
        stats["failed"] += len(errors)
    stats["median_seconds"] = stats["cold"]["median_seconds"]
    stats["output_sha256"] = sorted(hashes)
    return stats


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
        "libjxl",
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
    parser.add_argument("--worker", type=Path)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--references", type=Path, help="Save native reference outputs")
    parser.add_argument("workload", nargs="+", metavar="MODE=FIXTURE")
    args = parser.parse_args()
    if args.samples < 3 or args.samples > 30:
        parser.error("--samples must be between 3 and 30")
    if args.native:
        unknown = set(args.workload) - NATIVE_WORKLOADS.keys()
        if unknown:
            parser.error(f"Unknown native workloads: {', '.join(sorted(unknown))}")
        if args.references:
            args.references.mkdir()
    elif not args.worker:
        parser.error("--worker is required for public API workloads")
    elif args.references:
        parser.error("--references requires --native")
    source_root = Path(__file__).resolve().parents[2]
    info = args.build / "meson-info"
    project = json.loads((info / "intro-projectinfo.json").read_text())
    options = json.loads((info / "intro-buildoptions.json").read_text())
    result = {
        "version": project["version"],
        "revision": subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=source_root,
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
    if args.native:
        with args.executable.open("rb") as executable:
            result["native_executable_sha256"] = hashlib.file_digest(
                executable, "sha256"
            ).hexdigest()
    with tempfile.TemporaryDirectory(prefix="imagekit-benchmark-") as temporary:
        if args.native:
            for workload in args.workload:
                stats = measure_native(
                    args.executable.resolve(),
                    workload,
                    Path(temporary),
                    args.samples,
                    args.references,
                )
                base = old.get("workloads", {}).get(workload)
                if base and base.get("fixture_sha256") == stats["fixture_sha256"]:
                    stats["baseline_output_matches"] = (
                        base["output_sha256"] == stats["output_sha256"]
                    )
                    if base["median_seconds"] and stats["median_seconds"]:
                        stats["baseline_median_ratio"] = (
                            stats["median_seconds"] / base["median_seconds"]
                        )
                result["workloads"][workload] = stats
            return emit_result(result, args.output)
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
    return emit_result(result, args.output)


def emit_result(result: dict, output: Path | None) -> int:
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if output:
        output.write_text(encoded)
    print(encoded, end="")
    return int(
        any(
            stats.get("failed", 0)
            or (
                name.startswith("mif1-")
                and stats.get("baseline_output_matches") is False
            )
            for name, stats in result["workloads"].items()
        )
    )


if __name__ == "__main__":
    raise SystemExit(main())
