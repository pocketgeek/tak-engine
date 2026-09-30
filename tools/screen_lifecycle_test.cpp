#include "client/font.h"
#include "client/loadscreen.h"
#include "client/resultscreen.h"
#include "client/campaignscreen.h"
#include "client/settings.h"
#include "client/gpuvram.h"
#include "hpi/hpi.h"
#include <cstdio>
#include <stdexcept>

static void check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
int main(int argc,char** argv) try {
    if(argc!=2)return 2;
    SDL_SetMainReady();check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)==0,SDL_GetError());
    auto vfs=tak::hpi::mountRetailRoot(argv[1]);
    auto* window=SDL_CreateWindow("screen lifetime",0,0,640,480,SDL_WINDOW_HIDDEN);
    check(window,SDL_GetError());
    // Keep the renderer alive across sessions; SDL's renderer destruction must
    // not hide a missing texture release. Then repeat with a fresh renderer.
    for(int rendererPass=0;rendererPass<2;++rendererPass) {
        auto* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        check(renderer,SDL_GetError());
        const auto bytes=gpuvram::bytes(),count=gpuvram::count();
        for(int i=0;i<3;++i) {
            { tak::LoadScreen screen(renderer,vfs,"Ulasem Arena");screen.draw(); }
            check(gpuvram::bytes()==bytes && gpuvram::count()==count,
                  "loading screen leaves glyphs/textures behind");
            {
                Font font(renderer,vfs,"fonts/b_times new roman (100).gaf");
                check(font.ok(),"retail font absent");
                const auto loaded=gpuvram::bytes();
                font=Font(renderer,vfs,"fonts/b_times new roman (100).gaf");
                check(gpuvram::bytes()==loaded,"font replacement leaks glyphs");
            }
            check(gpuvram::bytes()==bytes && gpuvram::count()==count,"font scope leaks glyphs");
            {
                tak::Settings settings;
                tak::CampaignScreen campaign(renderer,vfs,settings);
                campaign.render(640,480);
            }
            check(gpuvram::bytes()==bytes && gpuvram::count()==count,"campaign screen leaks textures");
            // Missing heading font exercises the results screen's fallback to
            // its body font without giving two owners the same glyph textures.
            auto files=std::make_shared<tak::hpi::Vfs::Files>();
            (*files)["fonts/lombardic (cd).gaf"]={0};
            vfs.setMapFiles(files);
            SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_ESCAPE;
            SDL_PushEvent(&event);
            tak::ResultStats stats;
            for (const char* side : {"ara", "tar", "ver", "zon", "cre"}) {
                tak::ResultRow row; row.side=side; row.colorSlot=int(stats.rows.size());
                stats.rows.push_back(row);
            }
            tak::ResultScreen::run(renderer,vfs,true,"",false,nullptr,nullptr,&stats);
            vfs.setMapFiles({});
            check(gpuvram::bytes()==bytes && gpuvram::count()==count,"results screen leaks textures");
        }
        SDL_DestroyRenderer(renderer);
    }
    SDL_DestroyWindow(window);SDL_Quit();
    std::puts("PASS: repeated loading/results screens, font replacement and renderer lifetimes");
    return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
