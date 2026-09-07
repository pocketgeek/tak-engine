#include "viewer/campaignscreen.h"

#include <algorithm>
#include <cstdio>

#include "hpi/hpi.h"
#include "viewer/blockfont.h"
#include "viewer/settings.h"

namespace tak {

namespace {
bool inRect(const SDL_FRect& r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}
void fill(SDL_Renderer* r, const SDL_FRect& q, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRectF(r, &q);
}
}  // namespace

CampaignScreen::CampaignScreen(SDL_Renderer* ren, const hpi::Vfs& vfs, const Settings& s,
                               int initialTab)
    : ren_(ren), settings_(s), tab_(initialTab) {
    camps_ = loadCampaigns(vfs);
}

void CampaignScreen::layout(int winW, int winH) {
    u_ = std::clamp(std::min(winW / 1280.0f, winH / 720.0f), 1.0f, 3.0f);
    float panelW = std::min(780 * u_, winW * 0.82f);
    float panelH = std::min(620 * u_, winH * 0.9f);
    panel_ = {(winW - panelW) / 2, (winH - panelH) / 2, panelW, panelH};

    float titleH = 3.4f * 7 * u_ + 22 * u_;
    float tabsH = 40 * u_;
    float footerH = 52 * u_;

    // Campaign tabs across the top of the content area.
    tabRects_.clear();
    int nt = int(camps_.size());
    if (nt > 0) {
        float pad = 16 * u_, gap = 10 * u_;
        float tw = (panel_.w - 2 * pad - gap * (nt - 1)) / float(nt);
        float ty = panel_.y + titleH;
        for (int i = 0; i < nt; ++i)
            tabRects_.push_back({panel_.x + pad + i * (tw + gap), ty, tw, tabsH - 8 * u_});
    }
    tab_ = std::clamp(tab_, 0, std::max(0, nt - 1));

    // Mission list viewport (scrollable).
    listClip_ = {panel_.x + 16 * u_, panel_.y + titleH + tabsH,
                 panel_.w - 32 * u_, panel_.h - titleH - tabsH - footerH};
    rows_.clear();
    float rowH = 34 * u_;
    int done = 0, count = 0;
    bool hasAlt = false;
    if (nt > 0) {
        count = camps_[size_t(tab_)].count();
        done = settings_.campaignProgress(camps_[size_t(tab_)].id);
        hasAlt = !camps_[size_t(tab_)].altFinal.empty();
    }
    contentH_ = (count + (hasAlt ? 1 : 0)) * rowH;
    float maxScroll = std::max(0.0f, contentH_ - listClip_.h);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);
    float y = listClip_.y - scroll_;
    for (int i = 0; i < count; ++i) {
        Row r;
        r.mission = i;
        r.playable = i <= done;   // completed or the next-up mission
        r.rect = {listClip_.x, y, listClip_.w, rowH - 4 * u_};
        rows_.push_back(r);
        y += rowH;
    }
    if (hasAlt) {   // the alt ending branches at the last mission, so it unlocks with it
        Row r;
        r.mission = -2;
        r.playable = done >= count - 1;
        r.rect = {listClip_.x, y, listClip_.w, rowH - 4 * u_};
        rows_.push_back(r);
        y += rowH;
    }

    float bw = 160 * u_, bh = 32 * u_;
    backRect_ = {panel_.x + (panel_.w - bw) / 2, panel_.y + panel_.h - footerH + (footerH - bh) / 2, bw, bh};
}

bool CampaignScreen::input(const SDL_Event& e, int winW, int winH) {
    layout(winW, winH);
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) return true;
    if (e.type == SDL_MOUSEWHEEL) { scroll_ -= e.wheel.y * 42 * u_; return false; }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        float mx = float(e.button.x), my = float(e.button.y);
        if (inRect(backRect_, mx, my)) return true;
        for (size_t i = 0; i < tabRects_.size(); ++i)
            if (inRect(tabRects_[i], mx, my)) { tab_ = int(i); scroll_ = 0; return false; }
        if (my < listClip_.y || my > listClip_.y + listClip_.h) return false;   // outside the viewport
        for (const Row& r : rows_) {
            if (!inRect(r.rect, mx, my) || !r.playable) continue;
            const Campaign& c = camps_[size_t(tab_)];
            pickedStem_ = (r.mission == -2) ? c.altFinal : c.missions[size_t(r.mission)].stem;
            pickedCampaign_ = c.id;
            picked_ = true;
            return true;
        }
        return false;
    }
    return false;
}

void CampaignScreen::render(int winW, int winH) {
    layout(winW, winH);
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);

    fill(ren_, {0, 0, float(winW), float(winH)}, 0, 0, 0, 170);
    fill(ren_, panel_, 24, 26, 34, 245);
    // panel border
    SDL_SetRenderDrawColor(ren_, 90, 100, 130, 255);
    SDL_RenderDrawRectF(ren_, &panel_);

    float tpx = 3.4f * u_;
    const char* title = "CAMPAIGN";
    drawBlockText(ren_, title, panel_.x + (panel_.w - blockTextWidth(title, tpx)) / 2,
                  panel_.y + 14 * u_, tpx, {235, 225, 190, 255});

    if (camps_.empty()) {
        const char* none = "NO CAMPAIGNS FOUND";
        drawBlockText(ren_, none, panel_.x + (panel_.w - blockTextWidth(none, 2 * u_)) / 2,
                      panel_.y + panel_.h / 2, 2 * u_, {200, 120, 120, 255});
    }

    // Campaign tabs.
    for (size_t i = 0; i < tabRects_.size(); ++i) {
        const SDL_FRect& t = tabRects_[i];
        bool sel = int(i) == tab_;
        fill(ren_, t, sel ? 60 : 38, sel ? 70 : 42, sel ? 96 : 54, 255);
        SDL_SetRenderDrawColor(ren_, sel ? 150 : 80, sel ? 165 : 90, sel ? 210 : 116, 255);
        SDL_RenderDrawRectF(ren_, &t);
        std::string nm = camps_[i].title;
        float px = 1.6f * u_;
        while (px > 0.9f * u_ && blockTextWidth(nm, px) > t.w - 12 * u_) px -= 0.1f * u_;
        drawBlockText(ren_, nm, t.x + (t.w - blockTextWidth(nm, px)) / 2,
                      t.y + (t.h - 7 * px) / 2, px, sel ? SDL_Color{235, 235, 245, 255} : SDL_Color{170, 175, 190, 255});
    }

    // Mission rows, clipped to the viewport.
    SDL_Rect clip = {int(listClip_.x), int(listClip_.y), int(listClip_.w), int(listClip_.h)};
    SDL_RenderSetClipRect(ren_, &clip);
    int done = camps_.empty() ? 0 : settings_.campaignProgress(camps_[size_t(tab_)].id);
    for (const Row& r : rows_) {
        if (r.rect.y + r.rect.h < listClip_.y || r.rect.y > listClip_.y + listClip_.h) continue;
        bool alt = r.mission == -2;
        bool completed = !alt && r.mission < done;
        bool current = alt ? r.playable : r.mission == done;
        Uint8 br = alt ? (r.playable ? 46 : 30) : completed ? 30 : current ? 46 : 30;
        Uint8 bg = alt ? (r.playable ? 40 : 32) : completed ? 44 : current ? 54 : 32;
        Uint8 bb = alt ? (r.playable ? 60 : 40) : completed ? 38 : current ? 40 : 36;
        fill(ren_, r.rect, br, bg, bb, 255);
        if (current) {   // highlight a playable row
            SDL_SetRenderDrawColor(ren_, alt ? 180 : 210, alt ? 130 : 180, alt ? 210 : 90, 255);
            SDL_RenderDrawRectF(ren_, &r.rect);
        }
        char label[32];
        if (alt) std::snprintf(label, sizeof label, "ALT ENDING");
        else std::snprintf(label, sizeof label, "MISSION %d", r.mission + 1);
        SDL_Color col = alt ? (r.playable ? SDL_Color{215, 180, 235, 255} : SDL_Color{110, 100, 120, 255})
                        : completed ? SDL_Color{140, 200, 150, 255}
                        : current  ? SDL_Color{240, 225, 170, 255}
                                   : SDL_Color{110, 114, 128, 255};
        float px = 1.5f * u_;
        drawBlockText(ren_, label, r.rect.x + 14 * u_, r.rect.y + (r.rect.h - 7 * px) / 2, px, col);
        const char* tag = alt ? (r.playable ? "PLAY" : "LOCKED")
                        : completed ? "DONE" : current ? "PLAY" : "LOCKED";
        drawBlockText(ren_, tag, r.rect.x + r.rect.w - blockTextWidth(tag, px) - 14 * u_,
                      r.rect.y + (r.rect.h - 7 * px) / 2, px, col);
    }
    SDL_RenderSetClipRect(ren_, nullptr);

    // BACK button.
    fill(ren_, backRect_, 52, 56, 70, 255);
    SDL_SetRenderDrawColor(ren_, 120, 128, 150, 255);
    SDL_RenderDrawRectF(ren_, &backRect_);
    float bpx = 1.7f * u_;
    drawBlockText(ren_, "BACK", backRect_.x + (backRect_.w - blockTextWidth("BACK", bpx)) / 2,
                  backRect_.y + (backRect_.h - 7 * bpx) / 2, bpx, {215, 220, 235, 255});
}

}  // namespace tak
