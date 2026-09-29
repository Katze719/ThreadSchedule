#!/usr/bin/env python3
"""Deterministic checks for benchmark statistics, validation and publication."""

import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest

import generate_thread_report as report


def fixture(kind):
    result = {
        "schema_version": 1,
        "kind": kind,
        "metadata": {
            "compiler": "test compiler",
            "standard_library": "test library",
            "cpp_standard": 201703,
            "build_type": "Release",
            "compiler_flags": "-O3 -DNDEBUG",
            "cpu": "recorded CPU",
            "os": "recorded OS",
            "measured_at_utc": "2026-01-02T03:04:05Z",
        },
        "parameters": {"warmup": 2, "samples": 4},
        "scenarios": [],
    }
    if kind == "timing":
        result["parameters"].update(work_samples=4, work_iterations=1000)
        result["sizes"] = [{"type": name, "bytes": 8, "alignment": 8} for name in report.SIZE_TYPES]
    metrics = report.TIMING_METRICS if kind == "timing" else report.ALLOCATION_METRICS
    for index, identifier in enumerate(report.SCENARIOS):
        # Named baseline differs from unnamed baseline to detect incorrect ratios.
        values = [(index + 1) * value for value in (1000, 2000, 3000, 4000)]
        result["scenarios"].append({
            "id": identifier, "label": identifier,
            "configured": identifier.endswith("_named"), "status": "ok",
            "metrics": {metric: report.sample_statistics(values) for metric in metrics},
            "samples": {metric: list(values) for metric in metrics},
        })
    return result


class report_tests(unittest.TestCase):
    def setUp(self):
        self.timing = fixture("timing")
        self.allocations = fixture("allocations")

    def summary(self):
        return report.build_summary(self.timing, self.allocations, Path("timing.json"), Path("allocations.json"))

    def test_statistics(self):
        self.assertEqual(report.sample_statistics([4, 1, 3, 2]), {"median": 2.5, "p95": 4})
        self.assertEqual(report.sample_statistics([5, 1, 3]), {"median": 3, "p95": 5})
        self.assertEqual(report.sample_statistics(list(range(1, 21)))["p95"], 19)
        with self.assertRaises(ValueError):
            report.sample_statistics([])

    def test_ratios_use_matching_baseline(self):
        summary = self.summary()
        text = report.render_markdown(summary)
        self.assertIn("5.000 / 8.000 (2.00×)", text)
        self.assertIn("12.500 / 20.000 (1.25×)", text)
        self.assertIn("C++17", text)
        self.assertIn("recorded CPU", text)
        self.assertIn("2026-01-02T03:04:05Z", text)
        self.assertNotIn("samples", summary["timing"]["scenarios"][0])
        self.assertIn("samples", self.timing["scenarios"][0])

    def test_failures_are_visible_and_baseline_ratio_is_unavailable(self):
        baseline = self.timing["scenarios"][0]
        baseline.update(status="error", error="configuration rejected")
        del baseline["metrics"]
        del baseline["samples"]
        text = report.render_markdown(self.summary())
        self.assertIn("configuration rejected", text)
        self.assertIn("ratio unavailable", text)
        self.assertIn("| std_thread | error |", text)

    def test_rejects_invalid_numbers(self):
        for value in (float("nan"), float("inf"), -1, 0, True, "1"):
            with self.subTest(value=value):
                invalid = copy.deepcopy(self.timing)
                invalid["scenarios"][0]["metrics"]["creation_ns"]["median"] = value
                with self.assertRaises(ValueError):
                    report.validate_result(invalid, "timing")

    def test_zero_allocations_are_valid_but_have_no_ratio(self):
        for scenario in self.allocations["scenarios"]:
            for metric in report.ALLOCATION_METRICS:
                scenario["samples"][metric] = [0] * 4
                scenario["metrics"][metric] = {"median": 0, "p95": 0}
        self.assertIn("ratio unavailable", report.render_markdown(self.summary()))

    def test_rejects_mismatched_build_and_host(self):
        for field in (*report.BUILD_FIELDS, *report.HOST_FIELDS):
            with self.subTest(field=field):
                invalid = copy.deepcopy(self.allocations)
                invalid["metadata"][field] = 202002 if field == "cpp_standard" else "different"
                with self.assertRaises(ValueError):
                    report.validate_pair(self.timing, invalid)

    def test_rejects_wrong_sample_statistics_or_count(self):
        for samples in ([1, 2, 3, 4], [1000, 2000]):
            invalid = copy.deepcopy(self.timing)
            invalid["scenarios"][0]["samples"]["creation_ns"] = samples
            with self.assertRaises(ValueError):
                report.validate_result(invalid, "timing")

    def test_rejects_duplicates_and_wrong_configuration(self):
        invalid = copy.deepcopy(self.timing)
        invalid["scenarios"][1] = invalid["scenarios"][0]
        with self.assertRaises(ValueError):
            report.validate_result(invalid, "timing")
        invalid = copy.deepcopy(self.timing)
        invalid["scenarios"][0]["configured"] = True
        with self.assertRaises(ValueError):
            report.validate_result(invalid, "timing")

    def test_marker_update_preserves_surroundings_and_is_idempotent(self):
        original = f"before\n{report.START_MARKER}\nold\n{report.END_MARKER}\nafter\n"
        updated = report.replace_readme_region(original, "new\n")
        self.assertEqual(updated, report.replace_readme_region(updated, "new\n"))
        self.assertTrue(updated.startswith("before\n"))
        self.assertTrue(updated.endswith("\nafter\n"))
        for invalid in ("missing", original + report.START_MARKER, report.END_MARKER + report.START_MARKER):
            with self.assertRaises(ValueError):
                report.replace_readme_region(invalid, "new")

    def test_cli_checks_markers_before_writing_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            timing, allocations, summary, readme = [root / name for name in ("t.json", "a.json", "s.json", "README.md")]
            timing.write_text(json.dumps(self.timing), encoding="utf-8")
            allocations.write_text(json.dumps(self.allocations), encoding="utf-8")
            readme.write_text("keep this", encoding="utf-8")
            args = ["--timing", str(timing), "--allocations", str(allocations),
                    "--summary", str(summary), "--readme", str(readme)]
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(report.main(args), 1)
            self.assertFalse(summary.exists())
            self.assertEqual(readme.read_text(encoding="utf-8"), "keep this")
            readme.write_text(report.START_MARKER + "\n" + report.END_MARKER, encoding="utf-8")
            self.assertEqual(report.main(args), 0)
            first = readme.read_text(encoding="utf-8")
            self.assertEqual(report.main(args), 0)
            self.assertEqual(first, readme.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
