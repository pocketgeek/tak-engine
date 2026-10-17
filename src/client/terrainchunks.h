#pragma once
#include <SDL.h>
#include "terrain/terrain.h"
#include <atomic>
#include <memory>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace tak {
// Camera-independent terrain images. The worker owns mip generation/composition;
// all SDL operations and cache bookkeeping stay on the render thread.
class TerrainChunks {
public:
    static constexpr int span=1024;
    struct Stats {
        size_t bytes=0,images=0,uploads=0,visible=0,ready=0;uint64_t revision=0;size_t pending=0;
        // Render telemetry (cumulative since construction; the reader takes deltas).
        // Jobs: queued -> completed by the worker -> used (uploaded) or discarded;
        // cancelled = pending jobs cancelled by clear()/invalidate().
        uint64_t jobsQueued=0,jobsCompleted=0,jobsCancelled=0,jobsDiscarded=0,jobsUsed=0;
        uint64_t uploadBytes=0,uploadNs=0,composeNs=0;
        size_t mipBytes=0;   // worker-owned mip images (CPU), current
        // Workstream J: the mip cache's CPU budget. Pinned = held by the running job.
        size_t mipPinnedBytes=0,mipPeakBytes=0;
        uint64_t mipEvictions=0,mipRegens=0,mipTrims=0;   // cumulative
    };
    explicit TerrainChunks(terrain::Compositor& compositor);
    ~TerrainChunks();
    void clear();
    // Block coordinates, exclusive upper bounds. Includes filtering gutters.
    void invalidate(int bx0,int by0,int bx1,int by1);
    // Render thread: stop reading the old compositor before its images change.
    void resetSource();
    // nowMs: the owner's clock (wall time, or the render study's presentation clock)
    // for the mip cache's idle trim.
    void prepare(SDL_Renderer*,const tnt::Map&,float x,float y,float zoom,int w,int h,bool linear,uint64_t nowMs);
    // CPU budget of the worker's mip images (workstream J): above `high` the least
    // recently used images no running job holds are evicted down to `low`; after
    // `idleMs` (and 120 frames) without terrain work the worker drops them all.
    static constexpr size_t kMipHigh=size_t(64)<<20,kMipLow=size_t(56)<<20;
    static constexpr uint64_t kMipIdleMs=30000;
    void setMipBudget(size_t high,size_t low,uint64_t idleMs);
    bool draw(SDL_Renderer*,float x,float y,float zoom);
    // Did the last draw() cover this block, at the wanted level or a cached stand-in?
    bool covers(int blockX,int blockY) const;
    // Evict chunks not drawn this frame, oldest first, until gpuvram::wouldFit(need)
    // (need 0 = just get under the cap). Returns the bytes freed.
    size_t shed(size_t need);
    Stats stats() const {
        size_t ready=0;for(const auto& key:visible_)ready+=cache_.count(key);
        Stats s{bytes_,cache_.size(),uploads_,visible_.size(),ready,revision_,pending_.size()};
        s.jobsQueued=jobsQueued_;s.jobsCancelled=jobsCancelled_;s.jobsDiscarded=jobsDiscarded_;
        s.jobsUsed=jobsUsed_;s.uploadBytes=uploadBytes_;s.uploadNs=uploadNs_;
        s.jobsCompleted=jobsCompleted_.load(std::memory_order_relaxed);
        s.composeNs=composeNs_.load(std::memory_order_relaxed);
        s.mipBytes=mipBytes_.load(std::memory_order_relaxed);
        s.mipPinnedBytes=mipPinnedBytes_.load(std::memory_order_relaxed);
        s.mipPeakBytes=mipPeakBytes_.load(std::memory_order_relaxed);
        s.mipEvictions=mipEvictions_.load(std::memory_order_relaxed);
        s.mipRegens=mipRegens_.load(std::memory_order_relaxed);
        s.mipTrims=mipTrims_.load(std::memory_order_relaxed);
        return s;
    }
private:
    using Key=std::tuple<int,int,int>; // level, chunk x, chunk y
    struct Tile {uint32_t key;uint8_t col,row;};
    struct Job {
        Key key;uint64_t epoch;bool stockTerrain=false;
        std::shared_ptr<std::atomic_bool> cancelled;
        int mapW,mapH,bx,by,bw,bh,w,h;
        std::vector<Tile> tiles;
        std::vector<uint8_t> pixels;
    };
    struct Entry {SDL_Texture* texture=nullptr;int w=0,h=0;uint64_t used=0;};
    void work();
    void compose(Job&);
    // A pinned image of section `key` at mip `level` (0 = the compositor's decoded
    // section). The compose job holds the pins of every image it reads until it ends,
    // so neither cache can drop an input under it (workstream J).
    terrain::SectionRef mip(uint32_t key,int level,bool stockTerrain);
    // Worker only: evict least recently used, unpinned mips down to `target`.
    void evictMips(size_t target);
    terrain::Compositor& compositor_;
    // Worker only (all fields): the mip cache. A mip is pinned while a SectionRef to it
    // exists outside this map -- only the running job and mip()'s recursion hold one,
    // both on the worker, so use_count() is exact here.
    using MipId=std::tuple<uint32_t,int,bool>;
    struct Mip {terrain::SectionRef image;size_t bytes=0;uint64_t lastUse=0;};
    std::map<MipId,Mip> mips_;
    std::set<MipId> mipsEvicted_;   // evicted/trimmed at least once (regeneration count)
    uint64_t mipClock_=0;
    std::atomic<size_t> mipHigh_{kMipHigh},mipLow_{kMipLow};
    uint64_t mipIdleMs_=kMipIdleMs;   // render thread
    bool trimMips_=false;             // guarded by mutex_: the render thread asks, the worker trims
    uint64_t lastWorkMs_=0,lastWorkFrame_=0;size_t lastJobsSeen_=0;   // render thread
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> jobs_,done_;
    bool stop_=false,busy_=false;
    uint64_t epoch_=0,frame_=0,revision_=0;
    size_t bytes_=0,uploads_=0;
    // Telemetry: render-thread counters, plus worker-written atomics.
    uint64_t jobsQueued_=0,jobsCancelled_=0,jobsDiscarded_=0,jobsUsed_=0,uploadBytes_=0,uploadNs_=0;
    std::atomic<uint64_t> jobsCompleted_{0},composeNs_{0};
    std::atomic<size_t> mipBytes_{0},mipPinnedBytes_{0},mipPeakBytes_{0};
    std::atomic<uint64_t> mipEvictions_{0},mipRegens_{0},mipTrims_{0};
    std::map<Key,std::shared_ptr<std::atomic_bool>> pending_;
    std::map<Key,Entry> cache_;
    std::vector<Key> visible_;
    std::vector<std::pair<int,int>> drawn_;   // chunk cells the last draw() covered (any level), sorted
    int level_=0;
    bool linear_=false;
};
}
