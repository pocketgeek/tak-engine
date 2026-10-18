#include "net/matchconfig.h"

#include "ai/ai.h"   // incomeMultFor / difficultyFromLevel (inline)
#include "version.h"

namespace tak::net {

sim::MatchConfig matchConfigFor(const GameOptions& o, const SlotInfo* slots, uint32_t seed, bool mission) {
    sim::MatchConfig cfg;
    cfg.unitCap = o.unitCap;
    cfg.monarchExpendable = o.monarchExpendable != 0;
    cfg.doubleSight = !mission && o.doubleSight != 0;
    cfg.pathfindingMode = mission ? sim::PathfindingMode::Retail : o.pathfindingMode;
    cfg.stressTest = o.stressTest != 0;
    cfg.stressImmediate = o.stressTest == kStressImmediate;   // recorded before staging
    cfg.benchmark = o.benchmark;
    cfg.startMode = sim::StartMode(normalizeStartMode(o.randomStarts));
    cfg.mapScenario = !mission && o.mapScenario != 0;   // 259: the map's own scenario
    cfg.startSeed = seed;   // the seed sent in GameStarting
    int maxSlot = 0;
    for (int i = 0; i < kMaxSlots; ++i)
        if (slots[i].type == 1 || slots[i].type == 2) maxSlot = i;
    cfg.slots.resize(size_t(maxSlot + 1));
    for (int i = 0; i <= maxSlot; ++i) {
        const SlotInfo& s = slots[i];
        const auto level = ai::difficultyFromLevel(s.aiLevel);
        const float mm = s.type == 2 ? ai::incomeMultFor(level) : 1.0f;
        cfg.slots[size_t(i)] = {s.type == 1 || s.type == 2, s.faction % 5, s.team, mm, s.type == 2,
                                s.type == 2 && ai::defensiveLevel(level)};
    }
    return cfg;
}

ReplayHeader replayHeaderFor(const GameOptions& o, const std::string& mapId, const std::string& mission,
                             const SlotInfo* startSlots, uint32_t seed) {
    ReplayHeader h;
    h.mapId = mapId;
    h.mission = mission;
    h.engineVersion = tak::kVersion;
    h.crusades = o.crusades;
    h.forfeitSelfDestruct = o.forfeitSelfDestruct;
    h.overridePolicy = o.overridePolicy;
    h.unitCap = o.unitCap;
    h.monarchExpendable = o.monarchExpendable;
    h.doubleSight = o.doubleSight;
    h.pathfindingMode = o.pathfindingMode;
    h.stressTest = o.stressTest;
    h.randomStarts = o.randomStarts;
    h.benchmark = o.benchmark;
    h.mapScenario = o.mapScenario;
    h.seed = seed;
    for (int i = 0; i < kMaxSlots; ++i) {
        const SlotInfo& s = startSlots[i];   // start config, not the forfeited end state
        h.slotType[i] = s.type;
        h.slotFaction[i] = s.faction;
        h.slotColor[i] = s.color;
        h.slotTeam[i] = s.team;
        h.slotAiLevel[i] = s.aiLevel;
    }
    return h;
}

void matchInputsFrom(const ReplayHeader& h, GameOptions& o, SlotInfo* slots) {
    o = GameOptions{};
    o.crusades = h.crusades;
    o.forfeitSelfDestruct = h.forfeitSelfDestruct;
    o.overridePolicy = h.overridePolicy;
    o.unitCap = uint16_t(h.unitCap);
    o.monarchExpendable = h.monarchExpendable;
    o.doubleSight = h.doubleSight;
    o.pathfindingMode = h.pathfindingMode;
    o.stressTest = h.stressTest;
    o.randomStarts = h.randomStarts;
    o.benchmark = h.benchmark;
    o.mapScenario = h.mapScenario;
    for (int i = 0; i < kMaxSlots; ++i) {
        // Every field explicitly: a header without AI levels (format < 6) reads 0,
        // as the loader always did, not SlotInfo's default of 2.
        slots[i] = SlotInfo{};
        slots[i].type = h.slotType[i];
        slots[i].faction = h.slotFaction[i];
        slots[i].color = h.slotColor[i];
        slots[i].team = h.slotTeam[i];
        slots[i].aiLevel = h.slotAiLevel[i];
    }
}

}  // namespace tak::net
