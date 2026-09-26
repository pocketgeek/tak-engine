#pragma once

#include <SDL.h>
#include <memory>
#include <functional>
#include <string>

namespace tak {
namespace hpi { class Vfs; }
struct Settings;

// Retail's BOD book, chapter illustration and previous/next/play controls.
// Every installed chapter remains selectable; completion suggests a starting page.
class CampaignScreen {
public:
    CampaignScreen(SDL_Renderer*, const hpi::Vfs&, const Settings&, int initialTab=0,
                   std::function<void(const std::string&)> playSound={});
    ~CampaignScreen();
    bool input(const SDL_Event&, int winW, int winH);
    void render(int winW, int winH);
    bool picked() const;
    const std::string& pickedStem() const;
    const std::string& pickedCampaign() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};
} // namespace tak
