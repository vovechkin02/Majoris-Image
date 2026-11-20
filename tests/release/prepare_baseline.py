#!/usr/bin/env python3
"""Prepare an earlier source ZIP with the exact current performance harness.

Only benchmark and fixture-generator files are synchronized. Runtime sources,
codec implementations, public APIs and build options remain from the earlier
archive. The destination is a NEW directory, never an in-place overwrite.
"""

import argparse
import hashlib
import json
import stat
import zipfile
from pathlib import Path


# The reference archive predates this qualification suite; it does not
# contain these release-only helpers. They must be installed alongside the
# matched Meson test registration, without modifying its production source.
NEW_SUPPORT_FILES = (
    "tests/release/test_platform_qualification.py",
    "tests/release/qualify_matched.py",
    "tests/release/verify_raw_fixtures.py",
    "tests/release/test_memory_diagnostics.py",
    "tests/release/test_gpu_profiler.py",
    "tests/benchmarks/diagnose_viewer_memory.py",
    "tests/benchmarks/measure_gpu_memory.py",
)

HARNESS_FILES = (
    "tests/benchmarks/benchmark-viewer-lifecycle.vala",
    "tests/benchmarks/benchmark-scheduler.vala",
    "tests/benchmarks/benchmark.vala",
    "tests/benchmarks/run_benchmarks.py",
    "tests/benchmarks/meson.build",
    "tests/meson.build",
    "tests/support/make_apng_benchmarks.py",
    "tests/support/make-fixtures.c",
    "tests/release/collect_performance.py",
) + NEW_SUPPORT_FILES


def prepare(archive: Path, candidate: Path, destination: Path) -> dict:
    """Extract safely and synchronize benchmark-only files for matched runs."""
    if destination.exists():
        raise ValueError("Destination must not exist; use a fresh baseline directory")
    if destination == candidate or candidate in destination.parents:
        raise ValueError("Never prepare the baseline inside the candidate source")
    for name in HARNESS_FILES:
        if not (candidate / name).is_file():
            raise ValueError(f"Candidate benchmark harness is missing {name}")
    destination.mkdir(parents=True)
    original_hashes = {}
    with zipfile.ZipFile(archive) as source:
        if source.testzip() is not None:
            raise ValueError("Earlier archive is corrupt")
        files = set(source.namelist())
        for name in HARNESS_FILES:
            if name not in files and name not in NEW_SUPPORT_FILES:
                raise ValueError(f"Earlier archive lacks required benchmark {name}")
        for member in source.infolist():
            path = destination / member.filename
            if not path.resolve().is_relative_to(destination.resolve()):
                raise ValueError(f"Unsafe archive path: {member.filename}")
            mode = member.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise ValueError(
                    f"Baseline archive contains a symbolic link: {member.filename}"
                )
            if member.is_dir():
                path.mkdir(parents=True, exist_ok=True)
                continue
            data = source.read(member)
            original_hashes[member.filename] = hashlib.sha256(data).hexdigest()
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            path.chmod(mode & 0o777 or 0o644)
    for name in HARNESS_FILES:
        destination.joinpath(name).parent.mkdir(parents=True, exist_ok=True)
        destination.joinpath(name).write_bytes((candidate / name).read_bytes())
    # No runtime source changed. Only these benchmark and measurement files are patched.
    for path in destination.rglob("*"):
        if not path.is_file():
            continue
        name = path.relative_to(destination).as_posix()
        if name not in HARNESS_FILES:
            if hashlib.sha256(path.read_bytes()).hexdigest() != original_hashes[name]:
                raise RuntimeError(f"Unexpected baseline source change: {name}")
    return {
        "status": "baseline_tree_prepared_not_qualified",
        "source_archive": archive.name,
        "source_archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "harness_files": list(HARNESS_FILES),
        "destination": str(destination),
    }


def main() -> int:
    """Create an isolated earlier source tree without editing production algorithms."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = prepare(args.archive.resolve(), args.candidate.resolve(),
                         args.destination.resolve())
    except (OSError, RuntimeError, ValueError, zipfile.BadZipFile) as error:
        parser.error(str(error))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
