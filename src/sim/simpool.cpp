#include "simpool.h"

#include <chrono>
#include <cstdlib>
#if defined(__linux__)
#include <sched.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace tak::sim {

namespace {
inline void cpuRelax() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
}
// How long an idle worker polls for the next job before it sleeps. The jobs of
// one tick come back to back; between ticks the pool sleeps.
constexpr auto kSpin=std::chrono::microseconds(20);
} // namespace

unsigned SimPool::defaultWorkers() {
#ifndef NDEBUG
    if (const char* e=std::getenv("TAK_SIM_WORKERS"))
        return unsigned(std::clamp(std::atoi(e),0,15));
#endif
    unsigned cpus=std::thread::hardware_concurrency();
#if defined(__linux__)
    // The CPUs this process may run on (taskset, containers), not the machine's.
    cpu_set_t set;
    if (sched_getaffinity(0,sizeof(set),&set)==0) cpus=unsigned(CPU_COUNT(&set));
#endif
    return std::min(3u,cpus/8);
}

SimPool::SimPool(unsigned workers) {
    threads_.reserve(workers);
    for (unsigned k=1;k<=workers;++k) threads_.emplace_back([this,k]{work(k);});
}

SimPool::~SimPool() {
    {
        std::lock_guard lock(sleepMutex_);
        stop_.store(true,std::memory_order_release);
    }
    sleepCv_.notify_all();
    for (auto& t:threads_) t.join();
}

void SimPool::drain(unsigned slot) {
    for (;;) {
        const size_t k=next_.fetch_add(1,std::memory_order_relaxed);
        if (k>=chunks_) return;
        job_(ctx_,k,slot);
        done_.fetch_add(1,std::memory_order_release);
    }
}

void SimPool::run(size_t chunks,Job job,void* ctx) {
    // gen_ is even here and no worker is inside a job, so the fields are ours.
    job_=job;ctx_=ctx;chunks_=chunks;
    next_.store(0,std::memory_order_relaxed);
    done_.store(0,std::memory_order_relaxed);
    gen_.fetch_add(1,std::memory_order_seq_cst);   // open
    {
        std::lock_guard lock(sleepMutex_);
        if (sleeping_) sleepCv_.notify_all();
    }
    drain(0);
    auto relax=[](unsigned spins) { if (spins<4096) cpuRelax(); else std::this_thread::yield(); };
    // Every chunk finished (the release in each increment publishes its writes).
    for (unsigned spins=0;done_.load(std::memory_order_acquire)<chunks;++spins) relax(spins);
    // Close, then wait out the workers that joined before the close: a worker
    // registers in inflight_ BEFORE it re-checks that the job is still open, so
    // one that saw it open is counted here, and one that comes later sees it
    // closed and never touches the fields (seq_cst on both sides).
    gen_.fetch_add(1,std::memory_order_seq_cst);
    for (unsigned spins=0;inflight_.load(std::memory_order_seq_cst);++spins) relax(spins);
}

void SimPool::work(unsigned slot) {
    uint64_t seen=0;
    for (;;) {
        bool ready=false;
        const auto until=std::chrono::steady_clock::now()+kSpin;
        for (unsigned spins=0;;++spins) {
            if (gen_.load(std::memory_order_acquire)!=seen) { ready=true;break; }
            if (stop_.load(std::memory_order_acquire)) return;
            if ((spins&63)==63 && std::chrono::steady_clock::now()>=until) break;
            cpuRelax();
        }
        if (!ready) {
            std::unique_lock lock(sleepMutex_);
            ++sleeping_;
            sleepCv_.wait(lock,[&]{
                return stop_.load(std::memory_order_acquire) || gen_.load(std::memory_order_acquire)!=seen;
            });
            --sleeping_;
            if (stop_.load(std::memory_order_acquire)) return;
        }
        const uint64_t g=gen_.load(std::memory_order_acquire);
        seen=g;
        if (!(g&1)) continue;   // the job closed before this worker got here
        inflight_.fetch_add(1,std::memory_order_seq_cst);
        if (gen_.load(std::memory_order_seq_cst)==g) drain(slot);
        inflight_.fetch_sub(1,std::memory_order_seq_cst);
    }
}

} // namespace tak::sim
