// SimPool: every chunk runs exactly once with the fixed boundaries at any
// width, per-chunk results merged in chunk order equal the serial run, and the
// pool survives many back-to-back jobs (the per-tick pattern) at every width a
// player or a server operator can choose (SIMULATION THREADS, --sim-threads): 0
// workers, each count the default rule gives (1, 2, 3), a 24-thread machine's
// most (23) and the code's most (SimPool::kMaxWorkers, 63), with 5, 7, 15 and
// 31 between; asynchronous jobs (start/help/finish) run every chunk exactly
// once. At each width every slot of the pool runs, one thread to a slot (what
// per-slot scratch relies on). And the two rules that decide a simulation's
// width: the game's SIMULATION THREADS (AUTO, the split with a local server)
// and takserver's share per running game.
#include "sim/simpool.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <vector>

using tak::sim::SimPool;

static int failures=0;
static void check(bool ok,const char* what,unsigned workers) {
    if (!ok) { std::fprintf(stderr,"FAIL (workers=%u): %s\n",workers,what);++failures; }
}

static uint64_t mix(uint64_t h,uint64_t v) { return (h^v)*0x100000001b3ull; }

int main() {
    check(SimPool::usableCpus()>=1,"usable CPUs",0);
    check(SimPool::kMaxWorkers==63 && SimPool::defaultWorkers()<=SimPool::kMaxWorkers,"the widest pool",0);
    check(SimPool::debugWorkers()<0 || unsigned(SimPool::debugWorkers())==SimPool::defaultWorkers(),
          "TAK_SIM_WORKERS is the default when set",0);
    // The game's rule (SimPool::sessionThreads), on a machine of N threads: T is N at
    // AUTO, a count up to 2N as itself, one above as 2N. A game with its own local
    // server takes ceil(T / 2) and gives the server floor(T / 2), each at least 1;
    // any other game takes T. No simulation runs more than kMaxThreads.
    auto session=[](int choice,unsigned cpus,bool local,unsigned total,unsigned game,unsigned server) {
        const SimPool::Session s=SimPool::sessionThreads(choice,cpus,local);
        return s.total==total && s.game==game && s.server==server;
    };
    check(session(0,24,true,24,12,12) && session(0,24,false,24,24,0) && session(-3,24,true,24,12,12),"AUTO is N",24);
    check(session(1,24,true,1,1,1) && session(1,24,false,1,1,0),"T = 1: each at least 1",24);
    check(session(2,24,true,2,1,1) && session(2,24,false,2,2,0),"T = 2",24);
    check(session(5,24,true,5,3,2) && session(5,24,false,5,5,0),"T = 5: the game takes the odd one",24);
    check(session(8,24,true,8,4,4) && session(8,24,false,8,8,0),"T = 8",24);
    check(session(48,24,true,48,24,24) && session(48,24,false,48,48,0) && SimPool::maxChoice(24)==48,"T = 2N",24);
    check(session(49,24,true,48,24,24) && session(100000,24,false,48,48,0),"above 2N is 2N",24);
    check(session(0,1,true,1,1,1) && session(0,1,false,1,1,0) && session(3,1,true,2,1,1) && SimPool::maxChoice(1)==2,
          "a one-thread machine",1);
    check(session(0,5,true,5,3,2) && session(0,5,false,5,5,0),"AUTO on an odd count",5);
    check(session(128,64,true,128,64,64) && session(128,64,false,128,64,0) && session(0,128,true,128,64,64) &&
          session(0,128,false,128,64,0) && SimPool::maxChoice(128)==SimPool::kMaxChoice && SimPool::kMaxChoice==128,
          "no simulation above kMaxThreads",64);
    // takserver's rule (SimPool::serverShare): floor(S / running games), at least 2
    // (1 when S is 1), at most kMaxThreads.
    check(SimPool::serverShare(24,1)==24 && SimPool::serverShare(24,2)==12 && SimPool::serverShare(24,3)==8 &&
          SimPool::serverShare(24,5)==4 && SimPool::serverShare(24,0)==24,"S over the running games, rounded down",24);
    check(SimPool::serverShare(24,12)==2 && SimPool::serverShare(24,13)==2 && SimPool::serverShare(24,1000)==2 &&
          SimPool::serverShare(3,2)==2 && SimPool::serverShare(2,2)==2,"many games: never fewer than 2",24);
    check(SimPool::serverShare(1,1)==1 && SimPool::serverShare(1,2)==1 && SimPool::serverShare(1,1000)==1 &&
          SimPool::serverShare(0,3)==1,"S = 1 gives 1",1);
    check(SimPool::serverShare(128,1)==64 && SimPool::serverShare(128,2)==64 && SimPool::serverShare(128,3)==42,
          "no game above kMaxThreads",128);
    uint64_t reference=0;
    for (unsigned workers:{0u,1u,2u,3u,5u,7u,15u,23u,31u,SimPool::kMaxWorkers}) {
        SimPool pool(workers);
        check(pool.workers()==workers,"worker count",workers);
        for (size_t count:{size_t(0),size_t(1),size_t(255),size_t(256),size_t(257),size_t(5000),size_t(20011)}) {
            const size_t chunks=(count+SimPool::kUnitChunk-1)/SimPool::kUnitChunk;
            std::vector<uint32_t> hits(count,0);
            std::vector<uint64_t> partial(chunks,0);
            std::vector<uint8_t> boundsOk(chunks,0);
            pool.forChunks(count,SimPool::kUnitChunk,0,[&](size_t k,size_t b,size_t e,unsigned slot) {
                boundsOk[k]=b==k*SimPool::kUnitChunk && e==std::min(count,b+SimPool::kUnitChunk) && slot<=workers;
                uint64_t h=0xcbf29ce484222325ull;
                for (size_t i=b;i<e;++i) { ++hits[i];h=mix(h,i*2654435761u); }
                partial[k]=h;
            });
            bool once=true;for (auto n:hits) once&=n==1;
            check(once,"every index exactly once",workers);
            bool bounds=true;for (auto ok:boundsOk) bounds&=ok!=0;
            check(bounds,"fixed chunk boundaries",workers);
            uint64_t merged=0xcbf29ce484222325ull;
            for (auto p:partial) merged=mix(merged,p);   // ordered merge
            if (count==20011) {
                if (workers==0) reference=merged;
                check(merged==reference,"ordered merge equals serial",workers);
            }
        }
        // Many small jobs in a row, as one tick's phases dispatch them.
        std::vector<uint32_t> sum(4096,0);
        for (int rep=0;rep<2000;++rep)
            pool.forChunks(sum.size(),SimPool::kUnitChunk,0,[&](size_t,size_t b,size_t e,unsigned) {
                for (size_t i=b;i<e;++i) ++sum[i];
            });
        bool all=true;for (auto v:sum) all&=v==2000;
        check(all,"repeated jobs",workers);
        // Below minParallel the caller runs every chunk itself, in order.
        std::vector<size_t> order;
        pool.forChunks(1000,SimPool::kUnitChunk,1500,[&](size_t k,size_t,size_t,unsigned slot) {
            order.push_back(k);check(slot==0,"inline below minParallel",workers);
        });
        check(order==std::vector<size_t>{0,1,2,3},"inline chunk order",workers);
        // slot identifies the executing thread: scratch kept per slot (World's
        // aura masks and exploration lists, sized workers()+1) is one thread's
        // at a time. A job of workers+1 chunks, each of which waits until all
        // of them have started, ends only with every thread of the pool inside
        // it at once -- so every slot 0..workers is in use, and none twice.
        for (int rep=0;rep<8 && workers;++rep) {
            const size_t n=size_t(workers)+1;
            std::vector<std::atomic<uint32_t>> inSlot(n);
            std::atomic<size_t> started{0};
            std::atomic<bool> shared{false},stuck{false};
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            pool.forChunks(n,1,0,[&](size_t,size_t,size_t,unsigned slot) {
                if (slot>=n || inSlot[slot].fetch_add(1)!=0) shared=true;
                started.fetch_add(1);
                while (started.load()<n && !stuck.load()) {
                    if (std::chrono::steady_clock::now()>until) stuck=true;
                    std::this_thread::yield();
                }
                if (slot<n) inSlot[slot].fetch_sub(1);
            });
            check(!stuck,"every slot of the pool runs",workers);
            check(!shared,"one thread to a slot",workers);
        }
        // Asynchronous jobs (Legion stage 9): the caller keeps working while the workers run the chunks, may help,
        // waits for one chunk by helping, and finish() leaves every chunk run exactly once; while a job is open
        // forChunks runs inline on the caller; and many start/finish rounds back to back.
        for (int rep=0;rep<500;++rep) {
            const size_t n=size_t(1+rep%37);
            std::vector<std::atomic<uint32_t>> runs(n);
            auto fn=[&](size_t k,unsigned slot) {
                runs[k].fetch_add(1,std::memory_order_relaxed);
                if (slot>workers) check(false,"async slot in range",workers);
            };
            pool.start(n,fn);
            check(pool.busy(),"busy while open",workers);
            if (rep%3==0) {
                std::vector<size_t> inl;
                pool.forChunks(1000,SimPool::kUnitChunk,0,[&](size_t k,size_t,size_t,unsigned slot) {
                    inl.push_back(k);check(slot==0,"forChunks inline while an async job is open",workers);
                });
                check(inl==std::vector<size_t>{0,1,2,3},"inline order while open",workers);
            }
            if (rep%2==0) while (pool.help()) {}
            pool.finish();
            check(!pool.busy(),"not busy after finish",workers);
            bool once=true;for (auto& r:runs) once&=r.load()==1;
            check(once,"async: every chunk exactly once",workers);
        }
    }
    if (failures) return 1;
    std::puts("simpool ok");
    return 0;
}
