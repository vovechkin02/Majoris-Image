"""Measure GIO publication and probe GVfs WebDAV failure behavior on loopback."""

import argparse
import hashlib
import json
import os
import platform
import random
import shutil
import signal
import statistics
import struct
import subprocess
import tempfile
import time
import zlib
from collections.abc import Iterator
from contextlib import contextmanager
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Event, Thread
from urllib.parse import urlsplit
from urllib.request import urlopen
from xml.sax.saxutils import escape

from run_benchmarks import descendants, disk_usage, memory_kib

CHUNK_BYTES = 256 * 1024
MIB = 1024**2


def digest(path: Path) -> str:
    with path.open("rb") as contents:
        return hashlib.file_digest(contents, "sha256").hexdigest()


def make_png(reference: Path, output: Path, size: int) -> str:
    """Pad a small PNG with a deterministic, valid private ancillary chunk."""
    png = reference.read_bytes()
    padding = size - len(png) - 12
    if png[-8:-4] != b"IEND" or padding < 0:
        raise ValueError("Expected a small PNG ending with IEND")
    pattern = random.Random(12345)
    kind = b"paDd"
    crc = zlib.crc32(kind)
    with output.open("wb") as fixture:
        fixture.write(png[:-12])
        fixture.write(struct.pack(">I4s", padding, kind))
        for offset in range(0, padding, CHUNK_BYTES):
            chunk = pattern.randbytes(min(CHUNK_BYTES, padding - offset))
            crc = zlib.crc32(chunk, crc)
            fixture.write(chunk)
        fixture.write(struct.pack(">I", crc))
        fixture.write(png[-12:])
    return digest(output)


class DavServer(ThreadingHTTPServer):
    def __init__(self, store: Path, previous: bytes):
        super().__init__(("127.0.0.1", 0), DavHandler)
        self.store = store
        self.previous = previous
        self.fault = ""
        self.put_started = Event()
        self.put_finished = Event()
        self.move_started = Event()
        self.move_finished = Event()
        self.release = Event()
        self.requests = []
        self.backend_pid = 0

    @property
    def uri(self) -> str:
        return f"dav://127.0.0.1:{self.server_port}"


class DavHandler(BaseHTTPRequestHandler):
    def log_request(self, code="-", size="-"):
        self.server.requests.append((self.command, self.path, str(code)))

    def target(self, uri: str) -> Path:
        return self.server.store / Path(urlsplit(uri).path).name

    def respond(self, status: int, content: bytes = b"", **headers: str) -> None:
        self.send_response(status)
        self.send_header("Content-Length", str(len(content)))
        for name, value in headers.items():
            self.send_header(name.replace("_", "-"), value)
        self.end_headers()
        if content and self.command != "HEAD":
            self.wfile.write(content)

    def do_OPTIONS(self):
        self.respond(
            200, DAV="1", Allow="OPTIONS, PROPFIND, HEAD, GET, PUT, MOVE, DELETE"
        )

    def do_PROPFIND(self):
        self.rfile.read(int(self.headers.get("Content-Length", "0")))
        file = self.target(self.path)
        directory = urlsplit(self.path).path == "/"
        if not directory and not file.is_file():
            self.respond(404)
            return
        kind = "<D:collection/>" if directory else ""
        size = 0 if directory else file.stat().st_size
        xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<D:multistatus xmlns:D="DAV:"><D:response><D:href>{escape(self.path)}</D:href>
<D:propstat><D:prop><D:resourcetype>{kind}</D:resourcetype>
<D:getcontentlength>{size}</D:getcontentlength><D:getetag>"probe"</D:getetag>
</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>"""
        self.respond(207, xml.encode(), Content_Type="application/xml")

    def do_GET(self):
        file = self.target(self.path)
        if not file.is_file():
            self.respond(404)
            return
        with file.open("rb") as source:
            self.send_response(200)
            self.send_header("Content-Length", str(os.fstat(source.fileno()).st_size))
            self.end_headers()
            if self.command != "HEAD":
                shutil.copyfileobj(source, self.wfile, CHUNK_BYTES)

    do_HEAD = do_GET

    def do_PUT(self):
        try:
            self.publish()
        finally:
            self.server.put_finished.set()

    def publish(self):
        target = self.target(self.path)
        fault = self.server.fault
        if fault != "cancel-after-server-commit":
            self.server.put_started.set()
        rejections = {
            "forbidden": 403,
            "quota": 507,
            "protocol-error": 500,
            "missing-parent": 409,
            "authentication": 401,
        }
        if fault in rejections or fault == "cancel-close":
            remaining = int(self.headers["Content-Length"])
            while remaining:
                chunk = self.rfile.read(min(CHUNK_BYTES, remaining))
                if not chunk:
                    return
                remaining -= len(chunk)
            if fault == "cancel-close":
                self.server.release.wait(15)
                self.respond(503)
            elif fault == "authentication":
                self.respond(401, WWW_Authenticate='Basic realm="export-probe"')
            else:
                self.respond(rejections[fault])
            return
        if fault == "disconnect":
            self.rfile.read(1)
            self.log_request("dropped")
            return
        status = 204 if target.exists() else 201
        remaining = int(self.headers["Content-Length"])
        with tempfile.NamedTemporaryFile(dir=self.server.store, delete=False) as output:
            temporary = Path(output.name)
            try:
                while remaining:
                    chunk = self.rfile.read(min(CHUNK_BYTES, remaining))
                    if not chunk:
                        return
                    output.write(chunk)
                    remaining -= len(chunk)
                output.close()
                temporary.replace(target)
            finally:
                temporary.unlink(missing_ok=True)
        if fault == "lost-response":
            self.log_request("committed-response-dropped")
            return
        if fault == "cancel-after-server-commit":
            self.server.put_started.set()
            self.server.release.wait(15)
        self.respond(status)

    def do_MOVE(self):
        try:
            self.promote()
        finally:
            self.server.move_finished.set()

    def promote(self):
        self.server.move_started.set()
        if self.server.fault in ("move-denied", "cleanup-denied"):
            self.respond(403)
            return
        if self.server.fault == "cancel-move":
            self.server.release.wait(15)
            self.respond(409)
            return
        self.target(self.path).replace(self.target(self.headers["Destination"]))
        self.respond(204)

    def do_DELETE(self):
        if self.server.fault == "cleanup-denied":
            self.respond(403)
            return
        self.target(self.path).unlink(missing_ok=True)
        self.respond(204)


def backend_pids() -> set[int]:
    result = subprocess.run(
        ["ps", "-C", "gvfsd-dav", "-o", "pid="], capture_output=True, text=True
    )
    return {int(pid) for pid in result.stdout.split()}


def backend_rss(pid: int) -> int:
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except FileNotFoundError:
        pass
    return 0


def mount(dav: DavServer) -> None:
    previous_pids = backend_pids()
    subprocess.run(["gio", "mount", dav.uri], check=True, timeout=20)
    (dav.backend_pid,) = backend_pids() - previous_pids


def collect(
    command: list, environment: dict, dav: DavServer, worker: Path | None = None
) -> dict:
    backend_idle = backend_peak = backend_rss(dav.backend_pid)
    worker_peak = disk_peak = server_peak = 0
    cancel_at = None
    event = dav.move_started if dav.fault == "cancel-move" else dav.put_started
    should_cancel = dav.fault in (
        "cancel-close",
        "cancel-move",
        "cancel-after-server-commit",
    )
    began = time.monotonic()
    with tempfile.TemporaryFile(mode="w+") as log:
        with subprocess.Popen(
            command, env=environment, stdout=log, stderr=log
        ) as process:
            try:
                while True:
                    waited, status, usage = os.wait4(process.pid, os.WNOHANG)
                    if waited:
                        process.returncode = os.waitstatus_to_exitcode(status)
                        break
                    if should_cancel and cancel_at is None and event.is_set():
                        cancel_at = time.monotonic()
                        process.send_signal(signal.SIGUSR1)
                    if cancel_at is not None and time.monotonic() - cancel_at >= 0.1:
                        # Leave time for the client to observe cancellation before replying.
                        dav.release.set()
                    if time.monotonic() - began > 90:
                        raise TimeoutError(
                            f"Timed out running {command}; DAV {dav.requests}"
                        )
                    backend_peak = max(backend_peak, backend_rss(dav.backend_pid))
                    if worker is not None:
                        for pid in descendants(process.pid):
                            rss, name = memory_kib(pid)
                            if name in {"worker", worker.name[:15]}:
                                worker_peak = max(worker_peak, rss)
                    disk_peak = max(disk_peak, disk_usage(Path(environment["TMPDIR"])))
                    server_peak = max(server_peak, disk_usage(dav.store))
                    time.sleep(0.005)
            finally:
                dav.release.set()
                if process.returncode is None:
                    process.kill()
        elapsed = time.monotonic() - began
        log.seek(0)
        lines = log.read().splitlines()
    phases = {}
    for line in lines:
        if line.startswith("ImageKit-profile "):
            _, name, duration, quantity = line.split()
            phases[name] = {"us": int(duration), "quantity": int(quantity)}
    return {
        "status": process.returncode,
        "caller_canceled": next(
            (
                line.endswith("true")
                for line in lines
                if line.startswith("Caller canceled: ")
            ),
            None,
        ),
        "wall_seconds": elapsed,
        "cpu_seconds": usage.ru_utime + usage.ru_stime,
        "core_peak_rss_kib": usage.ru_maxrss,
        "gvfs_idle_rss_kib": backend_idle,
        "gvfs_sampled_peak_rss_kib": backend_peak,
        "worker_sampled_peak_rss_kib": worker_peak,
        "client_temp_peak_bytes": disk_peak,
        "server_sampled_peak_bytes": server_peak,
        "phases": phases,
        "diagnostics": [
            line for line in lines if not line.startswith("ImageKit-profile ")
        ],
    }


def readback(uri: str, path: Path) -> str:
    url = "http:" + uri.removeprefix("dav:")
    with urlopen(url, timeout=20) as response, path.open("wb") as output:
        shutil.copyfileobj(response, output, CHUNK_BYTES)
    return digest(path)


def sample(
    command: list,
    dav: DavServer,
    root: Path,
    existing: bool,
    fault: str = "",
    worker: Path | None = None,
) -> dict:
    for path in dav.store.iterdir():
        path.unlink()
    dav.requests.clear()
    dav.fault = fault
    dav.put_started.clear()
    dav.put_finished.clear()
    dav.move_started.clear()
    dav.move_finished.clear()
    dav.release.clear()
    target = dav.store / "export"
    if existing:
        target.write_bytes(dav.previous)
    with tempfile.TemporaryDirectory(dir=root) as work:
        staging = Path(work) / "tmp"
        staging.mkdir()
        environment = dict(os.environ, TMPDIR=str(staging), XDG_CACHE_HOME=work)
        if worker is not None:
            environment.update(
                MAJORIS_IMAGE_WORKER=str(worker),
                MAJORIS_IMAGE_PROFILE="1",
            )
        result = collect(command, environment, dav, worker)
        if any(staging.iterdir()):
            raise RuntimeError(
                f"Export left client temporary files: {list(staging.iterdir())}"
            )
    if command[1].startswith("remote-cancel-") or dav.put_started.is_set():
        dav.put_finished.wait(2)
    if dav.move_started.is_set():
        dav.move_finished.wait(2)
    result["backend_exited"] = backend_rss(dav.backend_pid) == 0
    if result["backend_exited"]:
        mount(dav)
    result["requests"] = list(dav.requests)
    result["remote_leftovers"] = sorted(
        path.name for path in dav.store.iterdir() if path != target
    )
    exists = target.exists()
    result["destination_exists"] = exists
    result["bytes_at_destination"] = target.stat().st_size if exists else 0
    result["sha256"] = None
    if exists:
        downloaded = root / "readback"
        result["sha256"] = readback(dav.uri + "/export", downloaded)
        if downloaded.stat().st_size != result["bytes_at_destination"]:
            raise RuntimeError(("Readback length did not match the server", result))
    return result


def successful_samples(
    args: argparse.Namespace, dav: DavServer, root: Path, size: int
) -> dict:
    results = {}
    fixture = root / "encoded.png"
    expected = make_png(args.png, fixture, size)
    for existing in (False, True):
        by_mode = {
            mode: []
            for mode in (
                "remote-bytes-baseline",
                "remote-fallback",
                "remote-stream",
                "remote-stage",
                "remote-push-stage",
            )
        }
        modes = list(by_mode)
        for iteration in range(args.samples):
            offset = iteration % len(modes)
            for mode in modes[offset:] + modes[:offset]:
                result = sample(
                    [args.executable, mode, fixture, dav.uri + "/export"],
                    dav,
                    root,
                    existing,
                )
                if (
                    result["status"]
                    or result["bytes_at_destination"] != size
                    or result["sha256"] != expected
                    or result["remote_leftovers"]
                    or result["backend_exited"]
                ):
                    raise RuntimeError((size, mode, existing, result))
                by_mode[mode].append(result)
        results[f"{size}-{'replace' if existing else 'create'}"] = {
            mode: summarize(samples, size) for mode, samples in by_mode.items()
        }
    return results


def summarize(samples: list[dict], size: int) -> dict:
    durations = [run["phases"]["workload"]["us"] for run in samples]
    return {
        "median_workload_us": statistics.median(durations),
        "range_workload_us": [min(durations), max(durations)],
        "median_mib_per_second": size / MIB * 1e6 / statistics.median(durations),
        **{
            f"median_{key}": statistics.median(run[key] for run in samples)
            for key in ("cpu_seconds", "core_peak_rss_kib", "gvfs_sampled_peak_rss_kib")
        },
        "samples": samples,
    }


def failure_samples(args: argparse.Namespace, dav: DavServer, root: Path) -> dict:
    fixture = root / "encoded.png"
    complete = make_png(args.png, fixture, 16 * MIB)
    previous = hashlib.sha256(dav.previous).hexdigest()
    cases = [
        ("remote-fallback-precanceled", "", 3),
        ("remote-cancel-write", "", 3),
        ("remote-cancel-midway", "", 3),
        ("remote-cancel-before-close", "", 3),
        ("remote-fallback", "cancel-close", 3),
        ("remote-stream", "cancel-close", 3),
        ("remote-fallback", "disconnect", 1),
        ("remote-stream", "disconnect", 1),
        ("remote-fallback", "forbidden", 1),
        ("remote-fallback", "quota", 1),
        ("remote-fallback", "protocol-error", 1),
        ("remote-fallback", "authentication", 1),
        ("remote-fallback", "missing-parent", 1),
        ("remote-fallback", "invalid-destination", 1),
        ("remote-stream", "forbidden", 1),
        ("remote-fallback", "lost-response", 1),
        ("remote-fallback", "cancel-after-server-commit", 3),
        ("remote-fallback-cancel-after-success", "", 0),
        ("remote-stage", "move-denied", 1),
        ("remote-stage", "cleanup-denied", 1),
        ("remote-stage", "cancel-move", 3),
        ("remote-push-stage", "cleanup-denied", 1),
    ]
    results = {}
    for mode, fault, status in cases:
        for existing in (False, True):
            uri = dav.uri + "/export"
            if fault == "invalid-destination":
                uri = "resource:///imagekit/nonexistent/export"
            elif fault == "missing-parent":
                uri = dav.uri + "/missing/export"
            result = sample(
                [args.executable, mode, fixture, uri],
                dav,
                root,
                existing,
                fault,
            )
            expected = (
                complete
                if status == 0
                or fault in ("lost-response", "cancel-after-server-commit")
                else (previous if existing else None)
            )
            expected_hashes = {expected}
            if mode.startswith("remote-cancel-"):
                size = result["phases"]["writes"]["quantity"]
                with fixture.open("rb") as source:
                    checksum = hashlib.sha256()
                    for offset in range(0, size, CHUNK_BYTES):
                        checksum.update(source.read(min(CHUNK_BYTES, size - offset)))
                    expected_hashes.add(checksum.hexdigest())
            if (
                result["status"] != status
                or result["sha256"] not in expected_hashes
            ):
                raise RuntimeError((mode, fault, existing, result))
            if fault == "invalid-destination" or mode.endswith("-precanceled"):
                if any(method == "PUT" for method, *_ in result["requests"]):
                    raise RuntimeError((mode, fault, result))
            if bool(result["remote_leftovers"]) != (fault == "cleanup-denied"):
                raise RuntimeError(result)
            results[
                f"{mode}/{fault or 'cancel'}/{'replace' if existing else 'create'}"
            ] = result
    return results


def stream_faults(args: argparse.Namespace, root: Path) -> dict:
    fixture = root / "encoded.png"
    make_png(args.png, fixture, MIB)
    cases = {
        "short-writes": (0, ""),
        "open-denied": (1, "Injected replacement open failure"),
        "first-write": (1, "Injected write failure"),
        "later-write": (1, "Injected write failure"),
        "cancel-write": (3, ""),
    }
    results = {}
    for fault, (status, message) in cases.items():
        process = subprocess.run(
            [args.executable, "remote-fallback", fixture, f"imagekit-probe:///{fault}"],
            capture_output=True,
            text=True,
            timeout=30,
        )
        canceled = "Caller canceled: true" in process.stdout.splitlines()
        if (
            process.returncode != status
            or message not in process.stderr
            or canceled != (fault == "cancel-write")
        ):
            raise RuntimeError((fault, process))
        results[fault] = {
            "status": process.returncode,
            "caller_canceled": canceled,
            "diagnostics": process.stderr.splitlines(),
        }
    return results


def decode(path: Path, root: Path) -> dict:
    fields = ("vips-loader", "width", "height", "bands", "format")
    decoded = {
        field: subprocess.check_output(
            ["vipsheader", "-f", field, path], text=True, timeout=30
        ).strip()
        for field in fields
    }
    pixels = root / "pixels"
    subprocess.run(["vips", "rawsave", path, pixels], check=True, timeout=30)
    decoded["pixel_sha256"] = digest(pixels)
    pixels.unlink()
    header = subprocess.check_output(["vipsheader", "-a", path], text=True, timeout=30)
    metadata = ("orientation", "icc-profile-data", "exif-data", "iptc-data", "xmp-data")
    decoded["metadata"] = {
        name: value
        for line in header.splitlines()
        for name, _, value in [line.partition(": ")]
        if name in metadata
    }
    return decoded


def public_samples(args: argparse.Namespace, dav: DavServer, root: Path) -> dict:
    results = {}
    for format_name in args.formats:
        operation = f"export-{format_name}"
        command = [args.public_benchmark, operation, args.source]
        with tempfile.TemporaryDirectory(dir=root) as work:
            staging = Path(work) / "tmp"
            staging.mkdir()
            environment = dict(
                os.environ,
                TMPDIR=str(staging),
                XDG_CACHE_HOME=work,
                MAJORIS_IMAGE_WORKER=str(args.worker),
                MAJORIS_IMAGE_PROFILE="1",
            )
            native = collect(command, environment, dav, args.worker)
            if native["status"]:
                raise RuntimeError((format_name, native))
            output = staging / "benchmark-export"
            native["sha256"] = digest(output)
            native["bytes_at_destination"] = output.stat().st_size
            native["decoded"] = decode(output, root)
            # Independent encoder runs can emit different container bytes. Publish
            # this exact encoded file separately to test byte-for-byte transport.
            same_artifact = sample(
                [args.executable, "remote-fallback", output, dav.uri + "/export"],
                dav,
                root,
                True,
            )
            if same_artifact["status"] or same_artifact["sha256"] != native["sha256"]:
                raise RuntimeError((format_name, native, same_artifact))
            output.unlink()
            if any(staging.iterdir()):
                raise RuntimeError("Native export left client temporary files")
        remote = sample(
            [*command, dav.uri + "/export"], dav, root, True, worker=args.worker
        )
        if remote["status"]:
            raise RuntimeError((format_name, remote))
        remote["decoded"] = decode(root / "readback", root)
        if native["decoded"] != remote["decoded"]:
            raise RuntimeError((format_name, native, remote))
        results[format_name] = {
            "native": native,
            "webdav": remote,
            "same_encoded_artifact": same_artifact,
            "independent_encoded_hashes_match": native["sha256"] == remote["sha256"],
        }
    return results


def public_faults(args: argparse.Namespace, dav: DavServer, root: Path) -> dict:
    cases = [
        ("export-png", "", 0, True),
        ("export-png", "forbidden", 1, False),
        ("export-png", "quota", 1, False),
        ("export-png", "protocol-error", 1, False),
        ("export-png", "disconnect", 1, False),
        ("export-png", "lost-response", 1, True),
        ("export-png", "cancel-close", 3, False),
        ("export-png", "cancel-after-server-commit", 3, True),
        ("export-png-precanceled", "", 3, False),
        ("export-png-cancel-after-success", "", 0, True),
    ]
    cases.append(("export-png", "invalid-worker-output", 1, False))
    results = {}
    previous = hashlib.sha256(dav.previous).hexdigest()
    expected_image = None
    for mode, fault, status, committed in cases:
        worker = args.fault_worker if fault == "invalid-worker-output" else args.worker
        for existing in (False, True):
            result = sample(
                [args.public_benchmark, mode, args.source, dav.uri + "/export"],
                dav,
                root,
                existing,
                fault,
                worker,
            )
            if result["status"] != status or result["remote_leftovers"]:
                raise RuntimeError((mode, fault, existing, result))
            canceled = status == 3 or mode.endswith("-cancel-after-success")
            if result["caller_canceled"] != canceled:
                raise RuntimeError((mode, fault, existing, result))
            if committed:
                image = decode(root / "readback", root)
                if expected_image is None:
                    expected_image = image
                if image != expected_image or result["bytes_at_destination"] == 0:
                    raise RuntimeError((mode, fault, existing, result))
            elif result["sha256"] != (previous if existing else None):
                raise RuntimeError((mode, fault, existing, result))
            puts = [request for request in result["requests"] if request[0] == "PUT"]
            before_put = mode.endswith("-precanceled") or fault == "invalid-worker-output"
            if len(puts) != (0 if before_put else 1):
                raise RuntimeError((mode, fault, result))
            if fault == "invalid-worker-output" and not any(
                "Decoder did not produce an output file" in line
                for line in result["diagnostics"]
            ):
                raise RuntimeError((mode, fault, result))
            results[f"{mode}/{fault or 'normal'}/{'replace' if existing else 'create'}"] = result
    return results


@contextmanager
def loopback_mount(root: Path, previous: bytes) -> Iterator[DavServer]:
    with (
        tempfile.TemporaryDirectory(dir=root) as store,
        DavServer(Path(store), previous) as dav,
    ):
        thread = Thread(target=dav.serve_forever, daemon=True)
        thread.start()
        try:
            mount(dav)
            yield dav
        finally:
            try:
                if backend_rss(dav.backend_pid):
                    subprocess.run(
                        ["gio", "mount", "-u", dav.uri], check=False, timeout=20
                    )
            finally:
                dav.shutdown()
                thread.join()


def run(args: argparse.Namespace) -> dict:
    workloads = {}
    previous = args.png.read_bytes()
    with tempfile.TemporaryDirectory(prefix="imagekit-webdav-") as directory:
        root = Path(directory)
        results = {
            "system": platform.platform(),
            "revision": subprocess.check_output(
                ["git", "rev-parse", "HEAD"], text=True, timeout=10
            ).strip(),
            "gio": subprocess.check_output(
                ["gio", "version"], text=True, timeout=10
            ).strip(),
            "storage": str(root.parent),
            "protocol": "WebDAV over HTTP to isolated IPv4 loopback",
            "backend": "gvfsd-dav",
            "workloads": workloads,
        }
        for size in args.sizes:
            with loopback_mount(root, previous) as dav:
                workloads.update(successful_samples(args, dav, root, size))
        if not args.skip_faults:
            results["stream_faults"] = stream_faults(args, root)
            with loopback_mount(root, previous) as dav:
                results["failures"] = failure_samples(args, dav, root)
        if args.public_benchmark:
            with loopback_mount(root, previous) as dav:
                results["public_exports"] = public_samples(args, dav, root)
                if not args.skip_faults:
                    results["public_failures"] = public_faults(args, dav, root)
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=lambda path: Path(path).resolve())
    parser.add_argument("png", type=Path, help="Reference PNG smaller than 1 KiB")
    parser.add_argument("--sizes", type=int, nargs="+", default=[1024, 16 * MIB])
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--skip-faults", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--public-benchmark", type=lambda path: Path(path).resolve())
    parser.add_argument("--worker", type=lambda path: Path(path).resolve())
    parser.add_argument("--fault-worker", type=lambda path: Path(path).resolve())
    parser.add_argument("--source", type=lambda path: Path(path).resolve())
    parser.add_argument("--formats", nargs="+", default=["png", "tiff", "jxl"])
    args = parser.parse_args()
    if args.samples < 1 or any(size < 1024 or size > 256 * MIB for size in args.sizes):
        parser.error(
            "Supply a positive sample count and sizes between 1 KiB and 256 MiB"
        )
    if any((args.public_benchmark, args.worker, args.source)) and not all(
        (args.public_benchmark, args.worker, args.source)
    ):
        parser.error(
            "Public exports require --public-benchmark, --worker, and --source"
        )
    if args.public_benchmark and not args.skip_faults and not args.fault_worker:
        parser.error("Public export fault cases require --fault-worker")
    os.environ["GVFS_DISABLE_FUSE"] = "1"
    args.output.write_text(json.dumps(run(args), indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
