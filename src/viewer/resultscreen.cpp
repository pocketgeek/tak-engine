#include "viewer/resultscreen.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "util/png.h"
#include "viewer/blockfont.h"
#include "viewer/cursors.h"
#include "viewer/menumusic.h"
#include "viewer/settings.h"

namespace tak {

namespace {
bool inRect(const SDL_FRect& r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}
}  // namespace

ResultChoice ResultScreen::run(SDL_Renderer* ren, const hpi::Vfs& vfs, bool victory,
                               const std::string& title, bool hasNext,
                               Settings* settings, MenuMusic* music) {
    CursorSet cursors;
    cursors.load(ren, vfs);
    SDL_ShowCursor(cursors.ok() ? SDL_DISABLE : SDL_ENABLE);

    SDL_PumpEvents();
    SDL_FlushEvents(SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP);

    // The buttons offered, top choice first: victory -> [Next] Replay Menu; defeat ->
    // Retry Menu.
    struct Btn { const char* label; ResultChoice choice; };
    std::vector<Btn> btns;
    if (victory) {
        if (hasNext) btns.push_back({"NEXT MISSION", ResultChoice::Next});
        btns.push_back({"REPLAY", ResultChoice::Retry});
        btns.push_back({"MAIN MENU", ResultChoice::Menu});
    } else {
        btns.push_back({"RETRY", ResultChoice::Retry});
        btns.push_back({"MAIN MENU", ResultChoice::Menu});
    }

    for (;;) {
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(ren, &w, &h);
        float u = std::clamp(std::min(w / 1280.0f, h / 720.0f), 1.0f, 3.0f);

        float panelW = std::min(620 * u, w * 0.7f);
        float bh = 44 * u, bgap = 16 * u;
        float panelH = 200 * u + btns.size() * (bh + bgap);
        panelH = std::min(panelH, h * 0.9f);
        SDL_FRect panel{(w - panelW) / 2, (h - panelH) / 2, panelW, panelH};

        std::vector<SDL_FRect> rects;
        float bw = 300 * u;
        float by = panel.y + panel.h - float(btns.size()) * (bh + bgap) - 8 * u;
        for (size_t i = 0; i < btns.size(); ++i)
            rects.push_back({panel.x + (panel.w - bw) / 2, by + i * (bh + bgap), bw, bh});

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) continue;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) return ResultChoice::Menu;
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER) return btns.front().choice;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                float mx = float(e.button.x), my = float(e.button.y);
                for (size_t i = 0; i < rects.size(); ++i)
                    if (inRect(rects[i], mx, my)) return btns[i].choice;
            }
        }
        if (music) music->poll();

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 12, 13, 18, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawColor(ren, 26, 28, 36, 255);
        SDL_RenderFillRectF(ren, &panel);
        SDL_SetRenderDrawColor(ren, 96, 106, 138, 255);
        SDL_RenderDrawRectF(ren, &panel);

        SDL_Color head = victory ? SDL_Color{235, 205, 110, 255} : SDL_Color{210, 110, 100, 255};
        const char* big = victory ? "VICTORY" : "DEFEAT";
        float hpx = 5.0f * u;
        drawBlockText(ren, big, panel.x + (panel.w - blockTextWidth(big, hpx)) / 2,
                      panel.y + 40 * u, hpx, head);
        float spx = 1.8f * u;
        drawBlockText(ren, title, panel.x + (panel.w - blockTextWidth(title, spx)) / 2,
                      panel.y + 40 * u + 7 * hpx + 22 * u, spx, {200, 205, 220, 255});

        for (size_t i = 0; i < rects.size(); ++i) {
            bool accent = (i == 0 && victory && hasNext) || (!victory && i == 0);
            SDL_SetRenderDrawColor(ren, accent ? 70 : 50, accent ? 104 : 54, accent ? 70 : 68, 255);
            SDL_RenderFillRectF(ren, &rects[i]);
            SDL_SetRenderDrawColor(ren, accent ? 130 : 112, accent ? 180 : 120, accent ? 130 : 146, 255);
            SDL_RenderDrawRectF(ren, &rects[i]);
            float px = 1.8f * u;
            drawBlockText(ren, btns[i].label,
                          rects[i].x + (rects[i].w - blockTextWidth(btns[i].label, px)) / 2,
                          rects[i].y + (rects[i].h - 7 * px) / 2, px, {224, 230, 242, 255});
        }

        if (cursors.ok()) {
            int mx = 0, my = 0; SDL_GetMouseState(&mx, &my);
            cursors.draw(ren, CursorId::Normal, mx, my, settings ? settings->cursorScale : 4);
        }
        if (const char* sp = std::getenv("TAK_SHOT_RESULT")) {
            std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
            if (SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ABGR8888, px.data(), w * 4) == 0)
                png::write(sp, w, h, px);
            return ResultChoice::Menu;
        }
        SDL_RenderPresent(ren);
        SDL_Delay(8);
    }
}

}  // namespace tak
