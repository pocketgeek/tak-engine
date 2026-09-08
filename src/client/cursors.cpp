#include "client/cursors.h"

#include "gaf/gaf.h"
#include "hpi/hpi.h"

#include <cctype>
#include <string>
#include <unordered_map>

namespace tak {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

// CursorId -> cursors.gaf sequence name (matched case-insensitively, since the GAF
// mixes cases: "CursorMove" vs "cursorselect"). Derived from KINGDOMS.icd's loader.
const char* seqName(CursorId c) {
    switch (c) {
        case CursorId::Normal:    return "cursornormal";
        case CursorId::Select:    return "cursorselect";
        case CursorId::Move:      return "CursorMove";
        case CursorId::Attack:    return "CursorAttack";
        case CursorId::Airstrike: return "cursorairstrike";
        case CursorId::TooFar:    return "cursortoofar";
        case CursorId::Patrol:    return "CursorPatrol";
        case CursorId::Defend:    return "CursorDefend";
        case CursorId::Repair:    return "cursorrepair";
        case CursorId::Load:      return "Cursorload";
        case CursorId::Unload:    return "CursorUnload";
        case CursorId::Reclaim:   return "Cursorreclamate";
        case CursorId::Revive:    return "cursorrevive";
        case CursorId::FindSite:  return "cursorfindsite";
        case CursorId::Green:     return "cursorgrn";
        case CursorId::Red:       return "cursorred";
        case CursorId::Hourglass: return "cursorhourglass";
        default:                  return "";
    }
}

}  // namespace

CursorSet::~CursorSet() {
    for (auto& frames : anims_)
        for (auto& f : frames)
            if (f.tex) SDL_DestroyTexture(f.tex);
}

bool CursorSet::load(SDL_Renderer* ren, const hpi::Vfs& vfs) {
    if (!ren) return false;
    std::vector<uint8_t> gafBytes, palBytes;
    try {
        gafBytes = vfs.read("anims/cursors.gaf");
        palBytes = vfs.read("palettes/cursors.pcx");
    } catch (...) { return false; }

    gaf::Palette pal;
    std::vector<gaf::Sequence> seqs;
    try {
        pal = gaf::Palette::fromBytes(palBytes, "palettes/cursors.pcx");
        // -1: honour each frame's own transparency index (the cursor art is authored
        // that way, so backgrounds decode straight to alpha=0 -- verified on export).
        seqs = gaf::load(gafBytes, pal, -1, "anims/cursors.gaf");
    } catch (...) { return false; }

    std::unordered_map<std::string, const gaf::Sequence*> byName;
    for (auto& s : seqs) byName[lower(s.name)] = &s;

    for (size_t i = 0; i < size_t(CursorId::Count); ++i) {
        auto it = byName.find(lower(seqName(CursorId(i))));
        if (it == byName.end()) continue;               // leave empty; draw() falls back
        for (const auto& fr : it->second->frames) {
            if (fr.width <= 0 || fr.height <= 0) continue;
            SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                               SDL_TEXTUREACCESS_STATIC, fr.width, fr.height);
            if (!t) continue;
            SDL_UpdateTexture(t, nullptr, fr.rgba.data(), fr.width * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);   // crisp 1:1 pixel cursor
            anims_[i].push_back({t, fr.width, fr.height, fr.xoff, fr.yoff});
        }
    }
    // Need at least the normal pointer to justify taking over from the OS cursor.
    ok_ = !anims_[size_t(CursorId::Normal)].empty();
    return ok_;
}

void CursorSet::draw(SDL_Renderer* ren, CursorId c, int mouseX, int mouseY, int scale, SDL_Color tint) {
    if (!ok_ || !ren) return;
    if (size_t(c) >= anims_.size() || anims_[size_t(c)].empty())
        c = CursorId::Normal;                           // fall back if this cursor didn't load
    const auto& frames = anims_[size_t(c)];
    if (frames.empty()) return;
    if (scale < 1) scale = 1;

    const uint64_t now = SDL_GetTicks64();
    if (c != cur_) { cur_ = c; animStartMs_ = now; }    // restart animation on a change
    size_t idx = 0;
    if (frames.size() > 1)
        idx = size_t((now - animStartMs_) * kFps / 1000) % frames.size();

    const Frame& f = frames[idx];
    // Set the mod every draw (default white = no-op) so a previous tint never lingers.
    SDL_SetTextureColorMod(f.tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(f.tex, tint.a);
    // Magnify sprite AND hotspot by `scale` so the anchor stays on the pointer.
    SDL_Rect dst{ mouseX - f.hx * scale, mouseY - f.hy * scale, f.w * scale, f.h * scale };
    SDL_RenderCopy(ren, f.tex, nullptr, &dst);
}

}  // namespace tak
