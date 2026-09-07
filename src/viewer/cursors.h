#pragma once

// Retail Total Annihilation: Kingdoms animated mouse cursors. Loads anims/cursors.gaf
// and draws the context-sensitive pointer (point / attack / move / reclaim / guard /
// ...). The cursor set, the per-context selection, the hotspot (each GAF frame's anchor)
// and the wall-clock ~15fps animation are reverse-engineered from KINGDOMS.icd by static
// analysis. Viewer-only -- never touches the deterministic sim or its state hash.

#include <SDL.h>

#include <array>
#include <cstdint>
#include <vector>

namespace tak {

namespace hpi { class Vfs; }

// The wired retail cursor set. Names map to cursors.gaf sequences in cursors.cpp. The
// shipped-but-never-selected placeholders (capture/teleport/pickup/resurrect/reanimate,
// all 1-frame clones of the normal arrow) and the non-pointer `pathicon` are omitted.
enum class CursorId {
    Normal, Select, Move, Attack, Airstrike, TooFar, Patrol, Defend,
    Repair, Load, Unload, Reclaim, Revive, FindSite, Green, Red, Hourglass,
    Count
};

class CursorSet {
public:
    CursorSet() = default;
    ~CursorSet();
    CursorSet(const CursorSet&) = delete;
    CursorSet& operator=(const CursorSet&) = delete;

    // Load anims/cursors.gaf + its palette through the VFS and bake per-frame textures.
    // Returns false if the assets are missing/unreadable (the caller keeps the OS arrow).
    bool load(SDL_Renderer* ren, hpi::Vfs& vfs);
    bool ok() const { return ok_; }

    // Draw `c` with its hotspot on the pixel (mouseX,mouseY) in renderer-output space.
    // Multi-frame cursors animate from wall-clock time; switching cursor restarts it.
    // `tint` colour-mods the sprite (default white = untinted); fight-move reuses the
    // Attack glyph with a tint so it reads apart from a real attack order.
    void draw(SDL_Renderer* ren, CursorId c, int mouseX, int mouseY,
              SDL_Color tint = SDL_Color{255, 255, 255, 255});

private:
    struct Frame { SDL_Texture* tex = nullptr; int w = 0, h = 0, hx = 0, hy = 0; };
    std::array<std::vector<Frame>, size_t(CursorId::Count)> anims_;
    bool ok_ = false;
    CursorId cur_ = CursorId::Count;   // != any real id, so the first draw seeds the clock
    uint64_t animStartMs_ = 0;

    static constexpr int kFps = 15;    // retail cursor cadence (KINGDOMS.icd frame delta)
};

}  // namespace tak
