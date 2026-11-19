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
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Event, Thread

GVFS_CHUNK_SIZE = 256 * 1024
GVFS_SOURCE_LIMIT = 1024**3

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
    """Measures native decoding and export operations using the benchmark driver."""
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
    """Enumerates child process IDs for memory and cancellation accounting."""
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
    """Reads process resident memory in KiB for benchmark sampling."""
    try:
        status = (Path("/proc") / str(pid) / "status").read_text()
    except OSError:
        return 0, ""
    lines = dict(line.split(":", 1) for line in status.splitlines() if ":" in line)
    return int(lines.get("VmHWM", "0").split()[0]), lines["Name"].strip()


def disk_usage(root: Path) -> int:
    """Measures storage occupied by staged or published benchmark files."""
    total = 0
    for path in root.rglob("*"):
        try:
            if path.is_file():
                total += path.stat().st_size
        except FileNotFoundError:
            pass  # Worker staging files may be atomically removed while sampling.
    return total


@contextmanager
def loopback_fixture(fixture: Path) -> Iterator[str]:
    """Serves a local HTTP fixture to reproduce remote-source behavior."""
    png = fixture.read_bytes()
    chunk = bytes(GVFS_CHUNK_SIZE)

    class Handler(BaseHTTPRequestHandler):
        """Handles HTTP fixture responses without contacting external servers."""
        def do_HEAD(self) -> None:
            """Returns headers for a known fixture so GIO can probe its metadata."""
            self.respond(False)

        def do_GET(self) -> None:
            """Streams fixture bytes to the benchmark client."""
            self.respond(True)

        def log_message(self, _format: str, *args: object) -> None:
            """Suppresses or captures incidental HTTP server logging."""
            pass

        def respond(self, send_body: bool) -> None:
            """Sends a controlled HTTP response for the fixture server."""
            data = png if self.path == "/gvfs" else chunk
            slow = self.path == "/gvfs-cancel"
            oversize = self.path == "/gvfs-limit"
            length = (
                GVFS_SOURCE_LIMIT + 1 if oversize else len(data) * (2 if slow else 1)
            )
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(length))
            self.send_header("Last-Modified", "Mon, 01 Jan 2024 00:00:00 GMT")
            self.end_headers()
            if send_body:
                try:
                    if oversize:
                        for _ in range(GVFS_SOURCE_LIMIT // len(chunk)):
                            self.wfile.write(chunk)
                        self.wfile.write(b"\0")
                    else:
                        self.wfile.write(data)
                    if slow:
                        self.wfile.flush()
                        # The client cancels its second read while this body is incomplete.
                        closed.wait(10)
                except OSError:
                    pass  # The client may cancel before receiving the complete body.

    closed = Event()
    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield f"http://127.0.0.1:{server.server_port}"
        finally:
            closed.set()
            server.shutdown()
            thread.join()


def measure(
    executable: Path,
    operation: str,
    source: str,
    root: Path,
    worker: Path,
    fault_worker: Path | None = None,
) -> dict:
    """Collects timing and memory measurements for a configured workload."""
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
        MAJORIS_IMAGE_WORKER=str(worker),
        MAJORIS_IMAGE_PROFILE="1",
    )
    if fault_worker:
        env["IMAGEKIT_BENCHMARK_FAULT_WORKER"] = str(fault_worker.resolve())
    command = [executable, operation, source]
    if operation == "gvfs-write-error":
        command = ["prlimit", "--fsize=131072:131072", "--", *command]
    host_peak = worker_peak = concurrency = temporary_bytes = 0
    with tempfile.TemporaryFile(mode="w+") as log:
        began = time.monotonic()
        with subprocess.Popen(
            command,
            stdout=log,
            stderr=log,
            env=env,
            restore_signals=operation != "gvfs-write-error",
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
    exported = {}
    if operation.startswith(("export-", "transfer-")):
        destination = staging / "benchmark-export"
        with destination.open("rb") as output_file:
            exported = {
                "bytes_transferred": destination.stat().st_size,
                "output_sha256": hashlib.file_digest(output_file, "sha256").hexdigest(),
            }
        if operation.startswith("transfer-") and operation.endswith("-symlink"):
            referent = Path("benchmark-referent")
            if not destination.is_symlink() or destination.readlink() != referent:
                raise RuntimeError(f"{operation} changed the destination link")
            (staging / referent).unlink()
        destination.unlink()
    if any(staging.iterdir()):
        raise RuntimeError(f"{operation} left temporary files or job directories")
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
    phases = {}
    fallback_metadata = {}
    for line in output.splitlines():
        if line.startswith("ImageKit-profile "):
            _, stage, elapsed, quantity = line.split()
            total = phases.setdefault(stage, {"events": 0, "us": 0, "quantity": 0})
            total["events"] += 1
            total["us"] += int(elapsed)
            total["quantity"] += int(quantity)
        if line.startswith("Fallback-pixels "):
            fallback_metadata["output_sha256"] = line.split()[1]
        if line.startswith("Fallback-color "):
            fallback_metadata["color_diagnostic"] = line.removeprefix("Fallback-color ")
    worker_launches = len(starts.read_text().splitlines())
    if operation == "cancel" and worker_launches != 1:
        raise RuntimeError("Worker cancellation requires exactly one sandbox launch")
    if (
        operation in {"gvfs-cancel", "gvfs-write-error", "gvfs-limit"}
        and worker_launches
    ):
        raise RuntimeError(f"{operation} launched a worker after incomplete staging")
    return {
        "phases": phases,
        "wall_seconds": wall,
        "warm_open_seconds": measured,
        "cpu_seconds_process_and_waited_children": usage.ru_utime + usage.ru_stime,
        "wait4_peak_rss_kib": usage.ru_maxrss,
        "sampled_host_peak_kib": host_peak or None,
        "sampled_worker_peak_kib": worker_peak or None,
        "worker_launches": worker_launches,
        "sampled_concurrent_workers": concurrency,
        "sampled_temp_and_cache_bytes": temporary_bytes,
        **exported,
        **fallback_metadata,
    }


def codec_versions() -> dict[str, str]:
    """Records installed codec versions to make benchmark results reproducible."""
    result = {}
    for name in (
        "glib-2.0",
        "gio-2.0",
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
    """Runs the selected benchmark workloads and writes the comparison report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--worker", type=Path)
    parser.add_argument("--fault-worker", type=Path)
    remote = parser.add_mutually_exclusive_group()
    remote.add_argument("--remote-uri", help="GVfs URI serving the gvfs= fixture bytes")
    remote.add_argument(
        "--loopback-gvfs", action="store_true", help="Serve GVfs fixtures locally"
    )
    parser.add_argument("--revision", help="Source revision when using a staged tree")
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
        "revision": args.revision
        or subprocess.run(
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
                "worker_profiling",
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
        result["bubblewrap_version"] = subprocess.check_output(
            [real_bwrap, "--version"], text=True, timeout=5
        ).strip()
        result["temporary_storage"] = str(root.parent.resolve())
        result["sample_mode"] = "fresh process and job storage, OS caches retained"
        for workload in args.workload:
            mode, marker, name = workload.partition("=")
            if not mode or not marker or not Path(name).is_file():
                parser.error(f"Missing or invalid workload fixture: {workload}")
            fixture = Path(name).resolve()
            fixture_identity = fixture.name
            if mode == "cancel":
                fixture_identity = "worker-fault stall token"
                digest = hashlib.sha256(b"T").hexdigest()
            elif mode == "gvfs-limit":
                fixture_identity = "1 GiB plus one byte, all zero (synthetic)"
                digest = hashlib.sha256(
                    f"zero-filled:{GVFS_SOURCE_LIMIT + 1}".encode()
                ).hexdigest()
            elif mode in {"gvfs-cancel", "gvfs-write-error"}:
                fixture_identity = "256 KiB zero-filled HTTP body"
                digest = hashlib.sha256(bytes(GVFS_CHUNK_SIZE)).hexdigest()
            else:
                with fixture.open("rb") as input_file:
                    digest = hashlib.file_digest(input_file, "sha256").hexdigest()
            if mode == "gvfs" and not (args.remote_uri or args.loopback_gvfs):
                parser.error("gvfs workload requires --remote-uri or --loopback-gvfs")
            if (
                mode in {"gvfs-cancel", "gvfs-write-error", "gvfs-limit"}
                and not args.loopback_gvfs
            ):
                parser.error(f"{mode} requires --loopback-gvfs")
            if mode in {"cancel", "fallback16"} and not args.fault_worker:
                parser.error(f"{mode} workload requires --fault-worker")
            worker = (
                args.fault_worker
                if mode in {"fallback", "cancel"} and args.fault_worker
                else args.worker
            ).resolve()
            source = args.remote_uri if mode == "gvfs" else str(fixture)
            if mode.startswith("gvfs") and args.loopback_gvfs:
                with loopback_fixture(fixture) as address:
                    source = f"{address}/{mode}"
                    samples = [
                        measure(args.executable.resolve(), mode, source, root, worker)
                        for _ in range(args.samples)
                    ]
            else:
                samples = [
                    measure(
                        args.executable.resolve(),
                        mode,
                        source,
                        root,
                        worker,
                        args.fault_worker if mode == "fallback16" else None,
                    )
                    for _ in range(args.samples)
                ]
            key = "warm_open_seconds" if mode == "warm" else "wall_seconds"
            phase = "fallback-open" if mode == "fallback16" else "workload"
            durations = sorted(
                sample[key]
                if mode == "warm"
                else sample["phases"].get(phase, {}).get("us", 0) / 1e6
                or sample[key]
                for sample in samples
            )
            median = statistics.median(durations)
            stats = {
                "fixture": fixture_identity,
                "fixture_sha256": digest,
                "source": source,
                "source_type": "GVfs"
                if mode.startswith("gvfs")
                else ("stream" if mode in {"stream", "cancel"} else "local"),
                "worker": str(worker),
                "worker_sha256": hashlib.sha256(worker.read_bytes()).hexdigest(),
                "count": len(durations),
                "median_seconds": median,
                "p95_seconds": durations[math.ceil(len(durations) * 0.95) - 1],
                "min_seconds": durations[0],
                "max_seconds": durations[-1],
                "mad_seconds": statistics.median(
                    abs(value - median) for value in durations
                ),
                "samples": samples,
            }
            base = old.get("workloads", {}).get(mode)
            if base and base.get("fixture_sha256") == digest:
                stats["baseline_median_ratio"] = median / base["median_seconds"]
            result["workloads"][mode] = stats
    return emit_result(result, args.output)


def emit_result(result: dict, output: Path | None) -> int:
    """Serializes one benchmark result with its environment metadata."""
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
