#pragma once
// Cross-platform process CPU + memory sampling for the benchmark stats screen. A Sample
// holds a process's CUMULATIVE CPU time (seconds) and resident memory (bytes); the caller
// diffs two samples over a wall-clock interval to get CPU%. Works for THIS process (pid 0)
// or another same-user process by pid (the local takserver via its child pid) -- Linux via
// /proc, macOS via libproc's proc_pid_rusage, Windows via GetProcessTimes/PSAPI.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace tak::proc {

struct Sample {
    double cpuSeconds = 0;   // cumulative user+system CPU time
    size_t rssBytes = 0;     // resident set size (working set)
    bool ok = false;         // false = couldn't read this process
};

// Sample process `pid` (0 = this process). Returns ok=false if it can't be read.
Sample sample(long pid);

// Cumulative OS CPU counters across all logical cores (units cancel in deltas).
struct SystemCpuSample {
    uint64_t total = 0, idle = 0;
    bool ok = false;
};
SystemCpuSample systemCpuSample();
inline double systemCpuPercent(const SystemCpuSample& previous, const SystemCpuSample& current) {
    if (!previous.ok || !current.ok || current.total <= previous.total || current.idle < previous.idle)
        return -1;
    const auto total = current.total - previous.total, idle = current.idle - previous.idle;
    if (idle > total) return -1;
    return 100.0 * double(total - idle) / double(total);
}

long selfPid();   // this process's pid
int numCpus();    // online logical CPUs (to report CPU% of one core vs. all cores)

// Perf busy and scheduled counters share nanosecond units. Parallel engines
// are evaluated separately; their percentages must never be added together.
inline double gpuBusyPercent(uint64_t oldBusy, uint64_t oldTime, uint64_t busy, uint64_t time) {
    if (busy < oldBusy || time <= oldTime) return -1;
    return std::clamp(100.0 * double(busy-oldBusy) / double(time-oldTime), 0.0, 100.0);
}

// Whole-device GPU statistics only: NVIDIA nvidia-smi, Linux AMD sysfs, or
// macOS IOAccelerator, or Linux Intel i915 perf engine counters (busiest engine).
// Unsupported or permission-blocked drivers report unavailable, never process-only
// utilization. Queries may spawn a process: use sparingly or off the render thread.
struct GpuSample {
    double utilPct = -1;    // GPU utilization %, -1 if unknown
    size_t memUsed = 0;     // device VRAM used (all processes), bytes; 0 if unknown
    size_t memTotal = 0;    // total device VRAM, bytes; 0 if unknown
    std::string name;       // adapter name if known
    bool systemWide = false;// utilPct covers the WHOLE device, not this process
                            // (macOS: IOAccelerator "Device Utilization %" --
                            // Apple has no public per-process GPU stat)
    bool ok = false;        // false = no GPU stats source on this system
};
GpuSample gpuSample();

}  // namespace tak::proc
