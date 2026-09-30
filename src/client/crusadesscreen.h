#pragma once
#include <SDL.h>
#include <memory>
#include <cstdint>
namespace tak {
namespace hpi { class Vfs; }
namespace net { class MpClient; }
// Strategic campaign UI only. The owner pumps MpClient and retains room flow.
class CrusadesScreen {
public:
    enum class Action { None, Back, Reconnect };
    CrusadesScreen(SDL_Renderer*, const hpi::Vfs&, net::MpClient&);
    ~CrusadesScreen();
    CrusadesScreen(const CrusadesScreen&)=delete;
    CrusadesScreen& operator=(const CrusadesScreen&)=delete;
    void update();
    void draw(int logicalWidth=960, int logicalHeight=540);
    Action input(const SDL_Event&, int logicalMouseX, int logicalMouseY);
    uint32_t selectedTerritory() const;
    void selectTerritory(uint32_t id);
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};
} // namespace tak
