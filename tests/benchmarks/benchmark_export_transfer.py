"""Compare isolated export transfers with deterministic fixtures."""

import argparse
import hashlib
import json
import platform
import random
import statistics
import tempfile
from pathlib import Path

from run_benchmarks import codec_versions, measure


def make_fixture(path: Path, size: int) -> str:
    """Creates a deterministic large export input with reproducible byte content."""
    digest = hashlib.sha256()
    pattern = random.Random(12345)
    with path.open("wb") as output:
        for offset in range(0, size, 256 * 1024):
            chunk = pattern.randbytes(min(256 * 1024, size - offset))
            output.write(chunk)
            digest.update(chunk)
    return digest.hexdigest()


def summarize(samples: list[dict], size: int) -> dict:
    """Summarizes measured transfer duration and process memory for comparison."""
    phase_names = set.intersection(*(set(sample["phases"]) for sample in samples))
    median_us = {
        name: statistics.median(sample["phases"][name]["us"] for sample in samples)
        for name in sorted(phase_names)
    }
    seconds = median_us["workload"] / 1_000_000
    return {
        "median_phase_us": median_us,
        "median_seconds": seconds,
        "median_mib_per_second": size / (1024**2 * seconds),
        "median_peak_rss_kib": statistics.median(
            sample["wait4_peak_rss_kib"] for sample in samples
        ),
        "median_cpu_seconds": statistics.median(
            sample["cpu_seconds_process_and_waited_children"] for sample in samples
        ),
        "peak_staging_payload_bytes": size,
        "payload_buffer_bytes": samples[0]["phases"]["payload-allocation"]["quantity"],
        "samples": samples,
    }


def compare(
    executable: Path,
    previous: Path | None,
    root: Path,
    size: int,
    count: int,
    suffix: str = "",
) -> dict:
    """Compares baseline and candidate export transfers under identical fixtures."""
    fixture = root / "encoded"
    digest = make_fixture(fixture, size)
    workloads = {
        "transfer-baseline": (executable, "transfer-baseline"),
        "transfer-stream": (executable, "transfer-stream"),
    }
    if previous is not None:
        workloads["previous-native"] = (previous, "transfer-stream")
    samples = {name: [] for name in workloads}
    order = list(workloads)
    for repetition in range(count):
        offset = repetition % len(order)
        for name in order[offset:] + order[:offset]:
            binary, mode = workloads[name]
            sample = measure(binary, mode + suffix, str(fixture), root, binary)
            if sample["bytes_transferred"] != size or sample["output_sha256"] != digest:
                raise RuntimeError(f"{name} changed the {size}-byte fixture")
            samples[name].append(sample)
    return {mode: summarize(runs, size) for mode, runs in samples.items()}


def measure_cancellation(executable: Path, root: Path, count: int) -> dict:
    # The last workload leaves the 256 MiB fixture in place.
    """Measures how quickly the export path stops after cancellation."""
    expected = hashlib.sha256(b"existing").hexdigest()
    fixture = str(root / "encoded")
    result = {}
    for mode in ("transfer-baseline-cancel", "transfer-stream-cancel"):
        samples = [
            measure(executable, mode, fixture, root, executable) for _ in range(count)
        ]
        if any(sample["output_sha256"] != expected for sample in samples):
            raise RuntimeError(f"{mode} modified the destination before cancellation")
        result[mode] = {
            "median_cleanup_us": statistics.median(
                sample["phases"]["cancellation-cleanup"]["us"] for sample in samples
            ),
            "samples": samples,
        }
    return result


def measure_rejection(executable: Path, root: Path, count: int) -> dict:
    """Confirms rejected output is not published or retained after validation fails."""
    fixture = root / "changed-source"
    expected = hashlib.sha256(b"existing").hexdigest()
    mode = "transfer-stream-changed"
    samples = []
    for _ in range(count):
        make_fixture(fixture, 1024**2)
        sample = measure(executable, mode, str(fixture), root, executable)
        if sample["output_sha256"] != expected:
            raise RuntimeError("Source-change rejection modified the existing destination")
        samples.append(sample)
    return {
        "median_phase_us": {
            name: statistics.median(sample["phases"][name]["us"] for sample in samples)
            for name in samples[0]["phases"]
        },
        "median_peak_rss_kib": statistics.median(
            sample["wait4_peak_rss_kib"] for sample in samples
        ),
        "samples": samples,
    }


def main() -> int:
    """Parses benchmark options, runs the selected transfer cases, and writes results."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--previous-executable", type=Path)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("--samples must be positive")
    with tempfile.TemporaryDirectory(prefix="imagekit-export-bench-") as temporary:
        root = Path(temporary)
        executable = args.executable.resolve()
        previous = args.previous_executable.resolve() if args.previous_executable else None
        result = {
            "system": platform.platform(),
            "codecs": codec_versions(),
            "temporary_storage": str(root.parent),
            "sample_mode": "fresh process, warm fixture, OS caches retained",
            "destinations": {
                kind: compare(executable, previous, root, 1024**2, args.samples, f"-{kind}")
                for kind in ("new", "symlink")
            },
            "workloads": {
                str(size): compare(executable, previous, root, size, args.samples)
                for size in [1024, *(mib * 1024**2 for mib in (1, 16, 64, 128, 256))]
            },
            "cancellation": measure_cancellation(executable, root, args.samples),
            "source_change_rejection": measure_rejection(executable, root, args.samples),
        }
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
