#include "sim/matchsetup.h"

#include "sim/detmath.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <unordered_map>

#include "hpi/hpi.h"
#include "sim/mission.h"
#include "tdf/tdf.h"

#include <cstdio>
#include <memory>
#include "tdf/tdf.h"
#include "tnt/tnt.h"

namespace tak::sim {

const char* const kMonarchs[5] = {"araking", "tarnecro", "vermage", "zonhunt", "cresage"};

void applyCommand(World& world, const TypeRegistry& reg, const tak::net::Command& c) {
    using tak::net::Cmd;
    auto owns = [&](int id) {
        const auto* u = world.unit(id);
        return u && u->player == int(c.player);
    };
    auto redirect = [&] { if (!c.queue) world.cancelBuilds(c.unitId); };
    switch (c.kind) {
        case Cmd::Move:
            if (owns(c.unitId)) { redirect(); world.order(c.unitId, c.x, c.z, c.queue); }
            break;
        case Cmd::Attack:
            if (owns(c.unitId)) { redirect(); world.attack(c.unitId, c.targetId, c.queue); }
            break;
        case Cmd::AttackMove:
            if (owns(c.unitId)) { redirect(); world.attackMove(c.unitId, c.x, c.z, c.queue); }
            break;
        case Cmd::Patrol:
            if (owns(c.unitId)) { world.cancelBuilds(c.unitId); world.patrol(c.unitId, c.x, c.z); }
            break;
        case Cmd::Stop:
            if (owns(c.unitId)) { world.cancelBuilds(c.unitId); world.stop(c.unitId); }
            break;
        case Cmd::Train:
            if (owns(c.unitId)) world.train(c.unitId, reg.find(c.type), c.targetId);
            break;
        case Cmd::Unqueue:
            if (owns(c.unitId)) world.dequeue(c.unitId, reg.find(c.type), std::max(1, c.targetId));
            break;
        case Cmd::Build:
            if (owns(c.unitId)) world.queueBuild(c.unitId, reg.find(c.type), c.x, c.z, c.queue);
            break;
        case Cmd::Assist:
            // Resume/assist an existing conjure: allowed only if the builder can
            // actually build the site's type (authoritative check, same as the UI).
            if (owns(c.unitId)) {
                const Unit* b = world.unit(c.unitId);
                const Unit* site = world.unit(c.targetId);
                if (b && b->type && site && site->type) {
                    const auto& menu = reg.buildable(b->type->id);
                    if (std::find(menu.begin(), menu.end(), site->type->id) != menu.end()) {
                        redirect();
                        world.assist(c.unitId, c.targetId);
                    }
                }
            }
            break;
        case Cmd::Guard:
            if (owns(c.unitId)) world.guard(c.unitId, c.targetId, c.queue);
            break;
        case Cmd::Load:
            if (owns(c.unitId)) world.loadInto(c.unitId, c.targetId);
            break;
        case Cmd::Unload:
            if (owns(c.unitId)) world.unloadAt(c.unitId, c.x, c.z);
            break;
        case Cmd::SetWeapon:
            if (owns(c.unitId)) world.setWeapon(c.unitId, c.targetId);
            break;
        case Cmd::RepeatTrain:
            if (owns(c.unitId)) world.setRepeat(c.unitId, reg.find(c.type));
            break;
        case Cmd::Destroy:
            if (owns(c.unitId)) world.destroy(c.unitId);
            break;
        case Cmd::Disco:
            world.startDisco(int(c.player));   // cosmetic; no ownership needed
            break;
        case Cmd::Headbang:
            world.startHeadbang(int(c.player));   // cosmetic; no ownership needed
            break;
        case Cmd::Reclaim:
            if (owns(c.unitId)) { redirect(); world.reclaim(c.unitId, c.targetId, c.queue); }
            break;
        case Cmd::Stance:
            if (owns(c.unitId)) world.setStance(c.unitId, c.targetId);
            break;
        case Cmd::Cloak:
            if (owns(c.unitId)) world.setCloak(c.unitId, c.targetId != 0);
            break;
        case Cmd::SetActive:
            if (owns(c.unitId)) world.setActive(c.unitId, c.targetId != 0);
            break;
        case Cmd::Repair:
            if (owns(c.unitId)) { redirect(); world.repair(c.unitId, c.targetId, c.queue); }
            break;
    }
}

void applyEvent(World& world, const tak::net::Event& e) {
    int p = e.player;
    if (p < 0 || p >= world.numPlayers()) return;
    for (auto& u : world.units())
        if (u.alive() && u.player == p) world.stop(u.id);
}

void setupRegistry(TypeRegistry& reg, const hpi::Vfs& vfs, bool crusades) {
    reg.loadMoveInfo(vfs, "gamedata/moveinfo.tdf");
    // Crusades overlay first (first-definition-wins), then the base roster. The
    // VFS already merges base + Iron Plague + community units into one namespace,
    // resolved by retail newest-date precedence, so one loadDir("units") suffices.
    if (crusades) {
        reg.loadDir(vfs, "unitscb");
        reg.loadBuildTree(vfs, "canbuildcb");
    }
    reg.loadDir(vfs, "units");
    reg.loadBuildTree(vfs, "canbuild");
}

std::vector<std::pair<float, float>> parseStartPositions(const hpi::Vfs& vfs,
                                                         const std::string& mapPath) {
    std::vector<std::pair<float, float>> out;
    std::filesystem::path ota = mapPath;
    ota.replace_extension(".ota");
    std::string otaPath = ota.generic_string();
    if (!vfs.has(otaPath)) return out;
    try {
        auto b = vfs.read(otaPath);
        auto root = tak::tdf::parseText(std::string(b.begin(), b.end()), otaPath);
        const auto* gh = root.child("globalheader");
        const auto* md = gh ? gh->child("map data") : nullptr;
        const auto* sp = md ? md->child("specials") : nullptr;
        if (!sp) return out;
        std::map<int, std::pair<float, float>> byIndex;
        for (const auto& name : sp->childOrder) {
            const auto* s = sp->child(name);
            if (!s) continue;
            std::string what = s->valueOr("specialwhat", "");
            if (what.rfind("StartPos", 0) != 0 && what.rfind("startpos", 0) != 0) continue;
            int n = std::atoi(what.c_str() + 8);
            if (n <= 0) continue;
            byIndex[n] = {float(s->numberOr("xpos", 0) * 16),
                          float(s->numberOr("zpos", 0) * 16)};
        }
        for (auto& [n, pos] : byIndex) out.push_back(pos);
    } catch (const std::exception&) {}
    return out;
}

namespace {
// Feature definition fields the sim cares about: is it a mana deposit, and its
// footprint (for nav blocking). Loaded from the feature TDFs.
struct FeatDef { bool mana = false; bool glowy = false; int blocking = 0; int fx = 1, fz = 1;
                 int reclaimable = 0; float energy = 0; };

std::unordered_map<std::string, FeatDef> loadFeatureDefs(const hpi::Vfs& vfs) {
    std::unordered_map<std::string, FeatDef> defs;
    try {
        for (const std::string& path : vfs.list("features")) {
            if (std::filesystem::path(path).extension() != ".tdf") continue;
            try {
                auto fb = vfs.read(path);
                auto root = tak::tdf::parseText(std::string(fb.begin(), fb.end()), path);
                for (const auto& n : root.childOrder) {
                    std::string k = n;
                    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                    const auto& node = root.children.at(n);
                    FeatDef d;
                    std::string cat = node.valueOr("category", "");
                    std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);
                    d.mana = (cat == "mana");
                    // The buildable deposit is the animated Sacred Stone centre;
                    // the static Standing Stones (animating=0) around it are ruins.
                    d.glowy = d.mana && node.numberOr("animating", 0) != 0;
                    d.blocking = int(node.numberOr("blocking", 0));
                    d.fx = int(node.numberOr("footprintx", 1));
                    d.fz = int(node.numberOr("footprintz", 1));
                    d.reclaimable = int(node.numberOr("reclaimable", 0));
                    d.energy = float(node.numberOr("energy", 0));   // reclaim mana yield
                    defs[k] = d;
                }
            } catch (const std::exception&) {}
        }
    } catch (const std::exception&) {}
    return defs;
}
}  // namespace

std::vector<std::pair<float, float>> setupMatch(World& world, const TypeRegistry& reg,
                                                const MatchConfig& cfg) {
    const hpi::Vfs& vfs = *cfg.vfs;
    tak::tnt::Map map = tak::tnt::Map::load(vfs.read(cfg.mapPath), cfg.mapPath);
    world.setTerrain(map.heights, map.width, map.height, map.seaLevel);

    // Features: block nav footprints, and gather mana-deposit positions. Iterate
    // in the same (row-major) order the client does so clustering is identical.
    auto defs = loadFeatureDefs(vfs);
    std::vector<std::pair<float, float>> rawMana, rawAll;
    if (!map.featureNames.empty()) {
        for (int cz = 0; cz < map.height; ++cz)
            for (int cx = 0; cx < map.width; ++cx) {
                uint16_t v = map.features[size_t(cz) * map.width + cx];
                if (v >= map.featureNames.size()) continue;
                std::string key = map.featureNames[v];
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                auto di = defs.find(key);
                if (di == defs.end()) continue;
                float x = float(cx) * 16 + 8, z = float(cz) * 16 + 8;
                if (di->second.mana) {
                    rawAll.push_back({x, z});
                    if (di->second.glowy) rawMana.push_back({x, z});   // buildable centre
                }
                // Retail nav-blocking: obstacle features + static Standing Stones
                // (blocking=1) block; only the glowy Sacred Stone centre stays clear.
                if (!di->second.glowy && (!di->second.mana || di->second.blocking != 0)) {
                    int fx = di->second.fx, fz = di->second.fz;
                    world.nav().block(int(x) / 16 - fx / 2, int(z) / 16 - fz / 2, fx, fz, true);
                }
                // Reclaimable obstacle features (trees/rocks/houses) enter the sim so
                // a mobile builder can clear them for mana. The id is derived from the
                // cell, so every peer records the identical feature (lockstep-safe).
                if (di->second.reclaimable && !di->second.mana) {
                    float work = std::max(di->second.energy, 60.0f);   // rocks (energy 0) still take a beat
                    world.addFeature(cz * map.width + cx, x, z, di->second.energy, work,
                                     di->second.fx, di->second.fz, di->second.blocking != 0);
                }
            }
    }
    // The buildable spot is the glowing Sacred Stone centre, not the ring of
    // static Standing Stones (both are category=mana). Fallback: a deposit with
    // no glowy centre in the data uses its category=mana features instead.
    if (rawMana.empty()) rawMana = rawAll;
    // Cluster mana features (union-find, 60px link) -> one deposit per cluster.
    std::vector<int> par(rawMana.size());
    for (size_t i = 0; i < par.size(); ++i) par[i] = int(i);
    std::function<int(int)> find = [&](int a) {
        while (par[size_t(a)] != a) { par[size_t(a)] = par[size_t(par[size_t(a)])]; a = par[size_t(a)]; }
        return a;
    };
    const float link2 = 60.0f * 60.0f;
    for (size_t i = 0; i < rawMana.size(); ++i)
        for (size_t j = i + 1; j < rawMana.size(); ++j) {
            float dx = rawMana[i].first - rawMana[j].first, dz = rawMana[i].second - rawMana[j].second;
            if (dx * dx + dz * dz < link2) par[size_t(find(int(i)))] = find(int(j));
        }
    std::map<int, std::pair<std::pair<double, double>, int>> acc;
    for (size_t i = 0; i < rawMana.size(); ++i) {
        auto& a = acc[find(int(i))];
        a.first.first += rawMana[i].first; a.first.second += rawMana[i].second; ++a.second;
    }
    std::vector<std::pair<float, float>> manaSpots;
    for (auto& [root, a] : acc)
        manaSpots.push_back({float(a.first.first / a.second), float(a.first.second / a.second)});
    world.setManaSpots(manaSpots);
    // Standing Stones block nav, but a large one can reach the glowy centre; keep
    // every deposit buildable by carving the 2x2 lodestone footprint clear at each
    // spot (must match the viewer's loadFeatures so SP and MP agree).
    for (const auto& [sx, sz] : manaSpots)
        world.nav().block(int(sx) / 16 - 1, int(sz) / 16 - 1, 2, 2, false);

    // Players + teams.
    world.setPlayerCount(int(cfg.slots.size()));
    for (int i = 0; i < int(cfg.slots.size()); ++i) world.setTeam(i, cfg.slots[i].team);
    // Gods: read the appear time from gods.tdf so every peer derives it the same.
    float godSec = 1e9f;   // 1e9 => never manifests (gods off)
    if (cfg.gods) {
        try {
            auto gb = vfs.read("gamedata/gods.tdf");
            auto g = tak::tdf::parseText(std::string(gb.begin(), gb.end()), "gamedata/gods.tdf");
            if (const auto* tm = g.child("TIMING"))
                godSec = float(tm->numberOr("AppearTimeMin", 30.0)) * 60.0f;
        } catch (const std::exception&) {}
    }
    world.enableGods(godSec);
    world.setUnitCap(cfg.unitCap);

    // Assign the used slots to start positions (ring fallback if the map has too few).
    int used = 0;
    for (auto& s : cfg.slots) if (s.used) ++used;
    auto starts = parseStartPositions(vfs, cfg.mapPath);
    float cx = map.blocksX * 16.0f, cz = map.blocksY * 16.0f;
    std::vector<std::pair<float, float>> spots = starts;
    while (int(spots.size()) < used) {
        float a = float(spots.size()) / float(std::max(used, 1)) * 6.2831853f;
        spots.push_back({cx + detmath::cos(a) * 300, cz + detmath::sin(a) * 300});
    }
    std::vector<std::pair<float, float>> assigned;
    int spot = 0;
    for (int i = 0; i < int(cfg.slots.size()); ++i) {
        if (!cfg.slots[i].used) continue;
        const UnitType* monarch = reg.find(kMonarchs[cfg.slots[i].faction % 5]);
        float mx = spots[size_t(spot)].first, mz = spots[size_t(spot)].second;
        assigned.push_back({mx, mz});
        ++spot;
        world.spawn(monarch, mx, mz, 0, i);
        world.player(i).mana = cfg.startMana;
    }
    // Block the (structure) footprints just spawned. Monarchs move, so this is a
    // no-op today, but it mirrors the client and covers any non-mover spawns.
    for (auto& u : world.units()) {
        if (!u.type || u.type->canMove) continue;
        blockFootprint(world.nav(), *u.type, u.x, u.z, true);
    }
    return assigned;
}

bool setupMission(World& world, const TypeRegistry& reg, const hpi::Vfs& vfs,
                  const std::string& stem, int& humanOut) {
    const std::string base = "missions/" + stem;
    if (!vfs.has(base + ".tnt") || !vfs.has(base + ".ota") || !vfs.has(base + ".cob")) {
        std::fprintf(stderr, "setupMission: '%s' not found\n", stem.c_str());
        return false;
    }
    auto otaBytes = vfs.read(base + ".ota");
    tdf::Node root = tdf::parseText(std::string(otaBytes.begin(), otaBytes.end()), base + ".ota");
    const tdf::Node* gh = root.child("globalheader");
    if (!gh) { std::fprintf(stderr, "setupMission: %s no [GlobalHeader]\n", stem.c_str()); return false; }

    // Players from Player<N> defs (1-based .ota slots -> 0-based World players). First-pass
    // diplomacy: human + allies + neutrals on team 0, explicit opponents on team 1, so the
    // human only auto-fights opponents. Human = the first interactive (non-AI) slot.
    int human = 0;
    std::vector<MatchSlot> slots(kMaxPlayers);
    const char* kingdoms[5] = {"aramon", "taros", "veruna", "zhon", "creon"};
    bool foundHuman = false;
    for (int n = 1; n <= kMaxPlayers; ++n) {
        const std::string* v = gh->value("player" + std::to_string(n));
        if (!v) continue;
        std::string def = *v;
        std::transform(def.begin(), def.end(), def.begin(), ::tolower);
        bool ai = def.find("strategic") != std::string::npos || def.find("passive") != std::string::npos;
        MatchSlot& s = slots[size_t(n - 1)];
        s.used = false;   // no monarch spawn -- units come from [Map Data][units]
        s.team = def.find("opponent") != std::string::npos ? 1 : 0;
        for (int f = 0; f < 5; ++f) if (def.find(kingdoms[f]) != std::string::npos) s.faction = f;
        if (!ai && !foundHuman) { human = n - 1; foundHuman = true; }
    }

    MatchConfig cfg;
    cfg.vfs = &vfs;
    cfg.mapPath = base + ".tnt";
    cfg.slots = slots;                 // no used slots -> setupMatch spawns no monarchs
    cfg.gods = false;
    cfg.unitCap = int(gh->numberOr("maxunits", 500));
    setupMatch(world, reg, cfg);       // terrain + features + player teams

    // Placed units from [Map Data][units].
    int spawned = 0;
    if (const tdf::Node* md = gh->child("map data"))
        if (const tdf::Node* units = md->child("units"))
            for (const auto& key : units->childOrder) {
                const tdf::Node& u = units->children.at(key);
                std::string name = u.valueOr("unitname", "");
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                const UnitType* t = reg.find(name);
                if (!t) continue;
                float x = float(u.numberOr("xpos", 0)) * 16 + 8;
                float z = float(u.numberOr("zpos", 0)) * 16 + 8;
                int player = std::clamp(int(u.numberOr("player", 1)) - 1, 0, kMaxPlayers - 1);
                float ang = float(u.numberOr("angle", 0)) * (3.14159265f / 180.0f);
                int id = world.spawn(t, x, z, ang, player);
                if (id >= 0) {
                    ++spawned;
                    if (auto* su = world.unit(id))
                        su->hp = su->type->maxHp * float(u.numberOr("healthpercentage", 100)) / 100.0f;
                }
            }

    // Attach + start the in-sim "god" script (its spawns/triggers run from World::tick).
    world.setMission(std::make_unique<MissionScript>(vfs.read(base + ".cob"), *gh, reg, human, stem));
    humanOut = human;
    std::fprintf(stderr, "setupMission: %s -- %d placed units, human=player%d\n",
                 stem.c_str(), spawned, human);
    return true;
}

}  // namespace tak::sim
