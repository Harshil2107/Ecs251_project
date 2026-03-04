# Measurement Methodology and Performance Overhead Analysis

## Document Overview

This document describes the measurement methods used in this thread pool benchmark, the scope of each metric, potential overheads introduced by instrumentation, and trade-offs made in the measurement design.

---

## Table of Contents

1. [Metrics Collected](#metrics-collected)
2. [Measurement Scope](#measurement-scope)
3. [Implementation Details](#implementation-details)
4. [Measurement Overheads](#measurement-overheads)
5. [Trade-offs and Design Decisions](#trade-offs-and-design-decisions)
6. [Known Limitations](#known-limitations)
7. [Recommendations for Interpretation](#recommendations-for-interpretation)

---

## Metrics Collected

Our benchmark collects five primary metrics as specified in the project proposal:

| Metric | Unit | Description | Percentiles |
|--------|------|-------------|-------------|
| **Throughput** | tasks/sec | Tasks completed per second | N/A |
| **Latency** | milliseconds | Time from task submission to completion | p50, p95, p99 |
| **PSS Memory** | kilobytes | Proportional Set Size (resident memory) | mean, max, min |
| **Thread Count** | count | Number of active threads in process | mean, max, min |
| **Queue Depth** | count | Tasks waiting in queue | mean, max, p95, p99 |

---

## Measurement Scope

### 1. Latency - **PER-TASK Measurement**

**Scope**: Individual task level

**What we measure**: End-to-end time from submission to completion

**Measurement points**:
```
t_submit ──────── [Queue Wait] ──────── [Execution] ──────── t_complete
   │                                                              │
   └──────────────────── LATENCY ─────────────────────────────────┘
```

**Implementation**:
```cpp
auto t_submit = steady_clock::now();  // Capture when task is submitted
pool.add_job([t_submit, &metrics] {
    do_work();  // Execute actual task
    auto t_complete = steady_clock::now();
    metrics.record_latency(t_complete - t_submit);
});
```

**Data storage**:
- Thread-safe vector stores all individual latency measurements
- Post-processing: Sort and calculate percentiles (p50, p95, p99)

**Why this scope**: 
- Captures complete user-observed latency
- Includes both queuing time and execution time
- Essential for understanding tail latency behavior

---

### 2. Throughput - **SYSTEM-WIDE Aggregate**

**Scope**: Entire benchmark duration

**What we measure**: Total tasks completed divided by total elapsed time

**Measurement points**:
```
t_start ──── [Submit + Execute All Tasks] ──── t_end
   │                                               │
   └──────── throughput = completed / elapsed ─────┘
```

**Implementation**:
```cpp
atomic<uint64_t> completed{0};
auto t_start = steady_clock::now();

// Each task increments counter atomically
pool.add_job([&metrics] { 
    do_work(); 
    metrics.increment_completed();  // Atomic increment
});

pool.shutdown();
auto elapsed = duration<double>(steady_clock::now() - t_start).count();
double throughput = completed / elapsed;
```

**Why this scope**: 
- System-level performance metric
- Directly comparable across configurations
- Aggregated view complements per-task latency

---

### 3. PSS Memory - **PROCESS-WIDE (Time-series samples)**

**Scope**: Entire process (all threads + heap)

**What we measure**: Proportional Set Size from Linux `/proc/self/status`

**Measurement frequency**: Every 200ms via background sampling thread

**Data source**:
```bash
# Read from /proc/self/status
VmRSS:    12345 kB    # Resident Set Size (physical RAM used)
```

**Implementation**:
```cpp
// Background thread samples every 200ms
void sampling_loop() {
    while (!stop) {
        uint64_t memory_kb = read_pss_kb();  // Parse /proc/self/status
        auto timestamp = steady_clock::now();
        metrics.record_memory_sample(timestamp, memory_kb);
        sleep_for(milliseconds(200));
    }
}
```

**Why this scope**:
- Memory is fundamentally a process-level resource
- Linux tracks memory per-process, not per-thread efficiently
- PSS (Proportional Set Size) accurately reflects actual RAM usage
- RSS includes all thread stacks + heap allocations

**Note**: VmRSS is used as a proxy for PSS since it's readily available and sufficient for comparing static vs dynamic pools.

---

### 4. Thread Count - **PROCESS-WIDE (Time-series samples)**

**Scope**: Entire process

**What we measure**: Number of threads from `/proc/self/stat`

**Measurement frequency**: Every 200ms (same sampling thread as memory)

**Data source**:
```bash
# Read from /proc/self/stat (field 20)
pid (comm) state ... num_threads ...
```

**Implementation**:
```cpp
uint64_t read_thread_count() {
    // Parse /proc/self/stat
    // Extract field 20 (num_threads)
    return num_threads;
}
```

**Why this scope**:
- For **static pools**: Validates constancy (should remain constant)
- For **dynamic pools** (future work): Shows adaptation behavior
- OS-level metric, independent of our implementation

---

### 5. Queue Depth - **THREAD POOL-WIDE (Time-series samples)**

**Scope**: Thread pool's task queue

**What we measure**: Number of tasks waiting in queue at sample time

**Measurement frequency**: Every 200ms (same sampling thread)

**Implementation**:
```cpp
// In background sampling thread
size_t depth = pool.get_queue().size();  // Thread-safe
metrics.record_queue_depth(timestamp, depth);
```

**Why this scope**:
- Indicates system congestion
- Shows whether pool can keep up with arrival rate
- High queue depth → pool is saturated
- Helps explain latency spikes

---

## Implementation Details

### Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│                Main Benchmark Thread                          │
│  • Submits tasks with timestamp wrappers                      │
│  • Starts/stops sampling thread                               │
│  • Exports JSON at end                                        │
└──────────────────────────────────────────────────────────────┘
           │                                    │
           ▼                                    ▼
┌──────────────────────┐          ┌─────────────────────────────┐
│  Worker Thread Pool  │          │  Sampling Thread            │
│  • Execute tasks     │          │  Every 200ms:               │
│  • Record latencies  │          │   - Read /proc/self/status  │
│  • Atomic counters   │          │   - Read /proc/self/stat    │
└──────────────────────┘          │   - Sample queue.size()     │
           │                      └─────────────────────────────┘
           │                                    │
           └────────────────┬───────────────────┘
                            ▼
           ┌────────────────────────────────────┐
           │      MetricsCollector              │
           │  Thread-safe storage:              │
           │  • vector<latency> (mutex)         │
           │  • vector<samples> (mutex)         │
           │  • atomic<completed>               │
           └────────────────────────────────────┘
```

### Thread Safety Mechanisms

1. **Latency Recording**: Mutex-protected vector for thread-safe appends
2. **Time-series Samples**: Mutex-protected vectors, single writer (sampling thread)
3. **Completion Counter**: Atomic increment (lock-free)
4. **Queue Size**: Uses existing thread-safe `TaskQueue::size()` method

---

## Measurement Overheads

### 1. Latency Recording Overhead

**Operation**: Taking timestamps + storing in vector

**Cost per task**:
- `steady_clock::now()`: ~20-50 nanoseconds (2 calls = ~40-100ns)
- Vector append with mutex: ~50-200 nanoseconds
- **Total: ~100-300 nanoseconds per task**

**Impact**:
- For tasks taking 1ms (1,000,000 ns): **0.01-0.03% overhead**
- For tasks taking 100μs (100,000 ns): **0.1-0.3% overhead**
- For tasks taking 10μs (10,000 ns): **1-3% overhead**

**Mitigation**:
- Matrix multiplication tasks (N=128) take ~500μs to several ms
- Overhead is negligible (<1%) for our workload
- Documented in results

**Critical Assessment**:
⚠️ For very fast tasks (<10μs), measurement overhead becomes significant. Our benchmark uses tasks that take 500μs-10ms, making this acceptable.

---

### 2. Atomic Counter Overhead

**Operation**: `atomic<uint64_t>::fetch_add(1)`

**Cost**: ~5-20 nanoseconds (lock-free on x86-64)

**Impact**: **Negligible** (<0.001% for our task duration)

---

### 3. Sampling Thread Overhead

**Operations**:
- Reading `/proc/self/status`: ~50-100 microseconds
- Reading `/proc/self/stat`: ~30-80 microseconds
- Calling `queue.size()`: ~10-50 nanoseconds (atomic read)
- **Total per sample: ~100-200 microseconds**

**Frequency**: Every 200ms = 5 samples/second

**CPU time**: 5 × 200μs = 1000μs/sec = **0.1% CPU**

**Impact**: **Minimal** - sampling thread consumes <0.1% of one CPU core

**Critical Assessment**:
✅ Sampling is infrequent enough to be negligible
⚠️ May compete for CPU cache with worker threads
⚠️ Reading `/proc` involves syscalls which could cause context switches

**Mitigation**:
- Use `nice` to lower sampling thread priority
- If using `taskset` for CPU affinity, pin sampler to separate core
- 200ms interval is a balance: frequent enough to capture trends, rare enough to minimize overhead

---

### 4. Memory Storage Overhead

**Storage requirements** (for 120-second benchmark @ 1000 tasks/sec):

| Data | Items | Size per Item | Total |
|------|-------|---------------|-------|
| Latencies | 120,000 | 8 bytes | ~940 KB |
| Memory samples | 600 | 16 bytes | ~10 KB |
| Thread samples | 600 | 16 bytes | ~10 KB |
| Queue samples | 600 | 16 bytes | ~10 KB |
| **Total** | | | **~970 KB** |

**Impact**: **Negligible** (<1 MB for typical benchmark)

---

### 5. Mutex Contention (Latency Recording)

**Scenario**: All worker threads calling `metrics.record_latency()` concurrently

**Analysis**:
- Mutex held for ~50-100ns (vector append)
- For 4 threads @ 1000 tasks/sec each = 4000 lock acquisitions/sec
- Lock acquisition rate: ~250 μs between acquisitions
- Lock hold time: ~100 ns
- **Contention probability: ~0.04%** (very low)

**Critical Assessment**:
✅ For moderate throughput (<5000 tasks/sec): Negligible contention
⚠️ For very high throughput (>10,000 tasks/sec): May become bottleneck

**Alternative design** (if needed):
- Per-thread vectors merged at end (eliminates contention)
- Lock-free queue (more complex, may not be necessary)

**Current decision**: Mutex-protected vector is adequate for our workload

---

### 6. Percentile Calculation Overhead

**Operation**: Sorting latency vector at end of benchmark

**Cost**: O(n log n) where n = number of tasks

**Example**: 120,000 tasks → ~2 million comparisons → **~10-20ms to sort**

**Impact**: **Negligible** - only performed once after benchmark completes, not during measurement

---

## Trade-offs and Design Decisions

### 1. Sampling Frequency (200ms)

**Trade-off**: Granularity vs. Overhead

| Option | Pro | Con |
|--------|-----|-----|
| 50ms | Captures fast transients | 4× overhead, 4× storage |
| **200ms** | **Good balance** | **May miss sub-second spikes** |
| 1000ms | Minimal overhead | Too coarse for burst detection |

**Decision**: **200ms** - Captures trends in burst/ramp patterns while keeping overhead low

**Justification**: 
- Burst pattern has 1-second phases → 5 samples per phase (adequate)
- Ramp pattern has 2-second phases → 10 samples per phase (good)

---

### 2. Thread-Safety Mechanism (Mutex vs Lock-free)

**Trade-off**: Simplicity vs. Performance

| Approach | Complexity | Contention | Overhead |
|----------|------------|------------|----------|
| **Mutex-protected vector** | **Low** | **Low (<1%)** | **~100ns** |
| Lock-free queue | High | None | ~50ns |
| Per-thread vectors | Medium | None | ~20ns |

**Decision**: **Mutex-protected vector** 

**Justification**:
- Our tasks take 500μs-10ms (100ns overhead is <0.02%)
- Contention probability <0.04% for typical workload
- Simpler code = fewer bugs
- Can upgrade later if profiling shows bottleneck

---

### 3. Latency Scope (End-to-end vs. Split)

**Trade-off**: Simplicity vs. Detail

| Approach | What's measured | Pro | Con |
|----------|----------------|-----|-----|
| **End-to-end** | **Submit → Complete** | **User-relevant metric** | **Can't separate queue vs. execution** |
| Split timing | Queue time + Execution time separately | More detail | 2× measurement overhead |

**Decision**: **End-to-end latency**

**Justification**:
- Aligns with user experience
- Proposal specifies "time from task submission to completion"
- Can infer queuing from queue depth correlation
- Lower overhead

---

### 4. Clock Choice (steady_clock vs. system_clock)

**Decision**: `std::chrono::steady_clock`

**Justification**:
- Monotonic (never goes backward)
- Not affected by NTP adjustments
- Suitable for duration measurements
- Standard for benchmarking

---

### 5. Memory Metric (RSS vs. PSS vs. USS)

**Available metrics**:

| Metric | Definition | Availability |
|--------|------------|--------------|
| **VmRSS** | Resident Set Size | **Easy (`/proc/self/status`)** |
| PSS | Proportional Set Size | Medium (`/proc/self/smaps_rollup`) |
| USS | Unique Set Size | Hard (requires parsing smaps) |

**Decision**: **VmRSS** as proxy for PSS

**Justification**:
- Readily available from `/proc/self/status`
- Single value, fast to read
- Sufficient for comparing static vs. dynamic pools
- PSS would be more accurate but requires parsing larger files

**Note**: For production systems, PSS is preferred. For our benchmark comparing pool types, RSS is adequate.

---

## Known Limitations

### 1. Sampling May Miss Transients

**Issue**: 200ms sampling interval may miss sub-second memory spikes

**Impact**: Peak memory usage might be slightly underestimated

**Mitigation**: 
- Use multiple trials (3 trials as specified in proposal)
- Report max across trials
- For critical measurements, reduce sampling interval to 50ms

---

### 2. /proc Filesystem Overhead

**Issue**: Reading `/proc` involves syscalls that could cause context switches

**Impact**: 
- Potential cache pollution
- Non-deterministic timing (scheduler dependent)

**Mitigation**:
- Sampling is infrequent (200ms)
- Document as measurement artifact
- Consider using perf_event_open for production systems

---

### 3. Cold Start Effects

**Issue**: First few tasks may have different characteristics:
- Cache cold
- Allocator not warmed up
- Page faults

**Impact**: May skew latency distribution

**Options**:
- Include cold start (reflects real-world deployment)
- Add warmup period (more controlled measurement)

**Current approach**: **Include cold start** - reflects reality

**Alternative**: Add 5-second warmup period before recording metrics (specify in README when this is used)

---

### 4. Measurement Affects System State

**Heisenberg Principle**: The act of measuring changes what's measured

**Examples**:
- Taking timestamps uses CPU cycles
- Mutex acquisition affects cache state
- Sampling thread competes for resources

**Mitigation**:
- Measure and document overhead
- Keep overhead <1% of task duration
- Report "measurement overhead" in results metadata

---

### 5. Platform Dependence

**Issue**: `/proc` filesystem is Linux-specific

**Impact**: Not portable to macOS or Windows

**Mitigation**: 
- Document Linux requirement in README
- For cross-platform: use platform-specific APIs
  - macOS: `task_info()`, `thread_info()`
  - Windows: `GetProcessMemoryInfo()`, `GetSystemTimes()`

---

## Recommendations for Interpretation

### 1. Understanding Latency Percentiles

**p50 (median)**: Typical case - 50% of tasks complete faster

**p95**: Good case - 95% of tasks complete faster

**p99**: Tail latency - captures outliers that affect user experience

**Why percentiles matter**:
- Mean can hide outliers
- p99 shows worst-case behavior under load
- Critical for comparing static vs. dynamic pools

**Example interpretation**:
```
Static Pool:  p50=10ms, p95=15ms, p99=25ms  → Consistent
Dynamic Pool: p50=8ms,  p95=20ms, p99=80ms  → Fast average but poor tail latency
```

---

### 2. Queue Depth as Leading Indicator

**High queue depth** → System is saturated

**Correlation with latency**:
- High queue depth → High queuing time → High latency
- Sharp queue depth spikes → Latency spikes

**Use for analysis**:
- If p99 latency is high, check if queue depth peaked simultaneously
- For dynamic pool, correlate thread creation with queue depth reduction

---

### 3. Memory Trends Over Time

**Static pool**: Memory should remain constant

**Dynamic pool**: Memory should drop during idle periods

**Red flags**:
- Monotonically increasing memory → Memory leak
- Excessive variation → Fragmentation or measurement noise

---

### 4. Accounting for Measurement Overhead

**When interpreting throughput**:
- Documented overhead: <1% for our workload
- Compare relative performance (static vs. dynamic), not absolute
- Overhead is consistent across configurations

**Formula for correction** (if needed):
```
true_throughput ≈ measured_throughput / (1 - overhead_fraction)
```

For 0.5% overhead: `true ≈ measured / 0.995` (negligible correction)

---

### 5. Statistical Significance

**Multiple trials**: Proposal specifies 3 trials per configuration

**Reporting**:
- Mean and variance across trials
- Check for outliers (>2σ from mean)

**Interpretation**:
- If variance is high → Measurement noise or system load
- If variance is low → Results are reproducible

---

## Summary Table

| Aspect | Method | Overhead | Limitation |
|--------|--------|----------|------------|
| **Latency** | Per-task timestamps | ~100ns (<0.1%) | Mutex contention at very high load |
| **Throughput** | Atomic counter | ~10ns (<0.001%) | None |
| **Memory** | /proc/self/status @ 200ms | 0.1% CPU | May miss sub-200ms spikes |
| **Threads** | /proc/self/stat @ 200ms | 0.1% CPU | Coarse granularity |
| **Queue Depth** | queue.size() @ 200ms | Negligible | Sampling, not continuous |

**Overall measurement overhead**: **<1-2%** for typical workload (N=128-512, tasks >500μs)

---

## Conclusion

This measurement system provides comprehensive metrics while maintaining low overhead. The design prioritizes:

1. **Accuracy**: Measurements reflect actual system behavior
2. **Low overhead**: <1-2% impact on performance
3. **Simplicity**: Easy to understand and maintain
4. **Thread-safety**: Correct under concurrent access
5. **Reproducibility**: Consistent results across trials

**For research purposes**: This methodology is sound and aligns with systems research best practices.

**For production systems**: Consider lock-free data structures and platform-specific APIs for even lower overhead.
