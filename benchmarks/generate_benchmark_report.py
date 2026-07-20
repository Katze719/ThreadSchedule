#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as dt
import html
import json
import os
import platform
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


TIME_TO_NS = {
    "ns": 1.0,
    "us": 1_000.0,
    "ms": 1_000_000.0,
    "s": 1_000_000_000.0,
}

POOL_NAMES = ("thread_pool_backend", "polling_pool_backend", "work_stealing_pool_backend", "lightweight_pool_backend")

EXPLICIT_GROUPS: dict[str, tuple[str, str, str]] = {}


@dataclass
class Run:
    source: str
    family: str
    full_name: str
    args: tuple[str, ...]
    label: str
    time_ns: float
    original_unit: str
    context: dict[str, object] = field(default_factory=dict)


@dataclass
class ComparisonGroup:
    title: str
    baseline: str
    runs: list[Run]


def run_command(command: list[str]) -> str:
    try:
        completed = subprocess.run(command, capture_output=True, text=True, check=True)
        return completed.stdout.strip()
    except Exception:
        return ""


def collect_system_info() -> dict[str, str]:
    info: dict[str, str] = {
        "Timestamp": dt.datetime.now().isoformat(timespec="seconds"),
        "Hostname": platform.node(),
        "Platform": platform.platform(),
        "Kernel": run_command(["uname", "-a"]),
    }

    lscpu = run_command(["lscpu"])
    if lscpu:
        def extract(pattern: str) -> str:
            match = re.search(pattern, lscpu, re.MULTILINE)
            return match.group(1).strip() if match else ""

        info["CPU"] = extract(r"^Model name:\s+(.+)$")
        info["CPU cores"] = extract(r"^Core\(s\) per socket:\s+(.+)$")
        info["CPU threads"] = extract(r"^CPU\(s\):\s+(.+)$")
        info["Max MHz"] = extract(r"^CPU max MHz:\s+(.+)$")
        info["L3 cache"] = extract(r"^L3 cache:\s+(.+)$")

    mem = run_command(["free", "-h"])
    if mem:
        lines = mem.splitlines()
        if len(lines) >= 2:
            parts = lines[1].split()
            if len(parts) >= 7:
                info["Memory total"] = parts[1]
                info["Memory available"] = parts[6]
        if len(lines) >= 3:
            parts = lines[2].split()
            if len(parts) >= 3:
                info["Swap total"] = parts[1]

    gpu = run_command(["sh", "-lc", "lspci | rg 'VGA|3D|Display'"])
    if gpu:
        info["GPU"] = gpu.splitlines()[0].strip()

    disks = run_command(["lsblk", "-d", "-o", "NAME,SIZE,MODEL"])
    if disks:
        disk_lines = [line.strip() for line in disks.splitlines()[1:] if line.strip()]
        if disk_lines:
            info["Storage"] = "; ".join(disk_lines[:4])

    git_commit = run_command(["git", "rev-parse", "--short", "HEAD"])
    if git_commit:
        info["Git commit"] = git_commit

    git_branch = run_command(["git", "branch", "--show-current"])
    if git_branch:
        info["Git branch"] = git_branch

    return {key: value for key, value in info.items() if value}


def load_runs(path: Path) -> list[Run]:
    payload = json.loads(path.read_text())
    context = payload.get("context", {})
    runs: list[Run] = []
    for bench in payload.get("benchmarks", []):
        if bench.get("aggregate_name") or bench.get("run_type") == "aggregate":
            continue
        if "real_time" not in bench and "cpu_time" not in bench:
            continue
        full_name = str(bench["name"])
        parts = full_name.split("/")
        family = parts[0]
        args = tuple(parts[1:])
        unit = str(bench.get("time_unit", "ns"))
        raw_value = float(bench.get("real_time", bench.get("cpu_time")))
        time_ns = raw_value * TIME_TO_NS.get(unit, 1.0)
        runs.append(
            Run(
                source=path.name,
                family=family,
                full_name=full_name,
                args=args,
                label=str(bench.get("label", "")),
                time_ns=time_ns,
                original_unit=unit,
                context=context,
            )
        )
    return runs


def detect_group(run: Run) -> tuple[str, str, str] | None:
    if run.family in EXPLICIT_GROUPS:
        title, variant, baseline = EXPLICIT_GROUPS[run.family]
        suffix = ", ".join(run.args) if run.args else "default"
        return (f"{title} ({suffix})", variant, baseline)

    if run.family == "BM_ComparePoolTypes_LightWorkload" and run.label:
        task_match = re.search(r"tasks=(\d+)", run.label)
        pool_name = next((name for name in POOL_NAMES if name in run.label), run.label)
        tasks = task_match.group(1) if task_match else (run.args[0] if run.args else "unknown")
        return (f"Pool comparison: light workload ({tasks} tasks)", pool_name, "thread_pool_backend")

    if run.family == "BM_PostVsSubmit":
        tasks = run.args[0] if run.args else "unknown"
        variant = run.label or ("submit(future)" if run.args[-1:] == ("0",) else "post(fire-forget)")
        return (f"Post vs submit ({tasks} tasks)", variant, "submit(future)")

    return None


def build_comparisons(runs: Iterable[Run]) -> list[ComparisonGroup]:
    grouped: dict[str, tuple[str, str, list[Run]]] = {}
    variant_names: dict[str, list[str]] = {}

    for run in runs:
        detected = detect_group(run)
        if not detected:
            continue
        title, variant, baseline = detected
        key = title
        if key not in grouped:
            grouped[key] = (title, baseline, [])
            variant_names[key] = []
        grouped[key][2].append(run)
        variant_names[key].append(variant)
        run.context = dict(run.context)
        run.context["variant_name"] = variant

    groups: list[ComparisonGroup] = []
    for key, (title, baseline, values) in grouped.items():
        if len(values) < 2:
            continue
        groups.append(ComparisonGroup(title=title, baseline=baseline, runs=values))
    groups.sort(key=lambda group: group.title)
    return groups


def format_time_ns(time_ns: float) -> str:
    if time_ns >= 1_000_000_000.0:
        return f"{time_ns / 1_000_000_000.0:.3f} s"
    if time_ns >= 1_000_000.0:
        return f"{time_ns / 1_000_000.0:.3f} ms"
    if time_ns >= 1_000.0:
        return f"{time_ns / 1_000.0:.3f} us"
    return f"{time_ns:.0f} ns"


def speedup_label(baseline_ns: float, run_ns: float) -> str:
    if run_ns <= 0:
        return "n/a"
    ratio = baseline_ns / run_ns
    if abs(ratio - 1.0) < 0.02:
        return "same speed"
    if ratio > 1.0:
        return f"{ratio:.2f}x faster"
    return f"{1.0 / ratio:.2f}x slower"


def render_bar_chart(items: list[tuple[str, float, str]], width: int = 920, bar_height: int = 30) -> str:
    if not items:
        return ""
    max_value = max(value for _, value, _ in items) or 1.0
    label_width = 280
    chart_width = width - label_width - 120
    height = len(items) * (bar_height + 18) + 24
    bars = []
    for index, (label, value, annotation) in enumerate(items):
        y = 20 + index * (bar_height + 18)
        bar_width = max(2.0, chart_width * (value / max_value))
        bars.append(
            f'<text x="8" y="{y + 19}" fill="#122033" font-size="13">{html.escape(label)}</text>'
            f'<rect x="{label_width}" y="{y}" width="{bar_width:.1f}" height="{bar_height}" rx="6" fill="#2a7fff" />'
            f'<text x="{label_width + bar_width + 10:.1f}" y="{y + 19}" fill="#122033" font-size="13">{html.escape(annotation)}</text>'
        )
    return (
        f'<svg viewBox="0 0 {width} {height}" width="100%" height="{height}" role="img">'
        + "".join(bars)
        + "</svg>"
    )


def comparison_table(group: ComparisonGroup) -> str:
    variants: list[tuple[str, Run]] = []
    for run in group.runs:
        variant = str(run.context.get("variant_name", run.label or run.family))
        variants.append((variant, run))

    baseline_run = next((run for variant, run in variants if variant == group.baseline), variants[0][1])
    rows = []
    chart_items = []

    for variant, run in sorted(variants, key=lambda item: item[1].time_ns):
        speedup = speedup_label(baseline_run.time_ns, run.time_ns)
        rows.append(
            "<tr>"
            f"<td>{html.escape(variant)}</td>"
            f"<td>{html.escape(format_time_ns(run.time_ns))}</td>"
            f"<td>{html.escape(speedup)}</td>"
            f"<td>{html.escape(run.source)}</td>"
            "</tr>"
        )
        chart_items.append((variant, run.time_ns, f"{format_time_ns(run.time_ns)} | {speedup}"))

    return (
        f"<section class='card'><h3>{html.escape(group.title)}</h3>"
        + render_bar_chart(chart_items)
        + "<table><thead><tr><th>Variant</th><th>Time</th><th>Relative to baseline</th><th>Source</th></tr></thead><tbody>"
        + "".join(rows)
        + "</tbody></table></section>"
    )


def overall_section(runs: list[Run]) -> str:
    top = sorted(runs, key=lambda run: run.time_ns)[:14]
    chart_items = []
    for run in top:
        label = run.label or run.full_name
        chart_items.append((label[:42], run.time_ns, format_time_ns(run.time_ns)))
    return (
        "<section class='card'><h3>Fastest benchmark runs</h3>"
        "<p>Absolute timings across the provided JSON files. Lower is better.</p>"
        + render_bar_chart(chart_items)
        + "</section>"
    )


def system_info_section(system_info: dict[str, str]) -> str:
    rows = "".join(
        f"<tr><th>{html.escape(key)}</th><td>{html.escape(value)}</td></tr>" for key, value in system_info.items()
    )
    return (
        "<section class='card'><h3>System information</h3>"
        "<table><tbody>"
        + rows
        + "</tbody></table></section>"
    )


def build_html(title: str, runs: list[Run], groups: list[ComparisonGroup], system_info: dict[str, str]) -> str:
    comparison_sections = "".join(comparison_table(group) for group in groups)
    if not comparison_sections:
        comparison_sections = "<section class='card'><h3>No comparison groups detected</h3><p>The input JSON did not match any known comparison patterns yet.</p></section>"

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>{html.escape(title)}</title>
  <style>
    :root {{
      --bg: #f3f6fb;
      --card: #ffffff;
      --ink: #122033;
      --muted: #5b6b82;
      --line: #dbe3ef;
      --accent: #2a7fff;
      --accent-soft: #d9e8ff;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      padding: 32px;
      background: radial-gradient(circle at top left, #ffffff 0%, var(--bg) 55%);
      color: var(--ink);
      font: 15px/1.45 "IBM Plex Sans", "Segoe UI", sans-serif;
    }}
    h1, h2, h3 {{ margin: 0 0 12px; }}
    p {{ margin: 0 0 12px; color: var(--muted); }}
    .layout {{
      display: grid;
      gap: 20px;
      max-width: 1320px;
      margin: 0 auto;
    }}
    .hero {{
      padding: 28px;
      border-radius: 18px;
      background: linear-gradient(135deg, #0e1b2d 0%, #163f73 100%);
      color: #ffffff;
      box-shadow: 0 24px 60px rgba(11, 31, 57, 0.16);
    }}
    .hero p {{ color: rgba(255,255,255,0.82); }}
    .grid {{
      display: grid;
      gap: 20px;
      grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
    }}
    .card {{
      background: var(--card);
      border: 1px solid var(--line);
      border-radius: 18px;
      padding: 22px;
      box-shadow: 0 10px 30px rgba(17, 34, 51, 0.06);
    }}
    table {{
      width: 100%;
      border-collapse: collapse;
      margin-top: 12px;
    }}
    th, td {{
      text-align: left;
      border-top: 1px solid var(--line);
      padding: 10px 8px;
      vertical-align: top;
    }}
    th {{ width: 220px; color: var(--muted); font-weight: 600; }}
    .section-title {{
      margin-top: 10px;
      padding-left: 6px;
      border-left: 4px solid var(--accent);
    }}
    @media (max-width: 720px) {{
      body {{ padding: 18px; }}
      th {{ width: 140px; }}
    }}
  </style>
</head>
<body>
  <main class="layout">
    <section class="hero">
      <h1>{html.escape(title)}</h1>
      <p>Google Benchmark comparison report with automatically collected machine data and relative speedups.</p>
      <p>Loaded benchmark runs: {len(runs)} | Comparison groups: {len(groups)}</p>
    </section>
    <div class="grid">
      {system_info_section(system_info)}
      {overall_section(runs)}
    </div>
    <h2 class="section-title">Relative speedups</h2>
    {comparison_sections}
  </main>
</body>
</html>
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a local HTML benchmark report with graphs and speedups.")
    parser.add_argument("json_files", nargs="+", help="Google Benchmark JSON files")
    parser.add_argument("--output", required=True, help="Output HTML file")
    parser.add_argument("--title", default="ThreadSchedule benchmark report", help="Report title")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    json_paths = [Path(value) for value in args.json_files]
    runs: list[Run] = []
    for path in json_paths:
        runs.extend(load_runs(path))

    if not runs:
        raise SystemExit("No benchmark runs found in the provided JSON files.")

    system_info = collect_system_info()
    groups = build_comparisons(runs)
    html_payload = build_html(args.title, runs, groups, system_info)

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(html_payload, encoding="utf-8")
    print(f"Wrote benchmark report to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
