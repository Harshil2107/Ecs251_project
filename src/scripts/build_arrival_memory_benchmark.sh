#!/usr/bin/env bash
# Build and optionally run the arrival process + memory-based task benchmark.
#
# Usage:
#   ./src/scripts/build_demo.sh          # build only
#   ./src/scripts/build_demo.sh --run    # build then run

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC_DIR="$REPO_ROOT/src"
OUT="$SRC_DIR/arrival_memory_benchmark"

echo "Compiling arrival_memory_benchmark.cpp..."
g++ -std=c++17 -pthread -O2 -Wall -Wextra \
    -I"$REPO_ROOT/queue" \
    -o "$OUT" \
    "$SRC_DIR/arrival_memory_benchmark.cpp"

echo "Binary: $OUT"

if [[ "${1:-}" == "--run" ]]; then
    echo ""
    echo "Running..."
    echo ""
    "$OUT"
fi
