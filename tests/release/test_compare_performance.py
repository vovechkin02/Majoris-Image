#!/usr/bin/env python3
"""Verify fail-closed performance comparison without requiring the native toolchain."""

import copy
import math
import statistics
import tempfile
import unittest
from pathlib import Path

from compare_performance import REQUIRED_WORKLOADS, compare
from collect_performance import benchmark_harness_hash, parse_viewer_report


def reference() -> dict:
    """Construct complete, unit-qualified workloads with stable output hashes."""
    values = {}
    for name in REQUIRED_WORKLOADS:
        values[name] = {
            "fixture_sha256": "fixture-" + name,
            "output_sha256": (
                "output-" + name if name.startswith("animation-") else None
            ),
            "count": 7, "median_value": 100.0, "mad_value": 0.0,
            "unit": "KiB" if name.startswith("viewer-") else "seconds",
            "samples": [100.0] * 7,
            "min_value": 100.0, "max_value": 100.0, "p95_value": 100.0,
        }
    return {
        "schema": 3, "status": "measured_not_compared",
        "version": "2.0.1", "harness_sha256": "same-test-harness",
        "environment": {"system": "test-linux", "machine": "amd64",
                        "cpu_model": "same-cpu", "meson": "1.0", "valac": "0.5"},
        "build_options": {"glycin": "enabled"},
        "compilers": {"vala": "same"}, "codecs": {"glycin": "same"},
        "workloads": values,
    }


def set_measurement_samples(result: dict, name: str, samples: list[float]) -> None:
    """Supply independent samples and correctly recomputed summary statistics."""
    record = result["workloads"][name]
    ordered = sorted(samples)
    median = statistics.median(samples)
    record.update({
        "count": len(samples), "samples": samples,
        "median_value": median,
        "mad_value": statistics.median(abs(item - median) for item in samples),
        "min_value": ordered[0], "max_value": ordered[-1],
        "p95_value": ordered[math.ceil(len(samples) * 0.95) - 1],
    })


class ViewerReportParsingTests(unittest.TestCase):
    """Validate the benchmark collector's Linux high-water parsing contract."""

    @staticmethod
    def output(peaks: list[int], tiled: bool = False) -> str:
        """Generate a six-cycle viewer report with independent peak observations."""
        rows = ["Viewer-memory initial_rss_kib=100"]
        for cycle, peak in enumerate(peaks, start=1):
            rows.append(f"Viewer-lifetime cycle={cycle} released_viewers=8")
            if tiled:
                rows.append(f"Viewer-tiles cycle={cycle} ready_viewers=8")
            rows.append(
                f"Viewer-memory cycle={cycle} live_rss_kib=600 "
                f"settled_rss_kib=150 peak_rss_kib={peak}"
            )
        rows.append("Viewer-memory final_rss_kib=150 retained_delta_kib=50")
        return "\n".join(rows)

    def test_captures_transient_process_peak(self) -> None:
        """A decoded-image peak remains visible even after sampled RSS falls."""
        retained, live, peak = parse_viewer_report(
            self.output([2_000] * 6, tiled=True), "viewer-large", True
        )
        self.assertEqual((retained, live, peak), (50.0, 600.0, 2_000.0))

    def test_missing_high_water_is_rejected(self) -> None:
        """A benchmark without VmHWM evidence cannot count as an RSS run."""
        stdout = self.output([2_000] * 6).replace(" peak_rss_kib=2000", "")
        with self.assertRaisesRegex(RuntimeError, "six ordered"):
            parse_viewer_report(stdout, "viewer", False)

    def test_nonmonotonic_high_water_is_rejected(self) -> None:
        """Linux process VmHWM cannot decline across cycles in one process."""
        with self.assertRaisesRegex(RuntimeError, "Inconsistent Linux"):
            parse_viewer_report(self.output([2_000, 1_000, 1_000, 1_000, 1_000, 1_000]),
                                "viewer", False)

    def test_high_water_smaller_than_live_is_rejected(self) -> None:
        """A maximum resident-memory counter cannot be below sampled live RSS."""
        with self.assertRaisesRegex(RuntimeError, "Inconsistent Linux"):
            parse_viewer_report(self.output([200] * 6), "viewer", False)

    def test_missing_viewer_finalization_is_rejected(self) -> None:
        """Peak readings cannot override a failure to release all viewers."""
        stdout = self.output([2_000] * 6).replace(
            "released_viewers=8", "released_viewers=7"
        )
        with self.assertRaisesRegex(RuntimeError, "finalized"):
            parse_viewer_report(stdout, "viewer", False)

    def test_missing_tiled_viewer_evidence_is_rejected(self) -> None:
        """A tiled measurement must prove tiles actually rendered."""
        with self.assertRaisesRegex(RuntimeError, "did not render tiles"):
            parse_viewer_report(self.output([2_000] * 6), "viewer-large", True)


class PerformanceComparisonTests(unittest.TestCase):
    """Exercise release-gate decisions independently of measured native workloads."""

    def test_matched_baseline_passes(self) -> None:
        """Equivalent timings under identical conditions qualify successfully."""
        earlier = reference()
        self.assertEqual(compare(earlier, copy.deepcopy(earlier))["status"], "passed")

    def test_blocked_reference_with_valid_workloads_is_inconclusive(self) -> None:
        """A blocked collector cannot qualify even with copied workload data."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        earlier["status"] = "blocked"
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_blocked_candidate_with_valid_workloads_is_inconclusive(self) -> None:
        """A failed candidate collector cannot qualify using populated records."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["status"] = "blocked"
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_unknown_collection_status_is_inconclusive(self) -> None:
        """Only the collector's measured-but-unqualified state is accepted."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["status"] = "passed"
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_missing_report_status_is_inconclusive(self) -> None:
        """Absence of collection outcome is not evidence of success."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        del current["status"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_missing_report_schema_is_inconclusive(self) -> None:
        """Reports without the v3 measurement schema cannot be compared."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        del current["schema"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_incompatible_report_schema_is_inconclusive(self) -> None:
        """Older and future unknown schemas require explicit migration."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["schema"] = 2
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_empty_harness_fingerprint_is_inconclusive(self) -> None:
        """Two missing harness hashes must not compare as matching."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        earlier["harness_sha256"] = ""
        current["harness_sha256"] = ""
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_missing_environment_fields_are_inconclusive(self) -> None:
        """The absence of the same machine/toolchain field on both sides fails closed."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        for report in (earlier, current):
            del report["environment"]["cpu_model"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_missing_compiler_metadata_is_inconclusive(self) -> None:
        """Identically absent compiler maps cannot establish a matched toolchain."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        for report in (earlier, current):
            del report["compilers"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_missing_source_version_is_inconclusive(self) -> None:
        """A report must identify the source version under test."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current.pop("version")
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_wrong_environment_shape_is_inconclusive(self) -> None:
        """Malformed host metadata must be rejected rather than raising an exception."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["environment"] = None
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_regression_fails(self) -> None:
        """A reproducible slowdown greater than the approved budget must fail."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(current, "animation-apng-100", [130.0] * 7)
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_timing_summary_tampering_is_inconclusive(self) -> None:
        """An unchanged claimed median cannot conceal much slower raw samples."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["animation-apng-100"]["samples"] = [10_000.0] * 7
        report = compare(earlier, current)
        self.assertEqual(report["status"], "inconclusive")
        self.assertIn("Inconsistent raw timing", report["workloads"]
                      ["animation-apng-100"]["reason"])

    def test_inconsistent_timing_extrema_are_inconclusive(self) -> None:
        """Tampered maximum or percentile must invalidate the timing evidence."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["scheduler-4800-drain"]["max_value"] = 0.01
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_missing_raw_timing_samples_are_inconclusive(self) -> None:
        """A reported mean or median without observations cannot qualify."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        del current["workloads"]["scheduler-120-queue"]["samples"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_nonfinite_timing_samples_are_inconclusive(self) -> None:
        """NaN and infinite duration samples must be rejected before comparison."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["animation-apng-50"]["samples"][0] = math.inf
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_nonpositive_timing_samples_are_inconclusive(self) -> None:
        """Zero and negative elapsed timings cannot enter acceptance statistics."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["scheduler-120-queue"]["samples"][0] = 0.0
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_missing_timing_mad_is_inconclusive(self) -> None:
        """An absent timing variability statistic invalidates the raw summary."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        del current["workloads"]["scheduler-120-drain"]["mad_value"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_invalid_sample_count_is_inconclusive(self) -> None:
        """A malformed sample count must not raise or inadvertently qualify."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["scheduler-120-drain"]["count"] = None
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_process_peak_growth_fails_with_stable_live_rss(self) -> None:
        """VmHWM spikes must fail even when sampled live RSS remains unchanged."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(earlier, "viewer-large-peak-rss", [600_000.0] * 7)
        set_measurement_samples(current, "viewer-large-peak-rss", [2_000_000.0] * 7)
        report = compare(earlier, current)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["workloads"]["viewer-large-live-rss"]["status"],
                         "passed")

    def test_missing_high_water_workload_is_inconclusive(self) -> None:
        """The qualification cannot omit either process peak-memory workload."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        del current["workloads"]["viewer-peak-rss"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_mismatched_sample_counts_are_inconclusive(self) -> None:
        """Baseline and candidate must use the same number of independent runs."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(current, "scheduler-120-queue", [100.0] * 5)
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_tampered_baseline_timing_is_inconclusive(self) -> None:
        """A faulty reference timing summary cannot substantiate a regression."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        earlier["workloads"]["animation-apng-10"]["p95_value"] = 0.0
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

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
        set_measurement_samples(current, "animation-apng-100", [140.0] * 7)
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
        set_measurement_samples(current, "scheduler-120-cancel",
                                [25.0, 25.0, 25.0, 100.0, 175.0, 175.0, 175.0])
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_viewer_rss_growth_fails(self) -> None:
        """Excessive settled host-memory growth blocks performance acceptance."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(current, "viewer-retained-rss", [70000.0] * 7)
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_large_live_viewer_memory_growth_fails(self) -> None:
        """Excessive live memory must fail even if post-release RSS is stable."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(current, "viewer-large-live-rss", [100_000_000.0] * 7)
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_small_live_viewer_memory_growth_fails(self) -> None:
        """Even the preview-only viewer workload must enforce its live RSS limit."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(current, "viewer-live-rss", [120.0] * 7)
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
        set_measurement_samples(earlier, "viewer-large-live-rss", [1_000_000.0] * 7)
        set_measurement_samples(current, "viewer-large-live-rss", [1_140_000.0] * 7)
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_large_peak_spike_fails_with_stable_median(self) -> None:
        """A single extreme live-memory sample cannot hide behind a stable median."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(earlier, "viewer-large-live-rss", [600_000.0] * 7)
        set_measurement_samples(current, "viewer-large-live-rss",
                           [600_000.0] * 6 + [2_000_000.0])
        report = compare(earlier, current)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(
            report["workloads"]["viewer-large-live-rss"]["status"], "failed"
        )
        self.assertEqual(
            report["workloads"]["viewer-large-live-rss"]["memory_statistics"]
            ["median_value"]["growth_kib"], 0.0
        )

    def test_peak_growth_fails_without_median_change(self) -> None:
        """A moderate peak regression fails even when the new median is identical."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(earlier, "viewer-live-rss", [600_000.0] * 7)
        set_measurement_samples(current, "viewer-live-rss",
                           [600_000.0] * 6 + [710_000.0])
        self.assertEqual(compare(earlier, current)["status"], "failed")

    def test_memory_summary_tampering_is_inconclusive(self) -> None:
        """A claimed low maximum must be consistent with every raw sample."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        current["workloads"]["viewer-large-live-rss"]["max_value"] = 1.0
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_missing_raw_memory_samples_are_inconclusive(self) -> None:
        """Host-memory comparison requires the independent measurement evidence."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        del current["workloads"]["viewer-live-rss"]["samples"]
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_unstable_baseline_memory_is_inconclusive(self) -> None:
        """Noisy baseline host residency is not suitable for regression claims."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(earlier, "viewer-large-live-rss",
                           [500_000.0] * 4 + [900_000.0] * 3)
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_noisy_candidate_without_regression_is_inconclusive(self) -> None:
        """Noise cannot count as a passing memory qualification."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(earlier, "viewer-large-live-rss", [1_000_000.0] * 7)
        set_measurement_samples(current, "viewer-large-live-rss",
                           [600_000.0] * 4 + [1_000_000.0] * 3)
        self.assertEqual(compare(earlier, current)["status"], "inconclusive")

    def test_retained_peak_growth_fails(self) -> None:
        """Retained memory must obey a high-water budget, not just its median."""
        earlier = reference()
        current = copy.deepcopy(earlier)
        set_measurement_samples(earlier, "viewer-retained-rss", [1000.0] * 7)
        set_measurement_samples(current, "viewer-retained-rss",
                           [1000.0] * 6 + [68000.0])
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
