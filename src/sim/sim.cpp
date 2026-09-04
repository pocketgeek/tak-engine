#include "sim/sim.h"

#include "tdf/tdf.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>

namespace tak::sim {

bool gInstantBuild = false;

// TAK_PHASE sim profiler globals (zero cost when unset).
static const bool g_phase = getenv("TAK_PHASE") != nullptr;
static double g_tcomb = 0, g_flowMs = 0, g_pathMs = 0;
static long g_flowN = 0, g_pathN = 0;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTick = 30.0f;               // FBI per-tick values -> per-second
constexpr float kCobAngle = 2 * kPi / 65536.0f;

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

float angleDiff(float a, float b) {
    float d = std::fmod(a - b + kPi, 2 * kPi);
    if (d < 0) d += 2 * kPi;
    return d - kPi;
}

} // namespace

void TypeRegistry::loadMoveInfo(const std::filesystem::path& tdf) {
    try {
        auto root = tdf::parse(tdf);
        for (const auto& cn : root.childOrder) {
            const tdf::Node& c = root.children.at(cn);
            std::string name = lower(c.valueOr("Name", ""));
            if (name.empty()) continue;
            MoveClass m;
            m.maxSlope = float(c.numberOr("MaxSlope", 255));
            m.maxWaterDepth = float(c.numberOr("MaxWaterDepth", 255));
            m.minWaterDepth = float(c.numberOr("MinWaterDepth", 0));
            moveClasses_[name] = m;
        }
    } catch (const std::exception&) {}   // no MOVEINFO -> units keep FBI defaults
}

void TypeRegistry::loadDir(const std::filesystem::path& unitsDir) {
    for (const auto& e : std::filesystem::directory_iterator(unitsDir)) {
        if (lower(e.path().extension().string()) != ".fbi") continue;
        try {
            auto root = tdf::parse(e.path());
            const auto* info = root.child("UNITINFO");
            if (!info) continue;
            UnitType t;
            std::string stem = lower(e.path().stem().string());
            t.id = lower(info->valueOr("objectname", stem));
            // Two files can claim the same objectname (e.g. the Iron Plague campaign's
            // tarnecr2.fbi also declares objectname TARNECRO, but drops builder=1). The
            // CANONICAL definition is the one whose filename matches the objectname
            // (tarnecro.fbi); it must win even though a variant may sort earlier. So:
            // keep the first def UNLESS this is the canonical file and the existing def
            // was only a variant -- then replace it.
            bool canonical = (stem == t.id);
            if (types_.count(t.id)) {
                if (!canonical || canonicalTypes_.count(t.id)) continue;   // keep existing
            }
            if (canonical) canonicalTypes_.insert(t.id);
            t.name = info->valueOr("name", t.id);
            t.side = info->valueOr("side", "");
            // Buildings often declare canmove=1; bmcode (0 = building,
            // 1 = mobile unit) is the authoritative mobility flag.
            t.canMove = info->numberOr("canmove", 0) != 0 &&
                        info->numberOr("bmcode", 1) != 0;
            t.maxVel = float(info->numberOr("maxvelocity", 0)) * kTick;
            // Velocity is px/tick (*kTick => px/s); acceleration and braking are
            // px/tick^2, so they need kTick^2. Using kTick left accel 30x too
            // small, so high-maxVel flyers never reached speed and all crawled.
            t.accel = float(info->numberOr("acceleration", 0.5)) * kTick * kTick;
            t.brake = float(info->numberOr("brakerate", 0.5)) * kTick * kTick;
            // FBI turnrate is COB angle units per tick.
            float tr = float(info->numberOr("turnrate", 500));
            t.turnRate = tr * kCobAngle * kTick;
            t.maxHp = float(info->numberOr("maxdamage", 100));
            t.isBuilder = info->numberOr("builder", 0) != 0;
            t.buildCost = float(info->numberOr("buildcost", 0));
            t.buildTime = float(info->numberOr("buildtime", 0));
            t.workerTime = float(info->numberOr("workertime", 1));
            t.income = float(info->numberOr("mogriumincome", 0));
            t.storage = float(info->numberOr("mogriumstorage", 0));
            t.footX = int(info->numberOr("footprintx", 1));
            t.footZ = int(info->numberOr("footprintz", 1));
            {
                std::string ym = info->valueOr("yardmap", "");
                std::erase_if(ym, [](char c) { return c == ' ' || c == '\t'; });
                // A stray 'S' (not a full footprint map) marks a lodestone that
                // must sit on a mana deposit.
                t.onMana = ym.find('S') != std::string::npos ||
                           ym.find('s') != std::string::npos;
                if (int(ym.size()) == t.footX * t.footZ) t.yardMap = ym;
            }
            // Every lodestone (Lodestone / Divine Lodestone) must sit on a mana
            // deposit, even ones whose FBI omits the 'S' yardmap (e.g. crelode).
            if (t.id.find("lode") != std::string::npos ||
                t.id.find("mana") != std::string::npos)
                t.onMana = true;
            t.canTransport = info->numberOr("cantransport", 0) != 0;
            // Prefer the size-based capacity when present (it caps summed
            // transportsize, which is what we compare); else the plain count.
            t.transportCap = int(info->numberOr("transportsizecapacity",
                                                info->numberOr("transportcapacity", 0)));
            t.buildDist = float(info->numberOr("builddistance", 0));
            t.soundClass = lower(info->valueOr("soundcategory",
                                               info->valueOr("soundclass", "")));
            t.sight = float(info->numberOr("sightdistance", 180));
            t.corpse = lower(info->valueOr("corpse", ""));
            t.shadowArt = lower(info->valueOr("shadowart", ""));
            t.veteranModel = lower(info->valueOr("veteranmodel", ""));
            t.bodyType = lower(info->valueOr("bodytype", "default"));
            // Extended stats.
            t.healTime = float(info->numberOr("healtime", 0));
            t.leash = float(info->numberOr("maneuverleashlength", 0));
            t.waterMult = float(info->numberOr("watermultiplier",
                                info->numberOr("watermultipliser", 1)));
            t.roadMult = float(info->numberOr("roadmultiplier",
                               info->numberOr("roadmultplier", 1)));
            t.maxWaterDepth = float(info->numberOr("maxwaterdepth", 0));
            t.maxSlope = float(info->numberOr("maxslope", 255));
            t.radar = float(info->numberOr("radardistance", 0));
            t.xpValue = int(info->numberOr("experiencepoints", 0));
            t.noVeteran = info->numberOr("noveteran", 0) != 0;
            t.maxMana = float(info->numberOr("maxmana", 0));
            t.manaRegen = float(info->numberOr("manarechargerate", 0));
            t.canReclaim = info->numberOr("canreclaim", 0) != 0;
            t.canResurrect = info->numberOr("canresurrect", 0) != 0;
            t.canCapture = info->numberOr("cancapture", 0) != 0;
            t.canCloak = info->numberOr("cancloak", 0) != 0;
            t.cloakCost = float(info->numberOr("cloakcost", 0));
            t.cloakCostMove = float(info->numberOr("cloakcostmoving", t.cloakCost));
            t.minCloakDist = float(info->numberOr("mincloakdistance", 0));
            t.hoverAttack = info->numberOr("hoverattack", 0) != 0;
            t.attractsGods = info->numberOr("attractsgods", 0) != 0;
            t.onOffable = info->numberOr("onoffable", 0) != 0;
            t.activateWhenBuilt = info->numberOr("activatewhenbuilt", 1) != 0;
            t.cantBeStoned = info->numberOr("cantbestoned", 0) != 0;
            t.cantBeFrozen = info->numberOr("cantbefrozen", 0) != 0;
            t.cantBeCaptured = info->numberOr("cantbecaptured", 0) != 0;
            t.cantBeTransported = info->numberOr("cantbetransported", 0) != 0;
            t.transportSize = int(info->numberOr("transportsize", 1));
            {   // bloodcolor1 = "r g b"
                std::string bc = info->valueOr("bloodcolor1", "");
                int r = 150, g = 30, b = 10;
                if (std::sscanf(bc.c_str(), "%d %d %d", &r, &g, &b) >= 1) {
                    t.blood[0] = uint8_t(std::clamp(r, 0, 255));
                    t.blood[1] = uint8_t(std::clamp(g, 0, 255));
                    t.blood[2] = uint8_t(std::clamp(b, 0, 255));
                }
            }
            t.canFly = info->numberOr("canfly", 0) != 0;
            std::string mc = lower(info->valueOr("movementclass", ""));
            if (mc.rfind("water", 0) == 0) t.domain = UnitType::Domain::Water;
            else if (mc.rfind("hover", 0) == 0) t.domain = UnitType::Domain::Hover;
            // Inherit terrain limits from the MOVEINFO movement class (the real
            // source of slope/water limits; most FBIs don't carry their own).
            if (auto mci = moveClasses_.find(mc); mci != moveClasses_.end()) {
                t.maxSlope = mci->second.maxSlope;
                t.maxWaterDepth = mci->second.maxWaterDepth;
                t.minWaterDepth = mci->second.minWaterDepth;
            }
            t.cruiseAlt = float(info->numberOr("cruisealt", 0)) / 4;
            for (int slot = 1; slot <= 3; ++slot) {
                const auto* w = root.child("WEAPON" + std::to_string(slot));
                if (!w) continue;
                Weapon wp;
                wp.name = w->valueOr("name", "");
                wp.range = float(w->numberOr("range", 0));
                wp.reload = float(w->numberOr("reloadtime", 1));
                wp.projVel = float(w->numberOr("weaponvelocity", 0));
                wp.melee = lower(w->valueOr("type", "")) == "melee";
                // Visual family: FBI hweffect is authoritative (it names the hit
                // effect); fall back to subtype/damagetype/name when it is absent.
                std::string hwe = lower(w->valueOr("hweffect", ""));
                std::string wtag = hwe + " " +
                                   lower(w->valueOr("subtype", "")) + " " +
                                   lower(w->valueOr("damagetype", "")) + " " +
                                   lower(wp.name);
                if (wtag.find("lightning") != std::string::npos)
                    wp.fx = WeaponFx::Lightning;
                else if (wtag.find("fire") != std::string::npos ||
                         wtag.find("flame") != std::string::npos ||
                         wtag.find("breath") != std::string::npos) {
                    wp.fx = WeaponFx::Fire;
                    // Fire "breath" is a short-range emission, not a long shot;
                    // cap the range so the drake closes in instead of breathing
                    // from across the map.
                    wp.range = std::min(wp.range, 170.0f);
                }
                // aimtolerance is in COB angle units; convert to radians. Ballistic
                // weapons lob an arc (viewer draws it); soundhitclass = impact sound.
                wp.aimTol = float(w->numberOr("aimtolerance",
                                  w->numberOr("aimtolerence", 1024))) * float(kCobAngle);
                wp.ballistic = lower(w->valueOr("type", "")) == "ballistic";
                wp.soundHit = lower(w->valueOr("soundhitclass",
                                               w->valueOr("soundhit", "")));
                wp.explosionClass = lower(w->valueOr("explosionclass", ""));
                wp.waterExplosionClass = lower(w->valueOr("waterexplosionclass", ""));
                wp.radiusArt[0] = lower(w->valueOr("radiusart0", ""));
                wp.radiusArt[1] = lower(w->valueOr("radiusart1", ""));
                wp.radiusArt[2] = lower(w->valueOr("radiusart2", ""));
                wp.ringCount = int(w->numberOr("ringcount", 0));
                wp.ringDelay = float(w->numberOr("ringdelay", 0.2));
                wp.ringDur = float(w->numberOr("ringduration", 1.0));
                wp.spriteCount = int(w->numberOr("spritecount", 24));
                wp.shakeMag = float(w->numberOr("shakemagnitude", 0));
                wp.shakeDur = float(w->numberOr("shakeduration", 0));
                wp.fireStarter = w->numberOr("firestarter", 0) != 0;
                wp.minRange = float(w->numberOr("minrange", 0));
                wp.noAir = w->numberOr("noairweapon", 0) != 0;
                wp.manaCost = float(w->numberOr("manapershot", 0));
                // Status-effect weapons (Creon freeze, medusa/paralyzer, petrify),
                // inferred from the hit-effect / damagetype / name.
                {
                    std::string s = hwe + " " + lower(w->valueOr("damagetype", "")) +
                                    " " + lower(w->valueOr("soundhitclass", "")) + " " +
                                    lower(wp.name);
                    if (s.find("freeze") != std::string::npos ||
                        s.find("frost") != std::string::npos ||
                        s.find("hail") != std::string::npos)
                        wp.status = Weapon::Status::Frozen;
                    else if (s.find("petrif") != std::string::npos ||
                             s.find("stone") != std::string::npos ||
                             s.find("medusa") != std::string::npos)
                        wp.status = Weapon::Status::Stoned;
                    else if (s.find("paraly") != std::string::npos)
                        wp.status = Weapon::Status::Paralyzed;
                    if (wp.status != Weapon::Status::None)
                        wp.statusDur = float(w->numberOr("duration", 5.0));
                }
                wp.aoe = float(w->numberOr("areaofeffect", 0));
                // The FBI data misspells this key both ways; accept either.
                wp.edge = float(w->numberOr("edgeeffectiveness",
                                w->numberOr("edgeeffectivness", 1.0)));
                if (const auto* dmg = w->child("DAMAGE")) {
                    wp.damage = float(dmg->numberOr("default", 0));
                    // Per-target-category damage (every DAMAGE key but `default`).
                    for (const auto& [k, v] : dmg->values)
                        if (k != "default")
                            wp.dmgVs[k] = float(std::atof(v.c_str()));
                }
                if (wp.damage > 0) t.weapons.push_back(wp);
            }
            if (!t.weapons.empty()) t.weapon = t.weapons[0];
            // Stat auras: [AdjustArmor]/[AdjustAttack]/[AdjustJoy] blocks under
            // UNITINFO make the unit a continuous buff/debuff field on nearby units.
            {
                struct AK { const char* tag; Aura::Kind kind; };
                for (const AK& ak : {AK{"AdjustArmor", Aura::Kind::Armor},
                                     AK{"AdjustAttack", Aura::Kind::Attack},
                                     AK{"AdjustJoy", Aura::Kind::Joy}}) {
                    if (const auto* a = info->child(ak.tag)) {
                        Aura au;
                        au.kind = ak.kind;
                        au.amount = float(a->numberOr("adjustment", 1));
                        au.affectsEnemy = a->numberOr("affectsenemy", 0) != 0;
                        au.radius = float(a->numberOr("radius", 200));
                        au.edge = float(a->numberOr("edgeeffectiveness", 1));
                        t.auras.push_back(au);
                    }
                }
            }
            // Target-category tokens for weapons' per-category damage: split
            // `category`, plus `damagecategory` and `tedclass`, all lowercased.
            {
                std::string cat = lower(info->valueOr("category", "")) + " " +
                                  lower(info->valueOr("damagecategory", "")) + " " +
                                  lower(info->valueOr("tedclass", ""));
                std::string tok;
                for (char c : cat + " ") {
                    if (c == ' ' || c == '\t') {
                        if (!tok.empty()) { t.categories.push_back(tok); tok.clear(); }
                    } else tok.push_back(c);
                }
            }
            types_[t.id] = std::move(t);
        } catch (const std::exception&) {
            // Skip malformed definitions rather than fail the registry.
        }
    }
}

void TypeRegistry::loadBuildTree(const std::filesystem::path& canbuildDir) {
    for (const auto& b : std::filesystem::directory_iterator(canbuildDir)) {
        if (!b.is_directory()) continue;
        std::string builder = lower(b.path().filename().string());
        std::vector<std::pair<double, std::string>> entries;
        for (const auto& e : std::filesystem::directory_iterator(b.path())) {
            if (lower(e.path().extension().string()) != ".tdf") continue;
            double prio = 99;
            try {
                auto root = tdf::parse(e.path());
                if (const auto* m = root.child("Menu")) prio = m->numberOr("priority", 99);
            } catch (const std::exception&) {}
            entries.push_back({prio, lower(e.path().stem().string())});
        }
        std::sort(entries.begin(), entries.end());
        auto& list = buildTree_[builder];
        // First dir to define a builder wins it whole -- so a Crusades-balance
        // canbuildcb loaded before canbuild replaces that builder's menu rather
        // than merging with it. (No effect on the usual single load.)
        if (!list.empty()) continue;
        for (auto& [p, id] : entries)
            if (std::find(list.begin(), list.end(), id) == list.end())
                list.push_back(id);
    }
}

const std::vector<std::string>& TypeRegistry::buildable(const std::string& builderId) const {
    static const std::vector<std::string> kEmpty;
    auto it = buildTree_.find(lower(builderId));
    return it == buildTree_.end() ? kEmpty : it->second;
}

const UnitType* TypeRegistry::find(const std::string& id) const {
    auto it = types_.find(lower(id));
    return it == types_.end() ? nullptr : &it->second;
}

int World::spawn(const UnitType* type, float x, float z, float heading, int player) {
    Unit u;
    u.id = nextId_++;
    // Clamp to a valid player slot: every players_[u.player] index downstream is
    // unchecked, so an out-of-range owner would read/write past the vector.
    if (player < 0 || player >= int(players_.size())) {
        std::fprintf(stderr, "sim: spawn player %d out of range [0,%d) -> clamped to 0\n",
                     player, int(players_.size()));
        player = 0;
    }
    u.player = player;
    u.type = type;
    u.x = x;
    u.z = z;
    u.heading = heading;
    u.hp = type ? type->maxHp : 100;
    u.mana = type ? type->maxMana : 0;   // casters start with a full pool
    u.active = type ? type->activateWhenBuilt : true;
    u.homeX = x;
    u.homeZ = z;
    units_.push_back(u);
    return u.id;
}

Unit* World::unit(int id) {
    // units_ is append-only with sequential ids (u.id = nextId_++ then push_back,
    // never erased), so id == index + 1 -- an O(1) lookup. This is called per unit
    // every tick in combat (unit(targetId)); the old linear scan made a big battle
    // O(n^2) and stalled the sim to single-digit fps. The scan stays as a fallback
    // in case that invariant is ever broken.
    if (id >= 1 && size_t(id) <= units_.size()) {
        Unit& u = units_[size_t(id) - 1];
        if (u.id == id) return &u;
    }
    for (auto& u : units_)
        if (u.id == id) return &u;
    return nullptr;
}

void World::setTerrain(const std::vector<uint8_t>& heights, int w, int h, int seaLevel) {
    // Ground: no cliffs, water at most ankle deep (moveinfo MaxWaterDepth ~20).
    nav_ = NavGrid(heights, w, h, 20);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            if (seaLevel - int(heights[size_t(z) * w + x]) > 20)
                nav_.block(x, z, 1, 1, true);
    // Water: needs depth (moveinfo MinWaterDepth ~13); slope irrelevant.
    navWater_ = NavGrid(heights, w, h, 255);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            if (seaLevel - int(heights[size_t(z) * w + x]) < 13)
                navWater_.block(x, z, 1, 1, true);
    // Hover: land without cliffs, or any water.
    navHover_ = NavGrid(heights, w, h, 20);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            if (seaLevel - int(heights[size_t(z) * w + x]) > 0)
                navHover_.block(x, z, 1, 1, false);
    // Occlusion block: a wall's baked-relief art leans its top up-and-north over
    // the low ground behind it (the 2.5D projection), so a unit that stops on that
    // ground is drawn hidden "behind the wall". Block those cells for land units so
    // they can't settle there -- the flat wall TOP has no higher cell to its south,
    // so it stays walkable (a reachable rampart). This mirrors the renderer's
    // occlusion exactly (modal ground height as the reference, ~1.1 px of northward
    // projection per height unit, scanning south = toward the camera).
    {
        long hist[256] = {0};
        for (uint8_t v : heights) hist[v]++;
        int ref = 0;
        for (int i = 1; i < 256; ++i) if (hist[i] > hist[ref]) ref = i;
        const float kProj = 1.1f;
        for (int z = 0; z < h; ++z)
            for (int x = 0; x < w; ++x) {
                int hu = heights[size_t(z) * w + x];
                for (int d = 1; d <= 7; ++d) {
                    int nz = z + d;
                    if (nz >= h) break;
                    int hw = heights[size_t(nz) * w + x];
                    if (hw <= hu + 24) continue;   // not a wall relative to this cell
                    if (float(hw - ref) * kProj > float(d) * 16.0f) {
                        nav_.block(x, z, 1, 1, true);
                        navHover_.block(x, z, 1, 1, true);
                        break;
                    }
                }
            }
    }
    // Per-cell slope (max 3x3 height spread) and water depth, for per-unit
    // maxSlope / maxWaterDepth checks on top of the shared domain grids.
    terW_ = w; terH_ = h;
    slope_.assign(size_t(w) * h, 0);
    depth_.assign(size_t(w) * h, 0);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x) {
            int lo = 255, hi = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = std::clamp(x + dx, 0, w - 1);
                    int nz = std::clamp(z + dz, 0, h - 1);
                    int v = heights[size_t(nz) * w + nx];
                    lo = std::min(lo, v); hi = std::max(hi, v);
                }
            slope_[size_t(z) * w + x] = uint8_t(std::min(255, hi - lo));
            int d = seaLevel - int(heights[size_t(z) * w + x]);
            depth_[size_t(z) * w + x] = uint8_t(std::clamp(d, 0, 255));
        }
}

void blockFootprint(NavGrid& nav, const UnitType& t, float x, float z, bool blocked) {
    int cx = int(x) / 16 - t.footX / 2, cz = int(z) / 16 - t.footZ / 2;
    if (t.yardMap.empty()) {
        nav.block(cx, cz, t.footX, t.footZ, blocked);
        return;
    }
    for (int j = 0; j < t.footZ; ++j)
        for (int i = 0; i < t.footX; ++i) {
            char c = t.yardMap[size_t(j) * t.footX + i];
            if (c == 'o' || c == 'O')
                nav.block(cx + i, cz + j, 1, 1, blocked);
        }
}

bool FlowField::build(const NavGrid& nav, float gx, float gz) {
    w_ = nav.width();
    h_ = nav.height();
    if (nav.empty() || w_ <= 0 || h_ <= 0) { w_ = h_ = 0; return false; }
    int gcx = std::clamp(int(gx) / 16, 0, w_ - 1);
    int gcz = std::clamp(int(gz) / 16, 0, h_ - 1);
    // Snap a blocked goal (a click on a wall/building) to the nearest walkable
    // cell, spiralling out, so the wavefront has somewhere to start.
    if (!nav.walkable(gcx, gcz)) {
        bool found = false;
        for (int r = 1; r < 24 && !found; ++r)
            for (int j = -r; j <= r && !found; ++j)
                for (int i = -r; i <= r && !found; ++i) {
                    if (std::max(std::abs(i), std::abs(j)) != r) continue;
                    if (nav.walkable(gcx + i, gcz + j)) {
                        gcx += i; gcz += j; found = true;
                    }
                }
        if (!found) { w_ = h_ = 0; return false; }
    }
    goal_ = gcz * w_ + gcx;

    const size_t n = size_t(w_) * h_;
    // Integration field in "centi-cells": orthogonal step 10, diagonal 14, so a
    // uint16 covers paths up to ~6500 cells. Dijkstra out from the goal.
    dist_.assign(n, 0xFFFF);
    struct Node { int cost, idx; };
    struct Cmp { bool operator()(const Node& a, const Node& b) const { return a.cost > b.cost; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> pq;
    dist_[size_t(goal_)] = 0;
    pq.push({0, goal_});
    static const int dcx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dcz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const int dcost[8] = {10, 10, 10, 10, 14, 14, 14, 14};
    while (!pq.empty()) {
        Node cur = pq.top();
        pq.pop();
        if (cur.cost > dist_[size_t(cur.idx)]) continue;
        int cx = cur.idx % w_, cz = cur.idx / w_;
        for (int k = 0; k < 8; ++k) {
            int nx = cx + dcx[k], nz = cz + dcz[k];
            if (!nav.walkable(nx, nz)) continue;
            // No diagonal corner-cutting past a blocked orthogonal neighbour.
            if (k >= 4 && (!nav.walkable(cx + dcx[k], cz) ||
                           !nav.walkable(cx, cz + dcz[k])))
                continue;
            int nd = cur.cost + dcost[k];
            size_t ni = size_t(nz) * w_ + nx;
            if (nd < dist_[ni] && nd < 0xFFFF) {
                dist_[ni] = uint16_t(nd);
                pq.push({nd, int(ni)});
            }
        }
    }

    // Flow field: each walkable cell stores the reachable neighbour index (0-7)
    // with the lowest integration cost (steepest descent toward the goal), -1 = none.
    dir_.assign(n, int8_t(-1));
    for (int cz = 0; cz < h_; ++cz)
        for (int cx = 0; cx < w_; ++cx) {
            size_t i = size_t(cz) * w_ + cx;
            if (dist_[i] == 0xFFFF || int(i) == goal_) continue;
            int best = -1, bestD = dist_[i];
            for (int k = 0; k < 8; ++k) {
                int nx = cx + dcx[k], nz = cz + dcz[k];
                if (!nav.walkable(nx, nz)) continue;
                if (k >= 4 && (!nav.walkable(cx + dcx[k], cz) ||
                               !nav.walkable(cx, cz + dcz[k])))
                    continue;
                int d = dist_[size_t(nz) * w_ + nx];
                if (d < bestD) { bestD = d; best = k; }
            }
            dir_[i] = int8_t(best);
        }
    return true;
}

void FlowField::dirAt(float x, float z, float& dx, float& dz) const {
    dx = dz = 0;
    if (w_ <= 0) return;
    int cx = int(x) / 16, cz = int(z) / 16;
    if (cx < 0 || cz < 0 || cx >= w_ || cz >= h_) return;
    int8_t d = dir_[size_t(cz) * w_ + cx];
    if (d < 0) return;   // goal cell / unreachable: (0,0), caller steers straight
    // Unit vectors for neighbour indices 0-7 (ortho then diagonal, normalised).
    static const float NX[8] = {1, -1, 0, 0, 0.70711f, 0.70711f, -0.70711f, -0.70711f};
    static const float NZ[8] = {0, 0, 1, -1, 0.70711f, -0.70711f, 0.70711f, -0.70711f};
    dx = NX[d];
    dz = NZ[d];
}

bool FlowField::reachable(float x, float z) const {
    if (w_ <= 0) return false;
    int cx = int(x) / 16, cz = int(z) / 16;
    if (cx < 0 || cz < 0 || cx >= w_ || cz >= h_) return false;
    return dist_[size_t(cz) * w_ + cx] != 0xFFFF;
}

void NavGrid::block(int cx, int cz, int w, int h, bool blocked) {
    for (int z = cz; z < cz + h; ++z)
        for (int x = cx; x < cx + w; ++x)
            if (x >= 0 && z >= 0 && x < w_ && z < h_)
                cells_[size_t(z) * w_ + x] = blocked ? 0 : 1;
}

NavGrid::NavGrid(const std::vector<uint8_t>& heights, int w, int h, int cliff)
    : w_(w), h_(h) {
    cells_.assign(size_t(w) * h, 1);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x) {
            // Block a cell only if a NEIGHBOUR rises more than `cliff` above it (a
            // face you can't climb). A cell that merely sits beside a DROP -- the
            // flat top edge of a cliff/plateau/wall -- stays walkable, so units can
            // be commanded right to the rim, not just the interior. (Using the local
            // max-min spread instead wrongly blocked the whole edge ring of every
            // plateau, leaving only its middle reachable.)
            int self = heights[size_t(z) * w + x];
            int hi = self;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = std::clamp(x + dx, 0, w - 1);
                    int nz = std::clamp(z + dz, 0, h - 1);
                    hi = std::max(hi, int(heights[size_t(nz) * w + nx]));
                }
            if (hi - self > cliff) cells_[size_t(z) * w + x] = 0;
        }
}

bool NavGrid::lineClear(int x0, int z0, int x1, int z1) const {
    int dx = std::abs(x1 - x0), dz = -std::abs(z1 - z0);
    int sx = x0 < x1 ? 1 : -1, sz = z0 < z1 ? 1 : -1, err = dx + dz;
    while (true) {
        if (!walkable(x0, z0)) return false;
        if (x0 == x1 && z0 == z1) return true;
        int e2 = 2 * err;
        if (e2 >= dz) { err += dz; x0 += sx; }
        if (e2 <= dx) { err += dx; z0 += sz; }
    }
}

bool NavGrid::losBetween(float wx0, float wz0, float wx1, float wz1,
                         int skip0, int skip1) const {
    if (empty()) return true;
    int ex0 = int(wx0) / 16, ez0 = int(wz0) / 16;
    int ex1 = int(wx1) / 16, ez1 = int(wz1) / 16;
    int x0 = ex0, z0 = ez0;
    int dx = std::abs(ex1 - x0), dz = -std::abs(ez1 - z0);
    int sx = x0 < ex1 ? 1 : -1, sz = z0 < ez1 ? 1 : -1, err = dx + dz;
    while (true) {
        if (x0 == ex1 && z0 == ez1) return true;   // reached the target cell
        int e2 = 2 * err;
        if (e2 >= dz) { err += dz; x0 += sx; }
        if (e2 <= dx) { err += dx; z0 += sz; }
        if (x0 == ex1 && z0 == ez1) return true;   // stepped onto the endpoint
        // Ignore cells inside either endpoint's own footprint (Chebyshev radius).
        if (std::max(std::abs(x0 - ex0), std::abs(z0 - ez0)) <= skip0) continue;
        if (std::max(std::abs(x0 - ex1), std::abs(z0 - ez1)) <= skip1) continue;
        if (!walkable(x0, z0)) return false;       // a wall stands between them
    }
}

std::vector<Order> NavGrid::findPath(float wx0, float wz0, float wx1, float wz1) const {
    constexpr int kCell = 16;
    int sx = int(wx0) / kCell, sz = int(wz0) / kCell;
    int tx = int(wx1) / kCell, tz = int(wz1) / kCell;
    if (!walkable(tx, tz) || !walkable(sx, sz)) return {};
    if (lineClear(sx, sz, tx, tz)) return {{wx1, wz1, 0}};

    // A* over cells, octile heuristic.
    struct Node { float f; int idx; };
    auto cmp = [](const Node& a, const Node& b) { return a.f > b.f; };
    std::vector<Node> open;
    std::vector<float> g(size_t(w_) * h_, 1e30f);
    std::vector<int> from(size_t(w_) * h_, -1);
    auto hcost = [&](int x, int z) {
        float ax = float(std::abs(x - tx)), az = float(std::abs(z - tz));
        return std::max(ax, az) + 0.41421f * std::min(ax, az);
    };
    int start = sz * w_ + sx, goal = tz * w_ + tx;
    g[size_t(start)] = 0;
    open.push_back({hcost(sx, sz), start});
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    int expansions = 0;
    while (!open.empty() && expansions++ < 400000) {
        std::pop_heap(open.begin(), open.end(), cmp);
        Node n = open.back();
        open.pop_back();
        if (n.idx == goal) break;
        int cx = n.idx % w_, cz = n.idx / w_;
        for (int d = 0; d < 8; ++d) {
            int nx = cx + DX[d], nz = cz + DZ[d];
            if (!walkable(nx, nz)) continue;
            if (d >= 4 && (!walkable(cx + DX[d], cz) || !walkable(cx, cz + DZ[d])))
                continue;   // no diagonal corner cutting
            float step = d >= 4 ? 1.41421f : 1.0f;
            float ng = g[size_t(n.idx)] + step;
            int ni = nz * w_ + nx;
            if (ng < g[size_t(ni)]) {
                g[size_t(ni)] = ng;
                from[size_t(ni)] = n.idx;
                open.push_back({ng + hcost(nx, nz), ni});
                std::push_heap(open.begin(), open.end(), cmp);
            }
        }
    }
    if (from[size_t(goal)] < 0) return {};

    std::vector<std::pair<int, int>> cells;
    for (int i = goal; i >= 0; i = from[size_t(i)]) {
        cells.push_back({i % w_, i / w_});
        if (i == start) break;
    }
    std::reverse(cells.begin(), cells.end());

    // Simplify: greedily skip waypoints with a clear straight line.
    std::vector<Order> out;
    size_t anchor = 0;
    for (size_t i = 2; i < cells.size(); ++i) {
        if (!lineClear(cells[anchor].first, cells[anchor].second, cells[i].first,
                       cells[i].second)) {
            anchor = i - 1;
            out.push_back({cells[anchor].first * 16.0f + 8, cells[anchor].second * 16.0f + 8, 0});
        }
    }
    out.push_back({wx1, wz1, 0});
    return out;
}

const FlowField* World::flowFor(const UnitType* type, float gx, float gz) {
    const NavGrid& grid = navFor(type);
    if (grid.empty()) return nullptr;
    int cx = std::clamp(int(gx) / 16, 0, grid.width() - 1);
    int cz = std::clamp(int(gz) / 16, 0, grid.height() - 1);
    int domain = type ? int(type->domain) : 0;
    long long key = domain * 100000000LL + (long long)(cz * grid.width() + cx);
    auto it = flowCache_.find(key);
    if (it != flowCache_.end()) {
        it->second.used = tickCounter_;
        return it->second.ready() ? &it->second : nullptr;
    }
    // Evict the least-recently-used field(s) when the cache is full, rather than
    // wiping ALL of them. The old clear-all meant a game with more concurrently
    // live goals than the cap (e.g. a big move order + a busy AI army) rebuilt
    // every field EVERY tick -- a full-map Dijkstra x N -- and stalled the sim to
    // single-digit fps. Fields are ~0.3MB each (1-byte dir index), so a generous
    // cap holds every live goal without thrashing.
    constexpr size_t kFlowCap = 128;
    while (flowCache_.size() >= kFlowCap) {
        auto oldest = flowCache_.begin();
        for (auto i = std::next(flowCache_.begin()); i != flowCache_.end(); ++i)
            if (i->second.used < oldest->second.used) oldest = i;
        flowCache_.erase(oldest);
    }
    FlowField ff;
    if (g_phase) { auto _b0=std::chrono::steady_clock::now(); ff.build(grid, gx, gz);
        g_flowMs += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-_b0).count(); ++g_flowN; }
    else ff.build(grid, gx, gz);
    ff.used = tickCounter_;
    auto& stored = flowCache_[key];
    stored = std::move(ff);
    return stored.ready() ? &stored : nullptr;
}

void World::order(int unitId, float x, float z, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canMove) return;
    if (!queue) u->orders.clear();
    if (u->type->canFly) {
        u->orders.push_back({x, z, 0});
        return;
    }
    // Ground/water units steer by a shared flow field toward the goal, so a
    // crowd sent to the same point spreads and flows around obstacles instead of
    // funnelling single-file into a corner. Only fall back to A* waypoints when
    // no field can be built or the goal is unreachable from here.
    const FlowField* ff = flowFor(u->type, x, z);
    if (ff) {
        // The flow field's reachable set is authoritative. If it's reachable, steer
        // by the field. If NOT, the goal is genuinely cut off from this unit -- push
        // a plain direct order and let the movement give-up cancel it, rather than
        // run a full-grid A* per unit (500 of those on one click stalls the sim).
        Order o;
        o.x = x; o.z = z; o.flow = ff->reachable(u->x, u->z);
        u->orders.push_back(o);
        return;
    }
    const NavGrid& grid = navFor(u->type);
    if (!grid.empty()) {
        auto path = grid.findPath(u->x, u->z, x, z);
        if (!path.empty()) {
            for (const auto& o : path) u->orders.push_back(o);
            return;
        }
    }
    u->orders.push_back({x, z, 0});
}

bool World::pathExists(const UnitType* type, float gx, float gz, float fx, float fz) {
    const FlowField* ff = flowFor(type, gx, gz);
    return ff && ff->reachable(fx, fz);
}

void World::loadInto(int unitId, int transportId) {
    Unit* u = unit(unitId);
    Unit* t = unit(transportId);
    if (!u || !t || !u->alive() || !t->alive() || u->embarked()) return;
    if (!u->type || !u->type->canMove || u->type->domain != UnitType::Domain::Ground)
        return;
    if (u->type->cantBeTransported) return;   // e.g. heavy/anchored units
    if (!t->type || !t->type->canTransport || u->player != t->player) return;
    // transportsize: sum the slot cost of the current cargo, not just the count.
    int used = 0;
    for (int id : t->cargo)
        if (const Unit* c = unit(id)) used += c->type ? c->type->transportSize : 1;
    if (used + u->type->transportSize > t->type->transportCap) return;
    u->orders.clear();
    Order o;
    o.targetId = transportId;
    o.load = true;
    u->orders.push_back(o);
}

void World::unloadAt(int transportId, float x, float z) {
    Unit* t = unit(transportId);
    if (!t || !t->alive() || t->cargo.empty()) return;
    t->orders.clear();
    // Sail there through the transport's own domain, then disembark.
    const NavGrid& grid = navFor(t->type);
    if (!grid.empty()) {
        auto path = grid.findPath(t->x, t->z, x, z);
        for (size_t i = 0; i + 1 < path.size(); ++i) t->orders.push_back(path[i]);
    }
    Order o;
    o.x = x;
    o.z = z;
    o.unload = true;
    t->orders.push_back(o);
}

void World::tickTransport(Unit& u, float dt) {
    (void)dt;
    Order& o = u.orders.front();
    if (o.load) {
        Unit* t = unit(o.targetId);
        if (!t || !t->alive() || !t->type ||
            int(t->cargo.size()) >= t->type->transportCap) {
            u.orders.pop_front();
            return;
        }
        float dx = t->x - u.x, dz = t->z - u.z;
        o.x = t->x;
        o.z = t->z;
        if (dx * dx + dz * dz < 70 * 70) {
            u.inTransport = t->id;
            t->cargo.push_back(u.id);
            u.orders.clear();
            u.speed = 0;
        }
        return;
    }
    // unload: sail close to the point, then place cargo on nearby land.
    float dx = o.x - u.x, dz = o.z - u.z;
    if (dx * dx + dz * dz > 150 * 150) return;   // keep sailing
    int cx = int(o.x) / 16, cz = int(o.z) / 16;
    for (int id : u.cargo) {
        Unit* c = unit(id);
        if (!c) continue;
        // Spiral out for a free ground cell.
        for (int r = 0; r < 12 && c->inTransport; ++r)
            for (int j = -r; j <= r && c->inTransport; ++j)
                for (int i = -r; i <= r && c->inTransport; ++i) {
                    if (std::max(std::abs(i), std::abs(j)) != r) continue;
                    if (!nav_.walkable(cx + i, cz + j)) continue;
                    c->x = float(cx + i) * 16 + 8;
                    c->z = float(cz + j) * 16 + 8;
                    c->inTransport = 0;
                }
    }
    std::erase_if(u.cargo, [&](int id) {
        Unit* c = unit(id);
        return !c || !c->inTransport;
    });
    if (u.cargo.empty()) u.orders.pop_front();
}

void World::attackMove(int unitId, float x, float z, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canMove) return;
    size_t before = queue ? u->orders.size() : 0;
    order(unitId, x, z, queue);
    for (size_t i = before; i < u->orders.size(); ++i) u->orders[i].attackMove = true;
}

void World::patrol(int unitId, float x, float z) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canMove) return;
    u->orders.clear();
    Order a;
    a.x = u->x; a.z = u->z; a.patrol = true; a.attackMove = true;
    Order b;
    b.x = x; b.z = z; b.patrol = true; b.attackMove = true;
    u->orders.push_back(b);
    u->orders.push_back(a);
}

void World::guard(int unitId, int targetId, bool queue) {
    Unit* u = unit(unitId);
    Unit* t = unit(targetId);
    if (!u || !t || !u->alive() || !t->alive() || u->id == t->id) return;
    if (!u->type || !u->type->canMove || u->player != t->player) return;
    if (!queue) u->orders.clear();
    Order o;
    o.targetId = targetId;
    o.guard = true;
    o.x = t->x;
    o.z = t->z;
    u->orders.push_back(o);
}

void World::stop(int unitId) {
    Unit* u = unit(unitId);
    if (!u) return;
    u->orders.clear();
    // Stop also halts a conjurer: cancel the infinite loop and drain the queue.
    u->repeatType = nullptr;
    u->buildQueue.clear();
    u->buildProgress = 0;
}

void World::destroy(int unitId) {
    // Self-destruct: drop hp to zero and let the normal death processing in
    // tick() handle the rest (kill credit is skipped -- lastHitBy is cleared).
    Unit* u = unit(unitId);
    if (!u || !u->alive()) return;
    u->hp = 0;
    u->lastHitBy = 0;
}

void World::setWeapon(int unitId, int slot) {
    Unit* u = unit(unitId);
    if (!u || !u->type) return;
    int n = int(u->type->weapons.size());
    if (n > 0) u->weaponSlot = std::clamp(slot, 0, n - 1);
}

void World::attack(int unitId, int targetId, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || u->type->weapon.damage <= 0) return;
    if (!queue) u->orders.clear();
    u->orders.push_back({0, 0, targetId});
}

float Weapon::damageVs(const UnitType* t) const {
    if (t && !dmgVs.empty())
        for (const auto& c : t->categories) {
            auto it = dmgVs.find(c);
            if (it != dmgVs.end()) return it->second;
        }
    return damage;
}

void World::applyHit(const Weapon& w, float hx, float hz, int fromPlayer, int fromId,
                     Unit* primary) {
    // Record the impact for the viewer (hit sound / effect).
    hits_.push_back({hx, hz, &w, primary ? primary->type : nullptr});
    // Attacker's veteran attack multiplier boosts damage dealt (retail scales the
    // attacker's attack stat by vetMul); the victim's boosts armour (below).
    const Unit* attacker = fromId ? unit(fromId) : nullptr;
    // Attack multiplier = veterancy × live aura buff (AdjustAttack).
    float atkMul = attacker ? attacker->vetMul() * attacker->atkBuff : 1.0f;
    // Damage + status one victim, honouring veterancy, auras and immunities.
    auto hurt = [&](Unit& e, float scale) {
        if (e.stonedFor > 0) return;   // petrified units are impervious
        // base × attacker-attack (↑) ÷ victim-armour (↓); armour = veterancy × aura.
        float armour = std::max(e.vetMul() * e.armBuff, 0.01f);
        float dealt = w.damageVs(e.type) * atkMul / armour * scale;
        e.hp -= dealt;
        if (fromId) e.lastHitBy = fromId;
        if (w.status != Weapon::Status::None && e.type) {
            bool immune =
                (w.status == Weapon::Status::Frozen && e.type->cantBeFrozen) ||
                (w.status == Weapon::Status::Stoned && e.type->cantBeStoned);
            if (!immune) {
                float& t = w.status == Weapon::Status::Frozen   ? e.frozenFor
                         : w.status == Weapon::Status::Stoned   ? e.stonedFor
                                                                : e.paralyzedFor;
                t = std::max(t, w.statusDur);
                e.speed = 0;
            }
        }
    };
    if (primary) hurt(*primary, 1.0f);
    if (w.aoe <= 0) return;
    // Splash the surrounding enemies, scaled from full at the centre to `edge`
    // at the rim (so an area weapon actually hits a crowd, per its FBI aoe).
    const float aoe = w.aoe;
    int splashed = 0;
    forEachNear(hx, hz, aoe, [&](int idx) {
        Unit& e = units_[size_t(idx)];
        if (!e.alive() || e.embarked() || !e.type || allied(e.player, fromPlayer)) return;
        if (&e == primary) return;   // already took the direct hit
        float dx = e.x - hx, dz = e.z - hz;
        float d = std::sqrt(dx * dx + dz * dz);
        if (d > aoe) return;
        float scale = 1.0f - (d / aoe) * (1.0f - w.edge);
        hurt(e, scale);
        ++splashed;
    });
    static const bool kLog = std::getenv("TAK_SPLASHLOG") != nullptr;
    if (splashed && kLog)
        std::fprintf(stderr, "splash %s aoe=%.0f hit %d extra\n",
                     w.name.c_str(), aoe, splashed);
}

void World::fire(Unit& u, Unit& target, int slot) {
    const Weapon& w = u.type->weapons[size_t(slot)];
    // manapershot: a caster spends personal mana to fire; if it can't pay, the
    // shot doesn't happen (reload not consumed, so it fires the moment it can).
    if (w.manaCost > 0 && u.type->maxMana > 0) {
        if (u.mana < w.manaCost) return;
        u.mana -= w.manaCost;
    }
    // Veterans reload faster (retail divides the cooldown by the veteran multiplier).
    float rl = w.reload / std::max(u.vetMul(), 0.01f);
    u.reloads[slot] = rl;
    u.reloadLeft = rl;
    u.justFired = true;
    if (w.melee || w.projVel <= 0) {
        // Instant hit (melee swing / hitscan bolt): apply damage + splash now.
        applyHit(w, target.x, target.z, u.player, u.id, &target);
        return;
    }
    Projectile p;
    p.x = u.x;
    p.z = u.z;
    float dx = target.x - u.x, dz = target.z - u.z;
    float dist = std::max(std::sqrt(dx * dx + dz * dz), 1e-3f);
    float vel = w.projVel * kTick / 30.0f;   // weaponvelocity is already px/s-ish
    p.vx = dx / dist * vel;
    p.vz = dz / dist * vel;
    p.damage = w.damage;
    p.wsrc = &w;
    p.targetId = target.id;
    p.fromPlayer = u.player;
    p.fromId = u.id;
    p.fx = w.fx;
    p.life = dist / vel + 0.5f;
    p.flight = dist / vel;
    projectiles_.push_back(p);
}

void World::tickCombat(Unit& u, float dt) {
    if (u.reloadLeft > 0) u.reloadLeft -= dt;
    for (auto& r : u.reloads)
        if (r > 0) r -= dt;
    if (!u.active) return;   // onoffable unit powered down: no acquisition/fire

    // Auto-acquire: idle armed units engage the nearest enemy in reach;
    // attack-movers and patrollers interrupt their route to fight.
    bool acquiring = u.orders.empty() ||
                     (u.orders.front().targetId == 0 &&
                      (u.orders.front().attackMove || u.orders.front().patrol)) ||
                     u.orders.front().guard;
    // Can any of this unit's weapons engage enemy `e`? (noairweapon gates flyers.)
    auto canTarget = [&](const Unit& e) {
        if (e.cloaked) return false;   // cloaked units are invisible to auto-acquire
        for (const auto& wp : u.type->weapons)
            if (!(e.type->canFly && wp.noAir)) return true;
        return false;
    };
    // Auto-acquisition is staggered across ticks by unit id: an idle armed unit
    // rescans for a target every kAcqStride ticks (~0.13s at 30Hz), not every
    // tick. That turns the dense-crowd O(n^2) neighbour scan into O(n^2/stride)
    // -- the dominant sim cost when thousands of idle armed units pile together.
    // Deterministic (id+tick, identical on every lockstep peer), so no desync.
    constexpr uint32_t kAcqStride = 4;
    bool acqTurn = (uint32_t(u.id) + tickCounter_) % kAcqStride == 0;
    if (acquiring && u.type->weapon.damage > 0 && acqTurn) {
        float ar = u.type->maxRange() + 90;
        int best = 0;
        float bestD = ar * ar;
        // maneuverleashlength limits how far an IDLE defender will chase from its
        // post. It must NOT apply while attack-moving/patrolling — those orders
        // mean "advance and engage everything en route", so an army that has
        // travelled far from its spawn still acquires (incl. just-conjured foes).
        float leash2 = (u.orders.empty() && u.type->leash > 0)
                           ? u.type->leash * u.type->leash : 1e30f;
        // A ranged unit only auto-acquires enemies it can actually see, so it
        // doesn't charge a target hidden behind a wall (which caused pile-ups at
        // corners). Melee/flyers acquire regardless.
        bool ranged = u.type->maxRange() > 64.0f && !u.type->canFly;
        int uFoot = std::max(u.type->footX, u.type->footZ) / 2;
        forEachNear(u.x, u.z, ar, [&](int idx) {
            const Unit& e = units_[size_t(idx)];
            if (!e.alive() || e.embarked() || allied(e.player, u.player) || !e.type) return;
            if (e.underConstruction) return;   // don't auto-react to a site still conjuring
            if (!canTarget(e)) return;
            float hx = e.x - u.homeX, hz = e.z - u.homeZ;
            if (hx * hx + hz * hz > leash2) return;   // outside the leash
            float dx = e.x - u.x, dz = e.z - u.z;
            float d = dx * dx + dz * dz;
            if (d >= bestD) return;
            if (ranged && !e.type->canFly &&
                !nav_.losBetween(u.x, u.z, e.x, e.z, uFoot,
                                 std::max(e.type->footX, e.type->footZ) / 2))
                return;                         // no clear shot: don't acquire it
            bestD = d; best = e.id;
        });
        if (best) u.orders.push_front({0, 0, best});
    }
    if (u.orders.empty() || u.orders.front().targetId == 0) return;

    if (u.orders.front().guard) {
        Order& o = u.orders.front();
        Unit* t = unit(o.targetId);
        if (!t || !t->alive()) {
            u.orders.pop_front();
            return;
        }
        o.x = t->x;
        o.z = t->z;
        float dx = t->x - u.x, dz = t->z - u.z;
        if (dx * dx + dz * dz <= 70 * 70)
            u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
        return;   // movement walks toward o when out of reach
    }

    Unit* target = unit(u.orders.front().targetId);
    if (!target || !target->alive()) {
        u.orders.pop_front();
        return;
    }
    float dx = target->x - u.x, dz = target->z - u.z;
    float dist = std::sqrt(dx * dx + dz * dz);
    // Only the player-selected weapon fires (retail auto-fires the primary; the
    // player picks secondary/tertiary via its command button). Approach to that
    // weapon's range, not the longest.
    int slot = u.type->weapons.empty()
                   ? 0 : std::clamp(u.weaponSlot, 0, int(u.type->weapons.size()) - 1);
    const Weapon* sel = slot < int(u.type->weapons.size()) ? &u.type->weapons[slot] : nullptr;
    float best = sel ? sel->range : u.type->maxRange();
    // Ranged units need a clear line to shoot; a wall between them means close
    // in / reposition rather than firing through it (melee & flyers are exempt).
    bool needLoS = best > 64.0f && !u.type->canFly &&
                   target->type && !target->type->canFly;
    bool los = !needLoS ||
               nav_.losBetween(u.x, u.z, target->x, target->z,
                               std::max(u.type->footX, u.type->footZ) / 2,
                               std::max(target->type->footX, target->type->footZ) / 2);
    if (!u.type->canMove && dist > best) {      // static units can't chase
        u.orders.pop_front();
        return;
    }
    if (dist > best * 0.95f || (!los && u.type->canMove)) {
        // Advance toward the target, steering around impassable terrain.
        u.repathLeft -= dt;
        Order& o = u.orders.front();
        o.x = target->x;
        o.z = target->z;
        const NavGrid& grid = navFor(u.type);
        if (!u.type->canFly && !grid.empty() && u.repathLeft <= 0) {
            u.repathLeft = 0.7f;
            auto path = grid.findPath(u.x, u.z, target->x, target->z);
            if (!path.empty()) {
                o.x = path.front().x;
                o.z = path.front().z;
            }
        }
        return;   // movement handled by the normal move logic
    }
    // In range: stop and face the target.
    u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
    float want = std::atan2(dx, dz);
    float diff = angleDiff(want, u.heading);
    float maxTurn = u.type->turnRate * dt;
    u.heading += std::clamp(diff, -maxTurn, maxTurn);
    // Fire the selected weapon when the target is in its [minrange, range] band,
    // within aimtolerance, has a clear shot, and (unless noairweapon) may hit air.
    if (sel && !(sel->noAir && target->type && target->type->canFly) &&
        (sel->melee || los) && u.reloads[slot] <= 0 && dist <= sel->range &&
        dist >= sel->minRange && std::abs(diff) < std::max(sel->aimTol, 0.03f))
        fire(u, *target, slot);
    // cancapture: a charmer converts the target after sustained contact (~3s) or
    // once it is worn down, rather than killing it.
    if (u.type->canCapture && target->type && !target->type->cantBeCaptured &&
        !allied(u.player, target->player)) {
        u.captureProg += dt;
        if (u.captureProg > 3.0f || target->hp < target->type->maxHp * 0.25f) {
            target->player = u.player;
            target->orders.clear();
            target->hp = std::max(target->hp, target->type->maxHp * 0.5f);
            target->lastHitBy = 0;
            u.captureProg = 0;
            u.orders.pop_front();   // done with this one
        }
    }
}

void World::rebuildGrid() {
    gCell_ = 32.0f;
    float minx = 1e30f, minz = 1e30f, maxx = -1e30f, maxz = -1e30f;
    bool any = false;
    // Include ALL alive units (buildings too) so combat can target structures.
    auto inGrid = [](const Unit& u) {
        return u.alive() && !u.embarked() && u.type;
    };
    for (const auto& u : units_) {
        if (!inGrid(u)) continue;
        any = true;
        minx = std::min(minx, u.x); maxx = std::max(maxx, u.x);
        minz = std::min(minz, u.z); maxz = std::max(maxz, u.z);
    }
    if (!any) { gW_ = gH_ = 0; return; }
    gOx_ = minx - gCell_;
    gOz_ = minz - gCell_;
    gW_ = int((maxx - minx) / gCell_) + 3;
    gH_ = int((maxz - minz) / gCell_) + 3;
    gHead_.assign(size_t(gW_) * gH_, -1);
    gNext_.assign(units_.size(), -1);
    for (size_t i = 0; i < units_.size(); ++i) {
        const Unit& u = units_[i];
        if (!inGrid(u)) continue;
        int cx = std::clamp(int((u.x - gOx_) / gCell_), 0, gW_ - 1);
        int cz = std::clamp(int((u.z - gOz_) / gCell_), 0, gH_ - 1);
        int c = cz * gW_ + cx;
        gNext_[i] = gHead_[size_t(c)];
        gHead_[size_t(c)] = int(i);
    }
}

bool World::onManaSpot(float x, float z) const {
    for (const auto& [sx, sz] : manaSpots_) {
        float dx = sx - x, dz = sz - z;
        if (dx * dx + dz * dz < 20.0f * 20.0f) return true;
    }
    return false;
}

bool World::canPlace(const UnitType* type, float x, float z) const {
    if (!type) return false;
    // Lodestones must sit on a mana deposit — but only on maps that have any
    // (deposit-less maps let them build on open ground). And only ONE lodestone
    // per deposit: reject if another already occupies the target deposit.
    if (type->onMana && !manaSpots_.empty()) {
        int spot = -1;
        float best = 24.0f * 24.0f;
        for (size_t i = 0; i < manaSpots_.size(); ++i) {
            float dx = manaSpots_[i].first - x, dz = manaSpots_[i].second - z;
            float d = dx * dx + dz * dz;
            if (d < best) { best = d; spot = int(i); }
        }
        if (spot < 0) return false;   // not on any deposit
        float sx = manaSpots_[size_t(spot)].first, sz = manaSpots_[size_t(spot)].second;
        // One lodestone per deposit. Some Sacred Stones register as two adjacent
        // spots (~22-40px apart); a 44px exclusion merges those into one deposit
        // so a second lodestone can't squeeze onto the same stone.
        for (const auto& u : units_) {
            if (!u.alive() || !u.type || !u.type->onMana) continue;
            float dx = u.x - sx, dz = u.z - sz;
            if (dx * dx + dz * dz < 44.0f * 44.0f) return false;   // deposit taken
        }
    }
    // Check the domain-appropriate grid so water units (Kraken) require water
    // and land units require land, rather than always testing the ground grid.
    const NavGrid& grid = navFor(type);
    int cx = int(x) / 16 - type->footX / 2, cz = int(z) / 16 - type->footZ / 2;
    for (int j = 0; j < type->footZ; ++j)
        for (int i = 0; i < type->footX; ++i)
            if (!grid.walkable(cx + i, cz + j)) return false;
    for (const auto& u : units_) {
        if (!u.alive()) continue;
        float dx = u.x - x, dz = u.z - z;
        float min = 16.0f * float(std::max(type->footX, type->footZ)) / 2 + 12;
        if (dx * dx + dz * dz < min * min) return false;
    }
    return true;
}

int World::startBuild(int builderId, const UnitType* type, float x, float z) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || !b->type->canMove)
        return 0;
    if (!canPlace(type, x, z)) return 0;
    int id = spawn(type, x, z, 3.14159f, b->player);
    Unit* site = unit(id);
    site->underConstruction = true;
    site->hp = type->maxHp * 0.05f;
    if (!type->canMove) { blockFootprint(nav_, *type, x, z, true); flowCache_.clear(); }
    b = unit(builderId);   // spawn may have reallocated units_
    b->buildSiteId = id;
    order(builderId, x, z + float(type->footZ) * 8 + 24, false);
    return id;
}

void World::queueBuild(int builderId, const UnitType* type, float x, float z, bool queue) {
    Unit* b = unit(builderId);
    if (!b || !type) return;
    if (!queue) b->buildOrders.clear();   // fresh order clears the pending queue
    if (b->buildSiteId == 0 && b->buildOrders.empty())
        startBuild(builderId, type, x, z);          // builder is free: start now
    else if (canPlace(type, x, z))
        b->buildOrders.push_back({type, x, z});     // busy: queue behind it
}

void World::cancelBuilds(int builderId) {
    Unit* b = unit(builderId);
    if (!b) return;
    b->buildOrders.clear();
    if (b->buildSiteId) {
        Unit* site = unit(b->buildSiteId);
        // A site that never actually started building is just a ghost — remove
        // it so its marker/ghost doesn't linger.
        if (site && site->underConstruction && !site->buildBegun) {
            if (site->type && !site->type->canMove) {
                blockFootprint(nav_, *site->type, site->x, site->z, false);
                flowCache_.clear();
            }
            site->underConstruction = false;
            site->deadFor = 1000.0f;   // fully gone (painter skips deadFor>=4)
        }
        b->buildSiteId = 0;
    }
}

void World::tickConstruction(Unit& b, float dt) {
    Unit* site = unit(b.buildSiteId);
    if (!site || !site->alive() || !site->underConstruction) {
        b.buildSiteId = 0;
        return;
    }
    float dx = site->x - b.x, dz = site->z - b.z;
    float half = 16.0f * float(std::max(site->type->footX, site->type->footZ)) / 2;
    // Reach = the builder's FBI builddistance (to the site edge) when it has one,
    // else the default footprint-derived range.
    float reach = std::max(half + 40.0f,
                           b.type->buildDist > 0 ? b.type->buildDist + half : 0.0f);
    if (dx * dx + dz * dz > reach * reach) return;   // still walking there
    site->buildBegun = true;   // in range: the site starts materialising now
    b.orders.clear();
    b.speed = 0;
    float total = site->type->buildTime / std::max(b.type->workerTime, 0.01f);
    Player& tm = players_[size_t(b.player)];
    if (gInstantBuild) {
        site->hp = site->type->maxHp;   // finishes this tick, free
    } else {
        float cost = site->type->buildCost * dt / std::max(total, 0.01f);
        if (tm.mana < cost) return;
        tm.mana -= cost;
        site->hp += site->type->maxHp * 0.95f * dt / std::max(total, 0.01f);
    }
    if (site->hp >= site->type->maxHp) {
        site->hp = site->type->maxHp;
        site->underConstruction = false;
        int bid = b.id;
        b.buildSiteId = 0;
        // Kick off the next queued build (skipping any whose spot is now taken).
        // startBuild may reallocate units_, so re-fetch by id each pass.
        while (Unit* nb = unit(bid)) {
            if (nb->buildOrders.empty()) break;
            BuildOrder o = nb->buildOrders.front();
            nb->buildOrders.pop_front();
            if (startBuild(bid, o.type, o.x, o.z) != 0) break;
        }
    }
}

void World::train(int builderId, const UnitType* type) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !type) return;
    b->buildQueue.push_back(type);
}

void World::setRepeat(int builderId, const UnitType* type) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !type) return;
    if (b->repeatType == type) {
        // ctrl+click the +++ icon again: stop now and clear what's pending.
        b->repeatType = nullptr;
        b->buildQueue.clear();
        b->buildProgress = 0;
    } else {
        b->repeatType = type;
        if (b->buildQueue.empty()) b->buildQueue.push_back(type);   // kick it off
    }
}

void World::tickAuras(float dt) {
    // Every unit's buffs relax back toward 1.0; aura projectors then refresh the
    // units in their radius, so a buff holds while in range and fades on leaving.
    float relax = std::min(1.0f, 2.0f * dt);   // ~0.5s to fade after leaving range
    for (auto& u : units_) {
        if (!u.alive()) continue;
        u.atkBuff += (1.0f - u.atkBuff) * relax;
        u.armBuff += (1.0f - u.armBuff) * relax;
    }
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& s = units_[i];
        if (!s.alive() || s.embarked() || !s.type || s.type->auras.empty() ||
            s.underConstruction || !s.active || s.incapacitated())
            continue;
        for (const Aura& a : s.type->auras) {
            if (a.kind == Aura::Kind::Joy || a.radius <= 0) continue;  // morale: not modelled
            float r2 = a.radius * a.radius;
            forEachNear(s.x, s.z, a.radius, [&](int idx) {
                Unit& e = units_[size_t(idx)];
                if (!e.alive() || e.embarked() || !e.type) return;
                bool enemy = !allied(e.player, s.player);
                if (enemy != a.affectsEnemy) return;   // buff friends OR debuff foes
                float dx = e.x - s.x, dz = e.z - s.z;
                float d2 = dx * dx + dz * dz;
                if (d2 > r2) return;
                // edgeeffectiveness: full at centre, (amount blended toward 1 by edge) at rim.
                float t = std::sqrt(d2) / a.radius;
                float amt = 1.0f + (a.amount - 1.0f) * (1.0f - t * (1.0f - a.edge));
                float& buff = a.kind == Aura::Kind::Armor ? e.armBuff : e.atkBuff;
                buff = a.affectsEnemy ? std::min(buff, amt) : std::max(buff, amt);
            });
        }
    }
}

void World::tickAbilities(float /*dt*/) {
    // A corpse is a recently-dead unit still lying on the field.
    auto isCorpse = [](const Unit& c) {
        return c.type && !c.alive() && c.deadFor >= 0 && c.deadFor < 30.0f;
    };
    struct Revive { const UnitType* type; float x, z; int player; };
    std::vector<Revive> revives;
    const float kR = 56.0f;
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        if (!u.alive() || !u.type || u.underConstruction || u.incapacitated()) continue;
        if (!u.type->canReclaim && !u.type->canResurrect) continue;
        if (!u.orders.empty()) continue;   // only while idle (not mid-order)
        // Corpses are dead (not in the spatial grid), so scan directly.
        for (size_t j = 0; j < units_.size(); ++j) {
            Unit& c = units_[j];
            if (j == i || !isCorpse(c)) continue;
            float dx = c.x - u.x, dz = c.z - u.z;
            if (dx * dx + dz * dz > kR * kR) continue;
            if (u.type->canResurrect && c.player == u.player) {
                float cost = c.type->buildCost;
                Player& tm = players_[size_t(u.player)];
                if (tm.mana >= cost) {
                    tm.mana -= cost;
                    revives.push_back({c.type, c.x, c.z, u.player});
                    c.deadFor = 1000.0f;   // consumed
                }
            } else if (u.type->canReclaim) {
                players_[size_t(u.player)].mana += c.type->buildCost * 0.25f;
                c.deadFor = 1000.0f;       // reclaimed away
            }
        }
    }
    for (const auto& r : revives) spawn(r.type, r.x, r.z, 3.14159f, r.player);
}

void World::updateVisibility() {
    if (nav_.empty()) return;
    if (visPlayer_ < 0) return;   // headless referee: nothing renders, no fog needed
    if (vis_.empty()) {
        visW_ = nav_.width();
        visH_ = nav_.height();
        vis_.assign(size_t(visW_) * visH_, 0);
    }
    for (auto& v : vis_)
        if (v == 2) v = 1;
    for (const auto& u : units_) {
        // Shared team vision: every unit on the viewing player's team reveals fog
        // (a negative visPlayer_ -- the headless referee -- reveals nothing).
        if (!u.alive() || !u.type || visPlayer_ < 0 || !allied(u.player, visPlayer_))
            continue;
        // Reveal to the greater of sight and radar range (radardistance).
        int r = int(std::max(u.type->sight, u.type->radar)) / 16 + 1;
        int cx = int(u.x) / 16, cz = int(u.z) / 16;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dz * dz > r * r) continue;
                int x = cx + dx, z = cz + dz;
                if (x >= 0 && z >= 0 && x < visW_ && z < visH_)
                    vis_[size_t(z) * visW_ + x] = 2;
            }
    }
}

void World::tickProduction(Unit& u, float dt) {
    if (u.underConstruction || u.buildQueue.empty()) return;
    const UnitType* t = u.buildQueue.front();
    float total = t->buildTime / std::max(u.type->workerTime, 0.01f);
    Player& tm = players_[size_t(u.player)];
    // Accumulate work (spending mana) until complete. Once complete, buildProgress
    // holds at `total` and grows only as a wait timer below.
    if (u.buildProgress < total) {
        if (gInstantBuild) {
            u.buildProgress = total;   // finishes this tick, free
        } else {
            float cost = t->buildCost * dt / std::max(total, 0.01f);
            if (tm.mana < cost) return;   // stalled: no mana
            tm.mana -= cost;
            u.buildProgress += dt;
        }
        if (u.buildProgress < total) return;   // not done yet
    }
    // Finished: the conjured unit emerges just south of the footprint and walks to
    // a rally point. Hold it until that tile is clear so a repeat/queued build
    // doesn't stack units on top of each other -- but never wait forever (2.5s cap)
    // if the exit is jammed.
    float sx = u.x, sz = u.z + float(u.type->footZ) * 8 + 20;
    float r = std::max(t->footX, t->footZ) * 8.0f + 8.0f;
    bool clear = true;
    forEachNear(sx, sz, r, [&](int idx) {
        const Unit& e = units_[size_t(idx)];
        if (!e.alive() || e.id == u.id) return;
        float dx = e.x - sx, dz = e.z - sz;
        if (dx * dx + dz * dz < r * r) clear = false;
    });
    if (!clear && u.buildProgress < total + 2.5f) {
        u.buildProgress += dt;   // waiting for the tile to clear (no mana spent)
        return;
    }
    u.buildProgress = 0;
    u.buildQueue.pop_front();
    // spawn() may reallocate units_, invalidating `u`; capture the id and re-fetch.
    int producerId = u.id, player = u.player;
    int id = spawn(t, sx, sz, 3.14159f, player);
    order(id, sx + float((id % 5) - 2) * 22, sz + 60, false);
    if (Unit* pu = unit(producerId)) {
        pu->justBuilt = id;
        // Infinite build: re-queue so the next one starts once this one clears.
        if (pu->buildQueue.empty() && pu->repeatType)
            pu->buildQueue.push_back(pu->repeatType);
    }
}

void World::tick(float dt) {
    ++tickCounter_;
    std::chrono::steady_clock::time_point _tk0, _sep0; double g_tsep=0;
    if (g_phase) { _tk0 = std::chrono::steady_clock::now();
                   g_tcomb = g_flowMs = g_pathMs = 0; g_flowN = g_pathN = 0; }
    // Cap A* repaths per tick: a big group that jams while moving can trip the
    // blocked/stuck watchdogs en masse, and hundreds of path searches in one tick
    // stall the sim. Deferred units retry a later tick. Deterministic (fixed budget,
    // unit-index order), so lockstep peers stay in sync.
    pathBudget_ = 24;
    for (auto& u : units_) { u.justFired = false; u.justBuilt = 0; }
    hits_.clear();   // per-tick weapon impacts (drained by the viewer for sounds/fx)
    clock_ += dt;    // wall-clock since the match started (for god timing)

    // Economy: recompute income/storage, apply income.
    for (auto& tm : players_) { tm.income = 0; tm.storage = 0; }
    std::vector<int> godPriests(players_.size(), 0);
    for (auto& u : units_) {
        if (!u.alive() || !u.type) continue;
        auto& tm = players_[size_t(u.player)];
        tm.income += u.type->income;
        tm.storage += u.type->storage;
        if (u.type->attractsGods && !u.underConstruction) godPriests[size_t(u.player)]++;
    }
    for (auto& tm : players_)
        tm.mana = std::min(tm.mana + tm.income * dt, std::max(tm.storage, 100.0f));
    // God favour: a player's priests channel its mana income into favour while any
    // is present; it fills toward kGodFavorNeeded, then godReady() lets the god come.
    if (godsEnabled_)
        for (size_t t = 0; t < players_.size(); ++t)
            if (godPriests[t] > 0)
                players_[t].godFavor = std::min(kGodFavorNeeded,
                    players_[t].godFavor + std::max(players_[t].income, 20.0f) * dt);

    // Index-based: tickProduction can spawn a trained unit, reallocating
    // units_ and invalidating any range-for iterator over it.
    for (size_t i = 0, n = units_.size(); i < n; ++i) {
        Unit& u = units_[i];
        if (u.alive() && u.type) tickProduction(u, dt);
    }
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        if (u.alive() && u.type && u.buildSiteId) tickConstruction(u, dt);
    }

    visTimer_ -= dt;
    if (visTimer_ <= 0) {
        visTimer_ = 0.25f;
        updateVisibility();
    }

    // Projectiles.
    for (auto& p : projectiles_) {
        float ox = p.x, oz = p.z;        // segment start (before this step)
        p.x += p.vx * dt;
        p.z += p.vz * dt;
        p.life -= dt;
        p.age += dt;
        Unit* t = unit(p.targetId);
        if (t && t->alive() && !t->embarked()) {
            // Distance from the target to the segment travelled this tick, so a
            // fast projectile (e.g. the totem's lightning, ~50px/tick) can't
            // skip past the small hit radius between ticks.
            float sx = p.x - ox, sz = p.z - oz;
            float seg = sx * sx + sz * sz;
            float u = seg > 0 ? ((t->x - ox) * sx + (t->z - oz) * sz) / seg : 0.0f;
            u = std::clamp(u, 0.0f, 1.0f);
            float cx = ox + u * sx, cz = oz + u * sz;
            float dx = t->x - cx, dz = t->z - cz;
            float r = t->type ? 8.0f + 8.0f * float(std::max(t->type->footX,
                                                             t->type->footZ)) : 8.0f;
            if (dx * dx + dz * dz < r * r) {
                // Apply the impact: direct hit + area splash (per the weapon's
                // FBI areaofeffect), using the grid from the previous rebuild.
                if (p.wsrc) applyHit(*p.wsrc, t->x, t->z, p.fromPlayer, p.fromId, t);
                else t->hp -= p.damage;
                p.life = -1;
            }
        }
    }
    std::erase_if(projectiles_, [](const Projectile& p) { return p.life <= 0; });

    rebuildGrid();   // spatial hash for this tick (combat acquire + separation)

    for (auto& u : units_) {
        if (!u.type) continue;
        if (!u.alive()) { u.deadFor += dt; continue; }
        if (u.hp <= 0) {
            // Award the destroyed unit's experiencepoints to the killer, then set
            // its veteran level = accumulatedXP / the killer's OWN experiencepoints,
            // capped at 10 (retail KINGDOMS.icd). No HP-pool change — veterancy
            // works through the attack/armor multipliers, not a bigger health bar.
            if (u.lastHitBy && u.type) {
                Unit* k = unit(u.lastHitBy);
                // Credit the killer's player with an enemy kill (F4 overlay). Counts
                // even if the killer is a structure or has since died -- what
                // matters is who landed the fatal blow, not that it still lives.
                if (k && k->type && !allied(k->player, u.player) &&
                    k->player >= 0 && k->player < int(players_.size()))
                    players_[size_t(k->player)].kills++;
                if (k && k->alive() && k->type && k->type->canMove &&
                    !k->type->noVeteran) {
                    // One veteran level per kill, capped at 10 (retail counts
                    // kills, not the victim's value — so a cheap unit that lands
                    // a single big kill doesn't jump straight to max veterancy).
                    k->xp += 1;
                    k->veteran = std::min(10, k->xp);
                }
            }
            u.deadFor = 0; u.orders.clear(); u.speed = 0; continue;
        }

        // Status timers count down; HP regenerates (healtime); mana recharges.
        if (u.frozenFor > 0) u.frozenFor = std::max(0.0f, u.frozenFor - dt);
        if (u.stonedFor > 0) u.stonedFor = std::max(0.0f, u.stonedFor - dt);
        if (u.paralyzedFor > 0) u.paralyzedFor = std::max(0.0f, u.paralyzedFor - dt);
        if (u.type->healTime > 0 && u.hp < u.type->maxHp)
            u.hp = std::min(u.type->maxHp, u.hp + dt / u.type->healTime);
        if (u.type->maxMana > 0 && u.mana < u.type->maxMana)
            u.mana = std::min(u.type->maxMana, u.mana + u.type->manaRegen * dt);
        if (u.orders.empty()) { u.homeX = u.x; u.homeZ = u.z; }   // leash anchor

        // Cloaking: drains player mana; an enemy within mincloakdistance forces a
        // decloak, and so does running dry of mana.
        if (u.type->canCloak) {
            bool enemyNear = false;
            float md = std::max(u.type->minCloakDist, 1.0f);
            forEachNear(u.x, u.z, md, [&](int idx) {
                const Unit& e = units_[size_t(idx)];
                if (e.alive() && !e.embarked() && !allied(e.player, u.player) && e.type) {
                    float dx = e.x - u.x, dz = e.z - u.z;
                    if (dx * dx + dz * dz <= md * md) enemyNear = true;
                }
            });
            float cost = (u.speed > 3.0f ? u.type->cloakCostMove : u.type->cloakCost) * dt;
            Player& tm = players_[size_t(u.player)];
            if (!enemyNear && tm.mana >= cost) { tm.mana -= cost; u.cloaked = true; }
            else u.cloaked = false;
        }

        if (u.underConstruction) continue;   // silent until finished
        if (u.embarked()) {                  // riding a transport
            Unit* t = unit(u.inTransport);
            if (t && t->alive()) { u.x = t->x; u.z = t->z; }
            else u.deadFor = 0;              // transport lost with all hands
            continue;
        }
        // Frozen / petrified / paralyzed: the unit is inert this tick.
        if (u.incapacitated()) { u.speed = 0; continue; }
        if (!u.orders.empty() && (u.orders.front().load || u.orders.front().unload))
            tickTransport(u, dt);
        else if (g_phase) { auto _c0=std::chrono::steady_clock::now(); tickCombat(u, dt); g_tcomb += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-_c0).count(); }
        else tickCombat(u, dt);

        bool combatHold =
            !u.orders.empty() && u.orders.front().targetId != 0 &&
            !u.orders.front().load && !u.orders.front().guard && [&] {
                Unit* t = unit(u.orders.front().targetId);
                if (!t) return false;
                float dx = t->x - u.x, dz = t->z - u.z;
                if (std::sqrt(dx * dx + dz * dz) > u.type->maxRange() * 0.95f) return false;
                // Don't sit still with no shot: a ranged unit whose line to the
                // target is blocked keeps moving to get around the wall.
                bool needLoS = u.type->maxRange() > 64.0f && !u.type->canFly &&
                               t->type && !t->type->canFly && u.type->canMove;
                return !needLoS ||
                       nav_.losBetween(u.x, u.z, t->x, t->z,
                                       std::max(u.type->footX, u.type->footZ) / 2,
                                       std::max(t->type->footX, t->type->footZ) / 2);
            }();
        if (combatHold) continue;

        if (u.orders.empty()) {
            u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
        } else {
            const Order& o = u.orders.front();
            float dx = o.x - u.x, dz = o.z - u.z;
            float dist = std::sqrt(dx * dx + dz * dz);
            if (o.guard && dist <= 70.0f) continue;   // in escort position
            // Flow-field orders complete a little short of the goal so a crowd
            // sharing one destination settles into a blob (spread by separation)
            // instead of every unit fighting for the exact same point.
            float arrive = o.flow ? 16.0f : 3.0f;
            if (dist < arrive) {
                if (o.targetId == 0) {
                    Order done = o;
                    u.orders.pop_front();
                    if (done.patrol) u.orders.push_back(done);
                }
                continue;
            }
            float want = std::atan2(dx, dz);
            if (o.flow) {
                // Steer along the shared flow field; near the goal (or in an
                // unreachable pocket) the field goes flat and we home straight in.
                const FlowField* ff = flowFor(u.type, o.x, o.z);
                float fx = 0, fz = 0;
                if (ff) ff->dirAt(u.x, u.z, fx, fz);
                if (fx != 0 || fz != 0) want = std::atan2(fx, fz);
            }
            float diff = angleDiff(want, u.heading);
            float maxTurn = u.type->turnRate * dt;
            u.heading += std::clamp(diff, -maxTurn, maxTurn);

            // Brake into the waypoint if it's the last one; slow for big turns.
            float target = u.type->maxVel;
            // watermultiplier: a ground unit wading shallow water moves slower.
            if (!u.type->canFly && u.type->waterMult != 1.0f && !depth_.empty()) {
                int cx = int(u.x) / 16, cz = int(u.z) / 16;
                if (cx >= 0 && cz >= 0 && cx < terW_ && cz < terH_ &&
                    depth_[size_t(cz) * terW_ + cx] > 0)
                    target *= u.type->waterMult;
            }
            if (std::abs(diff) > 0.8f) target *= 0.3f;
            bool last = u.orders.size() == 1;
            if (last) {
                float stopDist = u.speed * u.speed / (2 * u.type->brake);
                if (dist < stopDist) target = 0;
            }
            if (u.speed < target)
                u.speed = std::min(u.speed + u.type->accel * dt, target);
            else
                u.speed = std::max(u.speed - u.type->brake * dt, target);

            float mx = std::sin(u.heading) * u.speed * dt;
            float mz = std::cos(u.heading) * u.speed * dt;
            // Collide ground/water units with the nav grid so they can't walk
            // through walls and buildings (pathfinding routes around, but direct
            // steering in combat did not). Slide along a blocked axis.
            if (u.type->canFly) {
                u.x += mx; u.z += mz;
            } else {
                const NavGrid& g = navFor(u.type);
                auto free = [&](float nx, float nz) {
                    // Use the SAME grid the pathfinder used, so a unit never
                    // stalls on a cell its own path routed it through. (The
                    // per-unit slope/water limits still gate placement/pathing.)
                    return g.empty() || g.walkable(int(nx) / 16, int(nz) / 16);
                };
                if (free(u.x + mx, u.z + mz)) { u.x += mx; u.z += mz; }
                else if (free(u.x + mx, u.z)) { u.x += mx; }
                else if (free(u.x, u.z + mz)) { u.z += mz; }
                else {
                    // Fully blocked (a wall dead-ahead the straight path clipped):
                    // stop and repath around it toward the final destination, so
                    // the unit routes around instead of wedging permanently.
                    u.speed = 0;
                    u.repathLeft -= dt;
                    if (!g.empty() && u.repathLeft <= 0 &&
                        u.orders.front().targetId == 0) {
                        u.repathLeft = 0.5f;
                        float tx = u.orders.back().x, tz = u.orders.back().z;
                        // The flow field (built for this goal) already holds the
                        // reachable set. If this unit can't reach the goal, give up
                        // rather than run a full-grid A* that scans the whole map
                        // before failing -- hundreds of units doing that is the sim
                        // stall. Only repath when reachable, and within the budget.
                        const FlowField* ff = flowFor(u.type, tx, tz);
                        bool onWalkable = g.walkable(int(u.x) / 16, int(u.z) / 16);
                        if (ff && onWalkable && !ff->reachable(u.x, u.z)) {
                            u.orders.clear();
                        } else if (pathBudget_ > 0) {
                            --pathBudget_;
                            auto _p0=std::chrono::steady_clock::now(); auto path = g.findPath(u.x, u.z, tx, tz); if(g_phase){ g_pathMs += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-_p0).count(); ++g_pathN; }
                            if (!path.empty()) {
                                u.orders.clear();
                                for (const auto& wp : path) u.orders.push_back(wp);
                            }
                        }
                    }
                }
            }

            // Stuck watchdog: a unit that wants to move but makes no headway
            // (crowd jam at a corner/chokepoint) gets nudged sideways and
            // repathed to break the deadlock. Half the units nudge each way so
            // a crowd splits around an obstacle instead of piling up.
            if (!u.type->canFly) {
                float moved = std::hypot(u.x - u.stuckX, u.z - u.stuckZ);
                if (moved > 11.0f) {
                    u.stuckFor = 0; u.stuckX = u.x; u.stuckZ = u.z;
                } else {
                    u.stuckFor += dt;
                    if (u.stuckFor > 1.0f) {
                        u.stuckFor = 0; u.stuckX = u.x; u.stuckZ = u.z;
                        const NavGrid& g = navFor(u.type);
                        auto free = [&](float nx, float nz) {
                            return g.empty() || g.walkable(int(nx) / 16, int(nz) / 16);
                        };
                        float px = std::cos(u.heading), pz = -std::sin(u.heading);
                        float s = (u.id & 1) ? 1.0f : -1.0f;
                        for (float d : {20.0f, 34.0f}) {
                            if (free(u.x + px * s * d, u.z + pz * s * d)) {
                                u.x += px * s * d * 0.5f; u.z += pz * s * d * 0.5f; break;
                            }
                            if (free(u.x - px * s * d, u.z - pz * s * d)) {
                                u.x -= px * s * d * 0.5f; u.z -= pz * s * d * 0.5f; break;
                            }
                        }
                        if (!g.empty() && u.orders.front().targetId == 0) {
                            float tx = u.orders.back().x, tz = u.orders.back().z;
                            const FlowField* ff = flowFor(u.type, tx, tz);
                            bool onWalkable = g.walkable(int(u.x) / 16, int(u.z) / 16);
                            if (ff && onWalkable && !ff->reachable(u.x, u.z)) {
                                u.orders.clear();   // unreachable -> give up, no A*
                            } else if (pathBudget_ > 0) {
                                --pathBudget_;
                                auto _p0=std::chrono::steady_clock::now(); auto path = g.findPath(u.x, u.z, tx, tz); if(g_phase){ g_pathMs += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-_p0).count(); ++g_pathN; }
                                if (!path.empty()) {
                                    u.orders.clear();
                                    for (const auto& wp : path) u.orders.push_back(wp);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (g_phase) _sep0 = std::chrono::steady_clock::now();
    // Separation: push overlapping mobile units apart. The spatial hash limits
    // each unit to its ~3x3 neighbourhood, so this is O(n) not O(n^2). Each pair
    // is handled once (by the lower index), so the result matches the old loop.
    rebuildGrid();   // units moved this tick; rebuild for accurate neighbours
    tickAbilities(dt);   // reclaim / resurrect corpses (uses the fresh grid)
    tickAuras(dt);       // AdjustArmor/Attack stat auras (uses the fresh grid)
    constexpr float kSep = 13.0f;
    auto ok = [&](const Unit& u, float nx, float nz) {
        const NavGrid& g = navFor(u.type);
        return g.empty() || g.walkable(int(nx) / 16, int(nz) / 16);
    };
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& a = units_[i];
        if (!a.alive() || a.embarked() || !a.type || !a.type->canMove || a.type->canFly) continue;
        forEachNear(a.x, a.z, kSep, [&](int j) {
            if (size_t(j) <= i) return;   // handle each pair once, and skip self
            Unit& b = units_[size_t(j)];
            if (!b.alive() || b.embarked() || !b.type || !b.type->canMove || b.type->canFly) return;
            float dx = b.x - a.x, dz = b.z - a.z;
            float d2 = dx * dx + dz * dz;
            if (d2 >= kSep * kSep) return;
            if (d2 < 1e-6f) { b.x += 1.0f; return; }   // exactly stacked: nudge
            float d = std::sqrt(d2);
            float push = (kSep - d) * 0.5f;
            dx /= d; dz /= d;
            float axn = a.x - dx * push, azn = a.z - dz * push;
            float bxn = b.x + dx * push, bzn = b.z + dz * push;
            if (ok(a, axn, azn)) { a.x = axn; a.z = azn; }
            if (ok(b, bxn, bzn)) { b.x = bxn; b.z = bzn; }
        });
    }

    // Unstick: any ground unit that ends up inside a blocked cell (spawned by a
    // building, shoved by a crowd, or clipped a corner) is nudged toward the
    // nearest walkable cell so it can never wedge permanently.
    for (auto& u : units_) {
        if (!u.alive() || u.embarked() || !u.type || !u.type->canMove ||
            u.type->canFly)
            continue;
        const NavGrid& g = navFor(u.type);
        if (g.empty()) continue;
        int cx = int(u.x) / 16, cz = int(u.z) / 16;
        if (g.walkable(cx, cz)) continue;
        float bestD = 1e18f, tx = u.x, tz = u.z;
        bool found = false;
        for (int r = 1; r <= 4 && !found; ++r)
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx) {
                    if (!g.walkable(cx + dx, cz + dz)) continue;
                    float wx = (cx + dx) * 16 + 8.0f, wz = (cz + dz) * 16 + 8.0f;
                    float d = (wx - u.x) * (wx - u.x) + (wz - u.z) * (wz - u.z);
                    if (d < bestD) { bestD = d; tx = wx; tz = wz; found = true; }
                }
        if (found) {
            float dx = tx - u.x, dz = tz - u.z;
            float dl = std::sqrt(dx * dx + dz * dz);
            if (dl > 1e-3f) {
                float step = std::min(dl, 40.0f * dt + 3.0f);
                u.x += dx / dl * step;
                u.z += dz / dl * step;
            }
        }
    }
    // Win/defeat is derived from unit state on every sim (clients + referee),
    // so all peers agree on the tick a team is eliminated / the game is won.
    updateOutcome();
    if (g_phase) {
        auto _end = std::chrono::steady_clock::now();
        double tsep = std::chrono::duration<double,std::milli>(_end-_sep0).count();
        double ttot = std::chrono::duration<double,std::milli>(_end-_tk0).count();
        static double thr = getenv("TAK_PHASE_MS") ? atof(getenv("TAK_PHASE_MS")) : 15.0;
        if (ttot > thr) {   // only report a stall (threshold tunable via TAK_PHASE_MS)
            int alive = 0; for (auto& u : units_) if (u.alive()) ++alive;
            std::fprintf(stderr, "SIMPHASE tick=%.1fms combat=%.1f sep=%.1f flow=%.1f(x%ld) path=%.1f(x%ld) other=%.1f units=%d\n",
                         ttot, g_tcomb, tsep, g_flowMs, g_flowN, g_pathMs, g_pathN,
                         ttot - g_tcomb - tsep - g_flowMs - g_pathMs, alive);
        }
    }
}

uint64_t World::stateHash() const {
    // FNV-1a over the quantities that must agree between lockstep peers.
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFF;
            h *= 1099511628211ULL;
        }
    };
    auto mixf = [&](float f) {
        uint32_t b;
        static_assert(sizeof b == sizeof f);
        std::memcpy(&b, &f, 4);
        mix(b);
    };
    for (const auto& u : units_) {
        mix(uint64_t(u.id));
        mix(uint64_t(u.player));
        mixf(u.x);
        mixf(u.z);
        mixf(u.hp);
        mixf(u.heading);
        mix(uint64_t(u.orders.size()));
        // Order target + attack-move flag: two sims can hold the same order
        // COUNT while chasing different targets -- fold the head order in so a
        // divergence shows up before damage lands (the referee must attribute
        // desyncs fast at 8 players).
        if (!u.orders.empty()) {
            const Order& o = u.orders.front();
            mix(uint64_t(uint32_t(o.targetId)));
            mix(uint64_t(o.attackMove ? 1 : 0));
            mixf(o.x);
            mixf(o.z);
        }
        mix(uint64_t(u.alive() ? 1 : 0));
        mix(uint64_t(u.veteran));
        mixf(u.reloads[0]);
    }
    // Projectiles: count alone hides same-count divergence, so fold owner and
    // position of each in flight (mixf hashes the exact bits -- deterministic
    // across peers and NaN-safe, unlike an int cast).
    mix(uint64_t(projectiles_.size()));
    for (const auto& p : projectiles_) {
        mix(uint64_t(uint32_t(p.fromPlayer)));
        mixf(p.x);
        mixf(p.z);
    }
    for (const auto& t : players_) {
        mixf(t.mana);
        mixf(t.godFavor);
        // Team assignment drives sim behaviour (splash/acquire/auras) but is set
        // from setup -- fold it in so a lobby/config mismatch faults immediately
        // as a desync instead of diverging mysteriously.
        mix(uint64_t(uint32_t(t.team)));
    }
    return h;
}

int World::updateOutcome() {
    // A player is defeated when it has no living units. Compute per-player
    // alive counts, then per-team. This runs on every sim (referee included),
    // so all peers conclude win/defeat on the same tick.
    std::vector<int> aliveByPlayer(players_.size(), 0);
    for (const auto& u : units_)
        if (u.alive() && u.type &&
            u.player >= 0 && u.player < int(players_.size()))
            ++aliveByPlayer[size_t(u.player)];
    for (int p = 0; p < int(players_.size()); ++p)
        players_[size_t(p)].defeated = (aliveByPlayer[size_t(p)] == 0);

    // Count DISTINCT teams that still have a living unit (robust to any team id,
    // not just 0..n-1): a surviving player counts its team once -- the first time
    // that team appears among survivors. If exactly one team remains it wins.
    int survivingTeam = -1, survivingCount = 0;
    for (int p = 0; p < int(players_.size()); ++p) {
        if (aliveByPlayer[size_t(p)] == 0) continue;
        int tm = players_[size_t(p)].team;
        bool firstOfTeam = true;
        for (int q = 0; q < p; ++q)
            if (aliveByPlayer[size_t(q)] > 0 && players_[size_t(q)].team == tm) {
                firstOfTeam = false;
                break;
            }
        if (firstOfTeam) { ++survivingCount; survivingTeam = tm; }
    }
    winningTeam_ = (survivingCount == 1) ? survivingTeam : -1;
    return winningTeam_;
}

} // namespace tak::sim
