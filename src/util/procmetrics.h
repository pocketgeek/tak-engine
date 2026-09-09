#pragma once
// Cross-platform process CPU + memory sampling for the benchmark stats screen. A Sample
// holds a process's CUMULATIVE CPU time (seconds) and resident memory (bytes); the caller
// diffs two samples over a wall-clock interval to get CPU%. Works for THIS process (pid 0)
// or another same-user process by pid (the local takserver via its child pid) -- Linux via
// /proc, macOS via libproc's proc_pid_rusage, Windows via GetProcessTimes/PSAPI.
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

long selfPid();   // this process's pid
int numCpus();    // online logical CPUs (to report CPU% of one core vs. all cores)

// Best-effort GPU stats (whole device, all processes). Read from `nvidia-smi` if present
// (NVIDIA, any OS), else Linux AMD sysfs (amdgpu). ok=false when no source is available
// (e.g. Intel, macOS) -- callers show "N/A". Cheap-ish but the nvidia-smi path spawns a
// process, so sample it sparingly (e.g. per benchmark milestone, not per frame).
struct GpuSample {
    double utilPct = -1;    // GPU utilization %, -1 if unknown
    size_t memUsed = 0;     // device VRAM used (all processes), bytes; 0 if unknown
    size_t memTotal = 0;    // total device VRAM, bytes; 0 if unknown
    std::string name;       // adapter name if known
    bool ok = false;        // false = no GPU stats source on this system
};
GpuSample gpuSample();

}  // namespace tak::proc
