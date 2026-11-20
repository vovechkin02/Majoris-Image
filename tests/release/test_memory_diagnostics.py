#!/usr/bin/env python3
"""Test the long-cycle GTK memory-diagnostic parser without native GTK."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarks"))
from diagnose_viewer_memory import parse_diagnostics  # noqa: E402


def example(cycles: int = 20, low_high_water: bool = False) -> str:
    """Produce a 20-cycle release-independent memory-diagnosis log."""
    lines = ["Viewer-memory initial_rss_kib=1000"]
    for index in range(1, cycles + 1):
        lines.append(f"Viewer-tiles cycle={index} ready_viewers=8")
        lines.append(f"Viewer-lifetime cycle={index} released_viewers=8")
        lines.append(
            f"Viewer-memory cycle={index} live_rss_kib=3000 "
            f"settled_rss_kib={1000 + index} "
            f"peak_rss_kib={2000 if low_high_water else 4000}"
        )
        for stage in ("live", "settled"):
            lines.append(
                f"Viewer-diagnostic stage={stage} cycle={index} textures=16 "
                "rss_kib=2000 hwm_kib=4000 anon_kib=1500 "
                "file_kib=500 smaps_rss_kib=2100 smaps_pss_kib=1900 "
                "private_dirty_kib=1400 swap_kib=0"
            )
    lines.append(f"Viewer-memory final_rss_kib={1000 + cycles} "
                 f"retained_delta_kib={cycles}")
    return "\n".join(lines)


class MemoryDiagnosticTests(unittest.TestCase):
    """Require complete lifecycle evidence without assuming RSS means a leak."""

    def test_complete_long_cycle_diagnosis(self) -> None:
        """All 20 cycles retain their independent observations."""
        result = parse_diagnostics(example(), 20, 8, True)
        self.assertEqual(result["status"], "diagnostic_complete")
        self.assertEqual(len(result["cycles"]), 20)
        self.assertEqual(result["retained_delta_kib"], 20)
        self.assertEqual(result["last_five_settled_delta_kib"], 4)
        self.assertTrue(result["high_water_consistent"])
        self.assertEqual(len(result["diagnostics"]), 40)
        self.assertEqual(result["smaps_qualification"], "not_measured")
        self.assertIn("does not establish a memory leak", result["interpretation"])

    def test_complete_smaps_attribution_can_be_reported(self) -> None:
        """Both initial and final plus each cycle require parsed smaps samples."""
        details = (
            "rss_kib=1000 hwm_kib=4000 anon_kib=500 file_kib=500 "
            "smaps_rss_kib=1000 smaps_pss_kib=900 "
            "private_dirty_kib=400 swap_kib=0"
        )
        output = (example() + "\nViewer-diagnostic stage=initial cycle=0 " +
                  details + "\nViewer-diagnostic stage=final cycle=20 " + details)
        result = parse_diagnostics(output, 20, 8, True)
        self.assertEqual(result["smaps_qualification"], "measured")

    def test_inconsistent_kernel_high_water_is_not_accepted(self) -> None:
        """Kernel high-water anomalies remain explicitly visible in results."""
        result = parse_diagnostics(example(low_high_water=True), 20, 8, True)
        self.assertEqual(result["status"], "diagnostic_complete")
        self.assertFalse(result["high_water_consistent"])
        self.assertIn("cannot be accepted", result["high_water_note"])

    def test_missing_viewer_release_is_inconclusive(self) -> None:
        """A missing normal finalization is not a complete diagnosis."""
        output = example().replace(
            "cycle=5 released_viewers=8", "cycle=5 released_viewers=7"
        )
        self.assertEqual(parse_diagnostics(output, 20, 8, True)["status"],
                         "inconclusive")

    def test_missing_tile_coverage_is_inconclusive(self) -> None:
        """The tiled workload must actually render its decoded textures."""
        output = example().replace("cycle=7 ready_viewers=8", "cycle=7 ready_viewers=0")
        self.assertEqual(parse_diagnostics(output, 20, 8, True)["status"],
                         "inconclusive")

    def test_tampered_final_rss_delta_is_inconclusive(self) -> None:
        """Reported retention must agree with independent initial/final RSS."""
        output = example().replace("retained_delta_kib=20", "retained_delta_kib=0")
        self.assertEqual(parse_diagnostics(output, 20, 8, True)["status"],
                         "inconclusive")


if __name__ == "__main__":
    unittest.main()
