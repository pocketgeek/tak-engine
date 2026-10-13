#pragma once
#include "video/stream.h"
#include <SDL.h>
#include <functional>
#include <memory>
namespace tak {
struct Settings;
namespace credstore { class Store; }
constexpr int kStreamingEvent = 0x54414b53;
// Owned by the application; survives transitions between title and game.
// REMEMBER KEY (client/streamkey.h) reads/writes settings->streamRememberKey and calls
// `save` after changing it; without settings it still works for the session. `store`
// defaults to streamKeyStore(); tests inject a fake.
class Streaming {
public:
    explicit Streaming(SDL_Renderer*, Settings* settings = nullptr, std::function<void()> save = {},
                       std::shared_ptr<credstore::Store> store = nullptr);
    ~Streaming();
    video::Stream& stream();
    bool input(const SDL_Event&, int w, int h);
    // Capture BEFORE drawing private setup UI. Never stream the setup panel.
    void frame(int w, int h, bool drawPanel = true);
    void draw(int w, int h);
    bool shown() const;
    // Tests and screenshots: the REMEMBER KEY row, and a wait for its credential-store work.
    SDL_FRect rememberRect(int w, int h);
    bool waitKeyStore(int ms);
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
