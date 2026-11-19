#!/usr/bin/env python3
"""Compare matched Majoris-Image performance measurements with explicit release gates.

This tool never treats the absence of a baseline, incompatible conditions,
unstable samples or missing workloads as a successful qualification.
"""

import argparse
import json
import math
from pathlib import Path


REQUIRED_WORKLOADS = (
    "animation-apng-10", "animation-apng-50", "animation-apng-100",
    *(f"scheduler-{count}-{stage}" for count in (120, 1200, 4800)
      for stage in ("queue", "cancel", "drain")),
    "viewer-retained-rss",
)


def comparable_environment(baseline: dict, current: dict) -> list[str]:
    """Identify machine, toolchain and codec changes that invalidate comparisons."""
    differences = []
    for field in ("system", "machine", "cpu_model", "meson", "valac"):
        if (baseline.get("environment", {}).get(field) !=
                current.get("environment", {}).get(field)):
            differences.append(f"environment.{field}")
    for field in ("build_options", "compilers", "codecs", "schema", "harness_sha256"):
        if baseline.get(field) != current.get(field):
            differences.append(field)
    return differences


def compare(baseline: dict, current: dict, max_regression: float = 0.15,
            max_mad_fraction: float = 0.20,
            max_retained_growth_kib: int = 65536) -> dict:
    """Reject missing/incompatible/noisy results and measurable regressions.

    Runtime ratios apply to independently collected median timings. Retained
    host RSS uses an absolute allowance because its baseline can be near zero.
    Physical GPU memory remains outside the scope of this gate.
    """
    report = {
        "status": "passed", "reasons": [], "workloads": {},
        "limits": {
            "max_regression_percent": max_regression * 100,
            "max_mad_fraction": max_mad_fraction,
            "max_retained_growth_kib": max_retained_growth_kib,
        },
        "gpu_memory_qualification": "not_measured",
    }
    differences = comparable_environment(baseline, current)
    if differences:
        report["reasons"].append("Noncomparable baseline: " + ", ".join(differences))
    for workload in REQUIRED_WORKLOADS:
        older = baseline.get("workloads", {}).get(workload)
        newer = current.get("workloads", {}).get(workload)
        record = {"status": "inconclusive"}
        report["workloads"][workload] = record
        if not isinstance(older, dict) or not isinstance(newer, dict):
            record["reason"] = "Workload missing from matched baseline or current run"
            continue
        if older.get("fixture_sha256") != newer.get("fixture_sha256"):
            record["reason"] = "Different benchmark fixture or request configuration"
            continue
        if older.get("output_sha256") != newer.get("output_sha256"):
            record["reason"] = "Different output fingerprint"
            continue
        if min(older.get("count", 0), newer.get("count", 0)) < 3:
            record["reason"] = "Insufficient independent samples"
            continue
        before = older.get("median_value")
        after = newer.get("median_value")
        if (not all(isinstance(value, (int, float)) and math.isfinite(value)
                    for value in (before, after)) or before < 0 or after < 0):
            record["reason"] = "Missing or invalid sample medians"
            continue
        if workload == "viewer-retained-rss":
            growth = after - before
            record.update({"baseline_kib": before, "current_kib": after,
                           "growth_kib": growth})
            record["status"] = (
                "failed" if growth > max_retained_growth_kib else "passed"
            )
            continue
        if before <= 0:
            record["reason"] = "Baseline latency must be positive"
            continue
        base_mad = older.get("mad_value", float("nan"))
        current_mad = newer.get("mad_value", float("nan"))
        if not all(
            isinstance(value, (int, float)) and math.isfinite(value) and value >= 0
            for value in (base_mad, current_mad)
        ):
            record["reason"] = "Missing or invalid measurement variability"
            continue
        if base_mad / before > max_mad_fraction or (
            after > 0 and current_mad / after > max_mad_fraction
        ):
            record["reason"] = "Measurement variance exceeds the qualification budget"
            continue
        ratio = after / before
        record.update({"baseline": before, "current": after, "ratio": round(ratio, 4),
                       "baseline_mad": base_mad, "current_mad": current_mad})
        record["status"] = "failed" if ratio > 1 + max_regression else "passed"

    # A mismatched machine or benchmark methodology cannot establish a
    # performance regression, even if the observed timing looks slower.
    if differences:
        report["status"] = "inconclusive"
    elif any(item["status"] == "failed" for item in report["workloads"].values()):
        report["status"] = "failed"
    elif any(item["status"] == "inconclusive" for item in report["workloads"].values()):
        report["status"] = "inconclusive"
    return report


def main() -> int:
    """Write a comparison and fail for regressions or missing evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--current", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-regression-percent", type=float, default=15.0)
    parser.add_argument("--max-mad-fraction", type=float, default=0.20)
    parser.add_argument("--max-retained-growth-kib", type=int, default=65536)
    args = parser.parse_args()
    if not (0 <= args.max_regression_percent <= 100 and
            0 < args.max_mad_fraction <= 1 and args.max_retained_growth_kib >= 0):
        parser.error("Invalid performance acceptance budget")
    try:
        baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
        current = json.loads(args.current.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        parser.error(f"Cannot read performance evidence: {error}")
    if not isinstance(baseline, dict) or not isinstance(current, dict):
        parser.error("Performance evidence must be JSON objects")
    report = compare(baseline, current, args.max_regression_percent / 100,
                     args.max_mad_fraction, args.max_retained_growth_kib)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
