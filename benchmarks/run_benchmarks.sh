#!/usr/bin/env bash
# Runs the benchmark suite in a stable CPU environment:
#   - governor set to 'performance' (no frequency ramping mid-run)
#   - optionally turbo disabled with --no-turbo (steadier clocks, lower abs numbers)
#   - previous governor/turbo restored on exit
# Thread pinning is done in the benchmark binaries themselves (ThreadPin.h:
# outgoing→cpu1, engine→cpu2, incoming→cpu3).
#
# Usage: sudo benchmarks/run_benchmarks.sh [--no-turbo] [build-dir]
set -euo pipefail

NO_TURBO=0
if [[ "${1:-}" == "--no-turbo" ]]; then
  NO_TURBO=1
  shift
fi
BUILD_DIR="${1:-build}"

if [[ $EUID -ne 0 ]]; then
  echo "Needs root to set the CPU governor: sudo $0 $*" >&2
  exit 1
fi

CPUS=(/sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor)
PREV_GOVERNOR=$(cat "${CPUS[0]}")
TURBO_FILE=/sys/devices/system/cpu/intel_pstate/no_turbo
PREV_TURBO=$([[ -f $TURBO_FILE ]] && cat $TURBO_FILE || echo "")

restore() {
  for f in "${CPUS[@]}"; do echo "$PREV_GOVERNOR" > "$f"; done
  [[ -n "$PREV_TURBO" ]] && echo "$PREV_TURBO" > $TURBO_FILE
  echo "Restored governor='$PREV_GOVERNOR'${PREV_TURBO:+, no_turbo=$PREV_TURBO}"
}
trap restore EXIT

for f in "${CPUS[@]}"; do echo performance > "$f"; done
if [[ $NO_TURBO -eq 1 && -f $TURBO_FILE ]]; then echo 1 > $TURBO_FILE; fi
echo "Governor=performance$( [[ $NO_TURBO -eq 1 ]] && echo ', turbo disabled' )"

# Drop root for the actual benchmark runs if invoked via sudo.
RUN=(env)
if [[ -n "${SUDO_USER:-}" ]]; then RUN=(sudo -u "$SUDO_USER" env); fi

echo "== order latency (3 runs) =="
for i in 1 2 3; do
  "${RUN[@]}" "$BUILD_DIR/benchmarks/benchmark_orderlatency" | grep -E "Throughput|Average|Median|percentile"
done

echo "== agent latency =="
"${RUN[@]}" "$BUILD_DIR/benchmarks/benchmark_agentlatency"

echo "== simulation (3 repetitions, medians) =="
"${RUN[@]}" "$BUILD_DIR/benchmarks/benchmark_simulation" \
  --benchmark_repetitions=3 --benchmark_report_aggregates_only=true
