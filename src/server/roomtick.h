#pragma once
#include "net/protocol.h"
#include <array>
#include <optional>
#include <vector>
namespace tak::sim {class World;class TypeRegistry;}
namespace tak::ai {class Controller;}
namespace tak::srv {
struct TickInput {
    uint32_t tick=0,roomId=0;
    std::vector<net::Command> commands,scheduled;
    std::vector<net::Event> events;
    uint64_t replayBudget=0;
    bool multipleGames=false;
};
struct TickResult {
    std::vector<uint8_t> bundle;
    std::optional<uint64_t> hash;
    std::array<bool,net::kMaxSlots> defeated{};
    int missionOutcome=0;
    bool resourceLimited=false;
};
// Mutates only this game's simulation. No sockets, Room, account store or logs.
TickResult simulateRoomTick(sim::World& world,const sim::TypeRegistry& registry,
                            std::vector<ai::Controller>& controllers,TickInput input);
}
