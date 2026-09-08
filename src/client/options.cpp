#include "client/options.h"

#include "client/blockfont.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tak {

namespace {

void SDLCALL probeSilence(void*, Uint8* s, int len) { SDL_memset(s, 0, size_t(len)); }

// Open a probe device requesting `req` channels and return what it negotiates.
int probeChannels(int req) {
    SDL_AudioSpec want{}, got{};
    want.freq = 11025; want.format = AUDIO_S16SYS; want.channels = Uint8(req);
    want.samples = 1024; want.callback = probeSilence;   // callback-based, like SoundBank
    SDL_AudioDeviceID d = SDL_OpenAudioDevice(nullptr, 0, &want, &got, SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    int ch = (d && got.channels) ? got.channels : 0;
    if (d) SDL_CloseAudioDevice(d);
    return ch;
}

// Speaker label per channel count (mirrors SoundBank::channelRole / channelGains).
const char* role(int count, int i) {
    switch (count) {
        case 2: { static const char* r[] = {"LEFT", "RIGHT"}; return i < 2 ? r[i] : ""; }
        case 4: { static const char* r[] = {"FRONT L", "FRONT R", "REAR L", "REAR R"}; return i < 4 ? r[i] : ""; }
        case 6: { static const char* r[] = {"FRONT L", "FRONT R", "CENTER", "SUB", "REAR L", "REAR R"}; return i < 6 ? r[i] : ""; }
        case 8: { static const char* r[] = {"FRONT L", "FRONT R", "CENTER", "SUB", "REAR L", "REAR R", "SIDE L", "SIDE R"}; return i < 8 ? r[i] : ""; }
        default: return "MONO";
    }
}

std::string pctOf(float v, float full) { return std::to_string(int(v * 100 / full + 0.5f)) + "%"; }
std::string timesFmt(float v) { char b[16]; std::snprintf(b, sizeof b, "%.2fX", v); return b; }

}  // namespace

// The one true output-channel count for this process, so SoundBank (which mixes
// into it) and the Options sliders always agree. GetDefaultAudioInfo only advises
// the REQUEST (often 2 even on a 5.1/7.1 rig); the real layout is what the OPENED
// device negotiates. Probe once (callback-based, like SoundBank) and, if that's
// stereo, probe again asking for 5.1 -- many setups (PipeWire) advertise a 2ch
// default but open surround when asked. Cached: computed once, identical everywhere.
int detectOutputChannels() {
    static int cached = -1;
    if (cached >= 0) return cached;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return (cached = 2);
    int req = 2;
    SDL_AudioSpec def{};
    if (SDL_GetDefaultAudioInfo(nullptr, &def, 0) == 0 && def.channels >= 2)
        req = std::min<int>(def.channels, 8);
    int ch = probeChannels(req);
    if (ch <= 2) { int hi = probeChannels(6); if (hi > ch) ch = hi; }
    if (ch <= 0) ch = 2;
    cached = std::clamp(ch, 1, 8);
    std::fprintf(stderr, "audio: detected %d output channels (default advised %d)\n", cached, req);
    return cached;
}

OptionsScreen::OptionsScreen(SDL_Renderer* ren, Settings& s, std::function<void()> onChange,
                             std::function<void()> onSave, int audioChannels)
    : ren_(ren), s_(s), onChange_(std::move(onChange)), onSave_(std::move(onSave)) {
    build(audioChannels > 0 ? audioChannels : detectOutputChannels());
}

void OptionsScreen::build(int channels) {
    std::fprintf(stderr, "Options: %d speaker sliders\n", channels);
    auto section = [&](const char* label) { ctls_.push_back({Control::Section, label, 0, 0, {}, {}, {}, {}}); };
    auto slider = [&](const char* label, float lo, float hi, std::function<float()> get,
                      std::function<void(float)> set, std::function<std::string(float)> fmt) {
        ctls_.push_back({Control::Slider, label, lo, hi, std::move(get), std::move(set), std::move(fmt), {}});
    };
    auto toggle = [&](const char* label, std::function<float()> get, std::function<void(float)> set) {
        ctls_.push_back({Control::Toggle, label, 0, 1, std::move(get), std::move(set), {}, {}});
    };

    section("AUDIO");
    slider("MASTER VOLUME", 0, 256, [&] { return float(s_.masterVol); },
           [&](float v) { s_.masterVol = int(v + 0.5f); }, [](float v) { return pctOf(v, 256); });
    slider("MUSIC VOLUME", 0, 256, [&] { return float(s_.bgmVol); },
           [&](float v) { s_.bgmVol = int(v + 0.5f); }, [](float v) { return pctOf(v, 256); });
    slider("SOUND EFFECTS", 0, 256, [&] { return float(s_.sfxVol); },
           [&](float v) { s_.sfxVol = int(v + 0.5f); }, [](float v) { return pctOf(v, 256); });
    for (int i = 0; i < channels && i < 8; ++i) {
        std::string label = std::string("SPEAKER ") + role(channels, i);
        slider(label.c_str(), 0, 1, [this, i] { return s_.chanGain[i]; },
               [this, i](float v) { s_.chanGain[i] = v; }, [](float v) { return pctOf(v, 1); });
    }

    section("DISPLAY");
    toggle("FULLSCREEN", [&] { return s_.fullscreen ? 1.0f : 0.0f; },
           [&](float v) { s_.fullscreen = v > 0.5f; });
    toggle("VSYNC", [&] { return s_.vsync ? 1.0f : 0.0f; }, [&](float v) { s_.vsync = v > 0.5f; });
    slider("MAX FPS", 30, 240, [&] { return float(s_.maxFps); },
           [&](float v) { s_.maxFps = int(v + 0.5f); }, [](float v) { return std::to_string(int(v + 0.5f)); });
    slider("UI SCALE", 0.75f, 2.0f, [&] { return s_.uiScale; },
           [&](float v) { s_.uiScale = v; }, [](float v) { return pctOf(v, 1); });
    // Supersampling AA as a simple on/off; ON is 2x.
    toggle("ANTI-ALIASING", [&] { return s_.antiAlias >= 2 ? 1.0f : 0.0f; },
           [&](float v) { s_.antiAlias = v > 0.5f ? 2 : 0; });
    // Distant-unit impostors (perf) and the unit-sprite mode -- were F8 / F10 in-game.
    toggle("DISTANT IMPOSTORS", [&] { return s_.lod ? 1.0f : 0.0f; },
           [&](float v) { s_.lod = v > 0.5f; });
    slider("UNIT SPRITES", 0, 2, [&] { return float(s_.spriteMode); },
           [&](float v) { s_.spriteMode = std::clamp(int(v + 0.5f), 0, 2); },
           [](float v) { int l = int(v + 0.5f); return std::string(l >= 2 ? "OFF" : l >= 1 ? "ON" : "AUTO"); });
    // Where the in-game conjure/build icon row sits along the bottom of the screen.
    slider("BUILD MENU", 0, 2, [&] { return float(s_.buildBarAlign); },
           [&](float v) { s_.buildBarAlign = std::clamp(int(v + 0.5f), 0, 2); },
           [](float v) { int l = int(v + 0.5f); return std::string(l >= 2 ? "RIGHT" : l >= 1 ? "CENTER" : "LEFT"); });
    // Extra scale for the build icon row, applied ON TOP of UI SCALE (same range).
    slider("BUILD MENU SCALE", 0.75f, 2.0f, [&] { return s_.buildBarScale; },
           [&](float v) { s_.buildBarScale = v; }, [](float v) { return pctOf(v, 1); });
    // Retail's video option: smooth terrain + feature scaling (off = crisp pixels).
    toggle("BILINEAR FILTERING", [&] { return s_.bilinear ? 1.0f : 0.0f; },
           [&](float v) { s_.bilinear = v > 0.5f; });
    slider("HEALTH BARS", 0, 2, [&] { return float(s_.healthBars); },
           [&](float v) { s_.healthBars = std::clamp(int(v + 0.5f), 0, 2); },
           [](float v) { int l = int(v + 0.5f); return std::string(l >= 2 ? "ALWAYS" : l >= 1 ? "DAMAGED" : "OFF"); });

    section("CAMERA");
    slider("MOUSE ZOOM SPEED", 0.25f, 4.0f, [&] { return s_.mouseZoomSpeed; },
           [&](float v) { s_.mouseZoomSpeed = v; }, [](float v) { return timesFmt(v); });
    toggle("EDGE SCROLLING", [&] { return s_.edgeScroll ? 1.0f : 0.0f; },
           [&](float v) { s_.edgeScroll = v > 0.5f; });
    slider("EDGE SCROLL SPEED", 0.25f, 4.0f, [&] { return s_.edgeScrollSpeed; },
           [&](float v) { s_.edgeScrollSpeed = v; }, [](float v) { return timesFmt(v); });
    // Mouse-cursor size as an 8-stop slider: 1X..8X.
    slider("CURSOR SIZE", 1, 8, [&] { return float(s_.cursorScale); },
           [&](float v) { s_.cursorScale = int(v + 0.5f); },
           [](float v) { return std::to_string(int(v + 0.5f)) + "X"; });
    // SAVE / BACK are drawn as a fixed footer (see layout()/render()), not list rows.
}

void OptionsScreen::layout(int winW, int winH) {
    u_ = std::clamp(std::min(winW / 1280.0f, winH / 720.0f), 1.0f, 3.0f);
    auto rowH = [&](Control::Kind k) { return (k == Control::Section) ? 30 * u_ : 36 * u_; };
    float panelW = std::min(660 * u_, winW * 0.72f);
    float titleH = 3.4f * 7 * u_ + 26 * u_;
    float footerH = 52 * u_;    // SAVE / BACK buttons, fixed at the panel bottom
    contentH_ = 0;
    for (auto& c : ctls_) contentH_ += rowH(c.kind);
    float viewH = std::min(contentH_, winH * 0.92f - titleH - footerH);
    float panelH = titleH + viewH + footerH;
    panel_ = {(winW - panelW) / 2, (winH - panelH) / 2, panelW, panelH};

    float maxScroll = std::max(0.0f, contentH_ - viewH);
    scroll_ = std::clamp(scroll_, 0.0f, maxScroll);

    float vy = panel_.y + titleH;    // viewport top
    float y = vy - scroll_;
    for (auto& c : ctls_) {
        float h = rowH(c.kind);
        c.row = {panel_.x + 26 * u_, y, panelW - 52 * u_, h};
        y += h;
    }
    float bw = 150 * u_, bh = 32 * u_, gap = 18 * u_;
    float total = 3 * bw + 2 * gap;
    float x0 = panel_.x + (panel_.w - total) / 2;
    float by = panel_.y + panel_.h - footerH + (footerH - bh) / 2;
    defaultsRect_ = {x0, by, bw, bh};
    saveRect_ = {x0 + bw + gap, by, bw, bh};
    backRect_ = {x0 + 2 * (bw + gap), by, bw, bh};
}

void OptionsScreen::commit(Control& c, float mx) {
    float frac = std::clamp((mx - c.row.x) / std::max(1.0f, c.row.w), 0.0f, 1.0f);
    float v = c.lo + frac * (c.hi - c.lo);
    if (c.set) c.set(v);
    dirty_ = true;
    if (onChange_) onChange_();
}

bool OptionsScreen::input(const SDL_Event& e, int winW, int winH) {
    layout(winW, winH);
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) return true;
    if (e.type == SDL_MOUSEWHEEL) { scroll_ -= e.wheel.y * 42 * u_; return false; }

    auto in = [](const SDL_FRect& r, float mx, float my) {
        return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
    };
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        float mx = float(e.button.x), my = float(e.button.y);
        if (in(backRect_, mx, my)) return true;                     // close, no save
        if (in(saveRect_, mx, my)) {                                // persist (if dirty)
            if (dirty_ && onSave_) { onSave_(); dirty_ = false; }
            return false;
        }
        if (in(defaultsRect_, mx, my)) {                            // reset to defaults
            if (atDefaults()) return false;         // already default -> disabled, ignore
            std::string keepName = s_.playerName;   // not shown here -> preserve these
            std::string keepMap = s_.lastMap;
            s_ = Settings{};
            s_.playerName = keepName;
            s_.lastMap = keepMap;
            dirty_ = true;
            if (onChange_) onChange_();
            return false;
        }
        float titleH = 3.4f * 7 * u_ + 26 * u_, footerH = 52 * u_;  // scrollable viewport
        if (my < panel_.y + titleH || my > panel_.y + panel_.h - footerH) return false;
        for (size_t i = 0; i < ctls_.size(); ++i) {
            Control& c = ctls_[i];
            if (!in(c.row, mx, my)) continue;
            if (c.kind == Control::Toggle) {
                c.set(c.get() > 0.5f ? 0.0f : 1.0f); dirty_ = true; if (onChange_) onChange_(); return false;
            }
            if (c.kind == Control::Slider) { drag_ = int(i); commit(c, mx); return false; }
        }
        return false;
    }
    if (e.type == SDL_MOUSEMOTION && drag_ >= 0 && (e.motion.state & SDL_BUTTON_LMASK)) {
        layout(winW, winH);   // rows may have shifted if window resized mid-drag
        if (drag_ < int(ctls_.size())) commit(ctls_[size_t(drag_)], float(e.motion.x));
        return false;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) drag_ = -1;
    return false;
}

void OptionsScreen::render(int winW, int winH) {
    layout(winW, winH);
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);

    // Dim the whole screen, then the panel.
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 175);
    SDL_FRect dim{0, 0, float(winW), float(winH)}; SDL_RenderFillRectF(ren_, &dim);
    SDL_SetRenderDrawColor(ren_, 24, 26, 34, 245); SDL_RenderFillRectF(ren_, &panel_);
    SDL_SetRenderDrawColor(ren_, 120, 130, 160, 255); SDL_RenderDrawRectF(ren_, &panel_);

    const char* title = "OPTIONS";
    float tpx = 3.4f * u_;
    drawBlockText(ren_, title, panel_.x + (panel_.w - blockTextWidth(title, tpx)) / 2,
                  panel_.y + 13 * u_, tpx, {235, 225, 180, 255});

    // Clip the scrollable region to between the title and the footer so rows don't
    // spill onto the footer buttons.
    float titleH = 3.4f * 7 * u_ + 26 * u_, footerH = 52 * u_;
    SDL_Rect clip{int(panel_.x), int(panel_.y + titleH), int(panel_.w),
                  int(panel_.h - titleH - footerH)};
    SDL_RenderSetClipRect(ren_, &clip);

    float fpx = 2.0f * u_;
    for (auto& c : ctls_) {
        if (c.row.y + c.row.h < clip.y || c.row.y > clip.y + clip.h) continue;   // off-screen
        if (c.kind == Control::Section) {
            drawBlockText(ren_, c.label, c.row.x, c.row.y + 12 * u_, 2.2f * u_, {150, 200, 235, 255});
            SDL_SetRenderDrawColor(ren_, 70, 78, 96, 255);
            SDL_FRect ln{c.row.x, c.row.y + c.row.h - 3 * u_, c.row.w, 1.5f * u_};
            SDL_RenderFillRectF(ren_, &ln);
        } else if (c.kind == Control::Toggle) {
            bool on = c.get() > 0.5f;
            drawBlockText(ren_, c.label, c.row.x, c.row.y + 12 * u_, fpx, {225, 230, 240, 255});
            SDL_FRect pill{c.row.x + c.row.w - 78 * u_, c.row.y + 7 * u_, 78 * u_, 20 * u_};
            SDL_SetRenderDrawColor(ren_, on ? 60 : 44, on ? 120 : 48, on ? 90 : 58, 255);
            SDL_RenderFillRectF(ren_, &pill);
            SDL_SetRenderDrawColor(ren_, 130, 140, 170, 255); SDL_RenderDrawRectF(ren_, &pill);
            const char* t = on ? "ON" : "OFF";
            drawBlockText(ren_, t, pill.x + (pill.w - blockTextWidth(t, 2.0f * u_)) / 2,
                          pill.y + (pill.h - 7 * 2.0f * u_) / 2, 2.0f * u_,
                          on ? SDL_Color{210, 240, 215, 255} : SDL_Color{170, 175, 185, 255});
        } else if (c.kind == Control::Slider) {
            float val = c.get();
            drawBlockText(ren_, c.label, c.row.x, c.row.y + 4 * u_, fpx, {225, 230, 240, 255});
            if (c.fmt) {
                std::string vs = c.fmt(val);
                drawBlockText(ren_, vs, c.row.x + c.row.w - blockTextWidth(vs, fpx),
                              c.row.y + 4 * u_, fpx, {150, 195, 235, 255});
            }
            // track + fill + handle
            float ty = c.row.y + 22 * u_, th = 7 * u_;
            SDL_FRect track{c.row.x, ty, c.row.w, th};
            SDL_SetRenderDrawColor(ren_, 48, 52, 66, 255); SDL_RenderFillRectF(ren_, &track);
            float frac = std::clamp((val - c.lo) / std::max(0.0001f, c.hi - c.lo), 0.0f, 1.0f);
            SDL_FRect fill{c.row.x, ty, c.row.w * frac, th};
            SDL_SetRenderDrawColor(ren_, 80, 140, 225, 255); SDL_RenderFillRectF(ren_, &fill);
            SDL_FRect handle{c.row.x + c.row.w * frac - 4 * u_, ty - 4 * u_, 8 * u_, th + 8 * u_};
            SDL_SetRenderDrawColor(ren_, 205, 215, 240, 255); SDL_RenderFillRectF(ren_, &handle);
        }
    }
    SDL_RenderSetClipRect(ren_, nullptr);

    // Scrollbar hint (viewport is the panel minus the title and the footer).
    float viewH = panel_.h - titleH - footerH;
    if (contentH_ > viewH) {
        float th = viewH * viewH / contentH_;
        float ty = panel_.y + titleH + (scroll_ / (contentH_ - viewH)) * (viewH - th);
        SDL_SetRenderDrawColor(ren_, 110, 120, 150, 220);
        SDL_FRect bar{panel_.x + panel_.w - 5 * u_, ty, 3 * u_, th}; SDL_RenderFillRectF(ren_, &bar);
    }

    // Footer buttons. BACK is always active; SAVE only when there are unsaved changes
    // (dirty_), and DEFAULTS only when the settings aren't already at their defaults --
    // so each button's enabled state shows whether it would do anything.
    auto button = [&](const SDL_FRect& r, const char* label, bool on) {
        SDL_SetRenderDrawColor(ren_, on ? 60 : 34, on ? 66 : 38, on ? 86 : 46, 255);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, on ? 130 : 70, on ? 140 : 76, on ? 170 : 92, 255);
        SDL_RenderDrawRectF(ren_, &r);
        float bpx = 2.2f * u_;
        drawBlockText(ren_, label, r.x + (r.w - blockTextWidth(label, bpx)) / 2,
                      r.y + (r.h - 7 * bpx) / 2, bpx,
                      on ? SDL_Color{228, 232, 242, 255} : SDL_Color{110, 115, 125, 255});
    };
    button(defaultsRect_, "DEFAULTS", !atDefaults());   // disabled when already default
    button(saveRect_, "SAVE", dirty_);
    button(backRect_, "BACK", true);
}

}  // namespace tak
