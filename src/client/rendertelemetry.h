#pragma once

// Shared renderer vocabulary for the rendering optimization program
// (docs/render-optimization-progress.md): per-frame telemetry, generation and
// revision numbers, and stable render handles. Everything here is PRESENTATION
// state. None of it may become an input to the deterministic simulation, enter
// World::stateHash(), or change what a frame draws.
//
// Telemetry contract
//   * FrameStats is owned by the render thread. Workers never write it: they
//     leave per-item results (UnitGeom::reused, vertex counts, ...) that the
//     render thread tallies after the parallel pass. No atomics, no locks.
//   * Recording is off unless a debug harness asks for it (TAK_RENDER_STATS,
//     or the render study). A release build compiles enabled() to false, so
//     every guarded tally and ScopedPhase is a predictable not-taken branch.
//   * Never allocate or format strings in a hot loop. format() runs once per
//     frame into a caller-owned buffer.
//   * Phase times are CPU wall time on the render thread, including any worker
//     wait inside that phase. GPU time is reported separately as the GL draw
//     TIMELINE (GL_TIME_ELAPSED) -- it includes idle gaps waiting for CPU
//     submission and is NOT device busy time or utilization.
//
// Generation / revision vocabulary (use these words, not new ones)
//   * Generation: a monotonically increasing 64-bit number naming a LIFETIME.
//     0 (kNoGeneration) means "never valid". Anything that caches a resource
//     created under a generation stores that generation and treats a mismatch
//     as a miss. Generations defined for the renderer:
//       rendererGeneration -- SDL renderer / render-target lifetime. Bumped by
//                             GameView::invalidateRenderTargets (target or
//                             device reset) and GPU teardown. Every cached
//                             SDL_Texture*, GL buffer or atlas placement is
//                             only meaningful under the generation it was made.
//       assetGeneration    -- model / texture / palette registration lifetime
//                             (override remount, model reload).
//       snapshotGeneration -- the published sim snapshot the render pinned.
//   * Revision: a per-entry counter that increments when that entry's OUTPUT
//     changes. Stages each own one: pose, material (body appearance), shadow
//     coverage, placement (screen position / clipping). A revision is a cheap
//     equality token for downstream caches; it is never derived from the
//     simulation tick alone (presentation inputs change between ticks), and it
//     never replaces exact validation while a scheme is being proven.
//   * RenderHandle: {index, generation} into a bounded slot table. A handle is
//     a stable identity for a cache entry that survives painter-order changes.
//     A reused index always carries a new generation, so a stale handle (a
//     destroyed unit, a reused unit id, a replaced model) can never alias the
//     new occupant.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace tak::render {

using Generation = uint64_t;
inline constexpr Generation kNoGeneration = 0;

// A lifetime counter. Starts at 1 so a default/zero stored generation never
// matches a live one.
class GenerationCounter {
public:
    Generation current() const { return value_; }
    Generation bump() { return ++value_; }
private:
    Generation value_ = 1;
};

struct RenderHandle {
    static constexpr uint32_t kNoIndex = UINT32_MAX;
    uint32_t index = kNoIndex;
    uint32_t generation = 0;
    bool valid() const { return index != kNoIndex && generation != 0; }
    friend bool operator==(const RenderHandle&, const RenderHandle&) = default;
};

// Bounded generational slot allocator. Owns only identities; the caller keeps
// the payload in a parallel vector indexed by handle.index (which is stable:
// indices are recycled, never compacted). Acquire fails (invalid handle) at the
// capacity limit rather than growing without bound.
class HandleSlots {
public:
    explicit HandleSlots(uint32_t capacity) : capacity_(capacity) {}
    RenderHandle acquire() {
        uint32_t index;
        if (!free_.empty()) { index = free_.back(); free_.pop_back(); }
        else if (generations_.size() < capacity_) {
            index = uint32_t(generations_.size());
            generations_.push_back(0);
            live_.push_back(false);
        } else return {};
        // Skip generation 0 on wrap so a recycled slot never looks unallocated.
        if (++generations_[index] == 0) ++generations_[index];
        live_[index] = true;
        ++liveCount_;
        return {index, generations_[index]};
    }
    bool valid(RenderHandle h) const {
        return h.valid() && h.index < generations_.size() && live_[h.index] &&
               generations_[h.index] == h.generation;
    }
    bool release(RenderHandle h) {
        if (!valid(h)) return false;
        live_[h.index] = false;
        free_.push_back(h.index);
        --liveCount_;
        return true;
    }
    uint32_t live() const { return liveCount_; }
    uint32_t allocated() const { return uint32_t(generations_.size()); }
    uint32_t capacity() const { return capacity_; }
private:
    uint32_t capacity_;
    uint32_t liveCount_ = 0;
    std::vector<uint32_t> generations_;
    std::vector<bool> live_;
    std::vector<uint32_t> free_;
};

// ---- Telemetry ---------------------------------------------------------------

// CPU phases, render thread. Keep names stable: tools/render_benchmark.py
// parses them from the RENDERSTATS line (prefix "t_").
enum class Phase : uint8_t {
    Candidates,      // unit/feature candidate selection + fog/frustum culls
    PainterSort,     // painter-list stable sort
    AtlasMaintain,   // colour atlases, palette pages, animated texture repaint
    GeometryPrep,    // parallel buildUnitGeom (key evaluation + prep), incl. wait
    DistantPrepare,  // distant-image cache lookup / bakes
    ShadowPack,      // shadow atlas allocation, packing, coverage bakes
    ShadowComposite, // ground + air shadow composition draws
    BodyAssembly,    // planning + parallel vertex copy into the body buffer
    BodySubmit,      // body draw-op replay (features, special units, batches)
    Count
};

enum class Counter : uint8_t {
    UnitsScanned,        // snapshot units examined for the painter list
    UnitCandidates,      // units that passed fog + frustum (painter items)
    FeatureCandidates,   // feature-index candidates examined
    FeaturesPainted,     // features that entered the painter list
    PainterItems,
    VisibleUnits,        // visUnits_ (units with a geometry slot this frame)
    SpecialUnits,        // drawn whole through drawUnit (clip/effects/build)
    DistantImages,       // bodies drawn from a distant image
    DistantBakes,
    GeometryHits,
    GeometryMisses,      // total; by-cause breakdown is MissCause
    VertsGenerated,      // body vertices rebuilt this frame (misses only)
    ShadowVertsGenerated,
    BodyVertsCopied,     // vertices assembled into the shared body buffer
    BodyBytesCopied,
    BodySubmissions,     // logical batched body submissions (API calls)
    ShadowUnits,         // units with shadow coverage packed in the atlas
    ShadowTilesReused,
    ShadowTilesDirty,
    ShadowDirtyPixels,   // area of re-rasterized coverage tiles (incl. padding)
    ShadowPages,
    AtlasRegionsPainted, // animated-texture atlas regions repainted
    AtlasPixelsPainted,
    TargetSwitches,      // render-target changes the renderer made (where counted)
    GeomPoolEntries,     // retained per-unit geometry entries
    GeomPoolUsedBytes,   // bytes in use across retained geometry vectors
    GeomPoolCapacityBytes, // bytes retained (capacity) across those vectors
    BodyBufferCapacityBytes,
    TerrainChunkBytes,   // resident terrain chunk textures (GPU, tracked)
    TerrainChunkImages,
    TerrainChunkUploads, // cumulative
    TerrainChunkPending,
    TerrainSectionBytes,
    GpuTrackedBytes,     // gpuvram tracked textures + buffers (excl. driver internals)
    Count
};

// Why a retained entry could not be reused. Phase 0 can only attribute
// "Cold" (no prior entry for this unit in the slot) vs "Changed" (exact key
// mismatch); later workstreams split Changed into the specific stages.
enum class MissCause : uint8_t {
    Cold, Changed, Owner, Pose, Material, Placement, Resource, Settings, Reference, Count
};

inline constexpr const char* kPhaseNames[size_t(Phase::Count)] = {
    "candidates", "painter_sort", "atlas", "geometry", "distant", "shadow_pack",
    "shadow_composite", "body_assembly", "body_submit"};
inline constexpr const char* kCounterNames[size_t(Counter::Count)] = {
    "units_scanned", "unit_candidates", "feature_candidates", "features_painted",
    "painter_items", "visible_units", "special_units", "distant_images", "distant_bakes",
    "geometry_hits", "geometry_misses", "verts_generated", "shadow_verts_generated",
    "body_verts_copied", "body_bytes_copied", "body_submissions", "shadow_units",
    "shadow_tiles_reused", "shadow_tiles_dirty", "shadow_dirty_pixels", "shadow_pages",
    "atlas_regions", "atlas_pixels", "target_switches", "geom_entries", "geom_used_bytes",
    "geom_capacity_bytes", "body_capacity_bytes", "terrain_chunk_bytes",
    "terrain_chunk_images", "terrain_chunk_uploads", "terrain_chunk_pending",
    "terrain_section_bytes", "gpu_tracked_bytes"};
inline constexpr const char* kMissNames[size_t(MissCause::Count)] = {
    "cold", "changed", "owner", "pose", "material", "placement", "resource", "settings",
    "reference"};

struct FrameStats {
    bool on = false;   // sampled once per frame by the owner; guards every tally
    std::array<double, size_t(Phase::Count)> ms{};
    std::array<uint64_t, size_t(Counter::Count)> n{};
    std::array<uint64_t, size_t(MissCause::Count)> miss{};
    void begin(bool enabled) { on = enabled; ms.fill(0); n.fill(0); miss.fill(0); }
    void add(Counter c, uint64_t v = 1) { if (on) n[size_t(c)] += v; }
    void set(Counter c, uint64_t v) { if (on) n[size_t(c)] = v; }
    void addMiss(MissCause c, uint64_t v = 1) { if (on) miss[size_t(c)] += v; }
    uint64_t get(Counter c) const { return n[size_t(c)]; }

    // One line: "t_<phase>=ms ... <counter>=n ... miss_<cause>=n". Returns the
    // length written (truncated output still NUL-terminates).
    int format(char* out, size_t size) const {
        int used = 0;
        const auto put = [&](const char* fmt, const char* key, auto value) {
            if (size_t(used) >= size) return;
            const int w = std::snprintf(out + used, size - size_t(used), fmt, key, value);
            if (w > 0) used += w;
        };
        for (size_t i = 0; i < ms.size(); ++i) put(" t_%s=%.4f", kPhaseNames[i], ms[i]);
        for (size_t i = 0; i < n.size(); ++i)
            put(" %s=%llu", kCounterNames[i], static_cast<unsigned long long>(n[i]));
        for (size_t i = 0; i < miss.size(); ++i)
            put(" miss_%s=%llu", kMissNames[i], static_cast<unsigned long long>(miss[i]));
        return used;
    }
};

// Accumulates elapsed CPU time into one phase while in scope, only when the
// stats are on. steady_clock: monotonic, no SDL dependency (unit tests).
class ScopedPhase {
public:
    ScopedPhase(FrameStats& stats, Phase phase) : stats_(stats.on ? &stats : nullptr), phase_(phase) {
        if (stats_) start_ = std::chrono::steady_clock::now();
    }
    ~ScopedPhase() {
        if (stats_)
            stats_->ms[size_t(phase_)] += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start_).count();
    }
    ScopedPhase(const ScopedPhase&) = delete;
    ScopedPhase& operator=(const ScopedPhase&) = delete;
private:
    FrameStats* stats_;
    Phase phase_;
    std::chrono::steady_clock::time_point start_{};
};

}  // namespace tak::render
