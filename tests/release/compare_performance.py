#!/usr/bin/env python3
"""Compare matched Majoris-Image performance measurements with explicit release gates.

This tool never treats the absence of a baseline, incompatible conditions,
unstable samples or missing workloads as a successful qualification.
"""

import argparse
import json
import math
import statistics
from pathlib import Path


REQUIRED_WORKLOADS = (
    "animation-apng-10", "animation-apng-50", "animation-apng-100",
    "animation-apng-composite",
    *(f"scheduler-{count}-{stage}" for count in (120, 1200, 4800)
      for stage in ("queue", "cancel", "drain")),
    "viewer-retained-rss", "viewer-live-rss", "viewer-peak-rss",
    "viewer-large-retained-rss", "viewer-large-live-rss", "viewer-large-peak-rss",
)

MEMORY_WORKLOADS = frozenset(name for name in REQUIRED_WORKLOADS
                             if name.startswith("viewer-"))
ACTIVE_MEMORY_WORKLOADS = frozenset(
    name for name in MEMORY_WORKLOADS
    if name.endswith(("-live-rss", "-peak-rss"))
)


def validate_report_evidence(report: dict, label: str) -> list[str]:
    """Require a complete, successful *collection*, not an inferred release pass.

    The native collector emits ``measured_not_compared`` only after collecting
    every workload. ``blocked``, absent status, and unknown report schemas must
    never qualify even when workload records have been copied into the JSON.
    """
    reasons = []
    if not isinstance(report, dict):
        return [f"{label}: performance evidence is not an object"]
    if type(report.get("schema")) is not int or report["schema"] != 3:
        reasons.append(
            f"{label}: unsupported or missing measurement schema (expected 3)"
        )
    if report.get("status") != "measured_not_compared":
        reasons.append(f"{label}: collection status must be measured_not_compared")
    if (not isinstance(report.get("version"), str) or
            not report["version"].strip()):
        reasons.append(f"{label}: missing source version")
    if (not isinstance(report.get("harness_sha256"), str) or
            not report["harness_sha256"].strip()):
        reasons.append(f"{label}: missing benchmark harness fingerprint")
    environment = report.get("environment")
    if not isinstance(environment, dict):
        reasons.append(f"{label}: missing benchmark environment")
    else:
        for field in ("system", "machine", "cpu_model", "meson", "valac"):
            if (not isinstance(environment.get(field), str) or
                    not environment[field].strip()):
                reasons.append(f"{label}: missing environment.{field}")
    for field in ("build_options", "compilers", "codecs", "workloads"):
        value = report.get(field)
        if not isinstance(value, dict) or not value:
            reasons.append(f"{label}: missing or empty {field}")
    return reasons


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


def validate_measurement_samples(
    record: dict, kind: str
) -> tuple[dict | None, str | None]:
    """Recalculate every summary field from independent raw timing or RSS samples.

    Memory permits zero after-release residency deltas; elapsed timings must
    be strictly positive. Comparisons fail closed for missing, modified,
    nonfinite, or inconsistently counted observations.
    """
    samples = record.get("samples")
    positive = kind == "timing"
    if (not isinstance(samples, list) or len(samples) < 3 or
            type(record.get("count")) is not int or len(samples) != record["count"] or
            any(type(value) not in (int, float) or not math.isfinite(value) or
                (value <= 0 if positive else value < 0) for value in samples)):
        return None, f"Missing, nonfinite, or inconsistent raw {kind} samples"
    median = statistics.median(samples)
    ordered = sorted(samples)
    metrics = {
        "median_value": median,
        "mad_value": statistics.median(abs(value - median) for value in samples),
        "min_value": ordered[0],
        "max_value": ordered[-1],
        "p95_value": ordered[math.ceil(len(samples) * 0.95) - 1],
    }
    for name, expected in metrics.items():
        reported = record.get(name)
        if (type(reported) not in (int, float) or not math.isfinite(reported) or
                not math.isclose(reported, expected, rel_tol=1e-9, abs_tol=1e-6)):
            return None, f"Inconsistent raw {kind} statistics: {name}"
    return metrics, None


def validate_memory_samples(record: dict) -> tuple[dict | None, str | None]:
    """Validate nonnegative KiB observations and their five summary statistics."""
    return validate_measurement_samples(record, "memory")


def validate_timing_samples(record: dict) -> tuple[dict | None, str | None]:
    """Validate positive elapsed seconds and their five summary statistics."""
    return validate_measurement_samples(record, "timing")


def memory_is_unstable(metrics: dict, spread_fraction: float,
                       absolute_allowance_kib: int) -> bool:
    """Reject host-memory samples too variable for a defensible comparison."""
    median = metrics["median_value"]
    return (metrics["mad_value"] > max(1.0, median) * spread_fraction or
            metrics["max_value"] - metrics["min_value"] >
            max(absolute_allowance_kib, median * spread_fraction))


def compare(baseline: dict, current: dict, max_regression: float = 0.15,
            max_mad_fraction: float = 0.20,
            max_retained_growth_kib: int = 65536,
            max_live_growth_kib: int = 131072) -> dict:
    """Reject missing/incompatible/noisy results and measurable regressions.

    Runtime ratios use medians recalculated from independent timing samples.
    Viewer RSS checks median, p95, and maxima from raw samples; measured Linux
    VmHWM peaks have their own mandatory workloads. Live and peak RSS require
    relative and absolute growth limits; retained RSS uses the absolute limit.
    Unstable or inconsistent observations are inconclusive.
    Physical GPU memory remains outside the scope of this gate.
    """
    report = {
        "status": "passed", "reasons": [], "workloads": {},
        "limits": {
            "max_regression_percent": max_regression * 100,
            "max_mad_fraction": max_mad_fraction,
            "max_retained_growth_kib": max_retained_growth_kib,
            "max_live_growth_kib": max_live_growth_kib,
        },
        "gpu_memory_qualification": "not_measured",
    }
    # Reject malformed report envelopes before looking at individual workloads.
    # Comparison of incomplete evidence is never a successful qualification.
    invalid = validate_report_evidence(baseline, "baseline")
    invalid.extend(validate_report_evidence(current, "current"))
    if invalid:
        report["status"] = "inconclusive"
        report["reasons"].extend(invalid)
        return report
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
        if (not isinstance(older.get("fixture_sha256"), str) or
                not older["fixture_sha256"]):
            record["reason"] = "Missing benchmark fixture or request identity"
            continue
        if older.get("fixture_sha256") != newer.get("fixture_sha256"):
            record["reason"] = "Different benchmark fixture or request configuration"
            continue
        if older.get("output_sha256") != newer.get("output_sha256"):
            record["reason"] = "Different output fingerprint"
            continue
        if workload.startswith("animation-") and (
            not isinstance(older.get("output_sha256"), str) or
            not older["output_sha256"]
        ):
            record["reason"] = "Missing complete animation-output fingerprint"
            continue
        unit = "KiB" if workload in MEMORY_WORKLOADS else "seconds"
        if older.get("unit") != unit or newer.get("unit") != unit:
            record["reason"] = f"Missing or incompatible measurement unit ({unit})"
            continue
        if any(type(item.get("count")) is not int or item["count"] < 3
               for item in (older, newer)):
            record["reason"] = "Insufficient or invalid independent sample counts"
            continue
        if older["count"] != newer["count"]:
            record["reason"] = "Baseline/current sample counts differ"
            continue
        before = older.get("median_value")
        after = newer.get("median_value")
        if (not all(isinstance(value, (int, float)) and math.isfinite(value)
                    for value in (before, after)) or before < 0 or after < 0):
            record["reason"] = "Missing or invalid sample medians"
            continue
        if workload in MEMORY_WORKLOADS:
            baseline_memory, baseline_error = validate_memory_samples(older)
            current_memory, current_error = validate_memory_samples(newer)
            if baseline_error or current_error:
                record["reason"] = baseline_error or current_error
                continue
            assert baseline_memory is not None and current_memory is not None
            active = workload in ACTIVE_MEMORY_WORKLOADS
            absolute_limit = max_live_growth_kib if active else max_retained_growth_kib
            if active and baseline_memory["median_value"] <= 0:
                record["reason"] = "Live/high-water memory baseline must be positive"
                continue
            if memory_is_unstable(baseline_memory, max_mad_fraction, absolute_limit):
                record["reason"] = "Baseline memory samples exceed variability budget"
                continue
            record["baseline_kib"] = before
            record["current_kib"] = after
            record["growth_kib"] = after - before
            record["memory_statistics"] = {}
            regressed = False
            for name in ("median_value", "p95_value", "max_value"):
                previous = baseline_memory[name]
                measured = current_memory[name]
                growth = measured - previous
                ratio = measured / previous if previous > 0 else None
                exceeds = growth > absolute_limit or (
                    active and (ratio is None or ratio > 1 + max_regression)
                )
                record["memory_statistics"][name] = {
                    "baseline_kib": previous,
                    "current_kib": measured,
                    "growth_kib": growth,
                    "ratio": round(ratio, 4) if ratio is not None else None,
                    "exceeds_budget": exceeds,
                }
                regressed = regressed or exceeds
            if regressed:
                record["status"] = "failed"
                record["reason"] = "Viewer memory high-water or median regression"
            elif memory_is_unstable(current_memory, max_mad_fraction, absolute_limit):
                record["reason"] = "Current memory samples exceed variability budget"
            else:
                record["status"] = "passed"
            continue
        baseline_timing, baseline_error = validate_timing_samples(older)
        current_timing, current_error = validate_timing_samples(newer)
        if baseline_error or current_error:
            record["reason"] = baseline_error or current_error
            continue
        assert baseline_timing is not None and current_timing is not None
        before = baseline_timing["median_value"]
        after = current_timing["median_value"]
        base_mad = baseline_timing["mad_value"]
        current_mad = current_timing["mad_value"]
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
    parser.add_argument("--max-live-growth-kib", type=int, default=131072)
    args = parser.parse_args()
    if not (0 <= args.max_regression_percent <= 100 and
            0 < args.max_mad_fraction <= 1 and
            args.max_retained_growth_kib >= 0 and args.max_live_growth_kib >= 0):
        parser.error("Invalid performance acceptance budget")
    try:
        baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
        current = json.loads(args.current.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        parser.error(f"Cannot read performance evidence: {error}")
    if not isinstance(baseline, dict) or not isinstance(current, dict):
        parser.error("Performance evidence must be JSON objects")
    report = compare(baseline, current, args.max_regression_percent / 100,
                     args.max_mad_fraction, args.max_retained_growth_kib,
                     args.max_live_growth_kib)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
