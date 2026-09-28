#pragma once
#include <SDL.h>
#include <array>
#include <span>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace tak {
// Render-only impostors for tiny on-screen models. Cache memory and repaint work
// are bounded; unsupported targets/blending and overflow retain full geometry.
class DistantModels {
public:
    struct Item {
        int id=-1;
        uint64_t revision=0;
        float x=0,y=0,zoom=1;
        bool eligible=false;
        std::span<const SDL_Vertex> source;
        std::span<const std::pair<SDL_Texture*,int>> runs;
        SDL_Texture* texture=nullptr;
        std::array<SDL_Vertex,6> quad{};
    };
    void prepare(SDL_Renderer*,std::span<Item>,uint64_t now);
    void clear(); // renderer must still be alive
    size_t used=0, refreshed=0;
private:
    static constexpr int tileSize=64,pageSize=2048,tilesPerPage=1024,maxPages=4;
    struct Entry {
        int slot=-1;
        uint64_t seen=0,painted=0,revision=0;
        uintptr_t source=0;
        float x=0,y=0,w=0,h=0,zoom=1,sx=1,sy=1;
        int pixelsW=0,pixelsH=0;
        bool valid=false;
    };
    std::unordered_map<int,Entry> entries_;
    std::vector<SDL_Texture*> pages_;
    std::vector<int> free_;
    std::vector<SDL_Vertex> scratch_;
    uint64_t frame_=0;
    bool unsupported_=false;
};
}
