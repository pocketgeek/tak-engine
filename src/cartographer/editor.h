#pragma once
#include <SDL.h>
#include <functional>
#include <vector>

namespace cart {
// Dependency injection for recovery timing and the native startup prompt.
// Production callers leave these empty; integration tests use real files/jobs.
struct EditorHooks {
    std::function<uint64_t()> recoveryClock;
    std::function<int()> recoveryChoice;
    std::function<void(const std::vector<uint8_t>&)> minimapUpdated;
};
// The frame hook drives real SDL input in workflow tests. It is empty in the app.
int runEditor(int argc,char** argv,
              const std::function<void(SDL_Window*,SDL_Renderer*,int)>& frameHook = {}, const EditorHooks& hooks = {});
}
