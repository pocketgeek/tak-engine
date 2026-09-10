#pragma once

// Terrain map view: pans/zooms a TNT map and composites its tile chunks on a
// background worker thread (see chunkWorkerLoop in mapview.cpp). Extracted from
// client/main.cpp; kept at global scope so its unqualified use sites there are
// unchanged.
//
// Lifetime: the Vfs passed to the ctor/reload MUST outlive the MapView — the
// terrain::Compositor borrows it by reference. (GameView declares its vfs_
// member before its mapView_ member for exactly this reason.)
// Owns a worker thread + mutex, so it is implicitly non-copyable/non-movable.

#include <SDL.h>

#include "terrain/terrain.h"   // tak::terrain::Compositor (by-value member)
#include "tnt/tnt.h"           // tak::tnt::Map (by-value member)

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace tak::hpi { class Vfs; }

class MapView {
public:
    MapView(SDL_Renderer* ren, const tak::hpi::Vfs& vfs, const std::string& mapPath);
    ~MapView();

    // Swap in a different map (discarding cached chunk textures). The compositor's
    // decoded-tile cache is content-addressed by tile key, so it stays valid. Used at
    // game start so the render terrain matches the map the sim actually loaded.
    void reload(const tak::hpi::Vfs& vfs, const std::string& mapPath);

    void input(const SDL_Event& e);

    // The smallest zoom at which the map still fills the window in one dimension
    // (zooming out past this would run the view off the map edges).
    float minZoom(int winW, int winH) const;
    // Apply just the min-zoom floor (no offset change).
    void clampZoom(int winW, int winH);
    void clampOffset(int winW, int winH);

    // Create any terrain chunk textures that will be visible this frame. Called
    // BEFORE the render pass so texture creation never interleaves with draw calls.
    void ensureChunks(int winW, int winH);

    // Block until every queued chunk is composited and uploaded. Screenshot paths
    // only -- normal play never waits.
    void finishChunks();

    // A low-res whole-map overview (one texel per 32px block), drawn UNDER the chunk
    // grid so a not-yet-composited chunk shows blurry terrain instead of a black
    // rectangle. The owner sets this (GameView's minimap texture); null = none.
    void setUnderlay(SDL_Texture* t) { underlay_ = t; }

    void draw(int winW, int winH);

    // Bilinear terrain scaling (retail's video option). Applies to already-built
    // chunk textures immediately and to every chunk composited afterwards.
    void setBilinear(bool b);

    float offX() const { return offX_; }
    float offY() const { return offY_; }
    float zoom() const { return zoom_; }
    tak::terrain::Compositor& compositor() { return comp_; }
    void setZoom(float z) { zoom_ = z; }
    void setOffset(float x, float y) { offX_ = x; offY_ = y; }
    void setZoomSpeed(float m);
    const tak::tnt::Map& map() const { return map_; }

private:
    static constexpr int kChunk = 512;

    // Load a real map from the VFS, OR -- when mapPath is a "~gen1~" random-map id --
    // build it procedurally in memory (client & server share the deterministic gen).
    static tak::tnt::Map genOrLoad(const tak::hpi::Vfs& vfs, const std::string& mapPath);

    // Queue a chunk for background compositing (no-op if built, queued, or off-map).
    void requestChunk(int cx, int cy);
    // Main thread: turn finished composites into textures.
    void uploadReadyChunks();
    // Worker thread: composite queued chunks into CPU buffers.
    void chunkWorkerLoop();

    SDL_Renderer* ren_;
    tak::tnt::Map map_;
    tak::terrain::Compositor comp_;
    std::map<std::pair<int, int>, SDL_Texture*> chunks_;
    // Async chunk pipeline (see chunkWorkerLoop).
    struct DoneChunk { int cx, cy; std::vector<uint8_t> buf; };
    std::thread chunkWorker_;
    std::mutex chunkMu_;
    std::condition_variable chunkCv_;
    std::deque<std::pair<int, int>> chunkQueue_;
    std::set<std::pair<int, int>> chunkPending_;   // queued or in flight
    std::vector<DoneChunk> chunkDone_;
    bool chunkStop_ = false, chunkBusy_ = false;
    bool bilinear_ = false;   // smooth chunk scaling (Options; see setBilinear)
    float offX_ = 0, offY_ = 0, zoom_ = 0.35f;
    float zoomSpeed_ = 1.0f;   // wheel-zoom sensitivity exponent (Options)
    SDL_Texture* underlay_ = nullptr;   // low-res overview drawn under chunks (not owned)
};
