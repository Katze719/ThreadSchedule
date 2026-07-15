#!/usr/bin/env python3
"""Generate standalone SVG charts from Google Benchmark JSON for README embedding.

This intentionally has no third-party dependencies (no matplotlib): it emits
self-contained SVG files with an explicit light background so they render well
in both light and dark GitHub themes.
"""

from __future__ import annotations

import argparse
import html
import json
from dataclasses import dataclass
from pathlib import Path

TIME_TO_NS = {"ns": 1.0, "us": 1_000.0, "ms": 1_000_000.0, "s": 1_000_000_000.0}

POOL_ORDER = ("thread_pool_backend", "polling_pool_backend", "work_stealing_pool_backend", "lightweight_pool_backend")
POOL_COLORS = {
    "thread_pool_backend": "#2a7fff",
    "polling_pool_backend": "#16a34a",
    "work_stealing_pool_backend": "#f59e0b",
    "lightweight_pool_backend": "#db2777",
}
VARIANT_COLORS = {
    "submit(future)": "#2a7fff",
    "post(fire-forget)": "#16a34a",
}
WORKLOAD_ORDER = ("tiny", "medium", "heavy", "imbalanced")

CXX_COLORS = {"C++17": "#94a3b8", "C++20": "#2a7fff", "C++23": "#16a34a", "C++26": "#db2777"}
CALLABLE_BATCH = 256  # kBatch in callable_std_benchmarks.cpp
CALLABLE_CAPTURES = (("Small", "small (8 B)"), ("Medium", "medium (48 B)"), ("Large", "large (128 B)"))

INK = "#122033"
MUTED = "#5b6b82"
LINE = "#dbe3ef"
BG = "#ffffff"


@dataclass
class Entry:
    family: str
    args: tuple[str, ...]
    label: str
    time_ns: float
    items_per_second: float


def load_entries(path: Path) -> list[Entry]:
    payload = json.loads(path.read_text())
    entries: list[Entry] = []
    for bench in payload.get("benchmarks", []):
        if bench.get("run_type") == "aggregate" or bench.get("aggregate_name"):
            continue
        if "real_time" not in bench and "cpu_time" not in bench:
            continue
        parts = str(bench["name"]).split("/")
        unit = str(bench.get("time_unit", "ns"))
        raw = float(bench.get("real_time", bench.get("cpu_time")))
        entries.append(
            Entry(
                family=parts[0],
                args=tuple(parts[1:]),
                label=str(bench.get("label", "")),
                time_ns=raw * TIME_TO_NS.get(unit, 1.0),
                items_per_second=float(bench.get("items_per_second", 0.0)),
            )
        )
    return entries


def fmt_time(time_ns: float) -> str:
    if time_ns >= 1_000_000_000.0:
        return f"{time_ns / 1_000_000_000.0:.2f} s"
    if time_ns >= 1_000_000.0:
        return f"{time_ns / 1_000_000.0:.2f} ms"
    if time_ns >= 1_000.0:
        return f"{time_ns / 1_000.0:.2f} us"
    return f"{time_ns:.0f} ns"


def svg_header(width: int, height: int, title: str, subtitle: str) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
        f'width="{width}" height="{height}" font-family="\'Segoe UI\', Helvetica, Arial, sans-serif">',
        f'<rect x="0" y="0" width="{width}" height="{height}" rx="14" fill="{BG}" stroke="{LINE}" />',
        f'<text x="24" y="34" fill="{INK}" font-size="18" font-weight="700">{html.escape(title)}</text>',
        f'<text x="24" y="56" fill="{MUTED}" font-size="13">{html.escape(subtitle)}</text>',
    ]


def horizontal_bar_chart(
    title: str,
    subtitle: str,
    items: list[tuple[str, float, str, str]],
    value_suffix: str = "",
) -> str:
    """items: (label, value, annotation, color)."""
    width = 760
    top = 80
    bar_h = 34
    gap = 22
    label_w = 200
    right_pad = 150
    chart_w = width - label_w - right_pad - 24
    height = top + len(items) * (bar_h + gap) + 16
    max_value = max(v for _, v, _, _ in items) or 1.0

    parts = svg_header(width, height, title, subtitle)
    for i, (label, value, annotation, color) in enumerate(items):
        y = top + i * (bar_h + gap)
        bar_w = max(3.0, chart_w * (value / max_value))
        parts.append(
            f'<text x="24" y="{y + bar_h / 2 + 5:.0f}" fill="{INK}" font-size="13" font-weight="600">{html.escape(label)}</text>'
        )
        parts.append(
            f'<rect x="{label_w}" y="{y}" width="{bar_w:.1f}" height="{bar_h}" rx="6" fill="{color}" />'
        )
        parts.append(
            f'<text x="{label_w + bar_w + 10:.1f}" y="{y + bar_h / 2 + 5:.0f}" fill="{INK}" '
            f'font-size="13" font-weight="600">{html.escape(annotation)}</text>'
        )
    parts.append("</svg>")
    return "\n".join(parts)


def grouped_bar_chart(
    title: str,
    subtitle: str,
    group_labels: list[str],
    series: list[tuple[str, list[float], str]],
    y_axis_label: str,
) -> str:
    """series: (name, values_per_group, color)."""
    width = 820
    top = 96
    plot_h = 300
    left_pad = 64
    right_pad = 24
    plot_w = width - left_pad - right_pad
    height = top + plot_h + 86

    max_value = max((max(vals) for _, vals, _ in series), default=1.0) or 1.0
    # round max up to a nice number
    import math

    magnitude = 10 ** math.floor(math.log10(max_value)) if max_value > 0 else 1
    nice_max = math.ceil(max_value / magnitude) * magnitude
    if nice_max == 0:
        nice_max = 1

    parts = svg_header(width, height, title, subtitle)

    baseline_y = top + plot_h
    # gridlines + y ticks
    ticks = 5
    for t in range(ticks + 1):
        gy = baseline_y - plot_h * t / ticks
        val = nice_max * t / ticks
        parts.append(
            f'<line x1="{left_pad}" y1="{gy:.1f}" x2="{width - right_pad}" y2="{gy:.1f}" stroke="{LINE}" stroke-width="1" />'
        )
        parts.append(
            f'<text x="{left_pad - 8}" y="{gy + 4:.1f}" fill="{MUTED}" font-size="11" text-anchor="end">{val:.1f}</text>'
        )
    parts.append(
        f'<text x="16" y="{top - 8}" fill="{MUTED}" font-size="12" font-weight="600">{html.escape(y_axis_label)}</text>'
    )

    n_groups = len(group_labels)
    n_series = len(series)
    group_w = plot_w / n_groups
    inner_pad = group_w * 0.16
    bar_w = (group_w - 2 * inner_pad) / n_series

    for g in range(n_groups):
        gx = left_pad + g * group_w
        for s, (_, vals, color) in enumerate(series):
            value = vals[g]
            bh = plot_h * (value / nice_max)
            bx = gx + inner_pad + s * bar_w
            parts.append(
                f'<rect x="{bx:.1f}" y="{baseline_y - bh:.1f}" width="{bar_w - 2:.1f}" height="{bh:.1f}" rx="3" fill="{color}" />'
            )
        parts.append(
            f'<text x="{gx + group_w / 2:.1f}" y="{baseline_y + 18:.1f}" fill="{INK}" '
            f'font-size="12" font-weight="600" text-anchor="middle">{html.escape(group_labels[g])}</text>'
        )

    # legend
    legend_y = baseline_y + 44
    lx = left_pad
    for name, _, color in series:
        parts.append(f'<rect x="{lx}" y="{legend_y}" width="14" height="14" rx="3" fill="{color}" />')
        parts.append(
            f'<text x="{lx + 20}" y="{legend_y + 12}" fill="{INK}" font-size="12">{html.escape(name)}</text>'
        )
        lx += 30 + len(name) * 7.2
    parts.append("</svg>")
    return "\n".join(parts)


def pool_runs_by_tasks(entries: list[Entry]) -> dict[str, dict[str, Entry]]:
    """tasks -> pool_name -> Entry."""
    out: dict[str, dict[str, Entry]] = {}
    for e in entries:
        if e.family != "BM_ComparePoolTypes_LightWorkload":
            continue
        # Label format is "<PoolName> tasks=N"; match the leading token so that
        # "thread_pool_backend" does not shadow "polling_pool_backend" via substring matching.
        first_token = e.label.split()[0] if e.label else ""
        pool = first_token if first_token in POOL_ORDER else None
        if not pool:
            continue
        tasks = e.args[0] if e.args else "?"
        out.setdefault(tasks, {})[pool] = e
    return out


def build_pool_comparison(entries: list[Entry], out_dir: Path) -> Path | None:
    by_tasks = pool_runs_by_tasks(entries)
    if not by_tasks:
        return None
    tasks = max(by_tasks, key=lambda t: int(t) if t.isdigit() else 0)
    pools = by_tasks[tasks]
    baseline = pools.get("thread_pool_backend")
    items: list[tuple[str, float, str, str]] = []
    ordered = sorted(pools.items(), key=lambda kv: kv[1].time_ns)
    for pool, e in ordered:
        if baseline and baseline.time_ns > 0 and e.time_ns > 0:
            ratio = baseline.time_ns / e.time_ns
            if abs(ratio - 1.0) < 0.02:
                rel = "baseline"
            elif ratio >= 1.0:
                rel = f"{ratio:.2f}x faster"
            else:
                rel = f"{1.0 / ratio:.2f}x slower"
        else:
            rel = ""
        annotation = f"{fmt_time(e.time_ns)}  ({rel})" if rel else fmt_time(e.time_ns)
        items.append((pool, e.time_ns, annotation, POOL_COLORS.get(pool, "#2a7fff")))
    svg = horizontal_bar_chart(
        "Thread pool comparison \u2014 light workload",
        f"Wall-clock time to run {int(tasks):,} tiny tasks (lower is better, relative to thread_pool_backend)",
        items,
    )
    path = out_dir / "pool_comparison.svg"
    path.write_text(svg, encoding="utf-8")
    return path


def build_pool_throughput(entries: list[Entry], out_dir: Path) -> Path | None:
    by_tasks = pool_runs_by_tasks(entries)
    if not by_tasks:
        return None
    task_keys = sorted((t for t in by_tasks if t.isdigit()), key=lambda t: int(t))
    group_labels = [f"{int(t):,}" for t in task_keys]
    series: list[tuple[str, list[float], str]] = []
    for pool in POOL_ORDER:
        vals = []
        for t in task_keys:
            e = by_tasks[t].get(pool)
            vals.append((e.items_per_second / 1_000_000.0) if e else 0.0)
        if any(v > 0 for v in vals):
            series.append((pool, vals, POOL_COLORS.get(pool, "#2a7fff")))
    if not series:
        return None
    svg = grouped_bar_chart(
        "Thread pool throughput by batch size",
        "Tasks processed per second for the light workload (higher is better)",
        group_labels,
        series,
        "M tasks / second",
    )
    path = out_dir / "pool_throughput.svg"
    path.write_text(svg, encoding="utf-8")
    return path


def build_post_vs_submit(entries: list[Entry], out_dir: Path) -> Path | None:
    by_tasks: dict[str, dict[str, Entry]] = {}
    for e in entries:
        if e.family != "BM_PostVsSubmit":
            continue
        tasks = e.args[0] if e.args else "?"
        by_tasks.setdefault(tasks, {})[e.label] = e
    if not by_tasks:
        return None
    tasks = max(by_tasks, key=lambda t: int(t) if t.isdigit() else 0)
    variants = by_tasks[tasks]
    submit = variants.get("submit(future)")
    items: list[tuple[str, float, str, str]] = []
    for name, e in sorted(variants.items(), key=lambda kv: kv[1].time_ns):
        if submit and submit.time_ns > 0 and e.time_ns > 0:
            ratio = submit.time_ns / e.time_ns
            rel = "baseline" if abs(ratio - 1.0) < 0.02 else (
                f"{ratio:.2f}x faster" if ratio > 1.0 else f"{1.0 / ratio:.2f}x slower"
            )
            annotation = f"{fmt_time(e.time_ns)}  ({rel})"
        else:
            annotation = fmt_time(e.time_ns)
        items.append((name, e.time_ns, annotation, VARIANT_COLORS.get(name, "#2a7fff")))
    svg = horizontal_bar_chart(
        "post() vs submit()",
        f"Submission overhead for {int(tasks):,} tasks: post() skips the future/packaged_task path (lower is better)",
        items,
    )
    path = out_dir / "post_vs_submit.svg"
    path.write_text(svg, encoding="utf-8")
    return path


def build_pool_workload(entries: list[Entry], out_dir: Path) -> Path | None:
    by_wl: dict[str, dict[str, Entry]] = {}
    for e in entries:
        if e.family != "BM_ComparePoolWorkload" or not e.label:
            continue
        tokens = e.label.split()
        if len(tokens) < 2:
            continue
        pool, wl = tokens[0], tokens[1]
        if pool not in POOL_ORDER:
            continue
        by_wl.setdefault(wl, {})[pool] = e
    if not by_wl:
        return None

    group_labels = [wl for wl in WORKLOAD_ORDER if wl in by_wl]
    series: list[tuple[str, list[float], str]] = []
    for pool in POOL_ORDER:
        vals: list[float] = []
        for wl in group_labels:
            row = by_wl[wl]
            best = min((r.time_ns for r in row.values()), default=0.0) or 1.0
            e = row.get(pool)
            vals.append((e.time_ns / best) if e else 0.0)
        if any(v > 0 for v in vals):
            series.append((pool, vals, POOL_COLORS.get(pool, "#2a7fff")))
    if not series:
        return None

    svg = grouped_bar_chart(
        "Which pool wins depends on the workload",
        "Time relative to the fastest pool per workload (1.0 = winner, shorter is better; pool built once, 4 threads)",
        group_labels,
        series,
        "relative time (1.0 = fastest)",
    )
    path = out_dir / "pool_workload.svg"
    path.write_text(svg, encoding="utf-8")
    return path


def load_callable_medians(path: Path) -> dict[str, float]:
    """Return family -> per-task time (ns) from an aggregate-only callable JSON."""
    payload = json.loads(path.read_text())
    out: dict[str, float] = {}
    for bench in payload.get("benchmarks", []):
        if bench.get("aggregate_name") != "median":
            continue
        family = str(bench.get("run_name", bench.get("name", ""))).split("/")[0]
        unit = str(bench.get("time_unit", "ns"))
        ns = float(bench.get("real_time", 0.0)) * TIME_TO_NS.get(unit, 1.0)
        out[family] = ns / CALLABLE_BATCH
    return out


def standard_from_filename(path: Path) -> str | None:
    name = path.name
    for token in ("cxx17", "cxx20", "cxx23", "cxx26"):
        if token in name:
            return "C++" + token[3:]
    return None


def build_callable_charts(std_medians: dict[str, dict[str, float]], out_dir: Path) -> list[Path]:
    std_order = [s for s in ("C++17", "C++20", "C++23", "C++26") if s in std_medians]
    if not std_order:
        return []
    group_labels = [label for _, label in CALLABLE_CAPTURES]
    paths: list[Path] = []

    # Chart A: move_callable cost across standards (std::function vs move_only_function).
    series_a: list[tuple[str, list[float], str]] = []
    for std in std_order:
        vals = [std_medians[std].get(f"BM_MoveCallable_{key}", 0.0) for key, _ in CALLABLE_CAPTURES]
        if any(v > 0 for v in vals):
            series_a.append((std, vals, CXX_COLORS[std]))
    if series_a:
        svg = grouped_bar_chart(
            "Does replacing std::function help? (thread_pool_backend task storage)",
            "Build + invoke cost per task for detail::move_callable "
            "(std::function on C++17/20, std::move_only_function on C++23+); lower is better",
            group_labels,
            series_a,
            "ns per task",
        )
        path = out_dir / "callable_standards.svg"
        path.write_text(svg, encoding="utf-8")
        paths.append(path)

    # Chart B: copyable_callable cost across standards (std::function vs copyable_function).
    series_b: list[tuple[str, list[float], str]] = []
    for std in std_order:
        vals = [std_medians[std].get(f"BM_CopyableCallable_{key}", 0.0) for key, _ in CALLABLE_CAPTURES]
        if any(v > 0 for v in vals):
            series_b.append((std, vals, CXX_COLORS[std]))
    if series_b:
        svg = grouped_bar_chart(
            "Do C++26 copyable callbacks help?",
            "Build + invoke cost per task for detail::copyable_callable "
            "(std::function before C++26, std::copyable_function on C++26); lower is better",
            group_labels,
            series_b,
            "ns per task",
        )
        path = out_dir / "callable_copyable_standards.svg"
        path.write_text(svg, encoding="utf-8")
        paths.append(path)

    # Chart C: SBO callable vs std-library callable at the newest available standard.
    newest = std_order[-1]
    medians = std_medians[newest]
    move_vals = [medians.get(f"BM_MoveCallable_{key}", 0.0) for key, _ in CALLABLE_CAPTURES]
    sbo_vals = [medians.get(f"BM_Sbo_{key}", 0.0) for key, _ in CALLABLE_CAPTURES]
    if any(v > 0 for v in move_vals) and any(v > 0 for v in sbo_vals):
        series_c = [
            ("move_callable (thread_pool_backend / std lib)", move_vals, "#2a7fff"),
            ("sbo_callable (lightweight_pool_backend)", sbo_vals, "#db2777"),
        ]
        svg = grouped_bar_chart(
            f"Do the SBO callables help? ({newest})",
            "Per-task cost; the 48 B capture fits the SBO buffer but spills the std-library "
            "callable to the heap (lower is better)",
            group_labels,
            series_c,
            "ns per task",
        )
        path = out_dir / "callable_sbo.svg"
        path.write_text(svg, encoding="utf-8")
        paths.append(path)

    return paths


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json_files", nargs="+", help="Google Benchmark JSON files")
    parser.add_argument("--output-dir", required=True, help="Directory for generated SVG files")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    entries: list[Entry] = []
    std_medians: dict[str, dict[str, float]] = {}
    for value in args.json_files:
        path = Path(value)
        standard = standard_from_filename(path)
        if standard:
            std_medians[standard] = load_callable_medians(path)
        else:
            entries.extend(load_entries(path))

    if not entries and not std_medians:
        raise SystemExit("No benchmark entries found in the provided JSON files.")

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    generated: list[Path | None] = [
        build_pool_throughput(entries, out_dir),
        build_pool_comparison(entries, out_dir),
        build_pool_workload(entries, out_dir),
        build_post_vs_submit(entries, out_dir),
    ]
    generated.extend(build_callable_charts(std_medians, out_dir))
    for path in generated:
        if path:
            print(f"Wrote {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
