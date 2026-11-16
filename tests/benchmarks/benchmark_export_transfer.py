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
    digest = hashlib.sha256()
    pattern = random.Random(12345)
    with path.open("wb") as output:
        for offset in range(0, size, 256 * 1024):
            chunk = pattern.randbytes(min(256 * 1024, size - offset))
            output.write(chunk)
            digest.update(chunk)
    return digest.hexdigest()


def summarize(samples: list[dict], size: int) -> dict:
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


def compare(executable: Path, root: Path, size: int, count: int, suffix: str = "") -> dict:
    fixture = root / "encoded"
    digest = make_fixture(fixture, size)
    samples = {"transfer-baseline": [], "transfer-stream": []}
    for _ in range(count):
        for mode in samples:
            sample = measure(executable, mode + suffix, str(fixture), root, executable)
            if sample["bytes_transferred"] != size or sample["output_sha256"] != digest:
                raise RuntimeError(f"{mode} changed the {size}-byte fixture")
            samples[mode].append(sample)
    return {mode: summarize(runs, size) for mode, runs in samples.items()}


def measure_cancellation(executable: Path, root: Path, count: int) -> dict:
    # The last workload leaves the 256 MiB fixture in place.
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("--samples must be positive")
    with tempfile.TemporaryDirectory(prefix="imagekit-export-bench-") as temporary:
        root = Path(temporary)
        executable = args.executable.resolve()
        result = {
            "system": platform.platform(),
            "codecs": codec_versions(),
            "temporary_storage": str(root.parent),
            "sample_mode": "fresh process, warm fixture, OS caches retained",
            "destinations": {
                kind: compare(executable, root, 1024**2, args.samples, f"-{kind}")
                for kind in ("new", "symlink")
            },
            "workloads": {
                str(size): compare(executable, root, size, args.samples)
                for size in [1024, *(mib * 1024**2 for mib in (1, 16, 64, 128, 256))]
            },
            "cancellation": measure_cancellation(executable, root, args.samples),
        }
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
