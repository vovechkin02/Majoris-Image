#!/usr/bin/env python3
"""Exercise GPU-profiler subprocess lifetimes without requiring a physical GPU."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "benchmarks"))
from measure_gpu_memory import profile, read_output_tail  # noqa: E402


class GpuProfilerProcessTests(unittest.TestCase):
    """Prevent child-output deadlocks, runaway processes and false GPU passes."""

    def run_fake(self, program: str, timeout: float = 3,
                 max_output_mib: int = 2) -> dict:
        """Create and execute a disposable child with the benchmark CLI shape."""
        with tempfile.TemporaryDirectory() as directory:
            fake = Path(directory) / "fake-benchmark"
            fake.write_text(f"#!{sys.executable}\n{program}\n", encoding="utf-8")
            fake.chmod(0o755)
            fixture = Path(directory) / "dummy.tiff"
            fixture.write_bytes(b"test fixture")
            return profile(fake, fixture, 1, 1, "preview", timeout,
                           max_output_mib)

    def test_success_without_gpu_is_not_measured(self) -> None:
        """Host RSS sampling alone must never imply GPU qualification."""
        result = self.run_fake('print("healthy benchmark")')
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["qualification"], "not_measured")
        self.assertIn("healthy benchmark", result["output"])

    def test_timeout_kills_hanging_process(self) -> None:
        """A stalled child returns failed evidence within its deadline."""
        result = self.run_fake('import time\ntime.sleep(30)', timeout=0.4)
        self.assertEqual(result["qualification"], "failed")
        self.assertEqual(result["failure_reason"], "benchmark_timeout")

    def test_large_stdout_cannot_deadlock_profiler(self) -> None:
        """Output redirected to disk is bounded and keeps only a short tail."""
        result = self.run_fake(
            'import os\nos.write(1, b"x" * 3_000_000)\nimport time\ntime.sleep(10)',
            timeout=4, max_output_mib=1,
        )
        self.assertEqual(result["qualification"], "failed")
        self.assertEqual(result["failure_reason"], "benchmark_output_limit")
        self.assertTrue(result["output_truncated"])
        self.assertLessEqual(len(result["output"]), 65_536)

    def test_tail_reader_is_bounded(self) -> None:
        """Reading a long diagnostic log does not materialize the entire log."""
        with tempfile.TemporaryFile(mode="w+b") as stream:
            stream.write(b"a" * 120_000 + b"END")
            tail, truncated, size = read_output_tail(stream, 100)
        self.assertTrue(truncated)
        self.assertEqual(size, 120_003)
        self.assertTrue(tail.endswith("END"))
        self.assertEqual(len(tail), 100)


if __name__ == "__main__":
    unittest.main()
