#include "ai/ai.h"

#include <cstdlib>
#include <climits>

#include "hpi/hpi.h"
#include "sim/detmath.h"
#include "sim/footprint.h"
#include "sim/retailai.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace tak::ai {

Profile loadProfile(const tak::hpi::Vfs& vfs, const std::string& name) {
    Profile prof;
    // Campaign missions name their own build profile in the .ota (`aiprofile=`),
    // and retail ships a dozen of them (ai/mission18.txt, ai/takx07.txt...) tuned
    // for that mission's opposition. Fall back to default.txt when the named one
    // is absent -- most missions just say DEFAULT.
    std::string path = "ai/" + name + ".txt";
    if (name.empty() || !vfs.has(path)) path = "ai/default.txt";
    if (!vfs.has(path)) return prof;
    auto b = vfs.read(path);
    std::istringstream f(std::string(b.begin(), b.end()));
    std::string kw, unit;
    int v;
    for (std::string line; std::getline(f, line);) {
        if (line.size() < 2 || line[0] == '/') continue;
        std::istringstream ss(line);
        if (!(ss >> kw >> unit >> v)) continue;
        std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
        if (kw == "weight") prof.weight[unit] = v;
        else if (kw == "limit") prof.limit[unit] = v;
    }
    return prof;
}

DiffParams paramsFor(Difficulty d) {
    switch (d) {
        // Defensive: normal production and local interception, without offensive waves.
        case Difficulty::Passive: return {30, 5, 3, 100, false, false, 0};
        // Sluggish: reacts slowly, builds up slowly, and only commits once it has
        // gathered a sizeable group -- so it's passive and beatable. No raiding.
        case Difficulty::Easy:   return {90, 4, 1, 50,  false, true,  0};
        // Fast, army-heavy, and aggressive: reacts often, musters a large army before
        // the big push, harasses with raids meanwhile, and pushes bigger unit limits.
        case Difficulty::Hard:   return {20, 6, 8, 150, true,  true,  4};
        // Hard's behaviour, plus a 2x income cheat applied to the sim (incomeMultFor).
        case Difficulty::Absurd: return {20, 6, 8, 150, true,  true,  4};
        case Difficulty::Normal:
        default:                 return {30, 5, 3, 100, true,  true,  3};
    }
}

Controller::Controller(int player, const tak::sim::TypeRegistry& registry,
                       const Profile& profile, uint32_t seed, Difficulty difficulty,
                       std::vector<std::pair<float, float>> enemyStarts)
    : player_(player), registry_(registry), profile_(profile),
      // The seed is taken as-is: the CALLER hands distinct seeds to distinct players
      // when it wants them to diverge from tick one (the lobby does, per game+player).
      // Same-seed controllers still diverge within a few ticks, because every RNG draw
      // is gated on that player's own unit counts / income / producers, which differ
      // by map position. The AI runs server-side only, so this RNG exists just to make
      // a --mpai run repeatable, not for lockstep.
      rng_(seed), diff_(difficulty), dp_(paramsFor(difficulty)),
      enemyStarts_(std::move(enemyStarts)) {}

void Controller::emit(const CommandSink& sink, tak::net::Cmd kind, int unitId,
                      const std::string& type, float x, float z) const {
    tak::net::Command c;
    c.kind = kind;
    c.player = uint8_t(player_);
    c.unitId = unitId;
    c.x = x;
    c.z = z;
    std::snprintf(c.type, sizeof c.type, "%s", type.c_str());
    sink(c);
}

// What is this unit FOR? Derived purely from its stats, so it works for every faction:
//   Economy  - a structure that makes/holds mana (lodestone, mana storage)
//   Factory  - a structure that trains units (keep, castle, hell), OR a mobile
//              production creature (Zhon's Beast Handlers/Tamers/Lords -- Zhon has
//              NO static factories, its creatures ARE the production line)
//   Defense  - any other structure (towers, walls)
//   Builder  - a mobile unit that builds/expands (Monarch, mason, priest, arabuild)
//   Army     - any other mobile unit (the combatants)
BuildCat Controller::categoryOf(const tak::sim::UnitType* t) const {
    if (t->isStructure()) {
        if (!registry_.buildable(t->id).empty()) return BuildCat::Factory;
        if (t->income > 0 || t->storage > 0) return BuildCat::Economy;
        return BuildCat::Defense;
    }
    if (t->isBuilder) {
        // A mobile builder whose menu is DOMINATED by mobile combat units is really a
        // factory (Zhon's tamers train armies), not an economy/expansion builder. A
        // mobile CONSTRUCTOR (arabuild: mostly buildings) or a Monarch (the base
        // builder) stays a Builder. Without this, every Zhon producer landed in the
        // builder bucket -- capped at 2-3 -- so the Zhon AI built one Beast Handler
        // and then stalled with nothing left it wanted to make.
        if (!t->commander) {
            int combat = 0, structs = 0;
            for (const auto& id : registry_.buildable(t->id)) {
                const auto* b = registry_.find(id);
                if (!b) continue;
                if (b->isStructure()) ++structs;
                else if (!b->isBuilder) ++combat;
            }
            if (combat > structs && combat > 0) return BuildCat::Factory;
        }
        return BuildCat::Builder;
    }
    return BuildCat::Army;
}

// A builder=1 unit that walks: the Monarch, constructors, priests, Zhon's producers.
static bool mobileBuilder(const tak::sim::UnitType* t) {
    return t && !t->isStructure() && t->isBuilder;
}

bool Controller::mobileProduction(const std::string& side) const {
    if (side.empty()) return false;
    auto [it,inserted]=mobileProduction_.try_emplace(side,true);
    if (inserted)
        for (const auto& [id,type]:registry_.types())
            if (type.side==side && type.isStructure() && !registry_.buildable(id).empty()) {
                it->second=false;
                break;
            }
    return it->second;
}

bool Controller::navalFactory(const tak::sim::UnitType* type) const {
    if (!type || !type->isStructure()) return false;
    bool ships = false;
    for (const auto& id : registry_.buildable(type->id)) {
        const auto* child = registry_.find(id);
        if (!child || child->isStructure()) continue;
        if (child->canFly || child->domain != tak::sim::UnitType::Domain::Water) return false;
        ships = true;
    }
    return ships;
}

bool Controller::navalProducer(const tak::sim::UnitType* type) const {
    if (!type) return false;
    for (const auto& id:registry_.buildable(type->id)) {
        const auto* child=registry_.find(id);
        const auto weight=profile_.weight.find(id),limit=profile_.limit.find(id);
        if (child && !child->isStructure() && !child->canFly && !child->isBuilder &&
            child->domain==tak::sim::UnitType::Domain::Water && child->weapon.damage>0 &&
            weight!=profile_.weight.end() && weight->second>0 &&
            (limit==profile_.limit.end() || limit->second!=0)) return true;
    }
    return false;
}

const std::vector<std::pair<float,float>>& Controller::navalLaunchOffsets(const tak::sim::UnitType* type) const {
    auto [it,inserted] = navalOffsets_.try_emplace(type);
    if (!inserted || !type->productionScript) return it->second;
    const tak::sim::UnitType* ship = nullptr;
    for (const auto& id:registry_.buildable(type->id)) {
        const auto* child=registry_.find(id);
        if (child && !child->isStructure() && child->domain==tak::sim::UnitType::Domain::Water) {ship=child;break;}
    }
    if (!ship) return it->second;
    // QueryBuildInfo can change script statics/pieces. Evaluate the authored
    // output in private worlds, never on the referee whose state is replicated.
    // Cache both query phases and birth orientations once per shipyard type.
    const int samples=type->buildAngle ? 16 : 1;
    for (int angle=0; angle<samples; ++angle) {
        tak::sim::World preview;preview.setVisPlayer(-1);
        const auto heading=tak::sim::bamWrap(int(type->orientation)-int(type->buildAngle)/2+
            (samples>1 ? angle*(int(type->buildAngle)-1)/(samples-1) : 0));
        const int id=preview.spawn(type,2000,2000,tak::sim::radiansFromBam(heading),0);
        preview.tick(1.f/30.f); // Create counter-rotates the yard where the script requests it.
        for (int phase=0; phase<2; ++phase) {
            tak::sim::Fixed x,y,z;
            preview.productionPosition(id,ship,x,y,z);
            it->second.emplace_back(x.toFloat()-2000,z.toFloat()-2000);
        }
    }
    std::sort(it->second.begin(),it->second.end());
    it->second.erase(std::unique(it->second.begin(),it->second.end()),it->second.end());
    return it->second;
}

bool Controller::navalLaunchClear(const tak::sim::World& world,const tak::sim::UnitType* factory,
                                  const tak::sim::UnitType* ship,float x,float z,float dx,float dz) const {
    const auto& grid=world.navFor(ship);
    const int ox=tak::sim::footprintOrigin(x,factory->footX),oz=tak::sim::footprintOrigin(z,factory->footZ);
    const int sx=tak::sim::footprintOrigin(x+dx,ship->footX),sz=tak::sim::footprintOrigin(z+dz,ship->footZ);
    // The pad alone can fit while the mouth of the yard opens onto shallow
    // water. Check a short route out of the planned open yard, including its
    // solid cells. This is an AI site test; it never changes the live nav grid.
    const int x0=std::min(ox-ship->footX-2,sx),z0=std::min(oz-ship->footZ-2,sz);
    const int x1=std::max(ox+factory->footX+2,sx+1),z1=std::max(oz+factory->footZ+2,sz+1);
    const int width=x1-x0,height=z1-z0;
    std::vector<uint8_t> visited(size_t(width)*height,0);
    std::vector<std::pair<int,int>> queue{{sx,sz}};
    visited[size_t(sz-z0)*width+sx-x0]=1;
    for (size_t head=0;head<queue.size();++head) {
        const auto [cx,cz]=queue[head];
        bool fits=true;
        for (int j=0;j<ship->footZ && fits;++j) for (int i=0;i<ship->footX && fits;++i) {
            const int gx=cx+i,gz=cz+j;
            fits=grid.walkable(gx,gz);
            if (gx>=ox && gz>=oz && gx<ox+factory->footX && gz<oz+factory->footZ && !factory->yardMap.empty()) {
                const char cell=factory->yardMap[size_t(gz-oz)*factory->footX+gx-ox];
                fits=fits && (cell=='.' || cell==' ' || cell=='c' || cell=='C' || cell=='y' || cell=='Y');
            }
        }
        if (!fits) continue;
        if (cx+ship->footX<=ox || cx>=ox+factory->footX ||
            cz+ship->footZ<=oz || cz>=oz+factory->footZ) return true;
        for (const auto [nx,nz]:{std::pair{cx-1,cz},std::pair{cx+1,cz},std::pair{cx,cz-1},std::pair{cx,cz+1}}) {
            if (nx<x0 || nz<z0 || nx>=x1 || nz>=z1) continue;
            auto& seen=visited[size_t(nz-z0)*width+nx-x0];
            if (!seen) {seen=1;queue.emplace_back(nx,nz);}
        }
    }
    return false;
}

// Count the empire by category and set the targets the planner steers toward.
Needs Controller::assessNeeds(const tak::sim::World& world) const {
    Needs n;
    const auto& me = world.player(player_);
    n.income = me.income;   // spend the income actually available
    std::string side;       // the Monarch's, else a mobile builder's
    for (const auto& u : world.units()) {
        if (!u.alive() || u.player != player_ || !u.type) continue;
        if (!u.type->isStructure() && u.type->isBuilder && (side.empty() || u.type->commander))
            side = u.type->side;
        if (u.type->isBuilder) {
            const auto* site = u.buildSiteId ? world.unit(u.buildSiteId) : nullptr;
            bool economyProject = site && site->underConstruction && site->type &&
                                  categoryOf(site->type) == BuildCat::Economy;
            bool lodestoneProject = site && site->underConstruction && site->type && site->type->onMana;
            // Approach waypoints precede the actual build order. Count the queued
            // destination too, so distant expansions do not enlist every producer.
            for (const auto& order : u.orders)
                if (order.buildType) {
                    economyProject = economyProject || categoryOf(order.buildType) == BuildCat::Economy;
                    lodestoneProject = lodestoneProject || order.buildType->onMana;
                }
            if (economyProject) ++n.economyProjects;
            if (lodestoneProject) ++n.lodestoneProjects;
        }
        ++n.population;
        ++n.counts[u.type];
        if (u.underConstruction) ++n.unfinished[u.type];
        if (navalFactory(u.type)) ++n.navalFactories;
        if (navalProducer(u.type)) ++n.navalProducers;
        if (!u.type->isStructure() && !u.type->canFly && !u.type->isBuilder &&
            u.type->domain==tak::sim::UnitType::Domain::Water) ++n.navalArmy;
        switch (categoryOf(u.type)) {
            case BuildCat::Economy:  ++n.economy;   break;
            case BuildCat::Factory:  ++n.factories; break;
            case BuildCat::Builder:  ++n.builders;  break;
            case BuildCat::Army:     ++n.army;      break;
            case BuildCat::Defense:  ++n.defenses; break;
        }
        if (mobileBuilder(u.type)) ++n.mobileBuilders;
        // A builder trained or conjured but not yet begun is no unit yet: a
        // factory's queue, or a producer walking to its conjuring spot. Count
        // it, or every think hands the next idle producer one more builder.
        auto planned = [&](const tak::sim::UnitType* t) {
            if (!mobileBuilder(t)) return;
            ++n.population;++n.counts[t];++n.mobileBuilders;
            if (categoryOf(t) == BuildCat::Factory) ++n.factories; else ++n.builders;
        };
        for (size_t i = 0; i < u.buildQueue.size(); ++i)
            if (i > 0 || !u.productionSiteId) planned(u.buildQueue[i]);
        const auto* bound = u.buildSiteId ? world.unit(u.buildSiteId) : nullptr;
        for (size_t i = 0; i < u.orders.size(); ++i)
            if (u.orders[i].buildType && (i > 0 || !bound || bound->type != u.orders[i].buildType))
                planned(u.orders[i].buildType);
    }
    // One factory per ~40 income so production can actually spend what we earn.
    // Builders count the Monarch. Normal grows to the Monarch plus four as income
    // allows: retail's ai/default.txt limits each builder type, not the total --
    // arabuild, arafly and arapries 4 each, cremech and crechie 4, tarpries,
    // verlihr and verliege 3 -- and has no difficulty levels, so four is the most
    // any one type may field. Those per-type limits still apply in weightedPick.
    // Easy and Defensive keep the Monarch plus one; Hard/Absurd run hotter.
    n.desiredFactories = std::clamp(int(n.income / 40.0f) + 1, 1, 8);
    n.desiredArmy = std::clamp(12 + int(n.income * 1.5f), 12, 180);
    const bool aggressive = diff_ == Difficulty::Hard || diff_ == Difficulty::Absurd;
    n.builderCap = aggressive ? std::clamp(4 + int(n.income / 60), 4, 12)
                 : diff_ == Difficulty::Normal ? std::clamp(2 + int(n.income / 50), 2, 5) : 2;
    n.desiredDefenses = std::clamp(1 + int(n.income / 50), 1, 6);
    if (aggressive || diff_ == Difficulty::Passive) {
        // Income limits production speed, not the eventual size of the force.
        n.desiredArmy = world.unitCap() > 0 ? world.unitCap() : INT_MAX;
        n.desiredFactories = std::clamp(1 + int(n.income / 25), 1, 32);
        if (me.storage > 0 && me.mana > me.storage * 0.6f)
            n.desiredFactories = std::min(n.desiredFactories + 1, 32);
        if (diff_ == Difficulty::Passive)
            n.desiredDefenses = std::clamp(2 + int(n.income / 40) + n.army / 12, 2, 48);
    }
    if (diff_ == Difficulty::Easy) {
        n.desiredFactories = std::min(n.desiredFactories, 2);
        n.desiredArmy = std::clamp(8 + int(n.income * 0.5f), 8, 36);
        n.desiredDefenses = std::min(n.desiredDefenses, 2);
    }
    // Zhon has no factory buildings: its Beast Handlers, Tamers, Lords and Shamans
    // (builder=1, limit 3 each in ai/default.txt) train its army AND build its
    // lodestones, and they count as factories here. Their allowance is the
    // factories another side would have plus that side's builders past the
    // Monarch, so Zhon is not held to the producers of a side that has both.
    // Normal's most is 8 + 4 = 12, the four types' retail limits together.
    // Every builder=1 mobile unit counts against the cap: Aramon's and Taros's
    // priests are factories by their menus, but builders all the same. Zhon's
    // cap is its producers' allowance plus the Monarch.
    n.mobileBuilderCap = n.builderCap;
    if (mobileProduction(side)) {
        n.mobileBuilderCap += n.desiredFactories;
        n.desiredFactories += n.builderCap - 1;
    }
    const float capacity = me.manaCapacity();
    n.storeFull = capacity > 0.01f && float(me.mana) >= capacity * 0.9f;
    return n;
}

// Priority of building one more of a category, given the empire's needs. Higher wins;
// 0 means "have enough, don't". The ladder: guarantee production, floor the economy,
// keep a few builders, grow economy/factories to match income, then army as the sink.
int Controller::desire(BuildCat c, const Needs& n) const {
    switch (c) {
        case BuildCat::Economy:
            // Bootstrap income BEFORE the pricey first factory -- building a 1700-mana
            // keep out of the opening treasury with no income starves everything after.
            if (n.income < 20.0f) return 95;
            if (diff_ == Difficulty::Hard || diff_ == Difficulty::Absurd)
                return n.income < 25.0f + 20.0f * n.factories ? 80 : 60;
            if (n.income < 25.0f + 20.0f * n.factories) return 60;
            // A full store means the income target is not what holds production
            // back. Retail's builders draw from their whole menu, the lodestone
            // among it (weight 10, limit 7 in ai/default.txt); ours stood idle
            // instead: a Taros AI on "4x4 team" kept its Dark Masons idle
            // beside a full store on one lodestone for ten minutes. Below the
            // army, so a producer that can train one still does. Easy stays lean.
            return n.storeFull && diff_ != Difficulty::Easy ? 45 : 0;
        case BuildCat::Factory:
            if (n.factories == 0) return 90;                        // then: some production
            return n.factories < n.desiredFactories ? 70 : 0;       // scale with income
        case BuildCat::Builder:
            if ((diff_==Difficulty::Hard || diff_==Difficulty::Absurd) &&
                n.factories>0 && n.income>=20 && n.builders<n.builderCap) return 88;
            return n.builders < n.builderCap ? 65 : 0;              // a handful, then stop
        case BuildCat::Army:
            return n.army < n.desiredArmy ? 50 : 0;
        case BuildCat::Defense:
            return n.defenses < n.desiredDefenses ?
                (n.army >= 4 ? 55 : 30) : 0;
    }
    return 0;
}

// Retail builds a lodestone when it runs short of mana (icd 0x410810, ported as
// sim::retailBuildPriority): the store under 30% full, spending outpacing income
// over the last second, and none of that lodestone unfinished. Its constructor
// planner then draws among the menu's lodestones separately (0x412d00) and builds
// that instead of the ordinary pick wherever 0x412f80 finds a free deposit in the
// base radius (retailLodestoneRadius) -- unless a builder of the base is already on
// one (0x40c830). Normal and the tiers sharing its economy add that trigger to their
// own ladder, whose income target alone held them to one lodestone where a deposit
// pays 100 a second and the store ran dry; Hard and Absurd keep their own expansion
// ladder.
bool Controller::manaShort(const tak::sim::World& world, const Needs& needs) const {
    if (diff_ == Difficulty::Hard || diff_ == Difficulty::Absurd || !world.hasManaSpots() ||
        needs.lodestoneProjects > 0) return false;
    const auto& me = world.player(player_);
    const float capacity = me.manaCapacity();
    return (capacity > 0.01f ? float(me.mana) / capacity : 0.0f) < 0.3f &&
        me.displayResources.shortfall(float(me.mana));
}

// The base radius retail searches for that lodestone's deposit (0x412f80 ->
// 0x411510, radius 0x40cf30): 16 * max(5b, b*sqrt(n)/2), n the summed footprint
// (footX + footZ) of the base's buildings and b 7 for the Monarch, 10 for any
// other builder (0x40cfc0) -- 560 and 800 px until n passes 100. Our own
// ladder reaches 1600 from home: with the trigger firing nearly every think on a
// one-deposit-per-player map, a Normal builder walked 1250 px into an enemy's
// base for a freed home deposit and was lost there with the lodestone.
float Controller::retailLodestoneRadius(const tak::sim::World& world,
                                        const tak::sim::Unit& builder) const {
    int32_t footprint = 0;
    for (const auto& u : world.units())
        if (u.alive() && u.player == player_ && u.type && u.type->isStructure())
            footprint += u.type->footX + u.type->footZ;
    return float(tak::sim::retailAiBaseRadius(footprint, builder.type->commander ? 2 : 1));
}

// Pick what a producer should build: the most-needed category its menu can supply
// (desire()), then a weighted-random draw WITHIN that category (profile weights, for
// variety + retail flavour). A candidate must be affordable to finish and under its
// difficulty-scaled limit. Returns nullptr if nothing worth building is affordable now.
const tak::sim::UnitType* Controller::weightedPick(const tak::sim::World& world,
                                                   const tak::sim::Unit& producer,
                                                   const Needs& needs, int excludeCats,
                                                   bool lodestoneOnly, float* siteLimit) {
    if (siteLimit) *siteLimit = 0;
    const auto& menu = registry_.buildable(producer.type->id);
    const auto& me = world.player(player_);
    float income = me.income;   // include the Absurd income bonus
    // A menu entry the AI may build right now: has a positive weight, is under its
    // limit, and savings + income over its build time cover the cost (so a builder
    // never traps itself on a site the mana runs dry beneath).
    const bool aggressive = diff_ == Difficulty::Hard || diff_ == Difficulty::Absurd;
    const bool sustained = aggressive || diff_ == Difficulty::Passive;
    const bool mobileFactory = !producer.type->isStructure() &&
                               categoryOf(producer.type) == BuildCat::Factory;
    std::unordered_map<const tak::sim::UnitType*,bool> coastalProduction;
    auto coastalProducer = [&](const tak::sim::UnitType* type) {
        auto [it,inserted]=coastalProduction.try_emplace(type,false);
        if (!inserted) return it->second;
        if (navalFactory(type)) return it->second=true; // terrainWeight checks its launch
        auto future=producer;future.type=type;
        for (const auto& id:registry_.buildable(type->id)) {
            const auto* ship=registry_.find(id);
            const auto weight=profile_.weight.find(id),limit=profile_.limit.find(id);
            if (!ship || ship->isStructure() || ship->canFly || ship->isBuilder ||
                ship->domain!=tak::sim::UnitType::Domain::Water || !world.buildAllowed(ship) ||
                weight==profile_.weight.end() || weight->second<=0 ||
                (limit!=profile_.limit.end() && limit->second==0)) continue;
            if (terrainWeight(world,future,ship,1)>0) return it->second=true;
        }
        return false;
    };
    // The trigger's lodestone replaces the ordinary pick only where retail would
    // build it: a free deposit within the base radius of home. Otherwise the
    // ordinary ladder, with its own reach, decides as before.
    const bool lodestoneTrigger = manaShort(world, needs) && !producer.type->isStructure() &&
                                  producer.type->isBuilder;
    const float triggerRadius = lodestoneTrigger ? retailLodestoneRadius(world, producer) : 0.0f;
    std::unordered_map<const tak::sim::UnitType*,bool> triggerSites;
    auto triggered = [&](const tak::sim::UnitType* type) {
        if (!lodestoneTrigger || !type->onMana || !type->isStructure() || needs.unfinished.count(type)) return false;
        auto [it,inserted] = triggerSites.try_emplace(type,false);
        if (inserted) {
            float ox = producer.x.toFloat(), oz = producer.z.toFloat(), x, z;
            if (producer.type->commander) { auto h = homeOf(world); ox = h.first; oz = h.second; }
            it->second = placeSite(world, type, producer, ox, oz, x, z, triggerRadius);
        }
        return it->second;
    };
    auto priority = [&](const tak::sim::UnitType* type) {
        if (triggered(type)) return 96;
        if (lodestoneOnly) return 0;
        const auto cat = categoryOf(type);
        int value = desire(cat, needs);
        const bool navalBudget=needs.income>0 || me.mana>=type->buildCost;
        // Land production must not fill the whole factory budget before the AI
        // establishes its first usable shipyard. Terrain feasibility is checked below.
        if (navalFactory(type) && needs.navalFactories == 0 && needs.factories > 0 &&
            navalBudget) value = std::max(value, 70);
        // Aramon conjures ships directly; Zhon needs a higher-tier conjurer.
        // Reserve a route into naval production even after land factories fill
        // the ordinary budget, but only where its water units can be used.
        if (needs.navalProducers==0 && needs.factories>0 && navalBudget &&
            navalProducer(type) && coastalProducer(type)) value=std::max(value,70);
        if (cat==BuildCat::Army && !type->canFly &&
            type->domain==tak::sim::UnitType::Domain::Water && navalBudget &&
            needs.navalArmy<4) value=std::max(value,70);
        if (mobileFactory && needs.income >= 20 && value > 0) {
            // Zhon's factories can ALSO build economy. Keep them producing an
            // opening force instead of sending every handler off to a lodestone.
            if (cat == BuildCat::Army && needs.army < std::max(4, needs.factories * 3)) return 85;
            if (cat == BuildCat::Economy && needs.economyProjects >=
                (aggressive ? std::max(2,needs.factories/2) : std::max(1,needs.factories/3))) return 40;
        }
        return value;
    };
    std::unordered_map<const tak::sim::UnitType*,int> terrainWeights;
    auto usable = [&](const tak::sim::UnitType* ut) -> int {
        if (!world.buildAllowed(ut) || (world.unitCap() > 0 && needs.population >= world.unitCap())) return 0;
        auto count = needs.counts.find(ut);
        const int have = count == needs.counts.end() ? 0 : count->second;
        if (ut->totalAllowed > 0 && have >= ut->totalAllowed) return 0;
        auto wi = profile_.weight.find(ut->id);
        int w = wi == profile_.weight.end() ? 0 : wi->second;
        if (w <= 0) return 0;
        // Some coastal maps have no mana deposits. Sacred lodestones there
        // add storage but no income; unlimited expansion must not keep buying
        // them while the navy and the rest of the army receive nothing.
        if (ut->onMana && ut->sacredIncome && !world.hasManaSpots()) return 0;
        if (categoryOf(ut)==BuildCat::Economy && ut->income<=0 && needs.income<20) return 0;
        auto li = profile_.limit.find(ut->id);
        int lim = li == profile_.limit.end() ? -1 : li->second;
        if (lim == 0) return 0;
        // The difficulty's builder cap, the Monarch included (see assessNeeds).
        if (mobileBuilder(ut) && needs.mobileBuilders >= needs.mobileBuilderCap) return 0;
        if (lim > 0) {
            int limit = std::max(1, lim * dp_.limitScale / 100);
            const auto cat = categoryOf(ut);
            // Profile limits pace the old small armies. Keep disabled entries
            // disabled and enforce actual gameplay limits, but let these modes
            // grow ordinary troops, defenses and expanding income beyond them.
            // A builder type never passes its own retail limit (ai/default.txt
            // has no difficulty levels): Hard's 150% made four arabuilds six.
            // Zhon's producers are the exception: with no factory buildings they
            // take the factory raise, and only its whole allowance bounds them
            // (mobileBuilderCap, the factories plus the builder cap).
            if (mobileBuilder(ut)) {
                limit = std::min(limit, lim);
                if (sustained && cat == BuildCat::Factory && mobileProduction(ut->side))
                    limit = std::max(limit, needs.desiredFactories);
            } else {
                if (sustained && cat == BuildCat::Army) limit = std::max(limit, needs.desiredArmy);
                if (sustained && cat == BuildCat::Factory) limit = std::max(limit, needs.desiredFactories);
                if (sustained && cat == BuildCat::Defense) limit = std::max(limit, needs.desiredDefenses);
            }
            if (aggressive && cat == BuildCat::Economy && ut->income > 0) limit = INT_MAX;
            if (have >= limit) return 0;
        }
        if (ut->buildTime > 0) {
            float secs = ut->buildTime / std::max(producer.type->workerTime, 1.0f);
            // Continuous cheap land production can consume every spare mana
            // forever. Commit one first naval producer/ship using income;
            // the existing low-mana construction throttle then lets it finish.
            // Count unfinished/planned units so this never starts a whole fleet
            // of unaffordable projects at once.
            const bool firstNavy=income>0 &&
                ((needs.navalProducers==0 && needs.factories>0 && navalProducer(ut)) ||
                 (needs.navalArmy==0 && !ut->isStructure() && !ut->canFly && !ut->isBuilder &&
                  ut->domain==tak::sim::UnitType::Domain::Water));
            if (!firstNavy && me.mana + income * secs < ut->buildCost) return 0;
        }
        auto [it,inserted]=terrainWeights.try_emplace(ut,0);
        if (inserted) it->second=terrainWeight(world,producer,ut,w);
        return it->second;
    };
    // Pass 1: the highest desire among categories this producer can actually build now.
    int best = 0;
    for (const auto& id : menu) {
        const auto* ut = registry_.find(id);
        if (!ut || (excludeCats & (1 << int(categoryOf(ut))))) continue;
        const int value=priority(ut);
        if (value<=0 || usable(ut)<=0) continue;
        best = std::max(best, value);
    }
    // TAK_AI_PICK: why a producer chose nothing. A stalled economy is almost always
    // "every menu entry scored 0", and this says which gate did it.
    static const bool kPickLog = std::getenv("TAK_AI_PICK") != nullptr;
    if (kPickLog && best <= 0) {
        std::fprintf(stderr, "    pick %s: nothing usable (mana=%.0f income=%.0f)\n",
                     producer.type->id.c_str(), world.player(player_).mana, income);
        for (const auto& id : menu) {
            const auto* ut = registry_.find(id);
            if (!ut) { std::fprintf(stderr, "      %-9s MISSING from registry\n", id.c_str()); continue; }
            auto wi = profile_.weight.find(ut->id);
            const int w = wi == profile_.weight.end() ? -1 : wi->second;
            auto ci = needs.counts.find(ut);
            const int have = ci == needs.counts.end() ? 0 : ci->second;
            auto li = profile_.limit.find(ut->id);
            const int lim = li == profile_.limit.end() ? -1 : li->second;
            const float secs = ut->buildTime / std::max(producer.type->workerTime, 1.0f);
            std::fprintf(stderr, "      %-9s w=%d lim=%d have=%d cost=%.0f btime=%.0f "
                                 "afford=%s cat=%d usable=%d\n",
                         ut->id.c_str(), w, lim, have, ut->buildCost, ut->buildTime,
                         (world.player(player_).mana + income * secs >= ut->buildCost) ? "Y" : "N",
                         int(categoryOf(ut)), usable(ut));
        }
    }
    if (best <= 0) return nullptr;   // nothing needed is affordable -> wait (no spiral)
    // Pass 2: weighted-random among the usable entries in that top category.
    const tak::sim::UnitType* chosen = nullptr;
    int total = 0;
    for (const auto& id : menu) {
        const auto* ut = registry_.find(id);
        if (!ut || (excludeCats & (1 << int(categoryOf(ut)))) || priority(ut)!=best) continue;
        int w = usable(ut);
        if (w <= 0) continue;
        total += w;
        if (rand(total) < w) chosen = ut;   // reservoir sample
    }
    if (siteLimit && chosen && triggered(chosen)) *siteLimit = triggerRadius;
    return chosen;
}

// These are read-only terrain queries. Orders still go through the normal
// command stream and the existing retail movement controllers.
bool Controller::reachablePoint(const tak::sim::World& world, const tak::sim::UnitType* type,
                                 float fx,float fz,float tx,float tz,float& x,float& z) const {
    if (type->canFly) { x=tx;z=tz;return true; }
    const auto& nav=world.navFor(type);
    const int foot=std::clamp(std::max(type->footX,type->footZ),1,15);
    auto reachable=[&](float px,float pz) {
        return nav.fits(tak::sim::footprintCell(px,foot),tak::sim::footprintCell(pz,foot),foot) &&
            world.pathExists(type,px,pz,fx,fz);
    };
    if (reachable(tx,tz)) { x=tx;z=tz;return true; }
    // A ship can bombard a shore it cannot occupy. Likewise, approach the edge
    // of an occupied building rather than treating its blocked centre as a wall.
    const float range=type->weapon.melee ? 16.0f : std::max(16.0f,float(type->weapon.range)*0.8f);
    for (float r : {16.0f,range*0.5f,range})
        for (int i=0;i<8;++i) {
            const float a=float(i)*0.785398163f;
            const float px=tx+detmath::cos(a)*r,pz=tz+detmath::sin(a)*r;
            if (reachable(px,pz)) { x=px;z=pz;return true; }
        }
    if (type->domain == tak::sim::UnitType::Domain::Water) {
        // An inland starting position is a strategic destination, not a firing
        // target. Project it onto a nearby connected coast so ships can contest
        // the sea even when they cannot bombard the Monarch from there.
        for (float r = 256; r <= 1536; r += 256)
            for (int i = 0; i < 16; ++i) {
                const float a = float(i) * 0.392699082f;
                const float px = tx + detmath::cos(a) * r, pz = tz + detmath::sin(a) * r;
                if (reachable(px,pz)) { x=px;z=pz;return true; }
            }
    }
    return false;
}

int Controller::terrainWeight(const tak::sim::World& world,const tak::sim::Unit& producer,
                               const tak::sim::UnitType* type,int weight) const {
    if (type->isStructure()) {
        if (!navalFactory(type)) return weight;
        float nx = producer.x.toFloat(), nz = producer.z.toFloat();
        if (producer.type->commander) { auto home=homeOf(world);nx=home.first;nz=home.second; }
        float x, z;
        if (!placeSite(world,type,producer,nx,nz,x,z)) return 0;
        auto yard = producer; yard.type=type;
        yard.x = tak::sim::Fixed::fromFloat(x); yard.z = tak::sim::Fixed::fromFloat(z);
        for (const auto& id : registry_.buildable(type->id)) {
            const auto* ship = registry_.find(id);
            const auto wi = profile_.weight.find(id), li = profile_.limit.find(id);
            if (ship && !ship->isBuilder && world.buildAllowed(ship) &&
                wi != profile_.weight.end() && wi->second > 0 &&
                (li == profile_.limit.end() || li->second != 0) && terrainWeight(world,yard,ship,weight) > 0)
                return weight;
        }
        return 0;
    }
    float px=producer.x.toFloat(),pz=producer.z.toFloat();
    bool launch=false;
    if (!producer.type->isStructure() && type->domain==tak::sim::UnitType::Domain::Water && !type->canFly) {
        // Mobile conjurers can walk to a coast. Looking only beside their
        // current inland position silently excluded Aramon and Zhon navies.
        if (!placeSite(world,type,producer,px,pz,px,pz)) return 0;
        launch=true;
    }
    if (navalFactory(producer.type)) {
        const auto& outputs=navalLaunchOffsets(producer.type);
        launch=!outputs.empty();
        for (const auto& offset:outputs) {
            const float x=producer.x.toFloat()+offset.first,z=producer.z.toFloat()+offset.second;
            launch=launch && navalLaunchClear(world,producer.type,type,producer.x.toFloat(),producer.z.toFloat(),offset.first,offset.second);
            px=x;pz=z;
        }
        if (!launch && producer.type->productionScript) return 0;
    }
    for (int r=48;r<=304 && !launch;r+=64)
        for (int i=0;i<8;++i) {
            const float a=float(i)*0.785398163f;
            const float x=producer.x.toFloat()+detmath::cos(a)*r;
            const float z=producer.z.toFloat()+detmath::sin(a)*r;
            if (world.canPlace(type,x,z)) { px=x;pz=z;launch=true;break; }
        }
    if (!launch) return 0;
    if (categoryOf(type)!=BuildCat::Army || enemyStarts_.empty()) return weight;
    float x,z;
    for (const auto& [tx,tz] : enemyStarts_)
        if (reachablePoint(world,type,px,pz,tx,tz,x,z)) return weight;
    // Keep a smaller home defense on isolated land; don't fill a landlocked
    // lake with ships that cannot threaten any enemy shore.
    return type->domain==tak::sim::UnitType::Domain::Water ? 0 : std::max(1,weight/5);
}

// Turn a pick into a command: a factory (keep/castle) trains a mobile unit; a
// mobile builder places a structure or conjures a mobile unit near itself. The
// placement spot is probed against the (const) world, then issued as a Build.
bool Controller::produce(const tak::sim::World& world, const tak::sim::Unit& p,
                         const tak::sim::UnitType* pick, const CommandSink& sink, float siteLimit) {
    if (pick->isStructure()) {                  // structure
        if (!p.type->isStructure() && p.type->isBuilder) {
            // The Monarch builds relative to HOME, not wherever it has drifted -- so a
            // lodestone goes on the nearest deposit to the BASE and other structures
            // ring the base, keeping the Monarch near home instead of trekking across
            // the map (where losing it can lose the game). Other builders build where
            // they stand.
            float ox = p.x.toFloat(), oz = p.z.toFloat();
            if (p.type->commander) { auto h = homeOf(world); ox = h.first; oz = h.second; }
            float x, z;
            if (placeSite(world, pick, p, ox, oz, x, z, siteLimit)) {
                if (pick->onMana) plannedManaSites_.emplace_back(x,z);
                emit(sink, tak::net::Cmd::Build, p.id, pick->id, x, z);
                return true;
            } else {
                static const bool kPickLog = std::getenv("TAK_AI_PICK") != nullptr;
                if (kPickLog)
                    std::fprintf(stderr, "    NO SITE: %s#%d cannot site %s near (%.0f,%.0f)\n",
                                 p.type->id.c_str(), p.id, pick->id.c_str(), ox, oz);
            }
        }
        return false;
    } else if (p.type->isStructure()) {         // factory trains mobile
        emit(sink, tak::net::Cmd::Train, p.id, pick->id, 0, 0);
        return true;
    } else if (p.type->isBuilder) {             // mobile builder conjures mobile
        if (!pick->canFly && pick->domain==tak::sim::UnitType::Domain::Water) {
            float x,z;
            if (!placeSite(world,pick,p,p.x.toFloat(),p.z.toFloat(),x,z)) return false;
            emit(sink,tak::net::Cmd::Build,p.id,pick->id,x,z);
            return true;
        }
        for (float r = 40; r < 170; r += 20)
            for (float a = 0; a < 6.28f; a += 0.6f) {
                float x = p.x.toFloat() + detmath::cos(a) * r, z = p.z.toFloat() + detmath::sin(a) * r;
                if (world.canPlace(pick, x, z)) {
                    emit(sink, tak::net::Cmd::Build, p.id, pick->id, x, z);
                    return true;
                }
            }
        static const bool kPickLog = std::getenv("TAK_AI_PICK") != nullptr;
        if (kPickLog)
            std::fprintf(stderr, "    NO SPOT: %s#%d at (%.0f,%.0f) cannot place %s "
                                 "anywhere in r=40..170\n",
                         p.type->id.c_str(), p.id, p.x.toFloat(), p.z.toFloat(), pick->id.c_str());
    }
    return false;
}

// Find a build site for the AI: lodestones go on the nearest free mana deposit
// (when the map has any), everything else probes outward from the builder.
// Returns true and the chosen (outX,outZ) if a spot was found.
bool Controller::placeSite(const tak::sim::World& world, const tak::sim::UnitType* t,
                           const tak::sim::Unit& builder, float nx, float nz, float& outX, float& outZ,
                           float limit) const {
    if (!t) return false;
    const auto home=homeOf(world);
    const bool aggressive=diff_==Difficulty::Hard || diff_==Difficulty::Absurd;
    const bool naval=navalFactory(t);
    const bool waterUnit=!t->isStructure() && !t->canFly && t->domain==tak::sim::UnitType::Domain::Water;
    const float homeRadius=!dp_.attack ? 600.0f : (builder.type->commander ? 900.0f : aggressive ? 100000.0f : 1600.0f);
    // A coastal outpost may be farther away than an ordinary base building.
    // Send an expansion builder; keep the Monarch and Defensive AI near home.
    const float navalRadius=builder.type->commander || !dp_.attack ? 900.0f : 2400.0f;
    const float fullRadius=naval ? std::max(homeRadius,navalRadius) : waterUnit ? std::max(homeRadius,900.0f) : homeRadius;
    const float radius=limit>0 ? std::min(fullRadius,limit) : fullRadius;
    // Reserve the largest lodestone this faction may use, including upgrades.
    // This is AI policy, not a change to the player's placement/navigation rules.
    int manaFootX=0,manaFootZ=0;
    if (t->isStructure() && !t->onMana && world.hasManaSpots())
        for (const auto& [id,type]:registry_.types())
            if (type.onMana && type.isStructure() && type.side==builder.type->side) {
                manaFootX=std::max(manaFootX,type.footX);
                manaFootZ=std::max(manaFootZ,type.footZ);
            }
    const bool factory=!registry_.buildable(t->id).empty();
    const auto* launches=naval ? &navalLaunchOffsets(t) : nullptr;
    // The Defensive AI does not send a builder into a fight: a site with a spotted,
    // armed enemy within kFightRadius waits until the defenders have cleared it.
    constexpr float kFightRadius=400;
    std::vector<std::pair<float,float>> fights;
    if (!dp_.attack) {
        std::vector<const tak::sim::Unit*> eyes;
        for (const auto& u:world.units())
            if (u.alive() && u.type && !u.embarked() && !u.underConstruction && world.allied(player_,u.player))
                eyes.push_back(&u);
        for (const auto& e:world.units()) {
            if (!e.alive() || !e.type || e.embarked() || e.underConstruction || e.cloaked ||
                world.isNeutralPlayer(e.player) || world.allied(player_,e.player)) continue;
            if (!e.type->canCapture && e.type->weapon.damage<=0 &&
                std::none_of(e.type->weapons.begin(),e.type->weapons.end(),[](const auto& w) {
                    return w.damage>0 || w.mindControl;
                })) continue;
            const float ex=e.x.toFloat(),ez=e.z.toFloat();
            const float hx=ex-home.first,hz=ez-home.second,reach=radius+kFightRadius;
            if (hx*hx+hz*hz>reach*reach) continue;
            if (std::any_of(eyes.begin(),eyes.end(),[&](const tak::sim::Unit* eye) {
                const float r=std::max(world.sightDistance(*eye->type),world.radarDistance(*eye->type));
                const float dx=ex-eye->x.toFloat(),dz=ez-eye->z.toFloat();return dx*dx+dz*dz<=r*r;
            })) fights.emplace_back(ex,ez);
        }
    }
    auto usable=[&](float x,float z) {
        const float dx=x-home.first,dz=z-home.second;
        if (dx*dx+dz*dz>radius*radius || !world.canPlace(t,x,z,player_)) return false;
        if (std::any_of(fights.begin(),fights.end(),[&](const auto& f) {
            const float fx=f.first-x,fz=f.second-z;return fx*fx+fz*fz<kFightRadius*kFightRadius;
        })) return false;
        float approachX=x,approachZ=z;
        if (launches && !launches->empty()) {
            bool output=false;
            for (const auto& id:registry_.buildable(t->id)) {
                const auto* ship=registry_.find(id);
                const auto wi=profile_.weight.find(id),li=profile_.limit.find(id);
                if (!ship || ship->isBuilder || !world.buildAllowed(ship) ||
                    wi==profile_.weight.end() || wi->second<=0 || (li!=profile_.limit.end() && li->second==0)) continue;
                if (std::all_of(launches->begin(),launches->end(),[&](const auto& offset) {
                    return world.canPlace(ship,x+offset.first,z+offset.second) &&
                        navalLaunchClear(world,t,ship,x,z,offset.first,offset.second);
                })) {
                    float tx,tz;
                    const auto& offset=launches->front();
                    output=enemyStarts_.empty() || std::any_of(enemyStarts_.begin(),enemyStarts_.end(),[&](const auto& enemy) {
                        return reachablePoint(world,ship,x+offset.first,z+offset.second,enemy.first,enemy.second,tx,tz);
                    });
                    if (output) break;
                }
            }
            if (!output) return false;
        }
        if (manaFootX>0) for (const auto& [mx,mz]:world.manaSpots()) {
            const int x0=tak::sim::footprintOrigin(x,t->footX);
            const int z0=tak::sim::footprintOrigin(z,t->footZ);
            const int mx0=tak::sim::footprintOrigin(mx,manaFootX);
            const int mz0=tak::sim::footprintOrigin(mz,manaFootZ);
            if (x0<mx0+manaFootX && x0+t->footX>mx0 &&
                z0<mz0+manaFootZ && z0+t->footZ>mz0) return false;
            const float mdx=x-mx,mdz=z-mz;
            const float clearance=float(std::max(manaFootX,manaFootZ))*8+12;
            if (mdx*mdx+mdz*mdz<clearance*clearance) return false;
            // The lodestone planner also leaves working room around factories.
            if (factory && std::abs(mdx)<float(t->footX+manaFootX)*8+48 &&
                std::abs(mdz)<float(t->footZ+manaFootZ)*8+48) return false;
        }
        if (!builder.type->canFly) {
            // Check the position where MobileBuild will bring the constructor,
            // too. A rectangular factory can be clear now but reject its own
            // builder after a short-side approach enters the placement radius.
            const int sx=tak::sim::footprintOrigin(x,t->footX);
            const int sz=tak::sim::footprintOrigin(z,t->footZ);
            const tak::sim::RetailRectGoal rectangle{sx-builder.type->footX,sx+t->footX,
                                                     sz-builder.type->footZ,sz+t->footZ};
            const auto [cx,cz]=rectangle.navigationCell(
                tak::sim::footprintOrigin(builder.x,builder.type->footX),
                tak::sim::footprintOrigin(builder.z,builder.type->footZ));
            if (waterUnit || world.lodestoneUpgradeSource(t,x,z,player_)) {
                approachX=float(cx*16+builder.type->footX*8);
                approachZ=float(cz*16+builder.type->footZ*8);
            }
            if (waterUnit) {
                const auto& nav=world.navFor(builder.type);
                const int foot=std::max(builder.type->footX,builder.type->footZ);
                auto reachable=[&](float px,float pz) {
                    return nav.fits(tak::sim::footprintCell(px,foot),tak::sim::footprintCell(pz,foot),foot) &&
                        tak::sim::retailBuildWithinReach(tak::sim::Fixed::fromFloat(px),tak::sim::Fixed::fromFloat(pz),
                            tak::sim::Fixed::fromFloat(x),tak::sim::Fixed::fromFloat(z),builder.type->footX,builder.type->footZ,
                            t->footX,t->footZ,uint16_t(builder.type->buildDist)) &&
                        world.pathExists(builder.type,px,pz,builder.x.toFloat(),builder.z.toFloat());
                };
                bool reach=reachable(approachX,approachZ);
                for (int r=32;r<=builder.type->buildDist+std::max(t->footX,t->footZ)*8+16 && !reach;r+=16)
                    for (int i=0;i<16 && !reach;++i) {
                        const float angle=float(i)*0.392699082f;
                        const float px=x+detmath::cos(angle)*r,pz=z+detmath::sin(angle)*r;
                        if (reachable(px,pz)) {approachX=px;approachZ=pz;reach=true;}
                    }
                if (!reach) return false;
            }
            const float ax=float(cx*16+builder.type->footX*8)-x;
            const float az=float(cz*16+builder.type->footZ*8)-z;
            const float clearance=float(std::max(t->footX,t->footZ))*8+12;
            if (ax*ax+az*az<clearance*clearance) return false;
        }
        for (const auto& u : world.units()) {
            if (!u.alive() || u.player!=player_ || !u.type || !u.type->isStructure()) continue;
            if (registry_.buildable(u.type->id).empty() && registry_.buildable(t->id).empty()) continue;
            // Leave working room around production buildings, including their
            // scripted output point. Placement validity alone allows blocked doors.
            if (std::abs(x-u.x.toFloat()) < float(t->footX+u.type->footX)*8+48 &&
                std::abs(z-u.z.toFloat()) < float(t->footZ+u.type->footZ)*8+48) return false;
        }
        if (!builder.type->canFly && !world.pathExists(builder.type,approachX,approachZ,builder.x.toFloat(),builder.z.toFloat())) return false;
        if (waterUnit && !enemyStarts_.empty()) {
            float tx,tz;
            for (const auto& enemy:enemyStarts_)
                if (reachablePoint(world,t,x,z,enemy.first,enemy.second,tx,tz)) return true;
            return false;
        }
        return true;
    };
    if (t->onMana && world.hasManaSpots()) {
        float bestD = 1e18f;
        bool found = false;
        // Every difficulty reserves: Normal allies, and two builders of one AI,
        // raced for the same free deposit as Hard and Absurd once did.
        std::vector<std::pair<float,float>> reservedSites=plannedManaSites_;
        // Walking builders do not occupy their deposits yet. Gather queued
        // destinations once, rather than scan every unit for every deposit.
        // Reserve the SITE (buildX/buildZ): a MobileBuild order's x/z is the
        // builder's approach point beside it (World::makeBuildOrder), 32-70 px
        // from the deposit for a lodestone, so it never matched -- own and
        // allied builders kept walking to a deposit already being claimed.
        // A flyer's conjure orbit carries no site; its building already stands.
        // A builder that has stalled short of its deposit holds it no longer
        // (updateBuilderProgress).
        for (const auto& u:world.units()) {
            if(!u.alive() || u.id==builder.id || !world.allied(player_,u.player))continue;
            for(const auto& order:u.orders)
                if(order.buildType && order.buildType->onMana && (order.buildX.v || order.buildZ.v) &&
                   !stalledBuilder(u.id))
                    reservedSites.emplace_back(order.buildX.toFloat(),order.buildZ.toFloat());
        }
        const auto& deposits=world.manaSpots();
        for (size_t i=0;i<deposits.size();++i) {
            // Apple Clang 15 cannot capture structured bindings in this lambda.
            const float sx=deposits[i].first,sz=deposits[i].second;
            if (std::any_of(reservedSites.begin(),reservedSites.end(),[sx,sz](const auto& spot) {
                const float dx=spot.first-sx,dz=spot.second-sz;return dx*dx+dz*dz<32*32;
            })) continue;
            // Nor does this builder go back to a deposit it stalled on.
            if (std::any_of(stalledSites_.begin(),stalledSites_.end(),[&](const StalledSite& site) {
                const float dx=site.x-sx,dz=site.z-sz;return site.unit==builder.id && dx*dx+dz*dz<32*32;
            })) continue;
            // An ally still claiming its home deposit keeps it, and a nearer ally
            // takes it after (updateAllyClaims); upgrading our own lodestone
            // there is not a claim on it.
            if (i<allyHeldDeposits_.size() && allyHeldDeposits_[i] &&
                !world.lodestoneUpgradeSource(t,sx,sz,player_)) continue;
            if (!usable(sx, sz)) continue;   // taken or blocked
            float dx = sx - nx, dz = sz - nz, d = dx * dx + dz * dz;
            // Claim new income before replacing a nearby existing lodestone.
            if (aggressive && world.lodestoneUpgradeSource(t,sx,sz,player_)) d+=1e12f;
            if (d < bestD) { bestD = d; outX = sx; outZ = sz; found = true; }
        }
        return found;
    }
    const float searchRadius = naval ? std::min(radius,2400.0f) : t->minWaterDepth>0 ? std::min(radius,1600.0f) : 340.0f;
    for (float r = 70; r < searchRadius; r += 30)
        for (float a = 0; a < 6.28f; a += (naval || waterUnit) ? std::min(0.5f,48.0f/r) : 0.5f) {
            float x = tak::sim::footprintWaypoint(tak::sim::footprintCell(nx + detmath::cos(a) * r,t->footX),t->footX).toFloat();
            float z = tak::sim::footprintWaypoint(tak::sim::footprintCell(nz + detmath::sin(a) * r,t->footZ),t->footZ).toFloat();
            if (usable(x, z)) { outX = x; outZ = z; return true; }
        }
    return false;
}

// Nearest enemy the group at (cx,cz) can SEE (some AI unit within sight/radar of it)
// AND can actually REACH (flow connectivity), scanning closest-first. Fog: the AI
// never targets a unit it hasn't spotted; reachability keeps it off walled-in foes.
bool Controller::nearestVisibleEnemy(const tak::sim::World& world, float cx, float cz,
                                     const tak::sim::UnitType* atype,
                                     float& tx, float& tz) const {
    // My eyes: each own unit reveals a radius of max(sight, radar) around itself.
    struct Eye { float x, z, r2; };
    std::vector<Eye> eyes;
    for (auto& u : world.units())
        if (u.alive() && u.player == player_ && u.type && !u.embarked() &&
            !u.underConstruction) {   // a half-built unit has no eyes yet
            float s = std::max(world.sightDistance(*u.type), world.radarDistance(*u.type));
            eyes.push_back({u.x.toFloat(), u.z.toFloat(), s * s});
        }
    if (eyes.empty()) return false;
    // Sort candidates before testing visibility. Only the first 16 visible
    // positions can affect the result, and usually the first is reachable.
    // The key is identical to the old visible-only sort, including x/z ties.
    std::vector<std::pair<float, std::pair<float, float>>> candidates;
    for (auto& e : world.units()) {
        if (!e.alive() || e.embarked() || world.isNeutralPlayer(e.player) || world.allied(e.player, player_) || !e.type)
            continue;
        float dx = e.x.toFloat() - cx, dz = e.z.toFloat() - cz;
        candidates.push_back({dx * dx + dz * dz, {e.x.toFloat(), e.z.toFloat()}});
    }
    std::sort(candidates.begin(), candidates.end());
    int checked = 0;
    for (auto& e : candidates) {
        bool seen = false;
        for (const Eye& eye : eyes) {
            float dx = e.second.first - eye.x, dz = e.second.second - eye.z;
            if (dx * dx + dz * dz <= eye.r2) { seen = true; break; }
        }
        if (!seen) continue;
        if (++checked > 16) break;   // bound the reachability probes (flow builds)
        if (!atype || world.pathExists(atype, e.second.first, e.second.second, cx, cz)) {
            tx = e.second.first; tz = e.second.second;
            return true;
        }
    }
    return false;
}

// The nearest KNOWN enemy start position -- where the AI marches when fog hides the
// enemy, so the army pushes into a base (and spots its defenders) instead of idling.
bool Controller::nearestEnemyStart(float cx, float cz, float& tx, float& tz) const {
    float best = 1e18f;
    bool found = false;
    for (const auto& [x, z] : enemyStarts_) {
        float dx = x - cx, dz = z - cz, d = dx * dx + dz * dz;
        if (d < best) { best = d; tx = x; tz = z; found = true; }
    }
    return found;
}

// A player's base anchor: the centroid of its buildings, else its Monarch, else
// the centroid of its units (0,0 with none).
static std::pair<float, float> anchorOf(const tak::sim::World& world, int player) {
    double sx = 0, sz = 0; int n = 0;
    float kx = 0, kz = 0; bool haveKing = false;
    for (const auto& u : world.units()) {
        if (!u.alive() || u.player != player || !u.type) continue;
        if (u.type->isStructure()) { sx += u.x.toFloat(); sz += u.z.toFloat(); ++n; }
        if (u.type->commander && !haveKing) { kx = u.x.toFloat(); kz = u.z.toFloat(); haveKing = true; }
    }
    if (n) return {float(sx / n), float(sz / n)};   // centroid of the buildings
    if (haveKing) return {kx, kz};                  // no buildings yet: anchor on the Monarch
    for (const auto& u : world.units())
        if (u.alive() && u.player==player) { sx+=u.x.toFloat();sz+=u.z.toFloat();++n; }
    return n ? std::pair{float(sx/n),float(sz/n)} : std::pair{0.0f,0.0f};
}

// Pool idle (non-builder) fighters; once a strike force has gathered (waveSize, per
// difficulty), attack-move the whole group at ONE target so the wave arrives together
// rather than trickling in: the nearest enemy it can SEE, else the nearest enemy start
// (marching on the base). The original reason given here was that one target let them
// share a flow field; the flow fields are gone and paths are per-unit now, so arriving
// as a group is the whole of it.
// Also sends one early scout so the AI reveals + commits rather than turtling forever.
std::pair<float, float> Controller::homeOf(const tak::sim::World& world) const {
    if (home_) return *home_;
    return anchorOf(world, player_);
}

// Whether a builder of this type can claim a deposit: a lodestone in its menu.
bool Controller::buildsLodestone(const tak::sim::UnitType* type) const {
    auto [it,inserted]=lodestoneBuilders_.try_emplace(type,false);
    if (inserted)
        for (const auto& id:registry_.buildable(type->id)) {
            const auto* child=registry_.find(id);
            if (child && child->onMana && child->isStructure()) { it->second=true; break; }
        }
    return it->second;
}

// Hard/Absurd expand map-wide, and Normal's and Easy's builders 1600 px from home
// (Defensive's 600), far enough to reach a close ally's base. At every difficulty
// a free deposit in a living ally's home area -- within kAllyHome of its start
// and nearer that start than ours -- stays the ally's while the ally is claiming it:
//  - a builder is bound for it (placeSite's reservation never races one, the
//    ally's, another ally's or our own, while it is still getting there), or
//  - the ally has a builder that can reach it -- finished, not carried, not
//    disabled, with a lodestone in its menu -- and it has not sat unclaimed for
//    kAllyClaimTicks.
// Unclaimed means no lodestone on it and no builder bound for it, counted in game
// ticks from the first think or from the end of its last claim, so a lodestone
// lost in a raid gives the ally the same two minutes to rebuild; a builder's
// claim ends when it last gained on the deposit (updateBuilderProgress). A
// defeated ally claims nothing. Retail's AI never builds there at all -- icd
// 411510 takes the nearest free sacred site within its own base radius -- but
// leaving deposits to an ally that never takes them (a Normal or Defensive AI, a
// human who does not expand) cost a mixed team 20-35% of its income.
// Once the ally whose home it is no longer holds it -- two minutes unclaimed, or
// no builder of its own that can reach it (its Monarch lost under Monarch
// Expendable) -- the deposit goes to whichever other living ally's start is
// nearest it, not to all of them: a far ally's builder, once bound for it, held
// it for its whole walk across the map while the nearer allies, the owner
// included, could not race it (Aramon reserved Zhon's deposits from 6,000 map
// units away while Zhon's builders went to the enemy's base). Its cost: a
// deposit whose nearest other ally never builds there -- a human, or a Normal
// AI whose builders do not reach that far -- stays empty, though a farther AI
// would take it.
void Controller::updateAllyClaims(const tak::sim::World& world, uint32_t simTick) {
    constexpr float kAllyHome=900;
    constexpr uint32_t kAllyClaimTicks=2*60*30;   // two minutes of game time
    const auto& deposits=world.manaSpots();
    if (depositClaimedAt_.size()!=deposits.size()) depositClaimedAt_.assign(deposits.size(),simTick);
    allyHeldDeposits_.assign(deposits.size(),0);
    // Every lodestone standing and every site an allied builder is bound for
    // and still getting to, each with the tick it last counted, and each ally's
    // builders that could claim a deposit.
    struct Claim { float x, z; uint32_t tick; };
    std::vector<Claim> claims;
    std::vector<std::vector<const tak::sim::Unit*>> claimers(allyHomes_.size());
    for (const auto& u:world.units()) {
        if (!u.alive() || !u.type) continue;
        if (u.type->onMana) claims.push_back({u.x.toFloat(),u.z.toFloat(),simTick});
        if (!world.allied(player_,u.player)) continue;
        const auto progress=builderProgress_.find(u.id);
        if (progress==builderProgress_.end() || !progress->second.stalled)
            for (const auto& order:u.orders)
                if (order.buildType && order.buildType->onMana && (order.buildX.v || order.buildZ.v))
                    claims.push_back({order.buildX.toFloat(),order.buildZ.toFloat(),
                                      progress==builderProgress_.end() ? simTick : progress->second.gainedAt});
        if (u.type->isStructure() || !u.type->isBuilder || u.underConstruction || u.embarked() ||
            u.incapacitated() || !buildsLodestone(u.type)) continue;
        for (size_t a=0;a<allyHomes_.size();++a)
            if (allyHomes_[a].player==u.player) claimers[a].push_back(&u);
    }
    const auto home=homeOf(world);
    for (size_t i=0;i<deposits.size();++i) {
        const float sx=deposits[i].first,sz=deposits[i].second;
        // Lodestone placement keeps 44 px between lodestones: a claim that near
        // takes this deposit too.
        bool claimed=false;
        for (const auto& claim:claims) {
            const float dx=claim.x-sx,dz=claim.z-sz;
            if (dx*dx+dz*dz<44*44) { claimed=true; depositClaimedAt_[i]=std::max(depositClaimedAt_[i],claim.tick); }
        }
        if (claimed) continue;
        const float hx=sx-home.first,hz=sz-home.second,own=hx*hx+hz*hz;
        float nearest=std::min(kAllyHome*kAllyHome,own);
        size_t owner=allyHomes_.size();
        for (size_t a=0;a<allyHomes_.size();++a) {
            const float dx=sx-allyHomes_[a].x,dz=sz-allyHomes_[a].z,d=dx*dx+dz*dz;
            if (d<nearest && !world.player(allyHomes_[a].player).defeated) { nearest=d; owner=a; }
        }
        if (owner==allyHomes_.size()) continue;   // nobody's home but perhaps ours
        if (simTick-depositClaimedAt_[i]<kAllyClaimTicks &&
            std::any_of(claimers[owner].begin(),claimers[owner].end(),[&](const tak::sim::Unit* b) {
                return b->type->canFly || world.pathExists(b->type,sx,sz,b->x.toFloat(),b->z.toFloat());
            })) { allyHeldDeposits_[i]=1; continue; }
        allyHeldDeposits_[i]=std::any_of(allyHomes_.begin(),allyHomes_.end(),[&](const AllyHome& ally) {
            const float dx=sx-ally.x,dz=sz-ally.z;
            return &ally!=&allyHomes_[owner] && !world.player(ally.player).defeated && dx*dx+dz*dz<own;
        });
    }
}

// A builder bound for a deposit can stall short of it -- wedged in a crowd, or on
// ground its route does not clear -- while its order still reserves the deposit
// for every ally and claims it for the deposit's owner. In a 4v4 a Zhon handler
// sent 3,300 map units into a human ally's base moved 109 px in 440 s, and no
// ally built there for the rest of the game. So every think follows each allied
// builder whose orders hold a deposit on the task it is on now, the leg the sim
// is working (World::currentLeg): the lodestone itself for an AI's builder, but
// a human's may wait behind a move, a repair or another building. It notes the
// nearest the builder has come to that task's point (its site, or the leg's
// goal) and the tick it last came kGain nearer or was at work -- building, or
// repairing or reclaiming within reach (the sim assigns those jobs before the
// builder gets there) -- and a new task counts as a gain. One that has not for
// kBuilderStallTicks is stalled: placeSite no longer reserves its deposits,
// updateAllyClaims no longer counts them claimed (the claim ended when it last
// gained), and when it is ours this AI stops it, so that it is idle to take
// other work, and never sends it back to those deposits. A builder walking
// across the map gains every few seconds and keeps its claim however long the
// walk. One that arrives to find units on its site gives up on its own within
// the sim's 31 retries (310 ticks).
void Controller::updateBuilderProgress(const tak::sim::World& world, uint32_t simTick,
                                       const CommandSink& sink) {
    constexpr float kGain=64;
    constexpr uint32_t kBuilderStallTicks=60*30;   // a minute of game time
    std::erase_if(stalledSites_,[&](const StalledSite& site) {
        const auto* u=world.unit(site.unit);return !u || !u->alive();
    });
    auto holds=[](const tak::sim::Order& order) {
        return order.buildType && order.buildType->onMana && (order.buildX.v || order.buildZ.v);
    };
    std::unordered_map<int,BuilderProgress> next;
    for (const auto& u:world.units()) {
        if (!u.alive() || !u.type || !world.allied(player_,u.player) ||
            std::none_of(u.orders.begin(),u.orders.end(),holds)) continue;
        const auto& task=u.orders[tak::sim::World::currentLeg(u.orders)];
        const bool site=task.buildType && (task.buildX.v || task.buildZ.v);
        const float sx=(site ? task.buildX : task.x).toFloat(),sz=(site ? task.buildZ : task.z).toFloat();
        const float dx=u.x.toFloat()-sx,dz=u.z.toFloat()-sz,d=std::sqrt(dx*dx+dz*dz);
        BuilderProgress progress{sx,sz,d,simTick,false};
        const auto last=builderProgress_.find(u.id);
        if (last!=builderProgress_.end() && last->second.siteX==sx && last->second.siteZ==sz) {
            progress=last->second;
            if (u.buildSiteId || u.workScriptWorking || d<=progress.best-kGain) {
                progress.best=std::min(progress.best,d);
                progress.gainedAt=simTick;
            }
        }
        progress.stalled=simTick-progress.gainedAt>=kBuilderStallTicks;
        next.emplace(u.id,progress);
        if (!progress.stalled || u.player!=player_) continue;
        bool known=false;
        for (const auto& order:u.orders) {
            if (!holds(order)) continue;
            const float ox=order.buildX.toFloat(),oz=order.buildZ.toFloat();
            const bool listed=std::any_of(stalledSites_.begin(),stalledSites_.end(),[&](const StalledSite& site) {
                return site.unit==u.id && site.x==ox && site.z==oz;
            });
            known=known || listed;
            if (!listed) stalledSites_.push_back({u.id,ox,oz});
        }
        if (known) continue;   // stopped already; the Stop has not reached the world yet
        tak::net::Command stop;stop.kind=tak::net::Cmd::Stop;stop.player=uint8_t(player_);stop.unitId=u.id;
        sink(stop);
    }
    builderProgress_.swap(next);
}

bool Controller::stalledBuilder(int unitId) const {
    const auto progress=builderProgress_.find(unitId);
    return progress!=builderProgress_.end() && progress->second.stalled;
}

void Controller::sendDefenders(const tak::sim::World& world, const CommandSink& sink) {
    constexpr float kHomeRadius = 900, kSiteRadius = 400;
    // An enemy shooting into the territory from outside it is answered out to this
    // far from home, and melee defenders give up the chase beyond it.
    constexpr float kPursuitRadius = kHomeRadius + 600, kMeleeRange = 64;
    constexpr int kMaxCommands = 256, kMaxThreats = 16;
    const auto home=homeOf(world);
    struct Eye { float x,z,r2; };
    std::vector<Eye> eyes;
    std::vector<std::pair<float,float>> sites;
    for (const auto& u:world.units()) {
        if (!u.alive() || !u.type || u.embarked()) continue;
        if (world.allied(player_,u.player) && !u.underConstruction) {
            const float r=std::max(world.sightDistance(*u.type),world.radarDistance(*u.type));
            eyes.push_back({u.x.toFloat(),u.z.toFloat(),r*r});
        }
        if (u.player==player_ && (u.type->isStructure() || u.type->commander ||
            categoryOf(u.type)==BuildCat::Factory)) sites.emplace_back(u.x.toFloat(),u.z.toFloat());
    }
    auto local=[&](float x,float z) {
        const float dx=x-home.first,dz=z-home.second;
        if (dx*dx+dz*dz<=kHomeRadius*kHomeRadius) return true;
        return std::any_of(sites.begin(),sites.end(),[&](const auto& site) {
            const float sx=x-site.first,sz=z-site.second;
            return sx*sx+sz*sz<=kSiteRadius*kSiteRadius;
        });
    };
    // Only spotted threats to our territory mobilize the army. Enemy starts and
    // distant enemies revealed by a scout must never become defensive objectives.
    std::vector<std::pair<float,int>> threats;
    for (const auto& e:world.units()) {
        if (!e.alive() || !e.type || e.embarked() || e.underConstruction || e.cloaked ||
            world.isNeutralPlayer(e.player) || world.allied(player_,e.player)) continue;
        if (!e.type->canCapture && e.type->weapon.damage<=0 && !e.type->weapon.mindControl &&
            std::none_of(e.type->weapons.begin(),e.type->weapons.end(),[](const auto& weapon) {
                return weapon.damage>0 || weapon.mindControl;
            })) continue;
        const float x=e.x.toFloat(),z=e.z.toFloat();
        if (!local(x,z) || std::none_of(eyes.begin(),eyes.end(),[&](const Eye& eye) {
            const float dx=x-eye.x,dz=z-eye.z;return dx*dx+dz*dz<=eye.r2;
        })) continue;
        const float dx=x-home.first,dz=z-home.second;
        threats.emplace_back(dx*dx+dz*dz,e.id);
    }
    // Whoever is hitting our units inside the territory is a threat too, seen or
    // not ("we know where the arrows come from"): archers that outrange both our
    // sight and the defended radius otherwise pick a parked army apart. Bounded to
    // attackers still within their reach of the victim and near enough to home.
    // A defender we sent out past the territory still names whoever is hitting
    // it; otherwise the archers it was sent at drop off the list as soon as it
    // leaves the radius, and it is retargeted or walked home under fire.
    auto nearHome=[&](float x,float z) {
        const float dx=x-home.first,dz=z-home.second;
        return dx*dx+dz*dz<=kPursuitRadius*kPursuitRadius;
    };
    auto attackerOf=[&](const tak::sim::Unit& v)->const tak::sim::Unit* {
        const auto* e=v.lastHitBy ? world.unit(v.lastHitBy) : nullptr;
        if (!e || !e->alive() || !e->type || e->embarked() || e->underConstruction ||
            world.isNeutralPlayer(e->player) || world.allied(player_,e->player)) return nullptr;
        const float reach=std::max(float(e->type->weapon.range),e->type->maxRange())+64;
        const float dx=e->x.toFloat()-v.x.toFloat(),dz=e->z.toFloat()-v.z.toFloat();
        if (dx*dx+dz*dz>reach*reach || !nearHome(e->x.toFloat(),e->z.toFloat())) return nullptr;
        return e;
    };
    std::vector<int> shooters;
    for (const auto& v:world.units()) {
        if (!v.alive() || v.player!=player_ || !v.lastHitBy || v.embarked()) continue;
        const auto sent=defenders_.find(v.id);
        if (!local(v.x.toFloat(),v.z.toFloat()) &&
            (sent==defenders_.end() || sent->second.returning)) continue;
        const auto* e=attackerOf(v);
        if (!e) continue;
        if (std::any_of(threats.begin(),threats.end(),[&](const auto& t){return t.second==e->id;}) ||
            std::find(shooters.begin(),shooters.end(),e->id)!=shooters.end()) continue;
        shooters.push_back(e->id);
        const float hx=e->x.toFloat()-home.first,hz=e->z.toFloat()-home.second;
        threats.emplace_back(hx*hx+hz*hz,e->id);
    }
    std::sort(threats.begin(),threats.end());
    // Every threat may be answered by the unit it is hitting, and keeps a melee
    // defender already chasing it; only the general response picks among the
    // kMaxThreats nearest home.
    std::vector<int> threatIds;
    for (const auto& t:threats) threatIds.push_back(t.second);
    std::sort(threatIds.begin(),threatIds.end());
    auto isThreat=[&](int id){return std::binary_search(threatIds.begin(),threatIds.end(),id);};
    if (threats.size()>kMaxThreats) threats.resize(kMaxThreats);
    auto shooter=[&](int id){return std::find(shooters.begin(),shooters.end(),id)!=shooters.end();};
    auto chased=[&](int id) {
        if (isThreat(id)) return true;
        const auto* e=world.unit(id);
        return e && e->alive() && e->type && !e->embarked() && !e->cloaked &&
            !world.isNeutralPlayer(e->player) && !world.allied(player_,e->player) &&
            nearHome(e->x.toFloat(),e->z.toFloat());
    };
    for (auto it=defenders_.begin();it!=defenders_.end();) {
        const auto* u=world.unit(it->first);
        if (!u || !u->alive() || u->player!=player_) it=defenders_.erase(it);
        else ++it;
    }
    int issued=0;
    // World order, rather than unordered-map iteration, keeps command emission
    // repeatable. Reachability uses the existing cached component queries.
    for (const auto& u:world.units()) {
        if (issued==kMaxCommands) break;
        if (!u.alive() || u.player!=player_ || !u.type || u.type->isStructure() ||
            u.type->isBuilder || u.type->commander || u.underConstruction || u.embarked() ||
            u.incapacitated() || u.type->weapon.damage<=0) continue;
        auto response=defenders_.find(u.id);
        const bool assigned=response!=defenders_.end();
        const bool melee=std::max(float(u.type->weapon.range),u.type->maxRange())<=kMeleeRange;
        const bool ownsOrder=assigned && std::any_of(u.orders.begin(),u.orders.end(),[&](const auto& order) {
            if (response->second.targetId)
                return !response->second.returning && order.targetId==response->second.targetId && !order.attackMove;
            if (order.targetId || order.buildType || order.patrol || order.guard || order.load || order.unload)
                return false;
            const auto goal=order.missionTarget.value_or(std::pair{order.x,order.z});
            return std::abs(goal.first.toFloat()-response->second.goalX)<1 &&
                std::abs(goal.second.toFloat()-response->second.goalZ)<1 &&
                order.attackMove!=response->second.returning;
        });
        if (!waveFree(u) && !ownsOrder) {
            if (assigned) defenders_.erase(response);
            continue; // preserve unrelated explicit attacks and working orders
        }
        float tx=0,tz=0;
        auto answer=[&](const tak::sim::Unit& e) {
            return reachablePoint(world,u.type,u.x.toFloat(),u.z.toFloat(),
                e.x.toFloat(),e.z.toFloat(),tx,tz) &&
                (shooter(e.id) ? nearHome(tx,tz) : local(tx,tz));
        };
        // A melee defender goes for the unit hitting it, not for whichever threat
        // is nearest home: a Swordsman shot from 400 away has nothing else to do.
        const tak::sim::Unit* attacker=nullptr;
        if (melee) if (const auto* e=attackerOf(u);e && isThreat(e->id)) attacker=e;
        // It keeps chasing its target while that stays within kPursuitRadius,
        // instead of switching whenever another one edges nearer to home or the
        // target briefly drops off the list (an archer is listed through the
        // last unit it hit, which changes shot by shot in a fight) -- unless it
        // was sent at someone else and is now being hit itself.
        if (ownsOrder && response->second.targetId && chased(response->second.targetId) &&
            (response->second.ownAttacker || !attacker || attacker->id==response->second.targetId))
            continue;
        int targetId=0;
        bool ownAttacker=false;
        if (attacker && answer(*attacker)) {targetId=attacker->id;ownAttacker=true;}
        else if (melee) {
            // Otherwise the threat nearest the unit, so that a melee line spreads over
            // the attackers rather than all walking at the one nearest home.
            std::vector<std::pair<float,int>> near;
            for (const auto& threat:threats) if (const auto* e=world.unit(threat.second)) {
                const float dx=e->x.toFloat()-u.x.toFloat(),dz=e->z.toFloat()-u.z.toFloat();
                near.emplace_back(dx*dx+dz*dz,e->id);
            }
            std::sort(near.begin(),near.end());
            for (const auto& [d,id]:near) if (answer(*world.unit(id))) {targetId=id;break;}
        } else {
            for (const auto& threat:threats) {
                const auto* e=world.unit(threat.second);
                if (e && answer(*e)) {targetId=e->id;break;}
            }
        }
        if (targetId && melee) {
            // Melee goes straight for the attacker: a direct Attack is not held back
            // by the Defensive stance, so it closes on an archer that backs away. The
            // leash is kPursuitRadius -- once the target leaves it and is no threat,
            // the unit is retargeted or walked home below.
            if (assigned && ownsOrder && response->second.targetId==targetId) {
                response->second.ownAttacker=ownAttacker;continue;
            }
            DefenseResponse next=assigned ? response->second :
                DefenseResponse{u.x.toFloat(),u.z.toFloat(),tx,tz};
            next.goalX=tx;next.goalZ=tz;next.returning=false;next.targetId=targetId;
            next.ownAttacker=ownAttacker;
            defenders_[u.id]=next;
            tak::net::Command c;c.kind=tak::net::Cmd::Attack;c.player=uint8_t(player_);
            c.unitId=u.id;c.targetId=targetId;
            sink(c);++issued;
        } else if (targetId) {
            if (assigned && !response->second.returning && !response->second.targetId) {
                const float dx=tx-response->second.goalX,dz=tz-response->second.goalZ;
                const float ux=u.x.toFloat()-tx,uz=u.z.toFloat()-tz;
                if (dx*dx+dz*dz<96*96 && (ownsOrder ||
                    (u.orders.empty() && ux*ux+uz*uz<=48*48))) continue;
            }
            DefenseResponse next=assigned ? response->second :
                DefenseResponse{u.x.toFloat(),u.z.toFloat(),tx,tz};
            next.goalX=tx;next.goalZ=tz;next.returning=false;next.targetId=0;next.ownAttacker=false;
            defenders_[u.id]=next;
            emit(sink,tak::net::Cmd::AttackMove,u.id,"",tx,tz);++issued;
        } else if (assigned) {
            auto& next=response->second;
            const float dx=u.x.toFloat()-next.homeX,dz=u.z.toFloat()-next.homeZ;
            if (u.orders.empty() && (next.returning || dx*dx+dz*dz<=48*48)) {
                defenders_.erase(response);continue;
            }
            if (ownsOrder && next.returning) continue;
            next.goalX=next.homeX;next.goalZ=next.homeZ;next.returning=true;next.targetId=0;next.ownAttacker=false;
            emit(sink,tak::net::Cmd::Move,u.id,"",next.homeX,next.homeZ);++issued;
        }
    }
}

void Controller::sendWaves(const tak::sim::World& world, uint32_t simTick,
                           const CommandSink& sink) {
    if (diff_ == Difficulty::Easy &&
        (simTick < 4*60*30 || (scouted_ && simTick-lastRaidTick_ < 2*60*30))) return;
    if (!dp_.attack) return;   // Defensive responses run separately below.
    struct Fighter { int id; float x,z; };
    std::vector<Fighter> idle;
    const auto home=homeOf(world);
    float objectiveX=0,objectiveZ=0;
    if (!nearestVisibleEnemy(world,home.first,home.second,nullptr,objectiveX,objectiveZ) &&
        !nearestEnemyStart(home.first,home.second,objectiveX,objectiveZ)) return;
    for (const auto& u : world.units()) {
        if (!u.alive() || u.player!=player_ || !u.type || u.underConstruction || u.embarked() ||
            u.type->isStructure() || u.type->isBuilder || u.type->weapon.damage<=0 || !waveFree(u)) continue;
        float tx=0,tz=0;
        bool reached=reachablePoint(world,u.type,u.x.toFloat(),u.z.toFloat(),objectiveX,objectiveZ,tx,tz);
        if (!reached) for (const auto& [ex,ez] : enemyStarts_) {
            if ((reached=reachablePoint(world,u.type,u.x.toFloat(),u.z.toFloat(),ex,ez,tx,tz))) break;
        }
        if (reached) idle.push_back({u.id,tx,tz});
    }
    if (idle.empty()) return;

    // The "big push" army scales with mana INCOME: a rich economy masses a large army
    // before it commits, a lean one strikes with less. So a strong AI stops trickling
    // its units into the enemy and instead builds an overwhelming force. A tapped-out
    // economy still waits for the minimum wave instead of feeding in single units.
    const auto& me = world.player(player_);
    float income = me.income;   // scale the force with actual income
    int bigPush = std::clamp(dp_.waveSize + int(income * 0.25f), dp_.waveSize, 60);

    if (diff_ == Difficulty::Easy) bigPush = std::min(bigPush, 8);
    // A faction may have only a few permitted ships (Easy Aramon has two),
    // while its inland army inflates the economy-based wave target. Let a
    // small fleet sail without waiting for troops that cannot cross the sea.
    // Amphibious (hover) units are that fleet too when no ground unit can reach
    // the objective -- Taros has no ships, only the Rictus and Lich. Where the
    // ground army can walk there, they stay with it for the big push.
    const bool groundReaches=std::any_of(idle.begin(),idle.end(),[&](const auto& f) {
        const auto* u=world.unit(f.id);
        return u && !u->type->canFly && u->type->domain==tak::sim::UnitType::Domain::Ground;
    });
    auto seaborne=[&](const tak::sim::Unit* u) {
        return u && !u->type->canFly && (u->type->domain==tak::sim::UnitType::Domain::Water ||
            (!groundReaches && u->type->domain==tak::sim::UnitType::Domain::Hover));
    };
    const int navalCount=int(std::count_if(idle.begin(),idle.end(),[&](const auto& f) {
        return seaborne(world.unit(f.id));
    }));
    if (int(idle.size())<bigPush && navalCount>=(diff_==Difficulty::Easy ? 2 : 3) &&
        (!scouted_ || simTick-lastRaidTick_>=25*30)) {
        int sent=0;
        for (const auto& f:idle) {
            if (!seaborne(world.unit(f.id))) continue;
            emit(sink,tak::net::Cmd::AttackMove,f.id,"",f.x,f.z);
            if (++sent==256) break;
        }
        lastRaidTick_=simTick;scouted_=true;
        return;
    }
    if (int(idle.size()) >= bigPush) {
        // Commit the army -- but cap commands per think so a huge force (a near-cap
        // game, or a stress test with thousands of units) doesn't emit one giant tick
        // bundle that blows the wire frame limit. The rest stay idle and deploy over
        // the next few thinks; all march to the same goal, so they still share a flow
        // field. kMaxWaveCmds keeps even several coincident AIs well under the cap.
        constexpr int kMaxWaveCmds = 256;
        int n = std::min(int(idle.size()), diff_ == Difficulty::Easy ? 8 : kMaxWaveCmds);
        for (int i = 0; i < n; ++i)
            emit(sink, tak::net::Cmd::AttackMove, idle[size_t(i)].id, "", idle[size_t(i)].x, idle[size_t(i)].z);
        lastRaidTick_ = simTick;      // let the freshly-built stragglers regroup, don't raid next
        scouted_ = true;
        raidersSincePush_=0;
        return;
    }

    // Still mustering the big army -- but keep HARASSING so the enemy is pressured and
    // revealed instead of the AI turtling behind a wall. Peel off a small raiding party
    // (leaving a home core to keep growing toward the big push), rate-limited so the
    // army isn't bled away piecemeal. The very first raid doubles as the early scout.
    constexpr uint32_t kRaidCooldown = 25 * 30;   // ~25s between raids
    int homeCore = std::max(dp_.waveSize, bigPush / 3);   // never raid below this reserve
    bool firstProbe = dp_.scout && !scouted_ && int(idle.size()) >= 1;
    bool canRaid = dp_.raidSize > 0 &&
                   raidersSincePush_ + dp_.raidSize <= std::max(dp_.raidSize,bigPush/3) &&
                   int(idle.size()) >= homeCore + dp_.raidSize &&
                   simTick - lastRaidTick_ >= kRaidCooldown;
    if (firstProbe || canRaid) {
        int party = firstProbe && !canRaid ? 1 : dp_.raidSize;   // opening scout is a lone unit
        for (int i = 0; i < party && i < int(idle.size()); ++i)
            emit(sink, tak::net::Cmd::AttackMove, idle[size_t(i)].id, "", idle[size_t(i)].x, idle[size_t(i)].z);
        scouted_ = true;
        raidersSincePush_+=party;
        lastRaidTick_ = simTick;
    }
}

void Controller::tick(const tak::sim::World& world, uint32_t simTick,
                      const CommandSink& sink) {
    // Think cadence (per difficulty), staggered by player so several AIs don't all
    // fire on the same tick. Sim-tick driven so a --mpai run is repeatable. Hard
    // reacts more often (thinkPeriod=20) than Easy (60).
    if ((simTick % uint32_t(dp_.thinkPeriod)) != uint32_t(player_ % dp_.thinkPeriod)) return;
    if (world.player(player_).defeated) return;

    if (!home_) home_=homeOf(world);
    if (!allyHomesKnown_) {
        allyHomesKnown_=true;
        for (int p=0;p<world.numPlayers();++p)
            if (p!=player_ && world.allied(player_,p) &&
                std::any_of(world.units().begin(),world.units().end(),[p](const auto& u){return u.alive() && u.player==p;})) {
                const auto [x,z]=anchorOf(world,p);
                allyHomes_.push_back({p,x,z});
            }
    }
    if (world.hasManaSpots()) updateBuilderProgress(world,simTick,sink);
    if (!allyHomes_.empty() && world.hasManaSpots()) updateAllyClaims(world,simTick);
    plannedManaSites_.clear();
    Needs needs = assessNeeds(world);   // one empire assessment drives every producer
    // Snapshot the idle producers, then act on up to producersPerThink of them -- the
    // per-think cap is what paces the economy across difficulties (Easy builds one
    // thing per think, Hard many).
    std::vector<int> producers;
    for (auto& u : world.units()) {
        if (!u.alive() || u.player != player_ || !u.type) continue;
        // A building can carry canMove=1 in its FBI (the Keep, the Taros Hell), so
        // classify by isStructure() (maxVel), not canMove -- otherwise a canMove
        // building is mistaken for a mobile builder and issued Build orders it can't
        // honour. And never drive a producer that is itself still under construction.
        if (!u.type->isStructure() && u.type->isBuilder && !u.underConstruction &&
            u.orders.empty() && u.buildSiteId == 0)
            producers.push_back(u.id);                        // idle mobile builder
        else if (u.type->isStructure() && !u.underConstruction && u.buildQueue.empty() &&
                 !registry_.buildable(u.type->id).empty())
            producers.push_back(u.id);                        // idle factory
    }
    // Round-robin who acts when the per-think cap is smaller than the producer count
    // (Easy caps at 1): otherwise the lowest-id producer -- the Monarch -- takes the
    // only slot every think, so it keeps building economy and the factories never get
    // to train an army. Rotating by the think index gives each producer its turn.
    if (!producers.empty() && dp_.producersPerThink < int(producers.size())) {
        uint32_t rr = simTick / uint32_t(std::max(dp_.thinkPeriod, 1));
        std::rotate(producers.begin(),
                    producers.begin() + rr % uint32_t(producers.size()), producers.end());
    }
    // Over-commit throttle: while broke with construction already in progress, don't
    // start ANOTHER build. Concurrent builds share the one mana pool, so piling on more
    // while the treasury is empty starves them all and leaves half-built units that
    // never finish -- the "produces units then gets stuck" failure. Let the in-flight
    // ones finish (income flows into them) before starting the next. Retail's planner
    // likewise skips its ordinary pick below 70% allocation (0x40c890) but still
    // builds the lodestone it draws separately, so the starved AI may start that.
    bool throttle = false;
    if (world.player(player_).mana < 50.0f)
        for (const auto& u : world.units())
            if (u.player == player_ && u.alive() && u.underConstruction) { throttle = true; break; }
    int acted = 0;
    bool commanderActed = false;
    std::vector<int> assigned;
    // The Monarch fights back. The sim never lets a unit pick its own target while a
    // build order leads its queue, so a Monarch walking to a site -- or waiting there
    // for a converted unit to clear its spot -- stood and died under fire (a replay:
    // five converted units shot Elsin for a minute on a stalled Lodestone approach).
    // When it is building, moving or idle and something hits it or stands within its
    // reach, order it to attack that unit; production leaves it alone this think.
    for (const auto& u : world.units()) {
        if (!u.alive() || u.player != player_ || !u.type || !u.type->commander ||
            u.underConstruction || u.embarked() || u.incapacitated()) continue;
        const float reach = std::max(float(u.type->weapon.range), u.type->maxRange());
        const bool armed = u.type->weapon.damage > 0 ||
            std::any_of(u.type->weapons.begin(), u.type->weapons.end(), [](const auto& w) { return w.damage > 0; });
        if (!armed || reach <= 0) break;
        if (!u.orders.empty()) {
            const auto& front = u.orders.front();
            if (front.targetId || front.attackMove || front.patrol || front.guard ||
                front.load || front.unload) break;   // already fighting, or a deliberate task
        }
        const float sight = float(world.sightDistance(*u.type));
        int target = 0;
        float best = 1e18f;
        auto consider = [&](const tak::sim::Unit* e, bool seen) {
            if (!e || !e->alive() || !e->type || e->embarked() || e->underConstruction ||
                world.isNeutralPlayer(e->player) || world.allied(player_, e->player)) return;
            if (seen && e->cloaked) return;
            const float dx = e->x.toFloat() - u.x.toFloat(), dz = e->z.toFloat() - u.z.toFloat();
            const float d = dx * dx + dz * dz, limit = seen ? std::min(reach + 90, sight) : reach + 90;
            if (d <= limit * limit && d < best) { best = d; target = e->id; }
        };
        consider(world.unit(u.lastHitBy), false);   // whoever is hitting it, seen or not
        if (!target)
            for (const auto& e : world.units()) {
                if (e.type && (e.type->weapon.damage > 0 || e.type->canCapture ||
                    std::any_of(e.type->weapons.begin(), e.type->weapons.end(),
                                [](const auto& w) { return w.damage > 0 || w.mindControl; })))
                    consider(&e, true);
            }
        if (target) {
            tak::net::Command c; c.kind = tak::net::Cmd::Attack; c.player = uint8_t(player_);
            c.unitId = u.id; c.targetId = target;
            sink(c);
            assigned.push_back(u.id);
            commanderActed = true;   // the home leash below must not override it
        }
        break;
    }
    if (!throttle || manaShort(world, needs))
        for (int pid : producers) {
            if (acted >= dp_.producersPerThink) break;
            const auto* p = world.unit(pid);
            if (!p || !p->alive()) continue;
            if (std::find(assigned.begin(), assigned.end(), pid) != assigned.end()) continue;
            // Retry in the next-best category when a pick cannot be acted on.
            // Without this a producer whose top category is unproducible burns its
            // turn silently, every think, for ever: measured on Zhon, all three
            // producers picked a lodestone 153 times in 240s with every mana
            // deposit already taken, emitted nothing, and banked 6875 mana while
            // building no army at all. One category per attempt, so the worst case
            // is one pass over the five.
            int exclude = 0;
            for (int attempt = 0; attempt < 5; ++attempt) {
                float siteLimit = 0;
                const auto* pick = weightedPick(world, *p, needs, exclude, throttle, &siteLimit);
                if (!pick) break;
                if (produce(world, *p, pick, sink, siteLimit)) {
                    if (p->type && p->type->commander) commanderActed = true;
                    ++acted;
                    assigned.push_back(pid);
                    ++needs.population;
                    ++needs.counts[pick];
                    if (navalFactory(pick)) ++needs.navalFactories;
                    if (navalProducer(pick)) ++needs.navalProducers;
                    if (pick->onMana && pick->isStructure()) ++needs.lodestoneProjects;
                    if (mobileBuilder(pick)) ++needs.mobileBuilders;
                    if (!pick->isStructure() && !pick->canFly && !pick->isBuilder &&
                        pick->domain==tak::sim::UnitType::Domain::Water) ++needs.navalArmy;
                    switch (categoryOf(pick)) {
                        case BuildCat::Army: ++needs.army; break;
                        case BuildCat::Factory: ++needs.factories; break;
                        case BuildCat::Builder: ++needs.builders; break;
                        case BuildCat::Economy: ++needs.economy; ++needs.economyProjects; break;
                        case BuildCat::Defense: ++needs.defenses; break;
                    }
                    break;
                }
                exclude |= 1 << int(categoryOf(pick));
            }
        }
    // Most units are not factories. Build the ordered factory list once per
    // think, rather than scan the whole empire for every idle army member.
    // Keep IDs so the command sink cannot invalidate stored unit pointers.
    std::vector<int> exitFactories;
    for (const auto& factory:world.units())
        if (factory.alive() && factory.player==player_ && factory.type && !factory.underConstruction &&
            categoryOf(factory.type)==BuildCat::Factory)
            exitFactories.push_back(factory.id);
    // Idle newborns and constructors must clear factory doors even when this AI
    // never attacks. Issue ordinary local moves; do not relax body occupancy or
    // change the movement controller to make production succeed.
    int cleared = 0;
    for (const auto& u : world.units()) {
        if (cleared >= dp_.producersPerThink * 2) break;
        if (!u.alive() || u.player!=player_ || !u.type || u.type->isStructure() ||
            u.underConstruction || u.embarked() || !u.orders.empty() || u.buildSiteId ||
            std::find(assigned.begin(),assigned.end(),u.id)!=assigned.end()) continue;
        for (int factoryId:exitFactories) {
            const auto* found=world.unit(factoryId);
            if (!found) continue;
            const auto& factory=*found;
            const bool mobile = !factory.type->isStructure();
            if (mobile && categoryOf(u.type) != BuildCat::Army) continue;
            const float clearance=mobile ? 180.0f : float(std::max(factory.type->footX,factory.type->footZ))*8+64;
            const float dx=u.x.toFloat()-factory.x.toFloat(),dz=u.z.toFloat()-factory.z.toFloat();
            if (dx*dx+dz*dz>clearance*clearance) continue;
            bool moved=false;
            for (float r : {clearance+48,clearance+96,clearance+160,clearance+240,clearance+320}) {
                for (int i=0;i<16;++i) {
                    const float a=float((u.id+i)%16)*0.392699082f;
                    const float x=factory.x.toFloat()+detmath::cos(a)*r;
                    const float z=factory.z.toFloat()+detmath::sin(a)*r;
                    const auto home=u.type->domain==tak::sim::UnitType::Domain::Water ?
                        std::pair{factory.x.toFloat(),factory.z.toFloat()} : homeOf(world);
                    if (!dp_.attack && (x-home.first)*(x-home.first)+(z-home.second)*(z-home.second)>600*600) continue;
                    if (!world.canPlace(u.type,x,z) || (!u.type->canFly &&
                        !world.pathExists(u.type,x,z,u.x.toFloat(),u.z.toFloat()))) continue;
                    emit(sink,tak::net::Cmd::Move,u.id,"",x,z);
                    assigned.push_back(u.id);++cleared;moved=true;break;
                }
                if (moved) break;
            }
            if (moved) break;
        }
    }
    // Keep the Monarch safe: when it's idle (no build this think, no order, no site)
    // and has strayed beyond a leash of home, walk it back to the base. Losing the
    // Monarch can lose the game (Monarch Expendable), so it must not sit exposed out
    // in the field. A plain Move (not AttackMove) -- it retreats, it doesn't hunt.
    for (const auto& u : world.units()) {
        if (!u.alive() || u.player != player_ || !u.type || !u.type->commander) continue;
        if (commanderActed || std::find(assigned.begin(),assigned.end(),u.id)!=assigned.end() || !u.orders.empty() || u.buildSiteId != 0 || u.underConstruction)
            break;
        auto h = homeOf(world);
        float dx = u.x.toFloat() - h.first, dz = u.z.toFloat() - h.second;
        constexpr float kHomeLeash = 520.0f;   // ~a third of a small map's span
        if (dx * dx + dz * dz > kHomeLeash * kHomeLeash)
            emit(sink, tak::net::Cmd::Move, u.id, "", h.first, h.second);
        break;
    }
    if (!dp_.attack) {
        for (const auto& u : world.units()) {
            if (!u.alive() || u.player!=player_ || u.underConstruction || !u.type ||
                !u.type->canSetStance || (u.moveState==0 && u.fireState==2)) continue;
            tak::net::Command c; c.kind=tak::net::Cmd::Stance;
            c.player=uint8_t(player_);c.unitId=u.id;c.targetId=1;
            sink(c); // explicit interception moves; no autonomous long-distance chase
        }
        sendDefenders(world,sink);
    }
    sendWaves(world, simTick, sink);
}

}  // namespace tak::ai
