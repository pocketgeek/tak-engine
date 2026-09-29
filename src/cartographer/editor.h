#pragma once
#include <SDL.h>
#include <functional>

namespace cart {
// The frame hook drives real SDL input in workflow tests. It is empty in the app.
int runEditor(int argc,char** argv,
              const std::function<void(SDL_Window*,SDL_Renderer*,int)>& frameHook = {});
}
