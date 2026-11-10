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
// The caller never waits for a worker that is asleep or descheduled: it waits
// only for the chunks to finish and for workers already inside the job to
// leave, so a busy machine (two sims in single player) degrades to the caller
// doing the work itself rather than to a stall.

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
    // The widest simulation: the calling thread plus kMaxWorkers workers. Nothing
    // in the pool or its users is sized by a constant (per-slot scratch is sized
    // from workers(), see forChunks); 64 is the widest the determinism tests run
    // (tools/simpool_test.cpp, simpool_passes_test.cpp), so nothing wider is offered.
    static constexpr unsigned kMaxThreads=64,kMaxWorkers=kMaxThreads-1;
    // The CPU threads this process may use, at least 1: its affinity mask on
    // Linux (taskset, containers), the machine's thread count elsewhere.
    static unsigned usableCpus();
    // How many threads a simulation gets. Scheduling only; pure, so
    // tools/simpool_test.cpp pins both rules.
    //
    // The game: a SIMULATION THREADS choice (Options > GAME, Settings::simThreads)
    // is 0 = AUTO, every CPU thread, or a count from 1 up to twice the CPU threads
    // (above them is more threads than the computer has; Options draws that red).
    // kMaxChoice is the most any machine offers: two simulations of kMaxThreads.
    static constexpr unsigned kMaxChoice=2*kMaxThreads;
    static constexpr unsigned maxChoice(unsigned cpus) { return 2*std::min(std::max(cpus,1u),kMaxThreads); }
    // T, the threads a choice configures on a machine of `cpus` threads: a count
    // above maxChoice(cpus) (a profile from a bigger machine) is used as that.
    static constexpr unsigned configured(int choice,unsigned cpus) {
        return std::min(choice<=0 ? std::max(cpus,1u) : unsigned(choice),maxChoice(cpus));
    }
    // What a game session's simulations get of T. A game with its own local server
    // (single player, a LOCAL SERVER the player hosts, the Benchmark, --play-map)
    // shares T with it: ceil(T / 2) for the game's simulation and floor(T / 2) for
    // the server's (passed as takserver --sim-threads), each at least 1. Any other
    // session (a game on someone else's server, a replay) has the one simulation,
    // which gets T; `server` is then 0. No simulation gets more than kMaxThreads.
    struct Session { unsigned total,game,server; };
    static constexpr Session sessionThreads(int choice,unsigned cpus,bool localServer) {
        const unsigned t=configured(choice,cpus);
        if (!localServer) return {t,std::min(t,kMaxThreads),0};
        return {t,std::min(std::max((t+1)/2,1u),kMaxThreads),std::min(std::max(t/2,1u),kMaxThreads)};
    }
    // takserver: each running game's share of the server's total S (--sim-threads;
    // auto is usableCpus()): floor(S / running games), but never fewer than 2 --
    // than 1 when S is 1, so whoever asked for one thread gets one -- and never
    // more than kMaxThreads. The floor means a busy server can run more simulation
    // threads than it has CPUs.
    static constexpr unsigned serverShare(unsigned total,size_t running) {
        total=std::max(total,1u);
        const unsigned share=unsigned(total/std::max<size_t>(running,1));
        return std::min(std::max(share,std::min(2u,total)),kMaxThreads);
    }
    // Workers per sim when nothing chooses a width: clamp(usableCpus() / 8, 0, 3).
    // The library default, for the tools and tests that build a World and set no
    // width; the game and takserver always set one (World::setSimWorkers, from the
    // rules above). Debug builds honour TAK_SIM_WORKERS here (debugWorkers). A
    // World asks once and keeps the answer (World::simPool): this reads the CPU
    // mask anew on every call.
    static unsigned defaultWorkers();
    // Debug builds: TAK_SIM_WORKERS (0..kMaxWorkers workers beside the ticking
    // thread), else -1; always -1 in a release build. It overrides AUTO, and only
    // AUTO, wherever a width is decided: here, the game's latch and takserver.
    static int debugWorkers();

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
        if (threads_.empty() || chunks<2 || count<minParallel || async_) {
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

    // Asynchronous jobs (Legion 10,000 stage 9): start() opens a job of `chunks` chunks that the workers begin at
    // once and returns without waiting; the caller goes on with its own work, may run unclaimed chunks itself
    // (help(), e.g. while it waits for one chunk's result), and must call finish() -- which runs what is left and
    // waits for every chunk and every worker to leave -- before any other job on this pool. While one is open,
    // forChunks runs inline (busy()). fn(chunkIndex, slot) must stay valid until finish() returns, and each chunk
    // must write only its own slots. With no workers nothing runs until help()/finish(), on the caller.
    template<class Fn>
    void start(size_t chunks,Fn& fn) {
        open(chunks,[](void* p,size_t k,unsigned slot) {(*static_cast<Fn*>(p))(k,slot);},&fn);
    }
    bool help();
    void finish();
    bool busy() const { return async_; }

private:
    using Job=void(*)(void*,size_t,unsigned);
    void open(size_t chunks,Job job,void* ctx);
    bool async_=false;
    void run(size_t chunks,Job job,void* ctx);
    void work(unsigned slot);
    void drain(unsigned slot);

    std::vector<std::thread> threads_;
    // The current job; written by the caller while gen_ is even (closed), read
    // by a worker only after it has registered in inflight_ and seen gen_ odd.
    Job job_=nullptr;
    void* ctx_=nullptr;
    size_t chunks_=0;
    std::atomic<size_t> next_{0};       // next unclaimed chunk
    std::atomic<size_t> done_{0};       // chunks finished
    std::atomic<unsigned> inflight_{0}; // workers inside the current job
    std::atomic<uint64_t> gen_{0};      // odd: a job is open; even: closed
    std::atomic<bool> stop_{false};
    std::mutex sleepMutex_;
    std::condition_variable sleepCv_;
    unsigned sleeping_=0;               // under sleepMutex_
};

} // namespace tak::sim
