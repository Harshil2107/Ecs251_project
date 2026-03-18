# Thread Pool Benchmark: Static vs. Dynamic Architectures
ECS 251 — UC Davis | CloudLab: 2× Intel Xeon Silver 4114, 192 GB RAM, Ubuntu 24.04

## Code Structure
```
src/
├── static_thread_pool/
│   └── static_thread_pool.h          # fixed N threads, pop() blocks indefinitely
├── dynamic_thread_pool/
│   └── dynamic_thread_pool.h         # spawn on demand, reap after idle timeout
├── tasks/
│   ├── matrix_multiply.h             # N×N matmul (memory-bound)
│   └── latency.h                     # blocking sleep (I/O-bound)
├── traffic/
│   └── arrival_process.h             # steady / burst / ramp generators
├── metrics/
│   └── metrics_collector.h           # latency, memory, thread count sampling
├── scripts/
│   └── run_full_benchmark.sh         # full experiment matrix
├── arrival_memory_benchmark.cpp      # memory benchmark harness
└── arrival_latency_benchmark.cpp     # latency benchmark harness
```

## Concepts → Code

| Project Plan Concept                      | Implementation                                                        |
| :---------------------------------------- | :-------------------------------------------------------------------- |
| Static pool — pre-create N threads        | `static_thread_pool.h` — N=10 threads, never exit                    |
| Dynamic pool — create on demand           | `dynamic_thread_pool.h` — min=2, +1 per 10 queued, 500 ms timeout    |
| Shared FIFO queue (fair comparison)       | `queue/task_queue.h` — mutex + condition variable                     |
| Memory-based tasks, N ∈ {128, 256, 512}  | `tasks/matrix_multiply.h` → `make_matmul_task(N)`                    |
| Latency-based tasks, T ∈ {100 µs–10 ms}  | `tasks/latency.h` → `make_latency_task(sleep_us)`                    |
| Steady / Burst / Ramp traffic patterns    | `traffic/arrival_process.h` → `steady()`, `burst()`, `ramp()`        |
| Throughput, p50 / p95 / p99 latency       | `metrics/metrics_collector.h` — per-task timestamps                  |
| PSS memory, thread count over time        | Background sampler @ 200 ms via `/proc/self/status`, `/proc/self/stat`|
| CPU affinity (10 cores)                   | `taskset -c 0-9` in `run_full_benchmark.sh`                           |

## Build & Run
```bash
# Build
./src/scripts/build_arrival_memory_benchmark.sh
./src/scripts/build_arrival_latency_benchmark.sh

# Run full experiment matrix
./src/scripts/run_full_benchmark.sh results/full_benchmark

# Run single configuration
./src/arrival_memory_benchmark 10 256 ./out dynamic 2 10 Burst
```
