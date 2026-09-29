#!/usr/bin/env python3
"""Validate standalone thread measurements and generate their README tables."""

from __future__ import annotations

import argparse
import copy
import json
import math
import statistics
import sys
from pathlib import Path


SCENARIOS = (
    "std_thread", "ts_thread", "ts_create",
    "std_thread_named", "ts_thread_named", "ts_create_named",
)
TIMING_METRICS = ("creation_ns", "entry_ns", "ready_ns", "lifecycle_ns", "work_ns_per_iteration")
ALLOCATION_METRICS = ("allocations", "allocated_bytes")
BUILD_FIELDS = ("compiler", "standard_library", "cpp_standard", "build_type", "compiler_flags")
HOST_FIELDS = ("cpu", "os")
SIZE_TYPES = ("std::thread", "ts::thread", "ts::thread_view", "ts::result<ts::thread>")
START_MARKER = "<!-- thread-benchmark-results:start -->"
END_MARKER = "<!-- thread-benchmark-results:end -->"


def numeric(value: object, context: str, positive: bool = False) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{context}: expected a number")
    if not math.isfinite(value) or value < 0 or (positive and value == 0):
        raise ValueError(f"{context}: expected a finite {'positive' if positive else 'nonnegative'} number")
    return float(value)


def sample_statistics(values: list[float]) -> dict[str, float]:
    if not values:
        raise ValueError("sample array must not be empty")
    ordered = sorted(numeric(value, "sample") for value in values)
    return {"median": statistics.median(ordered), "p95": ordered[math.ceil(0.95 * len(ordered)) - 1]}


def validate_result(result: dict, kind: str) -> None:
    if not isinstance(result, dict) or result.get("schema_version") != 1 or result.get("kind") != kind:
        raise ValueError(f"expected schema_version 1 and kind {kind!r}")
    metadata = result.get("metadata")
    if not isinstance(metadata, dict):
        raise ValueError(f"{kind}: missing metadata")
    for field in BUILD_FIELDS:
        expected_type = int if field == "cpp_standard" else str
        if not isinstance(metadata.get(field), expected_type) or isinstance(metadata[field], bool):
            raise ValueError(f"{kind}: missing or invalid metadata.{field}")
    for field in (*HOST_FIELDS, "measured_at_utc"):
        if not isinstance(metadata.get(field), str) or not metadata[field]:
            raise ValueError(f"{kind}: missing or invalid metadata.{field}")
    parameters = result.get("parameters")
    required_parameters = ("warmup", "samples", "work_samples", "work_iterations") if kind == "timing" else ("warmup", "samples")
    if not isinstance(parameters, dict):
        raise ValueError(f"{kind}: missing parameters")
    for field in required_parameters:
        value = parameters.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < (0 if field == "warmup" else 1):
            raise ValueError(f"{kind}: invalid parameters.{field}")
    scenarios = result.get("scenarios")
    if not isinstance(scenarios, list) or len(scenarios) != len(SCENARIOS):
        raise ValueError(f"{kind}: expected exactly six scenarios")
    seen = set()
    for scenario in scenarios:
        if not isinstance(scenario, dict):
            raise ValueError(f"{kind}: invalid scenario")
        identifier = scenario.get("id")
        if not isinstance(identifier, str) or identifier not in SCENARIOS or identifier in seen:
            raise ValueError(f"{kind}: unknown or duplicate scenario {identifier!r}")
        seen.add(identifier)
        if not isinstance(scenario.get("label"), str) or not scenario["label"]:
            raise ValueError(f"{identifier}: missing label")
        if scenario.get("configured") is not identifier.endswith("_named"):
            raise ValueError(f"{identifier}: incorrect configured flag")
        if scenario.get("status") == "error":
            if not isinstance(scenario.get("error"), str) or not scenario["error"]:
                raise ValueError(f"{identifier}: missing error description")
            continue
        if scenario.get("status") != "ok":
            raise ValueError(f"{identifier}: invalid status")
        metrics = scenario.get("metrics")
        raw = scenario.get("samples", {})
        if not isinstance(metrics, dict) or not isinstance(raw, dict):
            raise ValueError(f"{identifier}: invalid metrics or samples")
        for metric in TIMING_METRICS if kind == "timing" else ALLOCATION_METRICS:
            values = metrics.get(metric)
            if not isinstance(values, dict) or "median" not in values or "p95" not in values:
                raise ValueError(f"{identifier}: missing {metric} statistics")
            median = numeric(values["median"], f"{identifier}.{metric}.median", positive=kind == "timing")
            p95 = numeric(values["p95"], f"{identifier}.{metric}.p95", positive=kind == "timing")
            if p95 < median:
                raise ValueError(f"{identifier}.{metric}: p95 is below median")
            if metric in raw:
                expected_count = parameters["work_samples" if metric == "work_ns_per_iteration" else "samples"]
                if not isinstance(raw[metric], list) or len(raw[metric]) != expected_count:
                    raise ValueError(f"{identifier}.{metric}: sample count does not match parameters")
                calculated = sample_statistics(raw[metric])
                for statistic in ("median", "p95"):
                    if not math.isclose(calculated[statistic], values[statistic], rel_tol=1e-6, abs_tol=1e-6):
                        raise ValueError(f"{identifier}.{metric}: {statistic} disagrees with samples")
    if kind == "timing":
        sizes = result.get("sizes")
        if not isinstance(sizes, list) or len(sizes) != len(SIZE_TYPES):
            raise ValueError("timing: expected four object sizes")
        if sorted(size.get("type", "") for size in sizes if isinstance(size, dict)) != sorted(SIZE_TYPES):
            raise ValueError("timing: invalid or duplicate size types")
        for size in sizes:
            for field in ("bytes", "alignment"):
                if isinstance(size.get(field), bool) or not isinstance(size.get(field), int) or size[field] < 1:
                    raise ValueError(f"{size['type']}: invalid {field}")


def validate_pair(timing: dict, allocations: dict) -> None:
    validate_result(timing, "timing")
    validate_result(allocations, "allocations")
    for field in (*BUILD_FIELDS, *HOST_FIELDS):
        if timing["metadata"][field] != allocations["metadata"][field]:
            raise ValueError(f"measurement metadata mismatch: {field}")


def condensed(result: dict) -> dict:
    output = copy.deepcopy(result)
    for scenario in output["scenarios"]:
        scenario.pop("samples", None)
    return output


def build_summary(timing: dict, allocations: dict, timing_path: Path, allocations_path: Path) -> dict:
    validate_pair(timing, allocations)
    return {
        "schema_version": 1,
        "kind": "thread_cost_summary",
        "metadata_version": 1,
        "system": {field: timing["metadata"][field] for field in (*HOST_FIELDS, "measured_at_utc")},
        "inputs": {"timing": timing_path.name, "allocations": allocations_path.name},
        "timing": condensed(timing),
        "allocations": condensed(allocations),
    }


def markdown_text(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ").replace("\r", " ").replace("`", "'")


def language_name(value: int) -> str:
    standards = {201703: "C++17", 202002: "C++20", 202100: "C++23", 202302: "C++23", 202400: "C++26"}
    return standards.get(value, f"__cplusplus={value}")


def metric_cell(scenario: dict, baseline: dict, metric: str, divisor: float = 1.0) -> str:
    if scenario["status"] != "ok":
        return "error"
    values = scenario["metrics"][metric]
    pair = f"{values['median'] / divisor:.3f} / {values['p95'] / divisor:.3f}"
    if baseline["status"] != "ok" or baseline["metrics"][metric]["median"] == 0:
        return pair + " (ratio unavailable)"
    ratio = values["median"] / baseline["metrics"][metric]["median"]
    return pair + f" ({ratio:.2f}×)"


def render_markdown(summary: dict) -> str:
    timing = summary["timing"]
    allocations = summary["allocations"]
    validate_pair(timing, allocations)
    scenarios = {scenario["id"]: scenario for scenario in timing["scenarios"]}
    system = summary["system"]
    metadata = timing["metadata"]
    parameters = timing["parameters"]
    lines = [
        f"Measured on {markdown_text(system['measured_at_utc'])}: **{markdown_text(system['cpu'])}**, "
        f"{markdown_text(system['os'])}. "
        f"Compiler: {markdown_text(metadata['compiler'])}; standard library: {markdown_text(metadata['standard_library'])}; "
        f"Language: {language_name(metadata['cpp_standard'])}; "
        f"build: {markdown_text(metadata['build_type'])}. "
        f"CMake compiler flags: `{markdown_text(metadata['compiler_flags'].strip())}`.",
        "",
        f"{parameters['warmup']:,} warmup rounds, {parameters['samples']:,} startup samples and "
        f"{parameters['work_samples']:,} running-work samples per variant; "
        f"{parameters['work_iterations']:,} iterations per work sample. "
        f"Allocations: {allocations['parameters']['warmup']:,} warmup rounds and "
        f"{allocations['parameters']['samples']:,} measured cycles per variant.",
        "",
        "Each timing cell is **median / p95**; parentheses show the median ratio to the "
        "`std::thread` baseline with the same configuration. Startup times are in **µs**.",
    ]
    for configured, title in ((False, "Without configuration"), (True, "With name configuration (`ts-bench`)")):
        lines.extend([
            "", f"**{title}**", "",
            "| Variant | Creation µs | First user code µs | Ready after configuration µs | Create + join µs |",
            "| --- | ---: | ---: | ---: | ---: |",
        ])
        baseline = scenarios["std_thread_named" if configured else "std_thread"]
        for identifier in SCENARIOS:
            scenario = scenarios[identifier]
            if scenario["configured"] != configured:
                continue
            cells = [metric_cell(scenario, baseline, metric, 1000.0) for metric in TIMING_METRICS[:4]]
            lines.append(f"| {markdown_text(scenario['label'])} | " + " | ".join(cells) + " |")
    lines.extend([
        "", "**Work inside an already running thread**", "",
        "| Variant | Name configured | ns/iteration: median / p95 (ratio) |",
        "| --- | --- | ---: |",
    ])
    for identifier in SCENARIOS:
        scenario = scenarios[identifier]
        baseline = scenarios["std_thread_named" if scenario["configured"] else "std_thread"]
        lines.append(f"| {markdown_text(scenario['label'])} | {'yes' if scenario['configured'] else 'no'} | "
                     f"{metric_cell(scenario, baseline, 'work_ns_per_iteration')} |")
    lines.extend([
        "", "**Object storage** (bytes; excludes dynamic allocations and OS thread stacks)", "",
        "| Type | `sizeof` | `alignof` |", "| --- | ---: | ---: |",
    ])
    for size in timing["sizes"]:
        lines.append(f"| `{size['type']}` | {size['bytes']} | {size['alignment']} |")
    lines.extend([
        "", "**C++ heap allocations per complete create/join cycle**", "",
        "| Variant | Name configured | Allocation count: median / p95 (ratio) | Requested bytes: median / p95 (ratio) |",
        "| --- | --- | ---: | ---: |",
    ])
    allocation_scenarios = {scenario["id"]: scenario for scenario in allocations["scenarios"]}
    for identifier in SCENARIOS:
        scenario = allocation_scenarios[identifier]
        baseline = allocation_scenarios["std_thread_named" if scenario["configured"] else "std_thread"]
        lines.append(f"| {markdown_text(scenario['label'])} | {'yes' if scenario['configured'] else 'no'} | "
                     f"{metric_cell(scenario, baseline, 'allocations')} | "
                     f"{metric_cell(scenario, baseline, 'allocated_bytes')} |")
    errors = [f"{result['kind']}, {markdown_text(scenario['label'])}: {markdown_text(scenario['error'])}"
              for result in (timing, allocations) for scenario in result["scenarios"] if scenario["status"] == "error"]
    if errors:
        lines.extend(["", "Measurement failures (excluded from successful measurements):", ""])
        lines.extend(f"- {error}" for error in errors)
    lines.extend([
        "",
        "Callable entry and constructor return can occur in either order; the startup columns are "
        "independent elapsed times from the same start point. For the named `std::thread`, first user code "
        "precedes `ts::this_thread::set_name()`; readiness follows successful configuration. "
        "Factory creation includes its success check and the move into an empty thread object.",
        "",
        "Running work excludes startup, configuration and join. Small differences may reflect measurement "
        "noise; these numbers describe this system and this integer-arithmetic workload, not a general "
        "speed guarantee. Allocation instrumentation counts C++ `new` requests in a separate executable; "
        "it excludes OS thread stacks and allocations made directly by the OS or C library. Requested "
        "bytes are cumulative, not peak or retained memory. No Windows results are inferred from this run.",
    ])
    return "\n".join(lines) + "\n"


def replace_readme_region(content: str, report: str) -> str:
    if content.count(START_MARKER) != 1 or content.count(END_MARKER) != 1:
        raise ValueError("README must contain exactly one start marker and one end marker")
    start = content.index(START_MARKER) + len(START_MARKER)
    end = content.index(END_MARKER)
    if start > end:
        raise ValueError("README markers are out of order")
    return content[:start] + "\n\n" + report.rstrip() + "\n\n" + content[end:]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timing", required=True, type=Path)
    parser.add_argument("--allocations", required=True, type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument("--markdown", type=Path)
    parser.add_argument("--readme", type=Path)
    args = parser.parse_args(argv)
    try:
        timing = json.loads(args.timing.read_text(encoding="utf-8"))
        allocations = json.loads(args.allocations.read_text(encoding="utf-8"))
        summary = build_summary(timing, allocations, args.timing, args.allocations)
        report = render_markdown(summary)
        updated_readme = replace_readme_region(args.readme.read_text(encoding="utf-8"), report) if args.readme else None
        # Validate all inputs and markers before writing any output.
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        if args.markdown:
            args.markdown.parent.mkdir(parents=True, exist_ok=True)
            args.markdown.write_text(report, encoding="utf-8")
        elif not args.readme:
            print(report, end="")
        if args.readme:
            args.readme.write_text(updated_readme, encoding="utf-8")
    except (OSError, ValueError) as error:
        print(f"thread report: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
