// cpu_topology_test -- the REAL CPU detection (sim/simcpus.h) on the machine this runs on.
// Prints what it found, checks that the counts and the hand-out order agree, and pins the
// calling thread to the first CPU of the order: the call must not fail. Where the system
// pins no thread (macOS) it checks that the detection says so and that a pin is refused.
// The Linux reader's rules are pinned on fixture trees by tools/simpool_test.cpp; this is
// the one place the Windows and macOS code runs (CI's standalone checks).
#include "sim/simcpus.h"

#include <algorithm>
#include <cstdio>

using namespace tak::sim;

static int failures=0;
static void check(bool ok,const char* what) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",what);++failures; }
}

int main() {
    const CpuTopology cpus=detectCpus();
    std::printf("performance cores %u, efficiency cores %u, hyperthreads %u: %u threads; can pin: %s\n",
                cpus.perf,cpus.eff,cpus.smt,cpus.threads(),cpus.canPin ? "yes" : "no");
    std::printf("hand-out order: %s\n",cpuListText(cpus.order).c_str());
    std::printf("AUTO: %u threads; SimPool::usableCpus(): %u\n",SimPool::autoThreads(cpus),SimPool::usableCpus());
    check(cpus.perf>=1 && cpus.threads()>=1,"at least one core");
    check(cpus.order.size()==cpus.threads(),"one entry of the order for every thread");
    std::vector<int> ids=cpus.order;
    std::sort(ids.begin(),ids.end());
    check(std::adjacent_find(ids.begin(),ids.end())==ids.end() && (ids.empty() || ids.front()>=0),
          "the order names each CPU once");
    check(SimPool::autoThreads(cpus)>=1 && SimPool::autoThreads(cpus)<=cpus.threads(),"AUTO is within the computer's threads");
    // Unpinned so far: unpinning is nothing to do, and must not fail either.
    unpinThisThread();
    if (cpus.canPin && !cpus.order.empty()) {
        check(pinThisThread(cpus.order.front()),"pinning this thread to the first CPU of the order");
        check(!pinThisThread(-1),"a CPU that does not exist is refused");
        unpinThisThread();
    } else {
        check(!cpus.canPin,"a system that cannot pin says so");
        check(!pinThisThread(cpus.order.empty() ? 0 : cpus.order.front()),"a pin is refused where the system cannot pin");
    }
    std::printf("%s\n",failures ? "FAILED" : "PASS: this machine's CPUs are detected and a thread pins to the first of them");
    return failures ? 1 : 0;
}
