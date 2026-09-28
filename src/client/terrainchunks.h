#pragma once
#include <SDL.h>
#include "terrain/terrain.h"
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>

namespace tak {
// Camera-independent terrain images. The worker owns mip generation/composition;
// all SDL operations and cache bookkeeping stay on the render thread.
class TerrainChunks {
public:
    static constexpr int span=1024;
    struct Stats { size_t bytes=0,images=0,uploads=0,visible=0,ready=0;uint64_t revision=0; };
    explicit TerrainChunks(terrain::Compositor& compositor);
    ~TerrainChunks();
    void clear();
    void prepare(SDL_Renderer*,const tnt::Map&,float x,float y,float zoom,int w,int h,bool linear);
    bool draw(SDL_Renderer*,float x,float y,float zoom);
    bool covers(int blockX,int blockY) const;
    Stats stats() const {
        size_t ready=0;for(const auto& key:visible_)ready+=cache_.count(key);
        return {bytes_,cache_.size(),uploads_,visible_.size(),ready,revision_};
    }
private:
    using Key=std::tuple<int,int,int>; // level, chunk x, chunk y
    struct Tile {uint32_t key;uint8_t col,row;};
    struct Job {
        Key key;uint64_t epoch;
        int mapW,mapH,bx,by,bw,bh,w,h;
        std::vector<Tile> tiles;
        std::vector<uint8_t> pixels;
    };
    struct Entry {SDL_Texture* texture=nullptr;int w=0,h=0;uint64_t used=0;};
    void work();
    void compose(Job&);
    const jpeg::Image& mip(uint32_t key,int level);
    terrain::Compositor& compositor_;
    std::map<std::pair<uint32_t,int>,jpeg::Image> mips_; // worker only
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> jobs_,done_;
    bool stop_=false;
    uint64_t epoch_=0,frame_=0,revision_=0;
    size_t bytes_=0,uploads_=0;
    std::set<Key> pending_;
    std::map<Key,Entry> cache_;
    std::vector<Key> visible_;
    int level_=0;
    bool linear_=false;
};
}
