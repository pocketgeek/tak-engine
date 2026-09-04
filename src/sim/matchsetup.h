#pragma once

// SDL-free match setup shared by the takview client and the takserver referee
// sim (docs/multiplayer-design.md, M4). Both must build a BIT-IDENTICAL initial
// world -- same terrain, feature nav-blocking, mana deposits, players, teams,
// and monarch spawns -- so their state hashes agree in lockstep.

#include <string>
#include <utility>
#include <vector>

#include "net/protocol.h"   // Command, Cmd, Event
#include "sim/sim.h"

namespace tak::sim {

// Apply one sequenced command to a world (the shared lockstep step used by every
// client AND the server's referee sim, so they mutate identically). Enforces
// per-unit ownership against Command::player.
void applyCommand(World& world, const TypeRegistry& reg, const tak::net::Command& c);

// Apply one sequenced lifecycle event (Forfeit/Leave -> the player's units go
// inert). Symmetric across peers so the sim stays in lockstep.
void applyEvent(World& world, const tak::net::Event& e);


// One player slot in a match (index = sim player id).
struct MatchSlot {
    bool used = false;
    int faction = 0;   // 0 ara, 1 tar, 2 ver, 3 zon, 4 cre
    int team = 0;
};

struct MatchConfig {
    std::string tntPath;     // the map (its .ota sibling holds start positions)
    std::string dataRoot;    // extracted game data
    std::vector<MatchSlot> slots;   // index = player; sized to the player count
    bool gods = false;
    float godAppearSec = 1800;      // when gods may manifest (if enabled)
    float startMana = 2800;
};

// The starting Monarch of each faction (index = faction id).
extern const char* const kMonarchs[5];

// Load the unit registry: MOVEINFO, units, canbuild -- with the Crusades overlay
// loaded first (it wins) and the Iron Plague data if present. Deterministic.
// Returns the IP data root used (empty if none).
std::string setupRegistry(TypeRegistry& reg, const std::string& dataRoot, bool crusades);

// Start positions from the map's .ota (world pixels), ordered by StartPos index.
std::vector<std::pair<float, float>> parseStartPositions(const std::string& tntPath);

// Build the world for a match. Idempotent w.r.t. terrain (setTerrain rebuilds the
// nav grid), so it may run after a client has already loaded the map for render.
// Returns the start position assigned to each USED slot, in slot order (for the
// camera). Every peer that calls this with the same config gets the same world.
std::vector<std::pair<float, float>> setupMatch(World& world, const TypeRegistry& reg,
                                                const MatchConfig& cfg);

}  // namespace tak::sim
