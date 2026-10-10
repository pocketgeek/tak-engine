#pragma once
// SimPool: a persistent fork-join worker pool for the deterministic simulation.
//
// Work is split into FIXED-size chunks of a range (kUnitChunk units by default),
// never into "count / threads" slices, so the chunk boundaries -- and therefore
// any per-chunk result -- do not depend on how many threads run them. The
// calling thread takes part; workers claim chunks from an atomic counter.
//
// Exactness rules for a caller (see WE-parallel P0):
//   - a chunk writes only its own slots (per-unit records, or arenas indexed by
//     the chunk number), or performs an order-independent update (an atomic OR);
//   - any merge of per-chunk results runs after forChunks returns, in chunk order.
// With 0 workers (World::setSerialThreads, or too small a range) the identical
// chunks run inline in ascending order, so serial == workers by construction.
//
// Workers spin briefly for the next job, then sleep on a condition variable, so
// an idle pool costs nothing between ticks and a fork-join costs no thread spawn.

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace tak::sim {

class SimPool {
public:
    static constexpr size_t kUnitChunk=256;
    // Workers per sim: clamp(hardware threads / 8, 0, 3). A single-player game runs
    // two sims (client + local referee), so the cap keeps both within 8 cores.
    // Debug builds honour TAK_SIM_WORKERS (0..15) to test other widths.
    static unsigned defaultWorkers();

    explicit SimPool(unsigned workers);
    ~SimPool();
    SimPool(const SimPool&)=delete;
    SimPool& operator=(const SimPool&)=delete;

    unsigned workers() const { return unsigned(threads_.size()); }

    // Calls fn(chunkIndex, begin, end, slot) once for every chunk
    // [k*chunk, min(count,(k+1)*chunk)) of [0,count). slot identifies the
    // executing thread (0 = the caller, 1..workers()), for per-thread scratch.
    // A range shorter than minParallel, or one chunk, runs inline on the caller.
    // fn must not throw.
    template<class Fn>
    void forChunks(size_t count,size_t chunk,size_t minParallel,Fn&& fn) {
        if (!count) return;
        if (!chunk) chunk=kUnitChunk;
        const size_t chunks=(count+chunk-1)/chunk;
        if (threads_.empty() || chunks<2 || count<minParallel) {
            for (size_t k=0;k<chunks;++k) fn(k,k*chunk,std::min(count,(k+1)*chunk),0u);
            return;
        }
        struct Ctx { Fn* fn;size_t count,chunk; };
        Ctx ctx{&fn,count,chunk};
        run(chunks,[](void* p,size_t k,unsigned slot) {
            auto& c=*static_cast<Ctx*>(p);
            (*c.fn)(k,k*c.chunk,std::min(c.count,(k+1)*c.chunk),slot);
        },&ctx);
    }

private:
    using Job=void(*)(void*,size_t,unsigned);
    void run(size_t chunks,Job job,void* ctx);
    void work(unsigned slot);
    void drain(unsigned slot);

    std::vector<std::thread> threads_;
    // The current job; written by the caller before gen_ is bumped (release),
    // read by workers after they observe the bump (acquire).
    Job job_=nullptr;
    void* ctx_=nullptr;
    size_t chunks_=0;
    std::atomic<size_t> next_{0};       // next unclaimed chunk
    std::atomic<unsigned> pending_{0};  // workers yet to finish the current job
    std::atomic<uint64_t> gen_{0};      // bumped once per job
    std::atomic<bool> stop_{false};
    std::mutex sleepMutex_;
    std::condition_variable sleepCv_;
    unsigned sleeping_=0;               // under sleepMutex_
};

} // namespace tak::sim
