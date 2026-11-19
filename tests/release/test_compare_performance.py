#!/usr/bin/env python3
"""Verify fail-closed performance comparison without requiring the native toolchain."""

import copy
import unittest

from compare_performance import REQUIRED_WORKLOADS, compare


def reference() -> dict:
    """Construct a minimal consistent 13-workload baseline with stable outputs."""
    values = {}
    for name in REQUIRED_WORKLOADS:
        values[name] = {
            "fixture_sha256": "fixture-" + name,
            "output_sha256": (
                "output-" + name if name.startswith("animation-") else None
            ),
            "count": 7, "median_value": 100.0, "mad_value": 2.0,
        }
    return {
        "schema": 1, "harness_sha256": "same-test-harness",
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


if __name__ == "__main__":
    unittest.main()
