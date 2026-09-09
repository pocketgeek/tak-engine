#pragma once
// Cross-platform process CPU + memory sampling for the benchmark stats screen. A Sample
// holds a process's CUMULATIVE CPU time (seconds) and resident memory (bytes); the caller
// diffs two samples over a wall-clock interval to get CPU%. Works for THIS process (pid 0)
// or another same-user process by pid (the local takserver via its child pid) -- Linux via
// /proc, macOS via libproc's proc_pid_rusage, Windows via GetProcessTimes/PSAPI.
#include <cstddef>
#include <cstdint>

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

}  // namespace tak::proc
