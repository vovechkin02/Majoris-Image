#!/usr/bin/env python3
"""Verify fail-closed performance comparison without requiring the native toolchain."""

import copy
import tempfile
import unittest
from pathlib import Path

from compare_performance import REQUIRED_WORKLOADS, compare
from collect_performance import benchmark_harness_hash


def reference() -> dict:
    """Construct complete, unit-qualified workloads with stable output hashes."""
    values = {}
    for name in REQUIRED_WORKLOADS:
        values[name] = {
            "fixture_sha256": "fixture-" + name,
            "output_sha256": (
                "output-" + name if name.startswith("animation-") else None
            ),
            "count": 7, "median_value": 100.0, "mad_value": 2.0,
            "unit": "KiB" if name.startswith("viewer-") else "seconds",
        }
    return {
        "schema": 2, "harness_sha256": "same-test-harness",
        "environment": {"system": "test-linux", "machine": "amd64",
                        "cpu_model": "same-cpu", "meson": "1.0", "valac": "0.5"},
        "build_options": {"glycin": "enabled"},
        "compilers": {"vala": "same"}, "codecs": {"glycin": "same"},
        "workloads": values,
    }


class PerformanceComparisonTests(unittest.TestCase):
    """Exercise release-gate decisions independently of measured native workloads."""

    def test_matched_baseline_passes(self) -> None:
        """Equivalent timings under identical conditions qualify successfully."""
        earlier = reference()
        self.assertEqual(compare(earlier, copy.deepcopy(earlier))["status"], "passed")

    def test_regression_fails(self) -> None:
        """A reproducible slowdown greater than the approved budget must fail."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["animation-apng-100"]["median_value"] = 130.0
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_missing_workload_is_inconclusive(self) -> None:
        """Missing output cannot be silently counted as a successful benchmark."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        del current["workloads"]["scheduler-4800-cancel"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_host_mismatch_is_inconclusive(self) -> None:
        """Measurements from different CPUs require a new matched baseline."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["environment"]["cpu_model"] = "different-cpu"
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_harness_change_is_inconclusive(self) -> None:
        """A different benchmark methodology must never compare as equivalent."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["harness_sha256"] = "different-test-harness"
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_host_mismatch_does_not_claim_a_regression(self) -> None:
        """A slowdown on unlike hosts is inconclusive, not a valid regression."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["environment"]["cpu_model"] = "another-cpu"
        current["workloads"]["animation-apng-100"]["median_value"] = 140.0
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_output_change_is_inconclusive(self) -> None:
        """A changed APNG pixel hash cannot qualify as a pure optimization."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["animation-apng-50"]["output_sha256"] = "wrong"
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_unstable_samples_are_inconclusive(self) -> None:
        """High timing variance requires new measurements instead of a false pass."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["scheduler-120-cancel"]["mad_value"] = 50.0
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_viewer_rss_growth_fails(self) -> None:
        """Excessive settled host-memory growth blocks performance acceptance."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["viewer-retained-rss"]["median_value"] = 70000.0
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_large_live_viewer_memory_growth_fails(self) -> None:
        """Excessive live memory must fail even if post-release RSS is stable."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["viewer-large-live-rss"]["median_value"] = 100_000_000.0
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_small_live_viewer_memory_growth_fails(self) -> None:
        """Even the preview-only viewer workload must enforce its live RSS limit."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["viewer-live-rss"]["median_value"] = 120.0
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_missing_large_viewer_is_inconclusive(self) -> None:
        """A run that skips tiled viewers cannot qualify the release."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        del current["workloads"]["viewer-large-retained-rss"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_incorrect_units_are_inconclusive(self) -> None:
        """Seconds and KiB must not be compared as if they were equivalent."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["viewer-large-live-rss"]["unit"] = "seconds"
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_missing_all_frame_fingerprint_is_inconclusive(self) -> None:
        """The absence of a complete animation digest must not pass validation."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["animation-apng-100"]["output_sha256"] = None
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_excessive_absolute_live_growth_fails(self) -> None:
        """A large live-memory increase also fails its absolute growth cap."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        earlier["workloads"]["viewer-large-live-rss"]["median_value"] = 1_000_000.0
        current["workloads"]["viewer-large-live-rss"]["median_value"] = 1_140_000.0
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_collector_changes_invalidate_harness_hash(self) -> None:
        """A changed collector cannot retain a supposedly identical benchmark ID."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = (
                "tests/release/collect_performance.py",
                "tests/benchmarks/run_benchmarks.py",
                "tests/benchmarks/benchmark.vala",
                "tests/benchmarks/benchmark-scheduler.vala",
                "tests/benchmarks/benchmark-viewer-lifecycle.vala",
                "tests/benchmarks/meson.build",
                "tests/meson.build",
                "tests/support/make_apng_benchmarks.py",
                "tests/support/make-fixtures.c",
            )
            for name in sources:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("baseline", encoding="utf-8")
            baseline = benchmark_harness_hash(root)
            (root / sources[0]).write_text("modified", encoding="utf-8")
            self.assertNotEqual(baseline, benchmark_harness_hash(root))


if __name__ == "__main__":
    unittest.main()
