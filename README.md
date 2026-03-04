# ECS 251 Project: Thread Pool with Traffic Pattern Benchmarking

## Project Overview

This project implements and benchmarks a **static thread pool** with a thread-safe task queue under various traffic arrival patterns. It includes memory-intensive matrix multiplication tasks to simulate realistic workload scenarios and evaluate performance under different traffic conditions.

## Project Structure

```
Ecs251_project/
├── README.md                          # This file
├── MEASUREMENT_METHODOLOGY.md         # Detailed metrics methodology and overhead analysis
├── proposal/                          # LaTeX proposal files
│   ├── main.tex
│   ├── references.bib
│   ├── acmart.cls
│   └── ACM-Reference-Format.bst
├── queue/                            # Thread-safe task queue
│   ├── task_queue.h                  # FIFO queue with mutex + condition variable
│   ├── test_queue.cpp                # Comprehensive unit tests
│   └── scripts/
│       └── run_queue_test.sh         # Script to build and test queue
└── src/                              # Main source code
    ├── arrival_memory_benchmark.cpp   # Main benchmark program
    ├── scripts/
    │   └── build_arrival_memory_benchmark.sh  # Build script
    ├── metrics/
    │   └── metrics_collector.h       # Comprehensive metrics collection system
    ├── static_thread_pool/
    │   ├── static_thread_pool.h      # Thread pool implementation
    │   └── static_thread_pool.cpp    # Example/demo usage
    ├── tasks/
    │   └── matrix_multiply.h         # Memory-intensive task (N×N matrix multiply)
    └── traffic/
        └── arrival_process.h         # Traffic pattern generators

```

## Components

### 1. Task Queue (`queue/task_queue.h`)
Thread-safe FIFO task queue with the following features:
- **Blocking pop()**: Waits for tasks using condition variables
- **Non-blocking try_pop()**: Returns immediately if empty
- **Shutdown mechanism**: Gracefully stops accepting new tasks
- **Thread-safe operations**: Protected by mutex

### 2. Static Thread Pool (`src/static_thread_pool/`)
Fixed-size thread pool that:
- Creates a specified number of worker threads at initialization
- Workers continuously pull tasks from the shared queue
- Supports graceful shutdown and joining of all threads
- Uses the TaskQueue for coordination

### 3. Matrix Multiplication Task (`src/tasks/matrix_multiply.h`)
Memory-intensive benchmark task:
- Computes C = A × B for N×N matrices
- Thread-safe: each invocation allocates its own matrices
- Configurable size: N = 128/256/512 for Light/Medium/Heavy workloads
- Uses cache-friendly ikj loop ordering

### 4. Traffic Patterns (`src/traffic/arrival_process.h`)
Three arrival patterns for realistic workload simulation:
- **STEADY**: Constant task arrival rate
- **BURST**: Alternating high-rate bursts and idle periods
- **RAMP**: Oscillating between high and low rates

### 5. Benchmark (`src/arrival_memory_benchmark.cpp`)
Main experiment that combines:
- Static thread pool with configurable thread count
- Matrix multiplication tasks
- Various traffic patterns
- Comprehensive performance metrics (see below)
- JSON export for detailed analysis

### 6. Metrics Collector (`src/metrics/metrics_collector.h`)
Comprehensive metrics collection system that tracks:
- **Latency**: Per-task timing (p50, p95, p99 percentiles)
- **Throughput**: Tasks completed per second
- **Memory Usage**: PSS memory sampled every 200ms
- **Thread Count**: Active threads sampled over time
- **Queue Depth**: Task queue backlog sampled over time
- Exports all metrics to JSON for analysis

## Prerequisites

- **C++ Compiler**: g++ with C++17 support
- **Operating System**: Linux (or any POSIX-compatible system)
- **Libraries**: pthread (POSIX threads)

## Steps to Run the Experiment

### Step 1: Test the Task Queue

Before running the main benchmark, verify that the task queue works correctly:

```bash
cd queue
./scripts/run_queue_test.sh
```

**Expected Output**: All tests should pass, showing:
- Basic push/pop operations
- FIFO ordering
- Thread-safety with multiple producers/consumers
- Shutdown behavior
- Blocking and non-blocking operations

### Step 2: Build the Benchmark

Navigate to the project root and build the main benchmark:

```bash
cd /users/hp2107/Ecs251_project
./src/scripts/build_arrival_memory_benchmark.sh
```

**What it does**:
- Compiles `arrival_memory_benchmark.cpp` with C++17 and optimizations (-O2)
- Links pthread library for multithreading
- Includes the queue header files
- Creates executable at `src/arrival_memory_benchmark`

### Step 3: Run the Benchmark

Execute the compiled benchmark:

```bash
./src/arrival_memory_benchmark
```

**Or build and run in one command**:

```bash
./src/scripts/build_arrival_memory_benchmark.sh --run
```

### Step 4: Interpret the Results

The benchmark will output console summary and detailed JSON files:

**Console Output**:
```
=== Steady ===
  submitted : 250
  completed : 250
  elapsed   : 5.02 s
  throughput: 49.80 tasks/s
  results saved to: results_Steady.json
```

**JSON Output Files**: `results_Steady.json`, `results_Burst.json`, `results_Ramp.json`

Each JSON file contains:

```json
{
  "config": {
    "experiment_name": "Steady_Static_4threads_N128",
    "pool_type": "static",
    "threads": 4,
    "matrix_size": 128,
    "traffic_pattern": "Steady",
    "traffic_rate": 50.0,
    "duration_sec": 5.0
  },
  "results": {
    "tasks_submitted": 250,
    "tasks_completed": 250,
    "elapsed_sec": 5.01,
    "throughput_tasks_per_sec": 49.94,
    "latency_ms": {
      "p50": 15.2,
      "p95": 45.8,
      "p99": 89.3,
      "mean": 18.5,
      "std_dev": 12.1
    },
    "memory_kb": {
      "mean": 12548,
      "max": 15200,
      "min": 11000
    },
    "thread_count": {
      "mean": 6.0,
      "max": 6,
      "min": 6
    },
    "queue_depth": {
      "mean": 2.3,
      "max": 15,
      "p95": 8,
      "p99": 12
    }
  },
  "time_series": {
    "memory_samples": [...],
    "thread_count_samples": [...],
    "queue_depth_samples": [...]
  }
}
```

**Metrics Explained**:
- **Throughput**: Tasks completed per second (system-wide)
- **Latency**: Per-task time from submission to completion
  - `p50` (median): 50% of tasks complete faster
  - `p95`: 95% of tasks complete faster (good-case latency)
  - `p99`: 99% of tasks complete faster (tail latency)
- **Memory**: PSS (Proportional Set Size) in kilobytes
- **Thread Count**: Total threads in process (workers + main + sampling)
- **Queue Depth**: Number of tasks waiting in queue
- **Time Series**: 200ms-sampled data for plotting trends

For detailed measurement methodology and overhead analysis, see [MEASUREMENT_METHODOLOGY.md](MEASUREMENT_METHODOLOGY.md).

## Running Tests

### Quick Test (5-9 seconds)

For rapid iteration and debugging, use the quick test configuration:

**Edit** [src/arrival_memory_benchmark.cpp](src/arrival_memory_benchmark.cpp):
```cpp
const int THREADS = 4;
const int N = 3;  // Small matrix for fast execution

// Quick test patterns (comment out production config)
run_traffic_pattern("Steady", THREADS, N, steady(/*rate=*/50, /*total=*/5));
run_traffic_pattern("Burst",  THREADS, N, burst(/*rate=*/200, /*burst=*/1, /*idle=*/2, /*total=*/9));
run_traffic_pattern("Ramp",   THREADS, N, ramp(/*high=*/100, /*low=*/20, /*phase=*/2, /*total=*/8));
```

Then rebuild and run:
```bash
./src/scripts/build_arrival_memory_benchmark.sh --run
```

**Expected runtime**: ~25 seconds total (5s + 9s + 8s + overhead)

---

### Production Benchmark (120 seconds per pattern)

For the full experiment as specified in the proposal:

**Edit** [src/arrival_memory_benchmark.cpp](src/arrival_memory_benchmark.cpp):
```cpp
const int THREADS = 10;   // 10 worker threads
const int N = 128;        // Light workload (use 256 or 512 for medium/heavy)

// Production benchmark: 120-second runs
run_traffic_pattern("Steady", THREADS, N, steady(/*rate=*/1000, /*total=*/120));
run_traffic_pattern("Burst",  THREADS, N, burst(/*rate=*/2000, /*burst=*/2, /*idle=*/2, /*total=*/120));
run_traffic_pattern("Ramp",   THREADS, N, ramp(/*high=*/1500, /*low=*/500, /*phase=*/10, /*total=*/120));
```

**Traffic patterns explained**:
- **Steady**: 1000 tasks/sec continuously for 120 seconds = ~120,000 tasks
- **Burst**: 2000 tasks/sec for 2 seconds, then idle for 2 seconds, repeated for 120 seconds total = ~60,000 tasks
- **Ramp**: Alternates between 1500 tasks/sec (10s) and 500 tasks/sec (10s) for 120 seconds total = ~120,000 tasks

**Build and run**:
```bash
./src/scripts/build_arrival_memory_benchmark.sh --run
```

**Expected runtime**: ~360 seconds (6 minutes) total + pool shutdown time

⚠️ **Warning**: High task rates with large N can saturate the system. Monitor with `htop` or `top`.

**Expected output summary**:

| Pattern | Tasks Submitted | Duration | Expected Throughput (N=128) |
|---------|----------------|----------|------------------------------|
| Steady  | ~120,000       | 120s     | ~1000 tasks/sec             |
| Burst   | ~60,000        | 120s     | ~500 tasks/sec (avg)        |
| Ramp    | ~120,000       | 120s     | ~1000 tasks/sec (avg)       |

**Notes**:
- With N=3 (quick test), throughput will be much higher (>10,000 tasks/sec)
- With N=256 or 512, pool may become saturated if rate > processing capacity
- Queue depth in JSON will show saturation (high p95/p99 indicates bottleneck)

---

### Running Multiple Trials (As Per Proposal)

The proposal specifies 3 trials per configuration. Run the benchmark 3 times and save results:

```bash
# Trial 1
./src/arrival_memory_benchmark
mv results_Steady.json results_Steady_trial1.json
mv results_Burst.json results_Burst_trial1.json
mv results_Ramp.json results_Ramp_trial1.json

# Trial 2
./src/arrival_memory_benchmark
mv results_Steady.json results_Steady_trial2.json
mv results_Burst.json results_Burst_trial2.json
mv results_Ramp.json results_Ramp_trial2.json

# Trial 3
./src/arrival_memory_benchmark
mv results_Steady.json results_Steady_trial3.json
mv results_Burst.json results_Burst_trial3.json
mv results_Ramp.json results_Ramp_trial3.json
```

Then aggregate the results to compute mean and variance across trials.

---

### CPU Affinity (Optional)

To fix CPU resources as specified in the proposal, use `taskset`:

```bash
# Run on cores 0-9 (10 cores for 10 threads)
taskset -c 0-9 ./src/arrival_memory_benchmark

# Or for 4-core experiment
taskset -c 0-3 ./src/arrival_memory_benchmark
```

This ensures consistent CPU allocation across runs.

---

## Customizing the Experiment

### Adjust Thread Count

**Current configuration**: 10 threads (production benchmark)

Edit [src/arrival_memory_benchmark.cpp](src/arrival_memory_benchmark.cpp):
```cpp
const int THREADS = 10;  // Change to 2, 4, 8, 16, etc.
const int N = 128;       // Keep matrix size constant when comparing thread counts
```

**Quick reference**:
- 4 threads: Typical for quad-core systems
- 10 threads: Good for 8-core systems with hyperthreading
- 16 threads: For high-core-count servers

### Increase Task Workload

Change the matrix size `N` to increase computational intensity:

```cpp
const int THREADS = 10;
const int N = 128;  // Light:128, Medium:256, Heavy:512
```

**Workload characteristics**:
- **N = 3**: Instant execution (~microseconds) - for quick testing only
- **N = 128**: Light workload (~0.5-2ms per task) - good starting point
- **N = 256**: Medium workload (~4-15ms per task) - realistic web service
- **N = 512**: Heavy workload (~30-120ms per task) - computation-intensive

⚠️ **Warning**: Larger N values significantly increase:
- Task execution time (scales as O(N³))
- Memory per task (3 × N² × 8 bytes for matrices)
- Total benchmark duration

### Modify Traffic Patterns

Edit the pattern parameters in [src/arrival_memory_benchmark.cpp](src/arrival_memory_benchmark.cpp):

**Current production config (120 seconds)**:
```cpp
// Steady: 1000 tasks/sec for 120 seconds = ~120,000 tasks
run_traffic_pattern("Steady", THREADS, N, steady(/*rate=*/1000, /*total=*/120));

// Burst: 2000 tasks/sec for 2 sec, idle 2 sec, repeat for 120 sec total = ~60,000 tasks
run_traffic_pattern("Burst", THREADS, N, burst(/*rate=*/2000, /*burst=*/2, /*idle=*/2, /*total=*/120));

// Ramp: alternate between 1500 and 500 tasks/sec, 10 sec phases, 120 sec total = ~120,000 tasks
run_traffic_pattern("Ramp", THREADS, N, ramp(/*high=*/1500, /*low=*/500, /*phase=*/10, /*total=*/120));
```

**Alternative quick test config (5-9 seconds)**:
```cpp
// Steady: 50 tasks/sec for 5 seconds = ~250 tasks
run_traffic_pattern("Steady", THREADS, N, steady(/*rate=*/50, /*total=*/5));

// Burst: 200 tasks/sec for 1 sec, idle 2 sec, repeat for 9 total seconds
run_traffic_pattern("Burst", THREADS, N, burst(/*rate=*/200, /*burst=*/1, /*idle=*/2, /*total=*/9));

// Ramp: alternate between 100 and 20 tasks/sec, 2 sec phases, 8 sec total
run_traffic_pattern("Ramp", THREADS, N, ramp(/*high=*/100, /*low=*/20, /*phase=*/2, /*total=*/8));
```

### Add Custom Traffic Patterns

Use the functions defined in [src/traffic/arrival_process.h](src/traffic/arrival_process.h):

```cpp
// Steady pattern
TrafficConfig cfg1 = steady(rate, total_duration);

// Burst pattern (high rate, then idle, repeat)
TrafficConfig cfg2 = burst(rate, burst_duration, idle_duration, total_duration);

// Ramp pattern (oscillate between high and low rates)
TrafficConfig cfg3 = ramp(rate_high, rate_low, phase_duration, total_duration);
```

## Development and Testing

### Run Queue Tests Only

```bash
cd queue
./scripts/run_queue_test.sh
```

### Build Static Thread Pool Example

```bash
cd src/static_thread_pool
g++ -std=c++17 -pthread -O2 -I../../queue -o static_thread_pool static_thread_pool.cpp
./static_thread_pool 4  # Run with 4 threads
```

### Clean Build Artifacts

```bash
rm -f queue/test_queue
rm -f src/arrival_memory_benchmark
rm -f src/static_thread_pool/static_thread_pool
rm -f results_*.json  # Remove JSON result files
```

## Analyzing Results

### View JSON Results

```bash
# Pretty-print JSON
cat results_Steady.json | python -m json.tool

# Extract specific metrics
cat results_Steady.json | grep -A10 "latency_ms"
cat results_Steady.json | grep -A5 "memory_kb"
```

### Compare Configurations

After running multiple configurations, compare JSON files:

```bash
# Compare throughput
for f in results_*.json; do 
  echo -n "$f: "
  grep "throughput_tasks_per_sec" $f
done

# Compare p99 latency
for f in results_*.json; do 
  echo -n "$f: "
  grep "p99" $f | head -1
done
```

### Plotting Time-Series Data

The JSON includes time-series samples that can be plotted using Python/matplotlib or R:

```python
import json
import matplotlib.pyplot as plt

with open('results_Steady.json') as f:
    data = json.load(f)

# Extract time-series
memory = data['time_series']['memory_samples']
times = [s['time_offset_sec'] for s in memory]
values = [s['value'] for s in memory]

plt.plot(times, values)
plt.xlabel('Time (seconds)')
plt.ylabel('Memory (KB)')
plt.title('Memory Usage Over Time')
plt.show()
```

## Performance Considerations

1. **Thread Count**: Should generally match or slightly exceed CPU core count
2. **Task Granularity**: Matrix size N determines task execution time
   - Too small: high scheduling overhead
   - Too large: poor load balancing
3. **Arrival Rate**: If rate exceeds processing capacity, queue will grow unbounded
4. **Compiler Optimizations**: Use -O2 or -O3 for realistic performance measurements
5. **Measurement Overhead**: Metrics collection adds <1-2% overhead
   - Latency recording: ~100ns per task
   - Background sampling: <0.1% CPU
   - See [MEASUREMENT_METHODOLOGY.md](MEASUREMENT_METHODOLOGY.md) for detailed analysis

## Troubleshooting

**Compilation errors**: Ensure g++ supports C++17:
```bash
g++ --version  # Should be gcc 7.0 or later
```

**Permission denied when running scripts**:
```bash
chmod +x queue/scripts/run_queue_test.sh
chmod +x src/scripts/build_arrival_memory_benchmark.sh
```

**Segmentation fault**: Check thread count and matrix size aren't excessive for your system

**Queue depth growing unbounded**: Pool cannot keep up with arrival rate
- Reduce task arrival rate
- Increase thread count
- Decrease matrix size N
- Check with: `cat results_*.json | grep -A4 "queue_depth"`

**System becomes unresponsive**: Too high load
- Kill process: `pkill arrival_memory`
- Reduce rate or N before rerunning
- Use `taskset -c 0-3` to limit to fewer cores

**Memory usage much higher than expected**: Check for memory leaks
- Monitor with: `ps aux | grep arrival`
- Compare RSS values across trials
- Ensure queue depth returns to 0 after benchmark

## Future Work

- Implement dynamic thread pool with adaptive sizing
- Add more task types (I/O-bound, CPU-bound variants)
- Collect detailed latency distributions
- Compare with other synchronization primitives (lock-free queues, etc.)
- Implement work stealing for better load balancing

## References

See [proposal/references.bib](proposal/references.bib) for academic references and related work.