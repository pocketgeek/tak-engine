#include "server/roomtick.h"
#include "server/rosterdump.h"
#include "ai/ai.h"
#include "sim/matchsetup.h"
#include "util/devenv.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
namespace tak::srv {
TickResult simulateRoomTick(sim::World& world,const sim::TypeRegistry& registry,
                            std::vector<ai::Controller>& controllers,TickInput input) {
    using namespace net;
    TickResult result;
    world.setSerialThreads(input.multipleGames);
    const auto start=std::chrono::steady_clock::now();
    for(auto& controller:controllers)
        controller.tick(world,input.tick,[&](const Command& command) {input.commands.push_back(command);});
    static const bool report=tak::devEnv("TAK_AIPHASE")!=nullptr;
    if(report) {
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        static const double threshold=tak::devEnv("TAK_AIPHASE_MS")?std::atof(tak::devEnv("TAK_AIPHASE_MS")):8.0;
        if(ms>threshold)std::fprintf(stderr,"AIPHASE tick=%u ai=%.1fms controllers=%zu room=%u\n",input.tick,ms,controllers.size(),input.roomId);
    }
    // Preserve the existing FIFO: immediate humans, AI, delayed humans. The
    // stable player sort retains each player's relative command order.
    input.commands.insert(input.commands.end(),input.scheduled.begin(),input.scheduled.end());
    std::stable_sort(input.commands.begin(),input.commands.end(),
        [](const Command& a,const Command& b) {return a.player<b.player;});
    Writer w;putBundle(w,input.tick,input.commands,input.events);
    if(w.b.size()+2*sizeof(std::vector<uint8_t>)>input.replayBudget) {
        result.resourceLimited=true;return result;
    }
    result.bundle=std::move(w.b);
    for(const auto& command:input.commands)sim::applyCommand(world,registry,command);
    for(const auto& event:input.events)sim::applyEvent(world,event);
    world.tick(1.0f/kServerHz);
#ifndef NDEBUG
    rosterDump(input.roomId,world,controllers);
#endif
    if(input.tick%uint32_t(kHashPeriod)==0) {
        result.hash=world.stateHash();
        if((input.tick/uint32_t(kHashPeriod))%10==0)result.posDigest=world.posDigest();
    }
    result.missionOutcome=world.missionOutcome();
    for(int i=0;i<std::min(kMaxSlots,world.numPlayers());++i)result.defeated[size_t(i)]=world.player(i).defeated;
    return result;
}
}
