#include "sim/matchsetup.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <unordered_map>

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
            if (owns(c.unitId)) world.train(c.unitId, reg.find(c.type));
            break;
        case Cmd::Build:
            if (owns(c.unitId)) world.queueBuild(c.unitId, reg.find(c.type), c.x, c.z, c.queue);
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
    }
}

void applyEvent(World& world, const tak::net::Event& e) {
    int p = e.player;
    if (p < 0 || p >= world.numPlayers()) return;
    for (auto& u : world.units())
        if (u.alive() && u.player == p) world.stop(u.id);
}

std::string setupRegistry(TypeRegistry& reg, const std::string& dataRoot, bool crusades) {
    reg.loadMoveInfo(dataRoot + "/gamedata/moveinfo.tdf");
    // Crusades overlay first (first-definition-wins), then the base roster.
    if (crusades) {
        std::string ucb = dataRoot + "/unitscb", ccb = dataRoot + "/canbuildcb";
        if (std::filesystem::is_directory(ucb)) reg.loadDir(ucb);
        if (std::filesystem::is_directory(ccb)) reg.loadBuildTree(ccb);
    }
    reg.loadDir(dataRoot + "/units");
    reg.loadBuildTree(dataRoot + "/canbuild");
    std::string ipRoot = dataRoot + "/../IPData";
    if (std::filesystem::exists(ipRoot + "/units")) {
        reg.loadDir(ipRoot + "/units");
        if (std::filesystem::exists(ipRoot + "/canbuild"))
            reg.loadBuildTree(ipRoot + "/canbuild");
        return ipRoot;
    }
    return {};
}

std::vector<std::pair<float, float>> parseStartPositions(const std::string& tntPath) {
    std::vector<std::pair<float, float>> out;
    std::filesystem::path ota = tntPath;
    ota.replace_extension(".ota");
    if (!std::filesystem::exists(ota)) return out;
    try {
        auto root = tak::tdf::parse(ota);
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
struct FeatDef { bool mana = false; int fx = 1, fz = 1; };

std::unordered_map<std::string, FeatDef> loadFeatureDefs(const std::string& dataRoot) {
    std::unordered_map<std::string, FeatDef> defs;
    try {
        for (const auto& e : std::filesystem::recursive_directory_iterator(dataRoot + "/features")) {
            if (e.path().extension() != ".tdf") continue;
            try {
                auto root = tak::tdf::parse(e.path());
                for (const auto& n : root.childOrder) {
                    std::string k = n;
                    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                    const auto& node = root.children.at(n);
                    FeatDef d;
                    std::string cat = node.valueOr("category", "");
                    std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);
                    d.mana = (cat == "mana");
                    d.fx = int(node.numberOr("footprintx", 1));
                    d.fz = int(node.numberOr("footprintz", 1));
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
    tak::tnt::Map map = tak::tnt::Map::load(cfg.tntPath);
    world.setTerrain(map.heights, map.width, map.height, map.seaLevel);

    // Features: block nav footprints, and gather mana-deposit positions. Iterate
    // in the same (row-major) order the client does so clustering is identical.
    auto defs = loadFeatureDefs(cfg.dataRoot);
    std::vector<std::pair<float, float>> rawMana;
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
                if (di->second.mana) rawMana.push_back({x, z});
                else {
                    int fx = di->second.fx, fz = di->second.fz;
                    world.nav().block(int(x) / 16 - fx / 2, int(z) / 16 - fz / 2, fx, fz, true);
                }
            }
    }
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

    // Players + teams.
    world.setPlayerCount(int(cfg.slots.size()));
    for (int i = 0; i < int(cfg.slots.size()); ++i) world.setTeam(i, cfg.slots[i].team);
    // Gods: read the appear time from gods.tdf so every peer derives it the same.
    float godSec = 1e9f;   // 1e9 => never manifests (gods off)
    if (cfg.gods) {
        try {
            auto g = tak::tdf::parse(cfg.dataRoot + "/gamedata/gods.tdf");
            if (const auto* tm = g.child("TIMING"))
                godSec = float(tm->numberOr("AppearTimeMin", 30.0)) * 60.0f;
        } catch (const std::exception&) {}
    }
    world.enableGods(godSec);

    // Assign the used slots to start positions (ring fallback if the map has too few).
    int used = 0;
    for (auto& s : cfg.slots) if (s.used) ++used;
    auto starts = parseStartPositions(cfg.tntPath);
    float cx = map.blocksX * 16.0f, cz = map.blocksY * 16.0f;
    std::vector<std::pair<float, float>> spots = starts;
    while (int(spots.size()) < used) {
        float a = float(spots.size()) / float(std::max(used, 1)) * 6.2831853f;
        spots.push_back({cx + std::cos(a) * 300, cz + std::sin(a) * 300});
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

}  // namespace tak::sim
