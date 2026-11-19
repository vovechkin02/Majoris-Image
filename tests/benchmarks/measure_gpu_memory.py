"""Measure physical GPU memory using DRM fdinfo or NVIDIA process telemetry."""

import argparse
import json
import os
import shutil
import subprocess
import time
from pathlib import Path


def rss_kib(pid: int) -> int | None:
    """Read a resident-set sample from Linux procfs for a running process."""
    try:
        data = (Path("/proc") / str(pid) / "status").read_text()
        return next(
            int(line.split()[1]) for line in data.splitlines()
            if line.startswith("VmRSS:")
        )
    except (OSError, StopIteration, ValueError):
        return None


def gpu_process_mib(pid: int) -> float | None:
    """Read NVIDIA compute-app GPU usage when the driver exposes a process entry."""
    try:
        rows = subprocess.check_output(
            ["nvidia-smi", "--query-compute-apps=pid,used_gpu_memory",
             "--format=csv,noheader,nounits"],
            text=True, stderr=subprocess.DEVNULL, timeout=3,
        )
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
        return None
    for row in rows.splitlines():
        fields = [field.strip() for field in row.split(",")]
        if len(fields) != 2 or fields[0] != str(pid):
            continue
        try:
            return float(fields[1])
        except ValueError:
            return None
    return None


def parse_drm_resident_mib(contents: str) -> tuple[str, float] | None:
    """Parse driver-reported DRM resident memory for a single GPU client.

    Linux drivers may expose per-region ``drm-resident-*`` counters in fdinfo.
    Non-resident allocation counters are intentionally not substituted because
    they do not measure the same quantity.
    """
    fields = dict(
        line.split(":", 1) for line in contents.splitlines() if ":" in line
    )
    driver = fields.get("drm-driver", "").strip()
    if not driver:
        return None
    resident = 0.0
    found = False
    for key, value in fields.items():
        if not key.startswith("drm-resident-"):
            continue
        items = value.strip().split()
        if len(items) != 2 or items[1] not in ("B", "KiB", "MiB", "GiB"):
            return None
        try:
            count = int(items[0])
        except ValueError:
            return None
        if count < 0:
            return None
        scale = {"B": 1 / (1024 * 1024), "KiB": 1 / 1024,
                 "MiB": 1, "GiB": 1024}[items[1]]
        resident += count * scale
        found = True
    return (driver, resident) if found else None


def drm_process_mib(pid: int) -> tuple[float, str] | None:
    """Sum unique DRM client residency from procfs without double-counting FDs.

    DRM client IDs are scoped to the driver/device. The result omits the
    compositor and shared allocations not attributed to this process.
    """
    directory = Path("/proc") / str(pid) / "fdinfo"
    clients = {}
    try:
        entries = list(directory.iterdir())
    except OSError:
        return None
    for entry in entries:
        try:
            contents = entry.read_text(encoding="utf-8")
        except OSError:
            continue
        measured = parse_drm_resident_mib(contents)
        if measured is None:
            continue
        driver, amount = measured
        fields = dict(
        line.split(":", 1) for line in contents.splitlines() if ":" in line
    )
        client = fields.get("drm-client-id", "").strip()
        device = fields.get("drm-pdev", "").strip()
        if not client:
            continue
        clients[(driver, device, client)] = max(
            amount, clients.get((driver, device, client), 0.0)
        )
    if not clients:
        return None
    return sum(clients.values()), "Linux DRM fdinfo resident per-client memory"


def profile(executable: Path, fixture: Path, cycles: int, viewers: int,
            mode: str) -> dict:
    """Collect host RSS and supported GPU samples from a real display session."""
    status = {
        "driver": "unavailable",
        "host_rss_peak_kib": None, "gpu_memory_peak_mib": None,
        "gpu_samples": 0, "host_samples": 0,
        "gpu_scope": (
            "application process only; compositor allocations are not included"
        ),
    }
    with subprocess.Popen(
        [str(executable), str(fixture), str(cycles), str(viewers), mode],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    ) as process:
        while process.poll() is None:
            host = rss_kib(process.pid)
            if host is not None:
                status["host_samples"] += 1
                status["host_rss_peak_kib"] = max(
                    status["host_rss_peak_kib"] or 0, host
                )
            drm = drm_process_mib(process.pid)
            if drm is not None:
                gpu, method = drm
            else:
                gpu = (
                    gpu_process_mib(process.pid)
                    if shutil.which("nvidia-smi") else None
                )
                method = "NVIDIA compute-app process telemetry"
            if gpu is not None:
                status["driver"] = method
                status["gpu_samples"] += 1
                status["gpu_memory_peak_mib"] = max(
                    status["gpu_memory_peak_mib"] or 0, gpu
                )
            time.sleep(0.2)
        output, _ = process.communicate(timeout=10)
    status["exit_code"] = process.returncode
    status["output"] = output
    status["qualification"] = (
        "measured" if process.returncode == 0 and status["gpu_samples"] > 0 and
        (status["gpu_memory_peak_mib"] or 0) > 0
        else "not_measured" if process.returncode == 0 else "failed"
    )
    return status


def main() -> int:
    """Emit a JSON result; unavailable GPU telemetry must not count as a pass."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=6)
    parser.add_argument("--viewers", type=int, default=8)
    parser.add_argument("--mode", choices=("preview", "tiled"), default="tiled")
    args = parser.parse_args()
    if not (args.executable.is_file() and args.fixture.is_file()):
        parser.error("The compiled benchmark and image fixture must exist")
    if not 1 <= args.cycles <= 50 or not 1 <= args.viewers <= 16:
        parser.error("Cycle/viewer counts exceed the bounded benchmark range")
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        parser.error("A real graphical session is required; Xvfb is not a GPU test")
    if os.environ.get("GSK_RENDERER") == "cairo":
        parser.error("A Cairo renderer cannot qualify physical GPU allocations")
    result = profile(args.executable.resolve(), args.fixture.resolve(),
                     args.cycles, args.viewers, args.mode)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["qualification"] == "measured" else 1


if __name__ == "__main__":
    raise SystemExit(main())
