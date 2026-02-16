#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QUEUE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "Compiling test_queue.cpp..."
# -std=c++17 : use C++17 standard (needed for std::optional)
# -pthread   : enable POSIX threads (needed for std::thread, std::mutex, std::condition_variable)
# -O2        : optimization level 2 for more realistic concurrency testing
# -Wall      : enable all common compiler warnings
# -Wextra    : enable additional warnings beyond -Wall
# -o         : specify the output binary path
g++ -std=c++17 -pthread -O2 -Wall -Wextra \
    -o "$QUEUE_DIR/test_queue" \
    "$QUEUE_DIR/test_queue.cpp"

echo "Running tests..."
"$QUEUE_DIR/test_queue"
