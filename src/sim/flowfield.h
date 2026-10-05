#pragma once

// Independent flowfield navigation. This module does not use or alter the retail
// search worker/scheduler. Integer costs and fixed neighbour order are mandatory.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_set>
#include <optional>
#include <compare>

namespace tak::sim::flow {
inline constexpr int kTileSize=64;
inline constexpr int kTileCells=kTileSize*kTileSize;
inline constexpr uint32_t kUnreachable=0xffffffffu;
struct Cell { int x=0,z=0; friend bool operator==(Cell,Cell)=default; };
// Compact controller goal in footprint-centre cells. Circle/ring outer radius
// is squared in cells; ring inner radius is squared in world units (16/cell).
struct GoalRegion {
    enum class Kind:uint8_t {Circle,Rectangle,Ring};
    Kind kind=Kind::Circle;
    int x=0,z=0,maxX=0,maxZ=0;
    int32_t outerSquared=0;
    uint64_t innerWorldSquared=0;
    auto operator<=>(const GoalRegion&) const=default;
    bool contains(Cell c) const;
    std::pair<Cell,Cell> bounds() const;
};
// Zero cost means blocked. Positive costs are traversal weights, not occupancy.
// A snapshot must already account for the movement profile's entire footprint.
struct Tile {
    std::array<uint16_t,kTileCells> cost{};
    std::array<uint16_t,kTileCells> component{}; // 0 blocked; 1..componentCount
    uint16_t componentCount=0,minimumCost=255;
};
struct Limits {
    size_t maxTiles=1024;       // 2048x2048 cells (64x64 map)
    size_t maxComponents=131072;
    size_t maxEdges=1048576;
};
struct Edge { uint32_t from=0,to=0; Cell inside,outside; };
struct Topology {
    int width=0,height=0,tilesX=0,tilesZ=0;
    std::vector<std::shared_ptr<const Tile>> tiles;
    std::vector<uint32_t> offsets; // component IDs: offsets[tile]+local-1
    std::vector<Edge> edges;
    std::vector<uint32_t> edgeOffsets; // outgoing edges grouped by component
    uint32_t components=0;
    int tileAt(Cell c) const;
    uint16_t cost(Cell c) const;
    uint32_t componentAt(Cell c) const;
    size_t bytes() const;
    size_t metadataBytes() const; // excludes shared Tile payload
};
// Incremental snapshot builder. setCost is used only before step(). The World
// adapter must budget sampling separately; workers receive only finished tiles.
// Work is bounded: one step unit scans/labels one cell or checks one border pair.
class TopologyBuilder {
public:
    TopologyBuilder(int width,int height,Limits limits={});
    void setCost(Cell c,uint16_t cost);
    // Reuse an immutable tile after a local edit; omitted tiles start blocked.
    void reuseTile(size_t index,std::shared_ptr<const Tile> tile);
    size_t step(size_t budget);
    bool done() const { return phase_==Phase::Done; }
    bool failed() const { return phase_==Phase::Failed; }
    std::shared_ptr<const Topology> finish() const;
    size_t bytes() const;
    size_t scratchBytes() const; // excludes the output topology and its tiles
    uint64_t checksum() const;
private:
    enum class Phase { Label,Offsets,Edges,Count,Prefix,Allocate,Scatter,Done,Failed };
    Phase phase_=Phase::Label;
    Limits limits_;
    std::shared_ptr<Topology> result_;
    std::vector<std::shared_ptr<Tile>> writable_;
    std::vector<bool> reused_;
    std::vector<Edge> sorted_;
    std::vector<uint32_t> cursor_;
    std::unordered_set<uint64_t> edgePairs_; // lookup only; insertion order defines graph order
    size_t tile_=0,scan_=0,head_=0,tail_=0,border_=0,index_=0;
    std::array<uint16_t,kTileCells> queue_{};
    bool started_=false;
    uint64_t digest_=1469598103934665603ull,work_=0;
    void labelOne();
    void borderOne();
};

// Reverse breadth-first routing on exact within-tile connected components.
// Edges retain the actual crossing cells: a connected tile is NOT assumed to be
// wholly traversable. This prevents mazes/shorelines from creating false routes.
class Destination {
public:
    Destination(std::shared_ptr<const Topology> topology,std::vector<Cell> goals);
    Destination(std::shared_ptr<const Topology> topology,GoalRegion region);
    size_t step(size_t budget);
    bool done() const { return regionSeed_==regionCells_ && head_==queue_.size(); }
    // Exit rule shared by FieldBuilder and exits(): a neighbour one hop closer.
    bool downhill(uint32_t from,uint32_t to) const;
    uint32_t distance(uint32_t component) const;
    bool reachable(Cell c) const;
    // After all region seeds are admitted, discovered BFS distances are final.
    // A tile can integrate once all its components have those final distances;
    // global exhaustion also proves any remaining components unreachable.
    bool tileReady(int tile) const;
    const Topology& topology() const { return *topology_; }
    const std::shared_ptr<const Topology>& topologyPointer() const {return topology_;}
    const std::vector<Cell>& goals() const { return goals_; }
    const std::optional<GoalRegion>& region() const {return region_;}
    bool goalContains(Cell c) const {return region_ && region_->contains(c);}
    size_t bytes() const;
    uint32_t estimate(Cell c) const; // geometric goal-box bias, not reachability
    static uint32_t estimate(Cell c,Cell low,Cell high); // same bias toward an explicit box
    std::pair<Cell,Cell> goalBox() const {return {goalMin_,goalMax_};}
    // Exact exit-seed mask FieldBuilder uses for a ready tile: bit n is border
    // cell n (top, right, bottom, left; 64 each) leading to a component one hop
    // closer. Together with tile content and the bias box it determines the
    // detailed field of any tile holding no goal seed.
    using Exits=std::array<uint64_t,4>;
    Exits exits(int tile) const;
    uint64_t checksum() const;
private:
    Cell goalMin_{},goalMax_{},seedMin_{},seedMax_{};
    std::optional<GoalRegion> region_;
    size_t regionSeed_=0,regionCells_=0;
    std::shared_ptr<const Topology> topology_;
    std::vector<Cell> goals_;
    std::vector<uint32_t> distance_,queue_;
    std::vector<uint16_t> undiscovered_;
    void discover(uint32_t component,uint32_t distance);
    size_t head_=0,edge_=0;
    uint64_t digest_=1469598103934665603ull,work_=0;
};

// One detailed tile. Work stays resumable; a caller can dispatch independent
// builders on bounded workers and join at a fixed simulation boundary. Results
// must not be consumed based on wall-clock readiness.
struct Field {
    int tile=0,originX=0,originZ=0;
    std::array<uint32_t,kTileCells> distance{};
    std::array<uint8_t,kTileCells> direction{}; // 0..7 neighbour, 8 goal, 255 unknown
    bool next(Cell from,Cell& to) const;
    uint64_t hash() const;
};
class FieldBuilder {
public:
    // An anchor replaces the exact goal box in the tie-breaking bias. Only
    // valid for a tile that holds no goal seed; the result then depends on
    // tile content, exits() and the anchor alone and may be shared.
    FieldBuilder(std::shared_ptr<const Destination> destination,int tile,
                 std::optional<std::pair<Cell,Cell>> anchor={});
    // A shared field built from its key alone: exactly these exit seeds and
    // this bias box, no goal and no destination. Equal to the anchored
    // destination form above for any destination reporting the same exits().
    FieldBuilder(std::shared_ptr<const Topology> topology,int tile,Destination::Exits exits,
                 std::pair<Cell,Cell> anchor);
    size_t step(size_t budget);
    bool done() const { return phase_==Phase::Done; }
    const Field& field() const { return result_; }
    const Topology& topology() const {return *topology_;}
    uint64_t checksum() const;
    size_t bytes() const;
private:
    enum class Phase { Seeds,Integrate,Done };
    Phase phase_=Phase::Seeds;
    std::shared_ptr<const Destination> destination_;
    std::shared_ptr<const Topology> topology_;
    std::optional<Destination::Exits> exits_;
    Field result_;
    int originX_=0,originZ_=0;
    uint32_t biasBase_=0;
    Cell seedMin_{},seedMax_{},boxMin_{},boxMax_{};
    size_t goalSeeds_=0;
    size_t scan_=0;
    std::array<int32_t,kTileCells> heapPos_{};
    std::array<uint16_t,kTileCells> heap_{};
    size_t heapSize_=0;
    uint64_t digest_=1469598103934665603ull,work_=0;
    void seedOne();
    void integrateOne();
    void place(int tile,std::pair<Cell,Cell> box);
    void offer(uint16_t cell,uint32_t distance,uint8_t direction);
    bool less(uint16_t a,uint16_t b) const;
    void swapHeap(size_t a,size_t b);
};
} // namespace tak::sim::flow
