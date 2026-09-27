#pragma once
#include "video/stream.h"
#include <SDL.h>
#include <memory>
namespace tak {
constexpr int kStreamingEvent = 0x54414b53;
// Owned by the application; survives transitions between title and game.
class Streaming {
public:
    explicit Streaming(SDL_Renderer*);
    ~Streaming();
    video::Stream& stream();
    bool input(const SDL_Event&, int w, int h);
    // Capture BEFORE drawing private setup UI. Never stream the setup panel.
    void frame(int w, int h, bool drawPanel = true);
    void draw(int w, int h);
    bool shown() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
