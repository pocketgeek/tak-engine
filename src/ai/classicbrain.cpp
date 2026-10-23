#include "ai/classicbrain.h"

#include "ai/classicdefence.h"
#include "ai/classicproduction.h"
#include "sim/footprint.h"
#include "sim/retailai.h"
#include "sim/retailreach.h"
#include "sim/sim.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>

#ifndef NDEBUG
#include <cctype>
#include <cstdio>
#endif

namespace tak::ai {

namespace {
// A unit's first mission (retail unit +0x60) as the AI reads it, from its orders
// (docs/classic-ai-plan.md U1): the class byte of the mission's descriptor
// (descriptor +0x10: 1 the attack kinds, 2 AirStrike, 0 MobileBuild, 6 HelpBuild
// and RepairUnit, 19 BuildingBuild ...), the flags it copies from the
// descriptor's +0x11 (8 on MobileBuild, HelpBuild and BuildingBuild; 0x200 a
// target unit, 0x400 a goal point), its target and its goal. Run-time flag
// 0x2000 marks the type's default mission, which the mission stepper (0x4d8597
// -> 0x4d83b0) pushes onto an AI or human unit left with none, and any mission
// later pushed in front of it (0x4d7709 copies the bit): our empty queue, and a
// queue holding only the World's own auto-attack (and its return leg) on top of
// one.
struct FirstMission {
    bool any = false;
    uint8_t cls = 0;
    uint32_t flags = 0;
    bool buildingBuild = false;
    const tak::sim::UnitType* builds = nullptr;   // what its build mission makes
    int target = 0;
    int32_t goalX = 0, goalZ = 0;
};
FirstMission firstMission(const tak::sim::World& world, const tak::sim::Unit& u) {
    FirstMission m;
    if (u.type->isStructure()) {
        // A factory at work has a BuildingBuild mission per queued unit.
        if (!u.buildQueue.empty()) { m.any = true; m.cls = 19; m.flags = 0x8010c; m.buildingBuild = true; m.builds = u.buildQueue.front(); }
        return m;
    }
    if (u.buildSiteId) {
        const auto* site = world.unit(u.buildSiteId);
        m.any = true; m.cls = 0; m.flags = 0x80508; m.builds = site ? site->type : nullptr;
        if (site) { m.goalX = site->x.v; m.goalZ = site->z.v; }
        return m;
    }
    if (u.orders.empty()) return m;
    // The current order is the first issued goal: path waypoints the sim splices
    // in ahead of it belong to it (retail's mission navigates on its own).
    size_t leg = u.orders.size() - 1;
    for (size_t k = 0; k < u.orders.size(); ++k)
        if (u.orders[k].goal) { leg = k; break; }
    const auto& o = u.orders[leg];
    m.any = true;
    m.goalX = o.x.v; m.goalZ = o.z.v;
    if (o.buildType) { m.cls = 0; m.flags = 0x80508; m.builds = o.buildType; m.goalX = o.buildX.v; m.goalZ = o.buildZ.v; }
    else if (o.repairTarget) { m.cls = 6; m.flags = 0x80200; m.target = o.repairTarget; }
    else if (o.reclaimFeat || o.reclaimArea) { m.cls = 11; m.flags = 0x800; }
    else if (o.load) { m.cls = 12; m.flags = 0x1000200; m.target = o.targetId; }
    else if (o.unload) { m.cls = 13; m.flags = 0x1000400; }
    else if (o.guard) { m.cls = 5; m.flags = 0x3000200; m.target = o.targetId; }
    else if (o.targetId) { m.cls = 1; m.flags = 0x1000280; m.target = o.targetId; }
    else if (o.patrol) { m.cls = 7; m.flags = 0x3000410; }
    else { m.cls = 14; m.flags = 0x3000400; }   // a move
    // Only the World's auto-attacks (and the return move behind a leashed one):
    // they sit on the default mission, so they carry its 0x2000.
    bool automatic = true, leash = false;
    for (const auto& q : u.orders) {
        if (!q.goal) continue;
        if (q.autoTarget) { leash = q.chaseLeashed; continue; }
        if (leash && !q.targetId && !q.patrol && !q.buildType && !q.guard && !q.load && !q.unload) { leash = false; continue; }
        automatic = false;
        break;
    }
    if (automatic) m.flags |= 0x2000;
    return m;
}
const tak::sim::Weapon* currentWeapon(const tak::sim::Unit& u) {
    const auto& w = u.type->weapons;
    if (w.empty()) return nullptr;
    const int slot = u.type->weaponSwitching ? std::clamp(u.weaponSlot, 0, int(std::min<size_t>(3, w.size())) - 1) : 0;
    return &w[size_t(slot)];
}
}  // namespace

ClassicBrain::ClassicBrain(int player, uint32_t roomSeed)
    : player_(player), seed_(seedFor(player, roomSeed)), profile_(catalogue_.size()),
      info_(catalogue_.size()), menu_(catalogue_.size()), count_(catalogue_.size()),
      completed_(catalogue_.size()), priority_(catalogue_.size()) {
    classicConstructSquads(squads_, 0, [&](int n) { return rand(n); });
    grids_ = classicDrawBlockGrids([&](int n) { return rand(n); });
}

ClassicBrain::ClassicBrain(int player, uint32_t roomSeed, const tak::sim::TypeRegistry& registry,
                           int32_t largestEnemyPopulation)
    : player_(player), seed_(seedFor(player, roomSeed)),
      catalogue_(ClassicCatalogue::from(registry)), profile_(catalogue_.size()),
      info_(catalogue_.size()), menu_(catalogue_.size()), count_(catalogue_.size()),
      completed_(catalogue_.size()), priority_(catalogue_.size()) {
    // 0x4f6660: the AI manager (0x40fed0) before the build manager (0x4121e0),
    // whose constructor draws the block grids (0x4110f0) before its creation pass.
    classicConstructSquads(squads_, largestEnemyPopulation, [&](int n) { return rand(n); });
    grids_ = classicDrawBlockGrids([&](int n) { return rand(n); });
    for (size_t t = 1; t < catalogue_.size(); ++t) {
        info_[t] = ClassicTypeInfo::from(*catalogue_.types[t], &registry);
        index_.emplace(catalogue_.types[t], uint16_t(t));
    }
    // 0x4c1d35: an FBI builder type's menu is CanBuild\<unitname>\*.tdf, each file
    // name looked up as a unit name (unknown ones dropped), sorted by [Menu]
    // Priority. The registry's build tree is already in that order, ties by name
    // (retail keeps ties in its archive listing's order, assumed to be by name;
    // the order only maps draws to types, not the odds).
    for (size_t t = 1; t < catalogue_.size(); ++t) {
        const auto* type = catalogue_.types[t];
        if (!type || !info_[t].builder) continue;
        const std::string& name = type->classicFbi.unitName.empty() ? type->id : type->classicFbi.unitName;
        for (const auto& entry : registry.buildable(name))
            if (const auto it = catalogue_.byName.find(entry); it != catalogue_.byName.end())
                menu_[t].push_back(it->second);
    }
    creationPass();
}

void ClassicBrain::creationPass() {
    // The build manager computes its priorities once as it is created at game
    // start (0x4102f0 -> 0x410810), before the profile file is read and before
    // the player has units or a store: every limit is still -1, every count and
    // the population 0, the store empty (fill 0) with an empty income history,
    // which reads as a shortfall. Reproduced exactly against a captured retail
    // PrintWeights table (tools/re/check_ai_printweights.py, Taros at tick 16).
    const tak::sim::RetailResourceHistory none;
    const bool shortfall = none.shortfall(0.0f);
    for (size_t t = 1; t < catalogue_.size(); ++t)
        priority_[t] = tak::sim::retailBuildPriority(info_[t].priorityInputs(0, 0, -1), 0, storeFill(0, 0),
                                                     shortfall, [&](int n) { return rand(n); });
    ++refreshes_;
}

void ClassicBrain::useProfile(const ClassicProfileScript& script) {
    profile_.reset(catalogue_.size());   // as retail's ReloadAIProfiles does first
    profile_.execute(script.text, catalogue_);
    profile_.path = script.path;
}

int ClassicBrain::buildWeight(size_t t) const {
    if (t == 0 || t >= priority_.size()) return 0;
    // An AI seat always has its limits on (retail player+0xe3 = 1 in skirmish).
    return tak::sim::retailAiBuildWeight(true, profile_.limit[t], count_[t], profile_.pref[t],
                                         int8_t(priority_[t]));
}

void ClassicBrain::recount(const tak::sim::World& world) {
    std::fill(count_.begin(), count_.end(), int16_t(0));
    std::fill(completed_.begin(), completed_.end(), int16_t(0));
    for (const auto& u : world.units()) {
        if (u.player != player_ || !u.alive() || !u.type) continue;
        const auto it = index_.find(u.type);
        if (it == index_.end()) continue;
        ++count_[it->second];
        if (!u.underConstruction) ++completed_[it->second];
    }
}

void ClassicBrain::recomputePriorities(const tak::sim::World& world) {
    // The owner's live unit count (player+0xe8) and its store: fill = stored /
    // capacity (0 under 0.01 capacity), shortfall from the 29-of-30-tick income
    // and spend history the World keeps for every player (displayResources).
    unsigned population = 0;
    for (const auto& u : world.units())
        if (u.player == player_ && u.alive() && u.type) ++population;
    const auto& me = world.player(player_);
    const float stored = float(me.mana);
    const float capacity = me.manaCapacity();
    const float ratio = storeFill(stored, capacity);
    const bool shortfall = me.displayResources.shortfall(stored);
    for (size_t t = 1; t < catalogue_.size(); ++t) {
        const auto in = info_[t].priorityInputs(count_[t], completed_[t], profile_.limit[t]);
        priority_[t] = tak::sim::retailBuildPriority(in, uint16_t(population), ratio, shortfall,
                                                     [&](int n) { return rand(n); });
    }
    ++refreshes_;
}

void ClassicBrain::bookkeeping(const tak::sim::World& world, uint32_t simTick) {
    if (tak::sim::retailTickPlayerCache(clock_, simTick, [&](int n) { return rand(n); }))
        recomputePriorities(world);
    recount(world);
}

void ClassicBrain::setStart(int index, float x, float z) {
    if (index < 0) return;
    // 0x4e0810: the start's 16.16 position shifted down to its 16-pixel cell.
    auto cell = [](float v) { return int16_t(uint32_t(int32_t(std::lround(double(v) * 65536.0))) >> 20); };
    if (squads_.starts.size() <= size_t(index)) squads_.starts.resize(size_t(index) + 1);
    squads_.starts[size_t(index)] = ClassicCell{cell(x), cell(z)};
    startIndex_ = index;
}

std::vector<ClassicSquadUnit> ClassicBrain::squadUnits(const tak::sim::World& world) {
    std::vector<ClassicSquadUnit> units;
    std::unordered_set<int> own;
    for (const auto& u : world.units()) {
        if (u.player != player_ || !u.type) continue;
        own.insert(u.id);
        const auto* t = u.type;
        const auto it = index_.find(t);
        const ClassicTypeInfo* info = it == index_.end() ? nullptr : &info_[it->second];
        ClassicSquadUnit s;
        s.id = u.id;
        const bool structure = t->isStructure();
        const bool armed = info ? info->armed : !t->weapons.empty();
        // The port's readings of the unit bits (docs/classic-ai-plan.md U6): a unit
        // in the World is a live slot, a dead one still listed is dying; a finished
        // unit is selectable (retail sets that once it has left its factory).
        s.flags = ClassicSquadUnit::kLive | (u.alive() ? 0u : ClassicSquadUnit::kDying) |
                  (u.underConstruction ? 0u : ClassicSquadUnit::kSelectable) |
                  (structure ? ClassicSquadUnit::kStructure : 0u) | (armed ? ClassicSquadUnit::kArmed : 0u);
        s.commander = info ? info->commander : t->commander;
        s.sacredSite = info && info->manaSite;
        s.builder = info && info->builder;
        s.finished = !u.underConstruction;
        s.carried = u.embarked();
        s.mover = !structure;
        s.type = it == index_.end() ? 0 : it->second;
        s.menu = int32_t(menu(s.type).size());
        // A factory's BuildingBuild mission is our production queue. The first
        // mission's class (U1) matters only to the constructor planner (P4b).
        s.producing = !u.buildQueue.empty();
        s.x = u.x.v; s.z = u.z.v;
        s.footX = int16_t(t->footX); s.footZ = int16_t(t->footZ);
        s.squad = squadOf(u.id);
        s.typeFlags = (info && info->builder ? 0x100u : 0u) | (t->canFly ? 0x800u : 0u) |
                      (armed ? 0x10000u : 0u) | (t->floater ? 0x80000u : 0u);
        s.secondaryFlags = (s.commander ? 0x40000u : 0u) | (s.sacredSite ? 0x80000000u : 0u);
        s.minWaterDepth = info ? info->minWaterDepth : int16_t(-10000);
        const FirstMission m = firstMission(world, u);
        s.mission = m.any; s.missionClass = m.cls; s.missionFlags = m.flags; s.buildingBuild = m.buildingBuild;
        s.missionTarget = m.target; s.goalX = m.goalX; s.goalZ = m.goalZ;
        if (m.builds && (m.flags & 8))
            if (const auto bt = index_.find(m.builds); bt != index_.end()) s.buildsManaSite = info_[bt->second].manaSite;
        if (const auto* w = currentWeapon(u)) {
            // 5307e9 stores the reload as a 16-bit tick count.
            s.weaponDamage = uint16_t(w->damage);
            s.weaponReload = uint16_t(int32_t(double(w->reload) * 30.0));
        }
        units.push_back(s);
    }
    // A member that is no longer the player's unit (removed from the World, or
    // given away) leaves its squad the way set-squad removes one.
    for (auto& squad : squads_.squads)
        for (size_t k = 0; k < squad.members.size();)
            if (!own.count(squad.members[k])) { squad.members[k] = squad.members.back(); squad.members.pop_back(); }
            else ++k;
    std::erase_if(squadOf_, [&](const auto& e) { return !own.count(e.first); });
    return units;
}

void ClassicBrain::scheduler(const tak::sim::World& world, uint32_t simTick,
                             const std::function<void(const tak::net::Command&)>& sink) {
    std::vector<ClassicSquadUnit> units;
    bool snapshot = false;
    auto random = [&](int n) { return rand(n); };
    auto command = [&](tak::net::Cmd kind, int unitId) {
        tak::net::Command c;
        c.kind = kind; c.player = uint8_t(player_); c.unitId = unitId; c.targetId = 0;
        sink(c);
    };
    tak::sim::retailTickAiSchedule(squads_.schedule, simTick, random,
        [&] {   // 0x40fe80
            units = squadUnits(world); snapshot = true;
            classicDropDying(squads_, units);
        },
        [&] {   // 0x40eeb0
            const ClassicAssignContext ctx{simTick, scenarioMode_, startIndex_};
            classicAssign(squads_, units, ctx,
                [&](size_t i) { command(tak::net::Cmd::Stance, units[i].id); },   // 0x5198a0(u, 2)
                [&](size_t i) { command(tak::net::Cmd::Stop, units[i].id); });    // 0x4d6a50(u, 0)
        },
        [&](unsigned slot, auto& clock) {
            // Each kind's think reschedules itself (and an empty strike or VTOL
            // squad goes inactive). An occupied base runs its member loop; strike
            // recruitment (0x40abd0) is P6, and the other squads' plans come in P5-P7.
            tak::sim::retailTickAiSquad(squads_.squads[slot], clock, simTick, random, [](auto&) {},
                [&](auto& squad) {
                    if (squad.kind == tak::sim::RetailAiSquadKind::Base) baseThink(world, int(slot), units, sink);
                });
        });
    if (!snapshot) return;
    for (const auto& u : units) {
        if (u.squad) squadOf_[u.id] = u.squad;
        else squadOf_.erase(u.id);
    }
}

uint16_t ClassicBrain::population(const tak::sim::World& world) const {
    unsigned n = 0;
    for (const auto& u : world.units())
        if (u.player == player_ && u.alive() && u.type) ++n;
    return uint16_t(n);
}

void ClassicBrain::baseThink(const tak::sim::World& world, int slot, std::vector<ClassicSquadUnit>& units,
                             const std::function<void(const tak::net::Command&)>& sink) {
    classicBaseMembers(squads_, units, slot,
        // 0x412000: a unit of the visible-enemy list inside the ring-0 radius.
        [&](const std::pair<int32_t, int32_t>& centre, int32_t radius) {
            return classicThreat(visibleEnemies(world), centre, radius);
        },
        [&](size_t i) { planFactory(world, units[i], sink); },
        [&](size_t i, bool danger) { planConstructor(world, slot, units, i, danger, sink); },
        [&](size_t i, bool danger) { planCombat(world, slot, units, units[i], danger, false, sink); });
}

void ClassicBrain::planFactory(const tak::sim::World& world, ClassicSquadUnit& unit,
                               const std::function<void(const tak::net::Command&)>& sink) {
    auto random = [&](int n) { return rand(n); };
    const auto* builder = unit.type ? catalogue_.types[unit.type] : nullptr;
    if (!builder) return;
    // Our World's per-player construction allocation is retail's: the store over
    // the tick's full-rate construction demand, capped at 1 (sim.cpp, 0x51d837).
    const float allocation = float(world.player(player_).constructionAllocation);
    classicPlanFactory(allocation, unit.producing, population(world), menu(unit.type),
        [&](uint16_t t) { return buildWeight(t); },
        [&](uint16_t t) { return info_[t].lodestoneClass; },
        [&](uint16_t t) { return catalogue_.types[t] && catalogue_.types[t]->side == builder->side; },
        [&](uint16_t t) { return info_[t].cost; }, random,
        [&](uint16_t t) {
            tak::net::Command c;
            c.kind = tak::net::Cmd::Train; c.player = uint8_t(player_);
            c.unitId = unit.id; c.targetId = 1;
            std::strncpy(c.type, catalogue_.types[t]->id.c_str(), sizeof c.type - 1);
            sink(c);
            ++queues_;
            // Its first mission is now that BuildingBuild (firstMission's reading).
            unit.producing = unit.mission = unit.buildingBuild = true;
            unit.missionClass = 19; unit.missionFlags = 0x8010c;
            unit.buildsManaSite = info_[t].manaSite;
        });
}

void ClassicBrain::readSites(const tak::sim::World& world) {
    sitesRead_ = true;
    // 0x4e0810's sacred-site table: one record per sacred feature, its anchor
    // cell, in the map's scan order (the World's list is built the same way).
    const auto& cells = world.mapPlacementCells();
    const bool plane = !cells.empty() && cells.size() == size_t(world.mapW()) * size_t(world.mapH());
    sacredTypes_.assign(plane ? world.mapPlacementTypes().size() : 0, false);
    for (const auto& site : world.sacredSites()) {
        sites_.push_back(ClassicCell{int16_t(site.x), int16_t(site.z)});
        sacredRects_.push_back({ClassicCell{int16_t(site.x), int16_t(site.z)}, ClassicCell{int16_t(site.fx), int16_t(site.fz)}});
        // The feature type at the anchor is a sacred one (sacredsite != 0).
        if (plane && site.x >= 0 && site.z >= 0 && site.x < world.mapW() && site.z < world.mapH()) {
            const uint16_t f = cells[size_t(site.z) * size_t(world.mapW()) + size_t(site.x)].feature;
            if (f < sacredTypes_.size()) sacredTypes_[f] = true;
        }
    }
}

bool ClassicBrain::sacredCell(const tak::sim::World& world, int x, int z) const {
    const auto& cells = world.mapPlacementCells();
    const int w = world.mapW(), h = world.mapH();
    if (cells.empty() || cells.size() != size_t(w) * size_t(h)) {
        // A World without a feature plane (synthetic tests): the sites' footprints.
        for (const auto& [at, foot] : sacredRects_)
            if (x >= at.first && z >= at.second && x < at.first + foot.first && z < at.second + foot.second) return true;
        return false;
    }
    return classicSacredCell(x, z, uint32_t(world.mapPlacementTypes().size()),
        [&](int cx, int cz) {
            if (cx < 0 || cz < 0 || cx >= w || cz >= h) return ClassicFeatureCell{};
            const auto& c = cells[size_t(cz) * size_t(w) + size_t(cx)];
            return ClassicFeatureCell{c.feature, c.backX, c.backZ};
        },
        [&](uint16_t f) { return f < sacredTypes_.size() && sacredTypes_[f]; });
}

const std::vector<uint8_t>& ClassicBrain::staticBlocked(const tak::sim::World& world, const tak::sim::NavGrid& nav,
                                                        int footX, int footZ) {
    for (const auto& p : staticPlanes_)
        if (p.nav == &nav && p.footX == footX && p.footZ == footZ) return p.blocked;
    // The grid's static bit is set at map load only (classicStaticPlane), so the
    // buildings and units that come later never count. A cell's own rating is
    // the World's search grade without bodies (searchCellGrade: the map feature,
    // then the terrain); without a feature plane (synthetic Worlds) the terrain
    // grade counts the obstacle overlay, as the World's does.
    const auto& cells = world.mapPlacementCells();
    const bool features = !cells.empty() && cells.size() == size_t(nav.width()) * size_t(nav.height());
    staticPlanes_.push_back({&nav, footX, footZ,
        classicStaticPlane(nav.width(), nav.height(), footX, footZ, [&](int x, int z) {
            return features ? world.mapFeatureGrade(x, z) == 0 || !nav.terrainWalkable(x, z) : !nav.walkable(x, z);
        })});
    return staticPlanes_.back().blocked;
}

void ClassicBrain::planConstructor(const tak::sim::World& world, int slot, std::vector<ClassicSquadUnit>& units,
                                   size_t i, bool threat, const std::function<void(const tak::net::Command&)>& sink) {
    ClassicSquadUnit& member = units[i];
    const auto* unit = world.unit(member.id);
    const auto* builder = member.type ? catalogue_.types[member.type] : nullptr;
    if (!unit || !unit->type || !builder) return;
    if (!sitesRead_) readSites(world);
    const detail::ClassicUnitIndex index(units);
    auto random = [&](int n) { return rand(n); };
    const auto centre = classicBaseCentre(squads_, units, index, slot);
    if (!centre) return;   // the base think stops before its members without one

    ClassicConstructorUnit u;
    u.commander = member.commander;
    u.hp = int16_t(unit->hp.floorInt());
    u.maxDamage = int32_t(unit->type->maxHp);
    u.mana = float(unit->mana);
    u.maxMana = unit->type->maxMana;
    if (const auto* w = currentWeapon(*unit)) {
        // 0x531ea0: mana per shot over the unit's cost multiplier (0x5193c0).
        u.weapon = true;
        u.shotCost = classicShotCost(w->manaCost, unit->type->noVeteran ? 1.0f : unit->vetMul());
    }
    u.mission = member.mission;
    u.missionClass = member.missionClass;
    u.building = member.mission && (member.missionFlags & 8);
    u.navigator = !unit->type->canFly;

    auto siteType = [&](uint16_t t) {
        const ClassicTypeInfo& f = info_[t];
        ClassicSiteType s;
        s.footX = int16_t(f.footX); s.footZ = int16_t(f.footZ); s.minWaterDepth = f.minWaterDepth;
        s.tower = classicSiteTower(uint8_t(f.bmcode), f.armed, f.firstWeaponRange, int32_t(menu(t).size()));
        s.lodestone = f.lodestoneClass;
        return s;
    };
    struct Host {
        ClassicBrain& b;
        const tak::sim::World& world;
        const std::vector<ClassicSquadUnit>& units;
        const detail::ClassicUnitIndex& index;
        int slot;
        ClassicSquadUnit& member;
        const tak::sim::Unit& unit;
        const tak::sim::UnitType& builder;
        std::pair<int32_t, int32_t> centre;
        decltype(random)& rnd;
        decltype(siteType)& placeType;
        const std::function<void(const tak::net::Command&)>& sink;

        float allocation() const { return float(world.player(b.player_).constructionAllocation); }
        int32_t firepower() const { return classicBaseFirepower(b.squads_, units, index, slot, 4); }
        int32_t builders() const { return classicBaseCount(b.squads_, units, index, slot, 2); }
        int32_t desiredBuilders() const { return b.squads_.squads[size_t(slot)].parameters[1]; }
        uint32_t tick() const { return world.tickCount(); }
        uint32_t alarm() const { return world.player(b.player_).aiThreatUntil; }
        uint16_t draw(bool special) {
            return classicMenuDraw(b.menu(member.type), special,
                [&](uint16_t t) { return b.buildWeight(t); },
                [&](uint16_t t) { return b.info_[t].lodestoneClass; },
                [&](uint16_t t) { return b.catalogue_.types[t] && b.catalogue_.types[t]->side == builder.side; },
                rnd);
        }
        ClassicConstructorType type(uint16_t t) const {
            const ClassicTypeInfo& f = b.info_[t];
            return {uint8_t(f.bmcode), int32_t(b.menu(t).size()), f.armed};
        }
        bool idleFactory(uint16_t t) const { return classicIdleFactory(b.squads_, units, index, slot, t); }
        int32_t typeCount(uint16_t t) const { return t >= 1 && t < b.count_.size() ? b.count_[t] : 0; }
        std::optional<int32_t> radius(unsigned ring) const {
            return classicBaseRadius(b.squads_, units, index, slot, ring);
        }
        std::optional<std::pair<int32_t, int32_t>> site(uint16_t t, int32_t r) {
            const ClassicSiteType s = placeType(t);
            const auto* type = b.catalogue_.types[t];
            auto valid = [&](int16_t x, int16_t z) {
                // 0x507d10(type, 0, cell, 1, 0): the World's placement test at the
                // footprint's centre.
                const auto c = classicSiteCentre({x, z}, s.footX, s.footZ);
                return type && world.canPlace(type, float(c.first) / 65536.0f, float(c.second) / 65536.0f, b.player_);
            };
            return classicPlaceSite(s,
                [&] {
                    return classicDepositSite(b.sites_, centre, r, s.footX, s.footZ,
                        [&](int16_t x, int16_t z) { return b.sacredCell(world, x, z); }, valid);
                },
                [&] {
                    return classicScatterSite(b.grids_, centre, r, s, rnd, valid,
                        [&](int x, int z) { return b.sacredCell(world, x, z); });
                });
        }
        bool reachable(uint16_t t, std::pair<int32_t, int32_t> at) const {
            // 0x4e1570 from the builder's cell (1 x 1) to the site's footprint grown
            // by its builddistance, over the grid's static bit (mode 1).
            const ClassicTypeInfo& f = b.info_[t];
            const ClassicReachRect r = classicReachRect(at, int16_t(f.footX), int16_t(f.footZ),
                                                        uint16_t(b.info_[member.type].buildDistance));
            const auto& nav = world.navFor(unit.type);
            const auto& blocked = b.staticBlocked(world, nav, unit.type->footX, unit.type->footZ);
            return tak::sim::retailRectangleReachable(nav.width(), nav.height(),
                tak::sim::footprintOrigin(unit.x, unit.type->footX), tak::sim::footprintOrigin(unit.z, unit.type->footZ),
                1, 1, r.x, r.z, r.w, r.h, true, false,
                [&](int x, int z) { return blocked[size_t(z) * size_t(nav.width()) + size_t(x)] ? 8 : 0; });
        }
        bool lodestoneBusy() const { return classicLodestoneBusy(b.squads_, units, index, slot); }
        void build(uint16_t t, std::pair<int32_t, int32_t> at) {
            tak::net::Command c;
            c.kind = tak::net::Cmd::Build; c.player = uint8_t(b.player_); c.unitId = member.id;
            c.x = float(at.first) / 65536.0f; c.z = float(at.second) / 65536.0f;
            std::strncpy(c.type, b.catalogue_.types[t]->id.c_str(), sizeof c.type - 1);
            sink(c);
            ++b.builds_;
            if (b.info_[t].lodestoneClass) ++b.lodestoneBuilds_;
            // 0x4d78a0 clears its missions and puts the build first: MobileBuild.
            member.mission = true; member.missionClass = 0; member.missionFlags = 0x80508;
            member.buildingBuild = false; member.buildsManaSite = b.info_[t].manaSite;
            member.missionTarget = 0; member.goalX = at.first; member.goalZ = at.second;
        }
        void cancel() {
            tak::net::Command c;
            c.kind = tak::net::Cmd::Stop; c.player = uint8_t(b.player_); c.unitId = member.id;
            sink(c);
            // 0x4d6a50(unit, 0): its missions are gone (our Stop keeps none).
            member.mission = member.buildingBuild = member.buildsManaSite = false;
            member.missionClass = 0; member.missionFlags = 0; member.missionTarget = 0;
        }
        void retreat() { b.planCombat(world, slot, units, member, threat, true, sink); }
        void combat() { b.planCombat(world, slot, units, member, threat, false, sink); }
        bool threat;
    } host{*this, world, units, index, slot, member, *unit, *builder, *centre, random, siteType, sink, threat};
    classicPlanConstructor(host, u, threat);
}

std::vector<ClassicEnemy> ClassicBrain::visibleEnemies(const tak::sim::World& world) const {
    // 411700's list +5 as the World last rebuilt it (it re-checks nothing, so a
    // unit that has since died is still listed, as dying). Its 0x2000 is our
    // underConstruction (U10: the World leaves those off the list already).
    std::vector<ClassicEnemy> out;
    for (const int id : world.visibleEnemies(player_)) {
        const auto* e = world.unit(id);
        if (!e || !e->type) continue;
        ClassicEnemy c;
        c.id = id;
        c.flags = ClassicEnemy::kLive | (e->alive() && e->hp > tak::sim::Fixed() ? 0u : ClassicEnemy::kDying) |
                  (e->underConstruction ? ClassicEnemy::kUnready : 0u) | (e->type->canFly ? 2u : 1u);
        c.x = e->x.v; c.y = (e->type->canFly ? e->flightY : e->groundY).v; c.z = e->z.v;
        c.cloaked = e->cloaked;
        out.push_back(c);
    }
    return out;
}

void ClassicBrain::planCombat(const tak::sim::World& world, int slot, const std::vector<ClassicSquadUnit>& units,
                              ClassicSquadUnit& member, bool threat, bool retreat,
                              const std::function<void(const tak::net::Command&)>& sink) {
    const auto* unit = world.unit(member.id);
    if (!unit || !unit->type) return;
    const auto* type = unit->type;
    const detail::ClassicUnitIndex index(units);
    ClassicCombatUnit u;
    u.armed = member.flags & ClassicSquadUnit::kArmed;
    u.commander = member.commander;
    u.builder = member.typeFlags & 0x100u;
    u.canFly = type->canFly;
    u.hp = int16_t(unit->hp.floorInt());
    u.maxDamage = uint32_t(type->maxHp);
    u.at = {unit->x.v, (type->canFly ? unit->flightY : unit->groundY).v, unit->z.v};
    u.sight = int16_t(world.sightDistance(*type));
    const auto* weapon = currentWeapon(*unit);
    u.range = weapon ? weapon->range : 0;
    u.melee = weapon && weapon->melee;
    u.mission = member.mission;
    u.missionFlags = member.missionFlags;
    u.missionTarget = member.missionTarget;
    u.goalX = member.goalX; u.goalZ = member.goalZ;
    const std::vector<ClassicEnemy> visible = visibleEnemies(world);
    struct Host {
        ClassicBrain& b;
        const tak::sim::World& world;
        const std::vector<ClassicSquadUnit>& units;
        const detail::ClassicUnitIndex& index;
        int slot;
        ClassicSquadUnit& member;
        const tak::sim::Unit& unit;
        const tak::sim::Weapon* weapon;
        const std::vector<ClassicEnemy>& visible;
        const std::function<void(const tak::net::Command&)>& sink;
        bool replaced = false;

        std::optional<ClassicPoint> centre() const {
            // The base think plans members only with a centre (0x412c10, the
            // profile home it would fall back on, is never reached from it).
            const auto c = classicBaseCentre(b.squads_, units, index, slot);
            if (!c) return std::nullopt;
            return ClassicPoint{c->first, 0, c->second};
        }
        std::optional<int32_t> radius(unsigned ring) const { return classicBaseRadius(b.squads_, units, index, slot, ring); }
        std::vector<ClassicEnemy> enemies(std::pair<int32_t, int32_t> at, int32_t r) const {
            return classicEnemiesNear(visible, at, r);
        }
        int32_t damage(const ClassicEnemy& e) const {
            const auto* t = world.unit(e.id);
            return weapon && t && t->type ? int32_t(weapon->damageVs(t->type)) : 0;
        }
        int random(int n) { return b.rand(n); }
        bool placeable(int32_t x, int32_t z) const {
            // 0x507d10(type, self, cell, 1, 0): its own footprint, top-left cell
            // of a footprint centred there, by the mobile placement rule.
            const int cx = classicFootprintCell(x, int16_t(unit.type->footX));
            const int cz = classicFootprintCell(z, int16_t(unit.type->footZ));
            const auto& cells = world.mapPlacementCells();
            if (!cells.empty()) return world.mobilePlacement(unit, cx, cz, false);
            // A World without map feature data (synthetic tests): the building test.
            return world.canPlace(unit.type, float(cx * 16 + unit.type->footX * 8), float(cz * 16 + unit.type->footZ * 8), b.player_);
        }
        int width() const { return world.mapW() * 16; }
        int height() const { return world.mapH() * 16; }
        uint8_t resolve(int command, const ClassicPoint&) const { return uint8_t(command); }
        void order(uint8_t kind, bool queue, const ClassicPoint& at, int32_t a6, int32_t) {
            // A retail MOVE with mission +0x4e = a6 > 0 ends as soon as the unit
            // is within a6 of its point (the centre leg's r, the retreat's 16):
            // a unit already that close skips the leg, one farther off heads for
            // the circle's edge on its way in.
            float x = float(at[0]) / 65536.0f, z = float(at[2]) / 65536.0f;
            if (kind == kClassicMove && a6 > 0) {
                const float ux = unit.x.toFloat(), uz = unit.z.toFloat();
                const float dx = ux - x, dz = uz - z, d = std::sqrt(dx * dx + dz * dz);
                if (d <= float(a6)) return;
                x += dx * float(a6) / d; z += dz * float(a6) / d;
            }
            tak::net::Command c;
            c.kind = kind == kClassicPatrol ? tak::net::Cmd::Patrol : tak::net::Cmd::Move;
            c.player = uint8_t(b.player_); c.unitId = member.id;
            c.x = x; c.z = z;
            // queue 0 clears its missions first (0x4d6a50); a skipped first leg
            // must not let a queued one append to the old missions.
            c.queue = queue && replaced ? 1 : 0;
            sink(c);
            replaced = true;
            ++b.combatOrders_;
            // Its first mission is now that move (or patrol); a build is gone.
            if (!c.queue) {
                member.mission = true; member.missionClass = kind == kClassicPatrol ? 7 : 14;
                member.missionFlags = kind == kClassicPatrol ? 0x3000410u : 0x3000400u;
                member.buildingBuild = member.buildsManaSite = false; member.missionTarget = 0;
                member.goalX = at[0]; member.goalZ = at[2];
            }
        }
    } host{*this, world, units, index, slot, member, *unit, weapon, visible, sink};
    if (retreat) { ++retreats_; classicRetreat(host, u); }
    else classicPlanCombat(host, u, threat);
}

void ClassicBrain::tick(const tak::sim::World& world, uint32_t simTick,
                        const std::function<void(const tak::net::Command&)>& sink) {
    // Retail's player loop: the AI scheduler (0x40fe00 -> 0x40fd80), then the
    // bookkeeping (0x4121c0 -> 0x411a90).
    scheduler(world, simTick, sink);
    bookkeeping(world, simTick);
#ifndef NDEBUG
    debugDump(simTick);
#endif
}

#ifndef NDEBUG
// Debug-only harness hook, for comparing with retail's PrintWeights dumps
// (tools/re/retail_ai_harness.py --printweights): TAK_CLASSIC_WEIGHTS names an
// output path and TAK_CLASSIC_WEIGHTS_AT a sim tick (default 600). At the first
// tick at or past it, each Classic brain writes <path>.p<player> once, one row
// per catalogue type in retail's column layout:
//     <limit, or n/a> - <priority> = <preference> - '<unit>\t\t:<name>'
// A release takserver compiles none of this and reads neither variable. It only
// writes a file, so --mpai hashes are the same with it on.
void ClassicBrain::debugDump(uint32_t simTick) {
    static const char* path = std::getenv("TAK_CLASSIC_WEIGHTS");
    static const uint32_t at = [] {
        const char* e = std::getenv("TAK_CLASSIC_WEIGHTS_AT");
        const long v = e ? std::strtol(e, nullptr, 10) : 600;
        return uint32_t(v > 0 ? v : 600);
    }();
    if (dumped_ || !path || !*path || simTick < at) return;
    dumped_ = true;
    const std::string out = std::string(path) + ".p" + std::to_string(player_);
    std::FILE* f = std::fopen(out.c_str(), "w");
    if (!f) { std::fprintf(stderr, "TAK_CLASSIC_WEIGHTS: cannot write %s\n", out.c_str()); return; }
    const unsigned secs = simTick / 30;
    std::fprintf(f, "gametime: '%02u:%02u:%02u'\nplayer:   'player %d' num: %d\ncontroller: AI (Classic)\n"
                    "profile:    '%s'\nsim tick: %u, priority refreshes: %u, units: %d\n"
                    "------------------------------------------------\n"
                    "<limit> - <dynamic priority> = <profile preference> - <unit>\n",
                 secs / 3600, secs / 60 % 60, secs % 60, player_, player_, profile_.path.c_str(), simTick,
                 refreshes_, [&] { int n = 0; for (auto c : count_) n += c; return n; }());
    for (size_t t = 1; t < catalogue_.size(); ++t) {
        char limit[16];
        if (profile_.limit[t] == -1) std::snprintf(limit, sizeof limit, "n/a ");
        else std::snprintf(limit, sizeof limit, "%4d", profile_.limit[t]);
        std::string name = catalogue_.names[t];
        for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
        std::fprintf(f, "%s - %3d = %3d - '%s\t\t:%s'\n", limit, int(int8_t(priority_[t])),
                     int(profile_.pref[t]), name.c_str(),
                     catalogue_.types[t] ? catalogue_.types[t]->name.c_str() : "");
    }
    std::fclose(f);
}
#endif

uint32_t classicMassAttackTicks(const std::string& line) {
    // strstr(line, "massattack"), then sscanf(at, "massattack %i"): the format's
    // space takes any whitespace, %i a sign and a 0x / 0 prefix.
    const size_t at = line.find("massattack");
    if (at == std::string::npos) return 0;
    const char* p = line.c_str() + at + 10;
    char* end = nullptr;
    const long v = std::strtol(p, &end, 0);
    if (end == p) return 0;
    return uint32_t(v) * 30u;   // the stored dword: n x 30, wrapping
}

}  // namespace tak::ai
