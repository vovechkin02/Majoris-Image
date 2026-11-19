#!/usr/bin/env python3
"""Verify licensed external camera RAW fixtures without altering or fetching them."""

import argparse
import hashlib
import json
import re
from pathlib import Path

EXPECTED_COLUMNS = 11
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")


def verify(root: Path, manifest: Path, expected_count: int = 9) -> dict:
    """Verify names, licensing, provenance, and byte hashes of every RAW sample.

    The files must be ordinary, non-symlink inputs directly in ``root``. A
    missing or changed file fails qualification rather than silently skipping
    an unsupported camera format.
    """
    results = {"status": "blocked", "verified": 0, "expected": expected_count,
               "samples": [], "reason": ""}
    try:
        if not root.is_dir() or not manifest.is_file():
            raise ValueError("RAW directory and checked-in manifest are required")
        seen = set()
        records = []
        lines = manifest.read_text(encoding="utf-8").splitlines()
        for line_number, line in enumerate(lines, 1):
            if not line.strip() or line.startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != EXPECTED_COLUMNS:
                raise ValueError(
                    f"Invalid RAW manifest field count at line {line_number}"
                )
            (filename, model, image_format, width, height, orientation, preview,
             bits, digest, license_id, provenance) = fields
            if (not filename or filename != Path(filename).name or
                    filename in (".", "..") or "\\" in filename or
                    "/" in filename or filename in seen):
                raise ValueError(
                    f"Invalid or duplicate RAW fixture name at line {line_number}"
                )
            seen.add(filename)
            if (not model.strip() or not image_format.strip() or
                    int(width) <= 0 or int(height) <= 0 or
                    int(bits) not in range(1, 65) or
                    preview not in ("true", "false") or
                    not orientation.strip() or
                    not SHA256_RE.fullmatch(digest) or
                    not license_id.strip() or not provenance.startswith("https://")):
                raise ValueError(f"Invalid RAW fixture contract at line {line_number}")
            sample = root / filename
            if sample.is_symlink() or not sample.is_file():
                raise ValueError(f"Missing or nonregular RAW sample: {filename}")
            checksum = hashlib.sha256()
            with sample.open("rb") as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    checksum.update(chunk)
            if checksum.hexdigest() != digest:
                raise ValueError(f"RAW SHA-256 mismatch: {filename}")
            records.append({"name": filename, "sha256": digest, "format": image_format,
                            "license": license_id, "provenance": provenance})
        if len(records) != expected_count:
            raise ValueError(
                f"Expected {expected_count} RAW fixtures, found {len(records)}"
            )
        results.update(
            status="verified", verified=len(records), samples=records, reason=""
        )
    except (OSError, ValueError, UnicodeError) as error:
        results["reason"] = str(error)
    return results


def main() -> int:
    """Write a machine-readable preflight result and fail closed on missing data."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument(
        "--manifest", type=Path,
        default=Path(__file__).resolve().parents[1] / "fixtures/raw-qualification.tsv",
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.root.resolve(), args.manifest.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps({"status": result["status"], "verified": result["verified"],
                      "reason": result["reason"]}, sort_keys=True))
    return 0 if result["status"] == "verified" else 1


if __name__ == "__main__":
    raise SystemExit(main())
