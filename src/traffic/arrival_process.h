#pragma once

#include <functional>
#include <chrono>
#include <thread>
#include <cstdint>

enum class TrafficPattern { STEADY, BURST, RAMP };

struct TrafficConfig {
    TrafficPattern pattern;
    double rate_high;       // tasks/second  (primary / burst rate)
    double rate_low;        // tasks/second  (low-phase rate; RAMP only)
    double burst_duration;  // seconds — length of each high-rate phase
    double idle_duration;   // seconds — length of each idle phase (BURST only)
    double total_duration;  // seconds — total simulation time
};

inline TrafficConfig steady(double rate,                              double total) {
    return {TrafficPattern::STEADY, rate, 0,    0,         0,         total};
}
inline TrafficConfig burst(double rate, double burst_dur, double idle_dur, double total) {
    return {TrafficPattern::BURST,  rate, 0,    burst_dur, idle_dur,  total};
}
inline TrafficConfig ramp(double rate_high, double rate_low, double phase_dur, double total) {
    return {TrafficPattern::RAMP,   rate_high, rate_low, phase_dur, 0, total};
}

// Submit tasks at a configured arrival rate for total_duration seconds.
// Returns the total number of tasks submitted.
inline uint64_t run_arrival(TrafficConfig cfg, std::function<void()> submit) {
    using Clock = std::chrono::steady_clock;
    using NS    = std::chrono::nanoseconds;

    // Convert seconds (double) to nanoseconds — like datetime.timedelta in Python.
    auto secs = [](double s) { return NS(static_cast<long long>(s * 1e9)); };

    auto deadline = Clock::now() + secs(cfg.total_duration);
    uint64_t total = 0;

    // Submit tasks at `rate` tasks/sec until phase_end or the global deadline.
    // Like: while time.time() < phase_end: submit(); time.sleep(1/rate)
    auto submit_at_rate = [&](double rate, Clock::time_point phase_end) {
        if (rate <= 0.0) return;
        auto interval = secs(1.0 / rate);
        auto next = Clock::now();
        while (Clock::now() < phase_end && Clock::now() < deadline) {
            submit();
            ++total;
            next += interval;
            std::this_thread::sleep_until(next);  // like time.sleep_until(next)
        }
    };

    // Sleep through idle time without submitting anything.
    // Like: time.sleep(seconds) but respects the global deadline.
    auto idle_for = [&](double seconds) {
        auto end = Clock::now() + secs(seconds);
        while (Clock::now() < end && Clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    };

    if (cfg.pattern == TrafficPattern::STEADY) {
        submit_at_rate(cfg.rate_high, deadline);

    } else if (cfg.pattern == TrafficPattern::BURST) {
        while (Clock::now() < deadline) {
            submit_at_rate(cfg.rate_high, Clock::now() + secs(cfg.burst_duration));
            idle_for(cfg.idle_duration);
        }

    } else if (cfg.pattern == TrafficPattern::RAMP) {
        bool high = true;
        while (Clock::now() < deadline) {
            double rate = high ? cfg.rate_high : cfg.rate_low;
            submit_at_rate(rate, Clock::now() + secs(cfg.burst_duration));
            high = !high;
        }
    }

    return total;
}
