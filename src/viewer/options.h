#pragma once

// A self-contained Options overlay, hosted by BOTH the main menu (MainMenu) and the
// in-game Esc menu (GameView). It edits a tak::Settings live and calls onChange()
// after every change so the host can apply it (audio / window / camera). It owns no
// game state; volumes/speeds/scale are all local display prefs (never hashed).

#include <SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "viewer/settings.h"

namespace tak {

// The process-wide output-channel count (1/2/4/6/8), probed once and cached. Shared
// by SoundBank (which requests it) and the Options per-speaker sliders, so they
// always agree even when PipeWire's default advises stereo on a surround rig.
int detectOutputChannels();

class OptionsScreen {
public:
    // ren: the app renderer. s: the settings to edit (mutated in place). onChange:
    // invoked after any value change so the host applies it live. onSave: invoked when
    // the user clicks SAVE (persist to disk). audioChannels: number of output channels
    // to expose per-channel sliders for (0 = auto-detect the default device).
    OptionsScreen(SDL_Renderer* ren, Settings& s, std::function<void()> onChange,
                  std::function<void()> onSave, int audioChannels = 0);

    // Feed one SDL event. Returns true when the user leaves (Esc / BACK); the host
    // should then stop showing the screen. Persisting is explicit (the SAVE button
    // fires onSave); BACK does not save.
    bool input(const SDL_Event& e, int winW, int winH);

    // Draw the overlay (call after the host's own frame so it sits on top).
    void render(int winW, int winH);

private:
    struct Control {
        enum Kind { Section, Slider, Toggle } kind;
        std::string label;
        float lo = 0, hi = 1;
        std::function<float()> get;
        std::function<void(float)> set;
        std::function<std::string(float)> fmt;   // value -> display text
        SDL_FRect row{};                          // filled by layout()
    };

    void build(int audioChannels);
    void layout(int winW, int winH);
    void commit(Control& c, float mx);            // set a slider from a mouse x

    // True when every setting DEFAULTS would reset already equals its default value
    // (playerName / lastMap are preserved by DEFAULTS, so they're excluded).
    bool atDefaults() const {
        Settings d;
        d.playerName = s_.playerName;
        d.lastMap = s_.lastMap;
        return s_ == d;
    }

    SDL_Renderer* ren_;
    Settings& s_;
    std::function<void()> onChange_;
    std::function<void()> onSave_;
    std::vector<Control> ctls_;
    bool dirty_ = false;    // unsaved changes since the last save/open -> SAVE enabled
    float scroll_ = 0;      // content scroll offset (px)
    float contentH_ = 0;    // total laid-out content height
    int drag_ = -1;         // index of the slider being dragged, or -1
    float u_ = 1.0f;        // layout unit (scaled to window)
    SDL_FRect panel_{};     // the panel rect (for scroll clamping)
    SDL_FRect defaultsRect_{}, saveRect_{}, backRect_{};   // footer buttons (filled by layout())
};

}  // namespace tak
