#pragma once
#include <SDL.h>
#include "terrain/terrain.h"
#include <atomic>
#include <memory>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
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
    };
    explicit TerrainChunks(terrain::Compositor& compositor);
    ~TerrainChunks();
    void clear();
    // Block coordinates, exclusive upper bounds. Includes filtering gutters.
    void invalidate(int bx0,int by0,int bx1,int by1);
    // Render thread: stop reading the old compositor before its images change.
    void resetSource();
    void prepare(SDL_Renderer*,const tnt::Map&,float x,float y,float zoom,int w,int h,bool linear);
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
    const jpeg::Image& mip(uint32_t key,int level,bool stockTerrain);
    terrain::Compositor& compositor_;
    std::map<std::tuple<uint32_t,int,bool>,jpeg::Image> mips_; // worker only
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
    std::atomic<size_t> mipBytes_{0};
    std::map<Key,std::shared_ptr<std::atomic_bool>> pending_;
    std::map<Key,Entry> cache_;
    std::vector<Key> visible_;
    std::vector<std::pair<int,int>> drawn_;   // chunk cells the last draw() covered (any level), sorted
    int level_=0;
    bool linear_=false;
};
}
