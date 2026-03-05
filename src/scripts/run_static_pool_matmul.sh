#!/usr/bin/env bash
# Run the static thread pool benchmark with 10 threads for matrix sizes
# 128x128, 256x256, 512x512, and 768x768.  Results are written to OUTPUT_DIR/<NxN>/.
#
# Usage:
#   ./src/scripts/run_static_pool_matmul.sh [output_dir] [traffic=all|Steady|Burst|Ramp]
#
# Examples:
#   ./src/scripts/run_static_pool_matmul.sh results/static_10t
#   ./src/scripts/run_static_pool_matmul.sh results/static_10t Steady
#   ./src/scripts/run_static_pool_matmul.sh results/static_10t Burst

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
THREADS=10
MATRIX_SIZES=(128 256 512 768)
OUTPUT_DIR="${1:-results/static2_${THREADS}t}"   # override via first argument
TRAFFIC="${2:-all}"              # override via second argument: all|Steady|Burst|Ramp
CORES="0-9"                      # pin to cores 0-9 (10 cores)

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC_DIR="$REPO_ROOT/src"
BINARY="$SRC_DIR/arrival_memory_benchmark"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo "=== Building arrival_memory_benchmark ==="
g++ -std=c++17 -pthread -O2 -Wall -Wextra \
    -I"$REPO_ROOT/queue" \
    -o "$BINARY" \
    "$SRC_DIR/arrival_memory_benchmark.cpp"
echo "Binary: $BINARY"
echo ""

# ---------------------------------------------------------------------------
# Run for each matrix size
# ---------------------------------------------------------------------------
for N in "${MATRIX_SIZES[@]}"; do
    RUN_DIR="$REPO_ROOT/$OUTPUT_DIR/${N}x${N}"
    mkdir -p "$RUN_DIR"

    echo "========================================================="
    echo " Threads : $THREADS"
    echo " Matrix  : ${N}x${N}"
    echo " Cores   : $CORES"
    echo " Traffic : $TRAFFIC"
    echo " Output  : $RUN_DIR"
    echo "========================================================="

    taskset -c "$CORES" "$BINARY" "$THREADS" "$N" "$RUN_DIR" static 1 1 "$TRAFFIC"

    echo ""
    echo "Results for ${N}x${N}:"
    ls -lh "$RUN_DIR"/results_*.json
    echo ""
done

echo "All runs complete."
echo "Results written to: $REPO_ROOT/$OUTPUT_DIR/"
