#!/usr/bin/env python3
"""Exercise RAW/GPU preflight and reproducible qualification support offline."""

import hashlib
import tempfile
import unittest
from pathlib import Path

from qualify_matched import project_source_digest
from verify_raw_fixtures import verify

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarks"))
from measure_gpu_memory import parse_drm_resident_mib


class RawPreflightTests(unittest.TestCase):
    """Verify external fixtures cannot be silently omitted or substituted."""

    def test_matching_fixture_passes(self) -> None:
        """A licensed, checksummed local fixture is a valid preflight sample."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sample = root / "sample.DNG"
            sample.write_bytes(b"synthetic-raw-test")
            fields = ["sample.DNG", "Synthetic", "DNG", "100", "50", "top, left",
                      "true", "12", hashlib.sha256(sample.read_bytes()).hexdigest(),
                      "CC0-1.0", "https://example.invalid/source"]
            manifest = root / "manifest.tsv"
            manifest.write_text("\t".join(fields) + "\n", encoding="utf-8")
            report = verify(root, manifest, expected_count=1)
            self.assertEqual(report["status"], "verified")
            self.assertEqual(report["verified"], 1)
            sample.write_bytes(b"tampered")
            self.assertEqual(verify(root, manifest, expected_count=1)["status"],
                             "blocked")

    def test_missing_fixture_is_blocked(self) -> None:
        """A missing RAW archive may not count as successful interoperability."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / "manifest.tsv"
            manifest.write_text("", encoding="utf-8")
            self.assertEqual(verify(root, manifest, expected_count=9)["status"],
                             "blocked")

    def test_symlink_is_rejected(self) -> None:
        """The qualification must not follow substituted or out-of-tree inputs."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            backing = root / "backing.bin"
            backing.write_bytes(b"real")
            (root / "sample.DNG").symlink_to(backing)
            fields = ["sample.DNG", "Synthetic", "DNG", "100", "50", "top, left",
                      "true", "12", hashlib.sha256(backing.read_bytes()).hexdigest(),
                      "CC0-1.0", "https://example.invalid/source"]
            manifest = root / "manifest.tsv"
            manifest.write_text("\t".join(fields) + "\n", encoding="utf-8")
            self.assertEqual(verify(root, manifest, expected_count=1)["status"],
                             "blocked")


class GpuEvidenceTests(unittest.TestCase):
    """Check physical-memory telemetry parsers with synthetic Linux driver data."""

    def test_drm_resident_bytes_and_kib(self) -> None:
        """Resident DRM regions use consistent MiB units without mixing allocations."""
        example = (
            "drm-driver: amdgpu\n"
            "drm-client-id: 7\n"
            "drm-memory-vram: 8192 KiB\n"
            "drm-resident-vram: 2048 KiB\n"
            "drm-resident-gtt: 1024 KiB\n"
        )
        self.assertEqual(parse_drm_resident_mib(example), ("amdgpu", 3.0))

    def test_missing_residency_remains_unmeasured(self) -> None:
        """Allocation counters alone must never masquerade as live GPU memory."""
        self.assertIsNone(parse_drm_resident_mib(
            "drm-driver: i915\ndrm-client-id: 3\ndrm-memory-vram: 4 MiB\n"))

    def test_invalid_residency_remains_unmeasured(self) -> None:
        """Malformed and negative GPU measurements cannot qualify release evidence."""
        self.assertIsNone(parse_drm_resident_mib(
            "drm-driver: amdgpu\ndrm-resident-vram: -10 KiB\n"))


class SourceEvidenceTests(unittest.TestCase):
    """Ensure source comparisons distinguish functional implementation changes."""

    def test_compilation_source_digest_changes(self) -> None:
        """A changed native source file must change the candidate's fingerprint."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "native").mkdir()
            path = root / "native/codec.c"
            path.write_text("int value = 1;\n", encoding="utf-8")
            earlier = project_source_digest(root)
            path.write_text("int value = 2;\n", encoding="utf-8")
            self.assertNotEqual(earlier, project_source_digest(root))


if __name__ == "__main__":
    unittest.main()
