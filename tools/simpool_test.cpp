// SimPool: every chunk runs exactly once with the fixed boundaries at any
// width, per-chunk results merged in chunk order equal the serial run, and the
// pool survives many back-to-back jobs (the per-tick pattern) at 0/1/3/7 workers.
#include "sim/simpool.h"

#include <cstdio>
#include <cstdint>
#include <vector>

using tak::sim::SimPool;

static int failures=0;
static void check(bool ok,const char* what,unsigned workers) {
    if (!ok) { std::fprintf(stderr,"FAIL (workers=%u): %s\n",workers,what);++failures; }
}

static uint64_t mix(uint64_t h,uint64_t v) { return (h^v)*0x100000001b3ull; }

int main() {
    uint64_t reference=0;
    for (unsigned workers:{0u,1u,3u,7u}) {
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
    }
    if (failures) return 1;
    std::puts("simpool ok");
    return 0;
}
