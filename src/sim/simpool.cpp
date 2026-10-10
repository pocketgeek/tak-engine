#include "simpool.h"

#include <chrono>
#include <cstdlib>
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
    return std::min(3u,std::thread::hardware_concurrency()/8);
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
    }
}

void SimPool::run(size_t chunks,Job job,void* ctx) {
    job_=job;ctx_=ctx;chunks_=chunks;
    next_.store(0,std::memory_order_relaxed);
    pending_.store(unsigned(threads_.size()),std::memory_order_relaxed);
    gen_.fetch_add(1,std::memory_order_release);
    {
        std::lock_guard lock(sleepMutex_);
        if (sleeping_) sleepCv_.notify_all();
    }
    drain(0);
    // Every worker acknowledges every job, so none can still be reading this
    // job's fields when the next one is written; the release in its decrement
    // publishes its chunk writes to the caller.
    for (unsigned spins=0;pending_.load(std::memory_order_acquire);++spins) {
        if (spins<4096) cpuRelax();
        else std::this_thread::yield();
    }
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
            if (gen_.load(std::memory_order_acquire)==seen) return;   // woken by stop_
        }
        // The caller waits for this worker's acknowledgement before it starts
        // another job, so gen_ is exactly one past `seen` here.
        seen=gen_.load(std::memory_order_acquire);
        drain(slot);
        pending_.fetch_sub(1,std::memory_order_acq_rel);
    }
}

} // namespace tak::sim
