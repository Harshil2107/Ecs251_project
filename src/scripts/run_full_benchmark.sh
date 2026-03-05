#!/usr/bin/env bash
# Full benchmark suite: runs static and dynamic pools across all traffic
# patterns for both the matrix-multiply (memory) and latency benchmarks,
# each repeated NUM_RUNS times.
#
# Usage:
#   ./src/scripts/run_full_benchmark.sh [output_base]
#
# Output layout:
#   $OUTPUT_BASE/matmul/<traffic>-matmul_<N>/run<R>/{static,dynamic}.json
#   $OUTPUT_BASE/latency/<traffic>-latency_<S>us/run<R>/{static,dynamic}.json

set -euo pipefail

# ===========================================================================
# Configuration — edit these to change experiment parameters
# ===========================================================================
THREADS=10              # static pool: fixed thread count
MIN_THREADS=2           # dynamic pool: always-alive minimum
TASKS_PER_THREAD=10     # dynamic pool: spawn 1 thread per N pending tasks
MATRIX_SIZES=(128 256 512)        # Light / Medium / Heavy working-set sizes (proposal §matrix)
SLEEP_US_VALUES=(100 1000 10000) # Light=100µs / Medium=1ms / Heavy=10ms  (proposal §latency)
TRAFFIC_PATTERNS=(Steady Burst Ramp)
NUM_RUNS=2
CORES="0-9"             # CPU affinity (10 cores)
# ===========================================================================

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC_DIR="$REPO_ROOT/src"
MEM_BINARY="$SRC_DIR/arrival_memory_benchmark"
LAT_BINARY="$SRC_DIR/arrival_latency_benchmark"
OUTPUT_BASE="$REPO_ROOT/${1:-results/full_benchmark}"

# ---------------------------------------------------------------------------
# Build both binaries
# ---------------------------------------------------------------------------
echo "=== Building arrival_memory_benchmark ==="
g++ -std=c++17 -pthread -O2 -Wall -Wextra \
    -I"$REPO_ROOT/queue" \
    -o "$MEM_BINARY" \
    "$SRC_DIR/arrival_memory_benchmark.cpp"
echo "Binary: $MEM_BINARY"

echo ""
echo "=== Building arrival_latency_benchmark ==="
g++ -std=c++17 -pthread -O2 -Wall -Wextra \
    -I"$REPO_ROOT/queue" \
    -o "$LAT_BINARY" \
    "$SRC_DIR/arrival_latency_benchmark.cpp"
echo "Binary: $LAT_BINARY"
echo ""

# Total runs: matmul (traffic x sizes x runs x 2 pools) + latency (traffic x sleep_vals x runs x 2 pools)
total=$(( (${#TRAFFIC_PATTERNS[@]} * ${#MATRIX_SIZES[@]} + ${#TRAFFIC_PATTERNS[@]} * ${#SLEEP_US_VALUES[@]}) * NUM_RUNS * 2 ))
done_count=0

# ---------------------------------------------------------------------------
# Helper: run one pool type and copy result
# ---------------------------------------------------------------------------
run_one() {
    local binary="$1"
    local pool="$2"
    local arg2="$3"       # matrix size OR sleep_us
    local out_file="$4"
    local traffic="$5"
    local min_t="$6"
    local tpt="$7"
    local label="$8"

    done_count=$(( done_count + 1 ))
    echo "  [${done_count}/${total}] ${label}"

    local TMPDIR
    TMPDIR=$(mktemp -d)
    taskset -c "$CORES" "$binary" \
        "$THREADS" "$arg2" "$TMPDIR" \
        "$pool" "$min_t" "$tpt" "$traffic"
    cp "$TMPDIR/results_${traffic}.json" "$out_file"
    rm -rf "$TMPDIR"
}

# ---------------------------------------------------------------------------
# Matrix-multiply (memory) benchmark
# ---------------------------------------------------------------------------
echo "##########################################################"
echo "## MATRIX-MULTIPLY (memory) BENCHMARK"
echo "##########################################################"
echo ""

for TRAFFIC in "${TRAFFIC_PATTERNS[@]}"; do
    for N in "${MATRIX_SIZES[@]}"; do

        DIR="$OUTPUT_BASE/matmul/${TRAFFIC,,}-matmul_${N}"
        mkdir -p "$DIR"

        echo "========================================================="
        echo " Traffic : $TRAFFIC  |  Matrix: ${N}x${N}"
        echo " Output  : $DIR"
        echo "========================================================="

        for RUN in $(seq 2 $((NUM_RUNS+1))); do
            RUN_DIR="$DIR/run${RUN}"
            mkdir -p "$RUN_DIR"

            run_one "$MEM_BINARY" static  "$N" "$RUN_DIR/static.json"  \
                    "$TRAFFIC" 1 1 "static  run ${RUN}/${NUM_RUNS}  (matmul ${N}x${N} ${TRAFFIC})"
            run_one "$MEM_BINARY" dynamic "$N" "$RUN_DIR/dynamic.json" \
                    "$TRAFFIC" "$MIN_THREADS" "$TASKS_PER_THREAD" \
                    "dynamic run ${RUN}/${NUM_RUNS}  (matmul ${N}x${N} ${TRAFFIC})"
        done

        echo ""
        echo "  Files written:"
        find "$DIR" -name "*.json" | sort | sed "s|$DIR/||"
        echo ""

    done
done

# ---------------------------------------------------------------------------
# Latency benchmark
# ---------------------------------------------------------------------------
echo "##########################################################"
echo "## LATENCY BENCHMARK  (${#SLEEP_US_VALUES[@]} durations: ${SLEEP_US_VALUES[*]}us)"
echo "##########################################################"
echo ""

for TRAFFIC in "${TRAFFIC_PATTERNS[@]}"; do
    for SLEEP_US in "${SLEEP_US_VALUES[@]}"; do

        DIR="$OUTPUT_BASE/latency/${TRAFFIC,,}-latency_${SLEEP_US}us"
        mkdir -p "$DIR"

        echo "========================================================="
        echo " Traffic : $TRAFFIC  |  Sleep: ${SLEEP_US}us"
        echo " Output  : $DIR"
        echo "========================================================="

        for RUN in $(seq 2 $((NUM_RUNS+1))); do
            RUN_DIR="$DIR/run${RUN}"
            mkdir -p "$RUN_DIR"

            run_one "$LAT_BINARY" static  "$SLEEP_US" "$RUN_DIR/static.json"  \
                    "$TRAFFIC" 1 1 "static  run ${RUN}/${NUM_RUNS}  (latency ${SLEEP_US}us ${TRAFFIC})"
            run_one "$LAT_BINARY" dynamic "$SLEEP_US" "$RUN_DIR/dynamic.json" \
                    "$TRAFFIC" "$MIN_THREADS" "$TASKS_PER_THREAD" \
                    "dynamic run ${RUN}/${NUM_RUNS}  (latency ${SLEEP_US}us ${TRAFFIC})"
        done

        echo ""
        echo "  Files written:"
        find "$DIR" -name "*.json" | sort | sed "s|$DIR/||"
        echo ""

    done
done

echo "========================================================="
echo "All ${total} runs complete."
echo "Results written to: $OUTPUT_BASE/"
echo "========================================================="
