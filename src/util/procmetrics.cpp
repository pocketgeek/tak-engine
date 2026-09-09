#include "util/procmetrics.h"

#include <thread>

#if defined(_WIN32)
#  include <windows.h>
#  include <psapi.h>
#elif defined(__APPLE__)
#  include <libproc.h>
#  include <unistd.h>
#  include <sys/resource.h>
#else   // Linux / other /proc systems
#  include <cstdio>
#  include <cstring>
#  include <unistd.h>
#endif

namespace tak::proc {

int numCpus() {
    unsigned n = std::thread::hardware_concurrency();
    return n ? int(n) : 1;
}

#if defined(_WIN32)

long selfPid() { return long(GetCurrentProcessId()); }

Sample sample(long pid) {
    Sample s;
    HANDLE h = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid))
                   : GetCurrentProcess();
    if (!h) return s;
    FILETIME ct, et, kt, ut;
    if (GetProcessTimes(h, &ct, &et, &kt, &ut)) {
        auto v = [](const FILETIME& f) {
            return (uint64_t(f.dwHighDateTime) << 32) | uint64_t(f.dwLowDateTime);  // 100ns units
        };
        s.cpuSeconds = double(v(kt) + v(ut)) / 1e7;   // 100ns -> seconds
        s.ok = true;
    }
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(h, &pmc, sizeof pmc)) s.rssBytes = size_t(pmc.WorkingSetSize);
    if (pid) CloseHandle(h);
    return s;
}

#elif defined(__APPLE__)

long selfPid() { return long(getpid()); }

Sample sample(long pid) {
    Sample s;
    int p = int(pid ? pid : getpid());
    rusage_info_v2 ri{};
    // proc_pid_rusage works for a same-user process without task_for_pid privileges.
    if (proc_pid_rusage(p, RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&ri)) == 0) {
        s.cpuSeconds = double(ri.ri_user_time + ri.ri_system_time) / 1e9;   // ns -> seconds
        s.rssBytes = size_t(ri.ri_resident_size);
        s.ok = true;
    }
    return s;
}

#else   // Linux

long selfPid() { return long(getpid()); }

Sample sample(long pid) {
    Sample s;
    long p = pid ? pid : long(getpid());
    char path[64];

    // CPU: /proc/<p>/stat, fields 14 (utime) + 15 (stime) in clock ticks. comm (field 2)
    // can contain spaces/parens, so parse from AFTER the last ')'.
    std::snprintf(path, sizeof path, "/proc/%ld/stat", p);
    if (FILE* f = std::fopen(path, "r")) {
        char buf[4096];
        size_t n = std::fread(buf, 1, sizeof buf - 1, f);
        std::fclose(f);
        buf[n] = 0;
        const char* rp = std::strrchr(buf, ')');
        if (rp) {
            // After ')' the fields are: state(3) ppid(4) ... utime(14) stime(15).
            // Skip 11 space-separated fields (3..13), then read utime + stime.
            const char* q = rp + 1;
            int field = 3;
            unsigned long long utime = 0, stime = 0;
            while (*q && field <= 15) {
                while (*q == ' ') ++q;
                char* end = nullptr;
                unsigned long long val = std::strtoull(q, &end, 10);
                if (end == q) break;
                if (field == 14) utime = val;
                else if (field == 15) stime = val;
                q = end;
                ++field;
            }
            long hz = sysconf(_SC_CLK_TCK);
            if (hz > 0) { s.cpuSeconds = double(utime + stime) / double(hz); s.ok = true; }
        }
    }

    // RSS: /proc/<p>/status "VmRSS:  N kB".
    std::snprintf(path, sizeof path, "/proc/%ld/status", p);
    if (FILE* f = std::fopen(path, "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            if (std::strncmp(line, "VmRSS:", 6) == 0) {
                unsigned long long kb = std::strtoull(line + 6, nullptr, 10);
                s.rssBytes = size_t(kb) * 1024;
                break;
            }
        }
        std::fclose(f);
    }
    return s;
}

#endif

}  // namespace tak::proc
