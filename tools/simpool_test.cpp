// SimPool: every chunk runs exactly once with the fixed boundaries at any
// width, per-chunk results merged in chunk order equal the serial run, and the
// pool survives many back-to-back jobs (the per-tick pattern) at every width a
// player or a server operator can choose (SIMULATION THREADS, --sim-threads): 0
// workers, each count the default rule gives (1, 2, 3), a 24-thread machine's
// most (23) and the code's most (SimPool::kMaxWorkers, 63), with 5, 7, 15 and
// 31 between; asynchronous jobs (start/help/finish) run every chunk exactly
// once. At each width every slot of the pool runs, one thread to a slot (what
// per-slot scratch relies on). And a chosen thread count maps to a worker count
// as documented.
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
    // A choice of threads (0 = AUTO) as the threads it runs on and the workers
    // a World is given: 1 is serial, a count above this machine's is its most.
    const unsigned most=SimPool::maxThreads();
    check(SimPool::usableCpus()>=1 && most>=1 && most<=SimPool::kMaxThreads && most<=SimPool::usableCpus(),
          "usable CPUs and the most threads",0);
    check(SimPool::kMaxWorkers==63 && SimPool::defaultWorkers()<=SimPool::kMaxWorkers,"the widest pool",0);
    check(SimPool::threadsFor(0)==1+SimPool::defaultWorkers() && SimPool::workersFor(0)==-1,"AUTO",0);
    check(SimPool::threadsFor(-3)==SimPool::threadsFor(0) && SimPool::workersFor(-3)==-1,"below AUTO reads as AUTO",0);
    check(SimPool::threadsFor(1)==1 && SimPool::workersFor(1)==0,"one thread is the serial simulation",0);
    for (unsigned n=1;n<=most;++n)
        check(SimPool::threadsFor(int(n))==n && SimPool::workersFor(int(n))==int(n)-1,"n threads: n - 1 workers",n);
    check(SimPool::threadsFor(int(most)+1)==most && SimPool::threadsFor(100000)==most &&
          SimPool::workersFor(100000)==int(most)-1,"more threads than this machine has: its most",0);
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
