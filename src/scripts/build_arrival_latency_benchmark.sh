#!/usr/bin/env bash
# Build and optionally run the arrival process + latency-based task benchmark.
#
# Usage:
#   ./src/scripts/build_arrival_latency_benchmark.sh          # build only
#   ./src/scripts/build_arrival_latency_benchmark.sh --run    # build then run

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC_DIR="$REPO_ROOT/src"
OUT="$SRC_DIR/arrival_latency_benchmark"

echo "Compiling arrival_latency_benchmark.cpp..."
g++ -std=c++17 -pthread -O2 -Wall -Wextra \
    -I"$REPO_ROOT/queue" \
    -o "$OUT" \
    "$SRC_DIR/arrival_latency_benchmark.cpp"

echo "Binary: $OUT"

if [[ "${1:-}" == "--run" ]]; then
    echo ""
    echo "Running..."
    echo ""
    "$OUT"
fi