#pragma once
#include "flowfield.h"
#include <functional>
#include <deque>

namespace tak::sim::flow {
// Sealed cell-level input. Out-of-map halo cells are zero. The uniform form
// certifies every IN-BOUNDS halo cell; preparation still clips map boundaries.
struct RawTile {
    int width=0,height=0,footX=1,footZ=1,originX=0,originZ=0;
    std::vector<uint16_t> costs;
    std::optional<uint16_t> uniform;
    // World gathering updates this as raw costs are appended, never by a worker.
    uint64_t fingerprint=1469598103934665603ULL;
};
// Pure resumable worker job: no World/sampler references. Fixed cardinal label
// order is identical to TopologyBuilder. The input is owned, never mutated.
class PreparedTileBuilder {
public:
    PreparedTileBuilder(RawTile input,std::shared_ptr<const Tile> previous={});
    size_t step(size_t budget);
    bool done() const {return stage_==Stage::Done;}
    std::shared_ptr<const Tile> finish() const {return done()?(previous_&&!changed_?previous_:result_):nullptr;}
    size_t bytes() const;
    uint64_t checksum() const;
private:
    enum class Stage {Sum,Emit,Label,Done};
    Stage stage_=Stage::Sum;
    const RawTile input_;
    std::shared_ptr<const Tile> previous_;
    std::shared_ptr<Tile> result_;
    std::vector<uint32_t> blocked_;
    std::array<uint16_t,kTileCells> queue_{};
    size_t cursor_=0,head_=0,tail_=0;
    int rawW_=0,rawH_=0;
    bool changed_=false;
    uint64_t fingerprint_=1469598103934665603ULL;
};

// Samples only on the World thread, then prepares at most two independent
// immutable tiles in parallel. Logical work/publication never depend on worker
// completion time. Callbacks are synchronous and must not mutate World state.
class SnapshotBuilder {
public:
    using Sampler=std::function<uint16_t(int,int)>;
    struct Rectangle {int x=0,z=0,w=0,h=0;};
    struct Hooks {
        // Caller certifies profile, exploration, terrain and gate revisions.
        std::function<std::shared_ptr<const Tile>(size_t)> prepared;
        std::function<std::optional<uint16_t>(Rectangle)> uniform;
        std::function<void(size_t,std::shared_ptr<const Tile>)> publish;
    };
    static constexpr size_t maxPreparingTiles=2;
    SnapshotBuilder(int width,int height,int footX,int footZ,Sampler sampler,
                    std::shared_ptr<const Topology> previous={},
                    std::vector<bool> dirty={},Limits limits={},Hooks hooks={},unsigned workers=0);
    size_t step(size_t budget);
    bool done() const {return stage_==Stage::Done;}
    bool failed() const {return stage_==Stage::Failed;}
    std::shared_ptr<const Topology> finish() const {return done()?(previous_&&!anyChanged_?previous_:builder_.finish()):nullptr;}
    size_t bytes() const;
    uint64_t samples() const {return samples_;}
    uint64_t checksum() const;
    static void dirtyRectangle(std::vector<bool>& dirty,int width,int height,int footX,int footZ,
                               int x,int z,int w,int h);
private:
    enum class Stage {Choose,Sample,Topology,Done,Failed};
    Stage stage_=Stage::Choose;
    int width_,height_,footX_,footZ_,tilesX_=0,rawW_=0,rawH_=0;
    Sampler sampler_;
    Hooks hooks_;
    unsigned workers_=0;
    std::shared_ptr<const Topology> previous_;
    std::vector<bool> dirty_;
    TopologyBuilder builder_;
    size_t tile_=0,cursor_=0;
    uint64_t samples_=0,work_=0,dirtyHash_=1469598103934665603ULL,publishedHash_=1469598103934665603ULL;
    bool anyChanged_=false;
    RawTile sampling_;
    struct Pending {
        size_t tile;std::unique_ptr<PreparedTileBuilder> builder;std::optional<uint16_t> uniform;
        std::shared_ptr<const Tile> ready;
        bool done() const {return !builder||builder->done();}
        std::shared_ptr<const Tile> finish() const {return builder?builder->finish():ready;}
    };
    std::shared_ptr<const Tile> uniformTile_;
    std::optional<uint16_t> uniformCost_;
    std::deque<Pending> pending_;
    void enqueue();
    size_t prepare(size_t budget);
};
}
