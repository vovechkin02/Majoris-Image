"""Compare the bounded worker reader with its previous asynchronous GIO sequence."""

import argparse
import hashlib
import json
import os
import platform
import shutil
import statistics
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

MIB = 1024**2
WORKLOADS = {
    "json-1-kib": ("json", 1024, MIB),
    "json-1-mib": ("json", MIB, MIB),
    "png-1-mib": ("png", MIB, 8 * MIB),
    "png-8-mib": ("png", 8 * MIB, 8 * MIB),
    **{
        f"mif1-{n}-mib": ("mif1", n * MIB + 12, 256 * MIB + 12)
        for n in (16, 64, 128, 256)
    },
    **{mode: ("mif1", MIB + 12, MIB + 12) for mode in ("cancel", "grow", "truncate")},
}
FAULT_OUTCOMES = {
    "cancel": "canceled",
    "grow": "invalid-response",
    "truncate": "invalid-response",
}


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data))
    )


def make_fixture(path: Path, kind: str, size: int) -> str:
    with path.open("wb") as stream:
        if kind == "json":
            stream.write(b'{"fixture":"' + b"a" * (size - 14) + b'"}')
        elif kind == "png":
            header = b"\x89PNG\r\n\x1a\n" + png_chunk(
                b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
            )
            pixels = png_chunk(b"IDAT", zlib.compress(b"\0\x01\x02\x03"))
            end = png_chunk(b"IEND", b"")
            padding = bytes(size - len(header) - len(pixels) - len(end) - 12)
            stream.write(header + png_chunk(b"paDd", padding) + pixels + end)
        else:
            stream.write(struct.pack(">4sII", b"MIF1", 4096, (size - 12) // 65536))
            row = struct.pack("<ffff", 0.25, 0.5, 1.0, 1.0) * 4096
            for _ in range((size - 12) // len(row)):
                stream.write(row)
        stream.flush()
        os.fsync(stream.fileno())
    assert path.stat().st_size == size
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare_cache(path: Path, cache: str) -> None:
    with path.open("rb") as stream:
        if cache == "cold-advised":
            os.posix_fadvise(stream.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
        else:
            while stream.read(MIB):
                pass


def measure(
    executable: Path,
    root: Path,
    name: str,
    fixture: Path,
    limit: int,
    digest: str,
    samples: int,
) -> dict:
    mode = name if name in FAULT_OUTCOMES else "normal"
    expected = FAULT_OUTCOMES.get(name, "ok")
    results = {}
    for cache in ("cold-advised", "warm") if mode == "normal" else ("warm",):
        runs = {"baseline": [], "bounded": []}
        for sample in range(samples):
            # Alternate the order to reduce systematic timing bias.
            order = tuple(runs) if sample % 2 == 0 else tuple(reversed(runs))
            for reader in order:
                path = fixture
                if mode != "normal":
                    path = root / "fault-output"
                    shutil.copyfile(fixture, path)
                prepare_cache(path, cache)
                process = subprocess.run(
                    [executable, reader, mode, path, str(limit)],
                    capture_output=True,
                    text=True,
                    check=True,
                    timeout=60,
                )
                result = json.loads(process.stdout)
                if mode == "normal":
                    assert result["outcome"] == "ok" and result["sha256"] == digest
                elif reader == "bounded":
                    assert (
                        result["outcome"] == expected and result["returned_bytes"] == 0
                    )
                if reader == "bounded":
                    assert 0 < result["bytes_consumed"] <= limit
                    assert result["read_calls"] > 0
                    assert result["output_allocations"] == 1
                runs[reader].append(result)
        results[cache] = {
            reader: {
                "samples": values,
                "median_read_us": statistics.median(s["read_us"] for s in values),
                "median_peak_rss_kib": statistics.median(
                    s["peak_rss_kib"] for s in values
                ),
            }
            for reader, values in runs.items()
        }
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("--samples must be positive")
    report = {"platform": platform.platform(), "workloads": {}}
    with tempfile.TemporaryDirectory(prefix="worker-output-benchmark-") as temporary:
        root = Path(temporary).resolve()
        fixture = root / "fixture"
        for name, (kind, size, limit) in WORKLOADS.items():
            digest = make_fixture(fixture, kind, size)
            report["workloads"][name] = {
                "file_bytes": size,
                "limit": limit,
                "fixture_sha256": digest,
                "results": measure(
                    args.executable.resolve(),
                    root,
                    name,
                    fixture,
                    limit,
                    digest,
                    args.samples,
                ),
            }
    output = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(output)
    else:
        print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
