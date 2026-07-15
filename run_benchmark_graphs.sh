#!/bin/bash

set -euo pipefail

QUICK_MODE=false
SHOW_HELP=false
OUTPUT_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick|-q)
            QUICK_MODE=true
            shift
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --help|-h)
            SHOW_HELP=true
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [[ "$SHOW_HELP" == "true" ]]; then
    cat <<'EOF'
ThreadSchedule Benchmark Graph Runner

Usage:
  ./run_benchmark_graphs.sh [--quick] [--output-dir DIR]

What it does:
  - Runs selected comparison-focused Google Benchmark targets
  - Writes JSON outputs into a report directory
  - Generates an HTML report with inline SVG graphs and speedups
  - Captures local machine specs automatically inside the report

Examples:
  ./run_benchmark_graphs.sh
  ./run_benchmark_graphs.sh --quick
  ./run_benchmark_graphs.sh --output-dir build/benchmark-reports/latest
EOF
    exit 0
fi

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BENCHMARK_DIR="${BUILD_DIR}/benchmarks"

if [[ -z "${OUTPUT_DIR}" ]]; then
    TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
    OUTPUT_DIR="${BUILD_DIR}/benchmark-reports/${TIMESTAMP}"
fi

mkdir -p "${OUTPUT_DIR}"

if [[ ! -d "${BENCHMARK_DIR}" ]]; then
    echo "Benchmark directory not found: ${BENCHMARK_DIR}" >&2
    echo "Build with -DTHREADSCHEDULE_BUILD_BENCHMARKS=ON first." >&2
    exit 1
fi

if [[ "$QUICK_MODE" == "true" ]]; then
    BENCH_MIN_TIME="0.4s"
    BENCH_REPETITIONS="1"
else
    BENCH_MIN_TIME="1.5s"
    BENCH_REPETITIONS="3"
fi

run_json_benchmark() {
    local executable="$1"
    local filter="$2"
    local output_json="$3"

    if [[ ! -x "${BENCHMARK_DIR}/${executable}" ]]; then
        echo "Skipping ${executable}: not built" >&2
        return 0
    fi

    "${BENCHMARK_DIR}/${executable}" \
        --benchmark_filter="${filter}" \
        --benchmark_min_time="${BENCH_MIN_TIME}" \
        --benchmark_repetitions="${BENCH_REPETITIONS}" \
        --benchmark_format=json \
        --benchmark_out="${output_json}" \
        --benchmark_out_format=json
}

JSON_FILES=()

THREADPOOL_JSON="${OUTPUT_DIR}/threadpool_comparisons.json"
run_json_benchmark "threadpool_basic_benchmarks" "BM_ComparePoolTypes_LightWorkload|BM_PostVsSubmit" "${THREADPOOL_JSON}"
if [[ -f "${THREADPOOL_JSON}" ]]; then
    JSON_FILES+=("${THREADPOOL_JSON}")
fi

if [[ ${#JSON_FILES[@]} -eq 0 ]]; then
    echo "No benchmark JSON files were produced." >&2
    exit 1
fi

python3 "${PROJECT_ROOT}/benchmarks/generate_benchmark_report.py" \
    --output "${OUTPUT_DIR}/index.html" \
    --title "ThreadSchedule benchmark comparison report" \
    "${JSON_FILES[@]}"

echo
echo "Benchmark graphs written to:"
echo "  ${OUTPUT_DIR}/index.html"
