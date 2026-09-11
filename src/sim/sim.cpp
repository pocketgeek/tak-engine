#include "sim/sim.h"

#include "hpi/hpi.h"
#include "sim/detmath.h"
#include "sim/mission.h"
#include "sim/scenario.h"
#include "tdf/tdf.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <atomic>
#include <thread>

namespace tak::sim {

bool gInstantBuild = false;

// TAK_PHASE sim profiler globals (zero cost when unset). The accumulators are
// thread_local: the server may tick several games in parallel, one World::tick per
// thread, so each thread accumulates (and prints) its own room's timing -- both
// race-free and correctly attributed.
static const bool g_phase = getenv("TAK_PHASE") != nullptr;
static double g_visMs = 0, g_burnMs = 0, g_gridMs = 0;   // finer "other" split
static thread_local double g_tcomb = 0, g_flowMs = 0, g_pathMs = 0;
static thread_local long g_flowN = 0, g_pathN = 0;

namespace {

constexpr float kPi = 3.14159265358979f;
// How long a released bomb takes to reach the ground from a flyer's cruise
// altitude. Retail drives it down under gravity from the terrain height it looks
// up at the release point; our projectiles are 2-D, so the fall is a fixed window
// after which the bomb detonates where it has drifted to.
constexpr float kBombFall = 0.8f;
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

void TypeRegistry::loadMoveInfo(const hpi::Vfs& vfs, const std::string& path) {
    try {
        auto b = vfs.read(path);
        auto root = tdf::parseText(std::string(b.begin(), b.end()), path);
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

void TypeRegistry::loadDir(const hpi::Vfs& vfs, const std::string& prefix) {
    for (const std::string& path : vfs.list(prefix)) {
        if (lower(std::filesystem::path(path).extension().string()) != ".fbi") continue;
        try {
            auto bytes = vfs.read(path);
            auto root = tdf::parseText(std::string(bytes.begin(), bytes.end()), path);
            const auto* info = root.child("UNITINFO");
            if (!info) continue;
            UnitType t;
            std::string stem = lower(std::filesystem::path(path).stem().string());
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
            t.commander = info->numberOr("commander", 0) != 0;   // the Monarch
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
            t.stoneFeat = lower(info->valueOr("stone", ""));
            t.frozenFeat = lower(info->valueOr("frozen", ""));
            t.corpseAdjX = int(info->numberOr("corpseadjustx", 0));
            t.corpseAdjZ = int(info->numberOr("corpseadjustz", 0));
            t.canAnimate = info->numberOr("cananimate", 0) != 0;
            t.animName_ = lower(info->valueOr("animatetype", ""));
            t.shadowArt = lower(info->valueOr("shadowart", ""));
            t.veteranModel = lower(info->valueOr("veteranmodel", ""));
            t.bodyType = lower(info->valueOr("bodytype", "default"));
            // Extended stats.
            t.healTime = float(info->numberOr("healtime", 0));
            t.leash = float(info->numberOr("maneuverleashlength", 0));
            t.waterMult = float(info->numberOr("watermultiplier",
                                info->numberOr("watermultipliser", 1)));
            // Exact key only: the icd's parser knows no typo fallback, so
            // verpult's "roadmultplier" never counted in retail either. The
            // retail default is ~1.2 (16.16 0x13333), NOT 1.0.
            t.roadMult = float(info->numberOr("roadmultiplier", 1.2));
            t.maxWaterDepth = float(info->numberOr("maxwaterdepth", 0));
            t.maxSlope = float(info->numberOr("maxslope", 255));
            t.radar = float(info->numberOr("radardistance", 0));
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
            t.attractsGods = info->numberOr("attractsgods", 0) != 0;
            t.onOffable = info->numberOr("onoffable", 0) != 0;
            t.activateWhenBuilt = info->numberOr("activatewhenbuilt", 1) != 0;
            t.cantBeStoned = info->numberOr("cantbestoned", 0) != 0;
            t.cantBeFrozen = info->numberOr("cantbefrozen", 0) != 0;
            // Retail forces both on Monarchs (icd: def bit18 -> cantbestoned +
            // cantbefrozen) -- else petrify would be an instant Monarch kill.
            if (t.commander) t.cantBeStoned = t.cantBeFrozen = true;
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
            // One weapon block -> a Weapon. Shared by WEAPON1..3 and by
            // [EXPLODEAS] (the death blast), which is the same block shape.
            auto parseWeapon = [](const tdf::Node* w) {
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
                         wtag.find("breath") != std::string::npos)
                    wp.fx = WeaponFx::Fire;
                // FBI weapon type = "Line of Sight" is a sustained hitscan beam
                // (the Gold Dragon's Fire Breath: range 600, emittime 45). It is
                // NOT a lobbed projectile -- earlier code capped every fire weapon's
                // range to 170px on the false premise that a breath is a short
                // emission, which forced the drake to dive to point-blank and spit a
                // single slow comet. Use the FBI range and drive the flame from
                // emittime instead. (emittime is in 30Hz frames.)
                wp.beam = lower(w->valueOr("type", "")) == "line of sight";
                wp.emitTime = float(w->numberOr("emittime", 0)) / 30.0f;
                // The remaining retail weapon classes. `type` is authoritative;
                // subtype adds the mind-control behaviour on top of either a
                // line-of-sight shot (Individual) or a Remote Effect (Area).
                {
                    std::string ty = lower(w->valueOr("type", ""));
                    if (ty == "guided") wp.kind = Weapon::Kind::Guided;
                    else if (ty == "remote effect") wp.kind = Weapon::Kind::Remote;
                    else if (ty == "wandering") wp.kind = Weapon::Kind::Wandering;
                    // turnrate is in degrees/second (a guided shot at 180 flips its
                    // heading in a second, which matches the shipped 120..600 range
                    // against 250-1200 px/s speeds).
                    wp.turnRate = float(w->numberOr("turnrate", 0)) * (kPi / 180.0f);
                    wp.buildUp = float(w->numberOr("builduptime", 0));
                    wp.decay = float(w->numberOr("decaytime", 0));
                    wp.duration = float(w->numberOr("duration", 0));
                    // maxvariation is NOT an angle: it is the wander jitter's
                    // half-width in PIXELS PER TICK (2..8 across the shipped
                    // storms, which dwarfs their 45..80 px/s drift -- that is what
                    // makes the path genuinely wander rather than curve).
                    wp.maxVariation = float(w->numberOr("maxvariation", 0));
                    wp.variationTime = float(w->numberOr("variationtime", 0));
                    wp.unitsOnly = w->numberOr("unitsonly", 0) != 0;
                    wp.particlesPerSec = float(w->numberOr("particlespersecond", 0));
                    std::string st = lower(w->valueOr("subtype", ""));
                    wp.mindControl = st == "mindcontrol";
                    // Which Remote Effect subclass this is (retail dispatches on
                    // subtype); it decides the damage cadence, not just the visuals.
                    // subtype=Dropped rides on a Ballistic weapon but is its own
                    // retail class: the bomb is let go, not launched.
                    if (st == "dropped") wp.kind = Weapon::Kind::Dropped;
                    if (st == "earthquake") wp.remote = Weapon::RemoteKind::Earthquake;
                    else if (st == "hailstorm") wp.remote = Weapon::RemoteKind::Hailstorm;
                    else if (st == "mindcontrol") wp.remote = Weapon::RemoteKind::MindCtl;
                    else if (st == "turntofrozen") wp.remote = Weapon::RemoteKind::Freeze;
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
                {
                    std::string dtp = lower(w->valueOr("damagetype", "normal"));
                    wp.dmgType = dtp == "fire" ? 2 : dtp == "explosion" ? 3
                               : dtp == "paralyzer" ? 4 : 1;
                }
                wp.minRange = float(w->numberOr("minrange", 0));
                wp.noAir = w->numberOr("noairweapon", 0) != 0;
                wp.manaCost = float(w->numberOr("manapershot", 0));
                // Status-effect weapons (Creon freeze, medusa/paralyzer, petrify),
                // inferred from the hit-effect / damagetype / name.
                {
                    // Freeze and petrify come from the SUBTYPE, never from the name.
                    // Retail selects them by subtype=turntofrozen/turntostone (6
                    // weapons, all named accordingly), and a name-substring
                    // heuristic also swept up the three HAILSTORMS -- "Hail Shower",
                    // "Ice Storm", "Ice Storms" -- which do not freeze. Because our
                    // freeze is an instant statue death, that quietly turned the
                    // Acolyte's 70-damage Hail Shower into a 200-radius instant kill.
                    std::string st = lower(w->valueOr("subtype", ""));
                    std::string s = hwe + " " + lower(w->valueOr("damagetype", "")) +
                                    " " + lower(w->valueOr("soundhitclass", "")) + " " +
                                    lower(wp.name);
                    if (st == "turntofrozen") wp.status = Weapon::Status::Frozen;
                    else if (st == "turntostone") wp.status = Weapon::Status::Stoned;
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
                return wp;
            };
            for (int slot = 1; slot <= 3; ++slot) {
                const auto* w = root.child("WEAPON" + std::to_string(slot));
                if (!w) continue;
                Weapon wp = parseWeapon(w);
                if (wp.damage > 0) t.weapons.push_back(wp);
            }
            if (!t.weapons.empty()) t.weapon = t.weapons[0];
            // [EXPLODEAS]: the weapon a unit detonates at its own position when it
            // dies -- the Kamikaze Rat's whole purpose (areaofeffect 203, 8000
            // damage), plus the Grenadier, Fire Demon, Balloon, crebomb, creshoc.
            if (const auto* ea = root.child("EXPLODEAS")) {
                t.explodeAs = parseWeapon(ea);
                t.hasExplodeAs = t.explodeAs.damage > 0;
            }
            // totalallowed: per-player cap on live units of this type (the five
            // dragons, the five gods and the Aerial Juggernaut all carry 1).
            t.totalAllowed = int(info->numberOr("totalallowed", 0));
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
    // Fix up cross-type references now that every unit is in the table
    // (animatetype=MONGHOUL names another unit the animator raises).
    for (auto& [id, t] : types_)
        if (!t.animName_.empty())
            if (auto it = types_.find(t.animName_); it != types_.end())
                t.animateType = &it->second;
}

void TypeRegistry::loadBuildTree(const hpi::Vfs& vfs, const std::string& prefix) {
    namespace fs = std::filesystem;
    // The VFS enumerates <prefix>/<builder>/<buildable>.tdf recursively; group by
    // the builder subdirectory (the path segment right after `prefix`).
    std::string pre = lower(prefix);
    while (!pre.empty() && (pre.back() == '/' || pre.back() == '\\')) pre.pop_back();
    std::map<std::string, std::vector<std::pair<double, std::string>>> byBuilder;
    std::vector<std::string> order;   // builders in first-seen order
    for (const std::string& path : vfs.list(prefix)) {
        if (lower(fs::path(path).extension().string()) != ".tdf") continue;
        // Split into segments; require exactly <prefix>/<builder>/<file>.
        std::vector<std::string> segs;
        for (const auto& part : fs::path(path)) {
            std::string s = part.string();
            if (!s.empty() && s != "/") segs.push_back(s);
        }
        size_t i = 0;
        while (i < segs.size() && lower(segs[i]) != pre) ++i;
        if (i + 3 != segs.size()) continue;   // require exactly <prefix>/<builder>/<file>
        std::string builder = lower(segs[i + 1]);
        double prio = 99;
        try {
            auto bytes = vfs.read(path);
            auto root = tdf::parseText(std::string(bytes.begin(), bytes.end()), path);
            if (const auto* m = root.child("Menu")) prio = m->numberOr("priority", 99);
        } catch (const std::exception&) {}
        if (!byBuilder.count(builder)) order.push_back(builder);
        byBuilder[builder].push_back({prio, lower(fs::path(path).stem().string())});
    }
    for (const std::string& builder : order) {
        auto entries = byBuilder[builder];
        std::sort(entries.begin(), entries.end());
        auto& list = buildTree_[builder];
        // First source to define a builder wins it whole -- so a Crusades-balance
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

std::vector<const UnitType*> TypeRegistry::combatUnits(const std::string& side) const {
    std::vector<const UnitType*> out;
    // types_ is a std::map keyed by lowercased id, so this walk is name-sorted and
    // identical on every peer -> a deterministic stress-test roster.
    for (const auto& [id, t] : types_) {
        if (t.side != side) continue;
        if (!t.canMove || t.isBuilder || t.isStructure() || t.commander) continue;
        if (t.weapons.empty()) continue;   // must be able to fight (drives real load)
        out.push_back(&t);
    }
    return out;
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
    players_[size_t(player)].unitCount++;   // keep the cap count exact within a tick
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

void World::setTerrain(const std::vector<uint8_t>& heights, int w, int h, int seaLevel,
                       const std::vector<uint16_t>* features) {
    heights_ = heights;   // keep raw heights for fog line-of-sight
    hW_ = w; hH_ = h;
    // Road cells: the retail map format marks them as 0xFFFB in the feature
    // plane (verified against Two Castles -- the mask traces the road network).
    roads_.clear();
    if (features && features->size() == size_t(w) * size_t(h)) {
        bool any = false;
        roads_.assign(size_t(w) * size_t(h), 0);
        for (size_t i = 0; i < roads_.size(); ++i)
            if ((*features)[i] == 0xFFFB) { roads_[i] = 1; any = true; }
        if (!any) roads_.clear();   // no roads on this map: skip the per-tick test
    }
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
    nav_.setRoads(&roads_);   // ground domain only: marches prefer highways
    // 0xFFFC cells are hard blockers (retail rates them impassable for every
    // movement domain AND rejects building placement on them -- icd 0x508190 /
    // 0x507705; they sit under castle wall/gate/dock art baked into terrain).
    if (features && features->size() == size_t(w) * size_t(h))
        for (int z = 0; z < h; ++z)
            for (int x = 0; x < w; ++x)
                if ((*features)[size_t(z) * w + x] == 0xFFFC) {
                    nav_.block(x, z, 1, 1, true);
                    navWater_.block(x, z, 1, 1, true);
                    navHover_.block(x, z, 1, 1, true);
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
    // Fog starts FULLY UNEXPLORED the moment terrain exists: before this, vis_ was
    // allocated lazily by the first updateVisibility (0.25s into the sim), and an
    // empty vis_ makes drawFog skip and cellVisible() report everything visible --
    // so the first rendered frames flashed the whole bare map (masked historically
    // by the synchronous load stall; exposed once terrain composited async). Local
    // display only (never hashed); the headless referee (visPlayer_ < 0) skips fog
    // entirely. Sized to THIS terrain, so loading a different map into the same
    // world also re-fits the grid instead of keeping stale dimensions.
    if (visPlayer_ >= 0) {
        visW_ = w;
        visH_ = h;
        vis_.assign(size_t(w) * size_t(h), 0);
        ++visGen_;   // the renderer re-uploads the (all-fogged) texture
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

bool FlowField::build(const NavGrid& nav, float gx, float gz, int foot) {
    w_ = nav.width();
    h_ = nav.height();
    if (nav.empty() || w_ <= 0 || h_ <= 0) { w_ = h_ = 0; return false; }
    int gcx = std::clamp(int(gx) / 16, 0, w_ - 1);
    int gcz = std::clamp(int(gz) / 16, 0, h_ - 1);
    // Snap a goal the unit can't fit at (a click on a wall/building, or a spot too
    // tight for its footprint) to the nearest fitting cell, spiralling out.
    if (!nav.fits(gcx, gcz, foot)) {
        bool found = false;
        for (int r = 1; r < 24 && !found; ++r)
            for (int j = -r; j <= r && !found; ++j)
                for (int i = -r; i <= r && !found; ++i) {
                    if (std::max(std::abs(i), std::abs(j)) != r) continue;
                    if (nav.fits(gcx + i, gcz + j, foot)) {
                        gcx += i; gcz += j; found = true;
                    }
                }
        if (!found) { w_ = h_ = 0; return false; }
    }
    goal_ = gcz * w_ + gcx;

    const size_t n = size_t(w_) * h_;
    // Integration field in "centi-cells": orthogonal step 10, diagonal 14, so a
    // uint16 covers paths up to ~6500 cells. Dijkstra out from the goal, using
    // Dial's algorithm: with a max step of 14 a circular array of 15 cost buckets
    // replaces the binary heap (no log-n push/pop, pure integer work, ~2-3x
    // faster). The result is bit-identical to the heap version -- Dijkstra's
    // final distances are unique, and superseded entries are skipped on pop.
    dist_.assign(n, 0xFFFF);
    constexpr int kMaxStep = 17;   // widest edge: diagonal onto a non-road cell
    std::array<std::vector<int>, kMaxStep + 1> buckets;
    dist_[size_t(goal_)] = 0;
    buckets[0].push_back(goal_);
    size_t remaining = 1;
    static const int dcx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dcz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const int dcost[8] = {10, 10, 10, 10, 14, 14, 14, 14};
    // Non-road step penalty, matching findPath's 1.2x road preference.
    static const int dcostOff[8] = {12, 12, 12, 12, 17, 17, 17, 17};
    for (int cost = 0; remaining > 0 && cost < 0xFFFF; ++cost) {
        // Every entry in this slot has logical cost == `cost`: pushes made while
        // processing it land at cost+10/cost+14 (< cost+15, so never back here),
        // and the slot was cleared before the ring could wrap around to it.
        auto& b = buckets[size_t(cost % (kMaxStep + 1))];
        for (size_t bi = 0; bi < b.size(); ++bi) {
            int idx = b[bi];
            if (int(dist_[size_t(idx)]) != cost) continue;   // superseded entry
            int cx = idx % w_, cz = idx / w_;
            for (int k = 0; k < 8; ++k) {
                int nx = cx + dcx[k], nz = cz + dcz[k];
                if (!nav.fits(nx, nz, foot)) continue;
                // No diagonal corner-cutting past a cell the unit can't fit through.
                if (k >= 4 && (!nav.fits(cx + dcx[k], cz, foot) ||
                               !nav.fits(cx, cz + dcz[k], foot)))
                    continue;
                // Reverse-Dijkstra: this edge is travelled ni -> idx in real
                // movement, so the road test is on idx (the cell stepped ONTO).
                // Road-less grids stay on base costs: 12/17's diagonal ratio
                // differs slightly from 10/14's, so applying it everywhere
                // would perturb tie-breaks (and hashes) on every map.
                int nd = cost + (!nav.hasRoads() || nav.roadAt(cx, cz)
                                     ? dcost[k] : dcostOff[k]);
                size_t ni = size_t(nz) * w_ + nx;
                if (nd < dist_[ni] && nd < 0xFFFF) {
                    dist_[ni] = uint16_t(nd);
                    buckets[size_t(nd % (kMaxStep + 1))].push_back(int(ni));
                    ++remaining;
                }
            }
        }
        remaining -= b.size();
        b.clear();
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
    // Footprint clearance depends on the walkability grid. When it has already been
    // built, refresh just the affected rect (values are clamped at kClearMax, so a
    // cell change can influence clearance at most kClearMax cells down-left) instead
    // of dirtying the whole map -- a full rebuild is a w*h DP that every building
    // placement used to re-trigger on the next fits() query.
    if (!clearDirty_ && !clear_.empty())
        updateClearanceRect(cx, cz, w, h);
    else
        clearDirty_ = true;
}

// Stored clearance saturates here. fits() only ever asks about footprints <= 15
// cells (footCells/flowFor clamp), so every value >= 15 answers identically -- and
// the clamp is what bounds how far a walkability edit can propagate (see block()).
static constexpr uint16_t kClearMax = 15;

// Largest all-walkable square with each cell as its min (bottom-left) corner, by the
// classic DP from the far corner inward, saturated at kClearMax. Deterministic
// (pure function of cells_).
void NavGrid::rebuildClearance() const {
    clear_.assign(size_t(w_) * size_t(h_), 0);
    for (int z = h_ - 1; z >= 0; --z)
        for (int x = w_ - 1; x >= 0; --x) {
            if (!cells_[size_t(z) * w_ + x]) continue;   // blocked -> 0
            uint16_t r  = (x + 1 < w_)               ? clear_[size_t(z) * w_ + x + 1]     : 0;
            uint16_t u  = (z + 1 < h_)               ? clear_[size_t(z + 1) * w_ + x]     : 0;
            uint16_t ru = (x + 1 < w_ && z + 1 < h_) ? clear_[size_t(z + 1) * w_ + x + 1] : 0;
            clear_[size_t(z) * w_ + x] =
                std::min<uint16_t>(kClearMax, uint16_t(1 + std::min({r, u, ru})));
        }
    clearDirty_ = false;
}

// Recompute the clearance DP over just the cells a walkability edit in the given
// rect can influence: the rect itself plus kClearMax cells down-left (the DP reads
// up-right neighbours, and saturation stops the influence beyond that band). Same
// order and arithmetic as the full rebuild, so the result is bit-identical to it.
void NavGrid::updateClearanceRect(int cx, int cz, int w, int h) const {
    int x0 = std::max(0, cx - int(kClearMax));
    int z0 = std::max(0, cz - int(kClearMax));
    int x1 = std::min(w_ - 1, cx + w - 1);
    int z1 = std::min(h_ - 1, cz + h - 1);
    for (int z = z1; z >= z0; --z)
        for (int x = x1; x >= x0; --x) {
            uint16_t v = 0;
            if (cells_[size_t(z) * w_ + x]) {
                uint16_t r  = (x + 1 < w_)               ? clear_[size_t(z) * w_ + x + 1]     : 0;
                uint16_t u  = (z + 1 < h_)               ? clear_[size_t(z + 1) * w_ + x]     : 0;
                uint16_t ru = (x + 1 < w_ && z + 1 < h_) ? clear_[size_t(z + 1) * w_ + x + 1] : 0;
                v = std::min<uint16_t>(kClearMax, uint16_t(1 + std::min({r, u, ru})));
            }
            clear_[size_t(z) * w_ + x] = v;
        }
}

bool NavGrid::fits(int cx, int cz, int foot) const {
    if (foot <= 1) return walkable(cx, cz);
    if (clearDirty_) rebuildClearance();
    int off = foot / 2;
    int bx = cx - off, bz = cz - off;   // min corner of the foot x foot block
    if (bx < 0 || bz < 0 || bx + foot > w_ || bz + foot > h_) return false;
    return clear_[size_t(bz) * w_ + size_t(bx)] >= foot;
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

std::vector<Order> NavGrid::findPath(float wx0, float wz0, float wx1, float wz1, int foot) const {
    constexpr int kCell = 16;
    int sx = int(wx0) / kCell, sz = int(wz0) / kCell;
    int tx = int(wx1) / kCell, tz = int(wz1) / kCell;
    // Goal must fit the unit; the START only needs to be walkable (the unit is already
    // there -- it may be momentarily in a spot too tight for a fresh placement).
    if (!fits(tx, tz, foot) || !walkable(sx, sz)) return {};
    // Straight-shot shortcut for point-size units only; a footprint unit needs the
    // full A* since a clear centre-line can still clip a gap its body won't pass.
    if (foot <= 1 && lineClear(sx, sz, tx, tz)) return {{wx1, wz1, 0}};

    // A* over cells, octile heuristic. g/from live in generation-stamped scratch
    // members (see sim.h): a cell is initialized iff its stamp equals this call's
    // generation, so per-call setup is one counter bump instead of a ~1.1MB fill.
    struct Node { float f; int idx; };
    auto cmp = [](const Node& a, const Node& b) { return a.f > b.f; };
    std::vector<Node> open;
    const size_t n_ = size_t(w_) * h_;
    if (pathStamp_.size() != n_) {
        pathG_.assign(n_, 1e30f);
        pathFrom_.assign(n_, -1);
        pathStamp_.assign(n_, 0);
        pathGen_ = 0;
    }
    if (++pathGen_ == 0) { pathStamp_.assign(n_, 0); pathGen_ = 1; }   // u32 wrap
    auto gAt = [&](size_t i) -> float {
        return pathStamp_[i] == pathGen_ ? pathG_[i] : 1e30f;
    };
    auto touch = [&](size_t i, float gv, int fromIdx) {
        pathG_[i] = gv;
        pathFrom_[i] = fromIdx;
        pathStamp_[i] = pathGen_;
    };
    // Octile heuristic. With road preference active, off-road steps cost 1.2x
    // while a base-rate h underestimates by up to 20% -- which made A* explore
    // FAR more nodes (single 50ms searches on Two Castles). Weight h by the
    // off-road factor on road maps: paths may be up to 20% suboptimal (they
    // just prefer roads a little harder), but search cost returns to normal.
    const float hw = roads_ ? 1.2f : 1.0f;
    auto hcost = [&](int x, int z) {
        float ax = float(std::abs(x - tx)), az = float(std::abs(z - tz));
        return (std::max(ax, az) + 0.41421f * std::min(ax, az)) * hw;
    };
    int start = sz * w_ + sx, goal = tz * w_ + tx;
    touch(size_t(start), 0, -1);
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
        // Skip entries superseded by a later, cheaper push (their re-expansion is a
        // pure no-op that only burns the expansion cap): recompute this node's best
        // possible f from the CURRENT g -- the same float ops as at push time, so a
        // non-stale entry compares exactly equal, never greater.
        if (n.f > gAt(size_t(n.idx)) + hcost(cx, cz)) continue;
        for (int d = 0; d < 8; ++d) {
            int nx = cx + DX[d], nz = cz + DZ[d];
            if (!fits(nx, nz, foot)) continue;
            if (d >= 4 && (!fits(cx + DX[d], cz, foot) || !fits(cx, cz + DZ[d], foot)))
                continue;   // no diagonal corner cutting
            float step = d >= 4 ? 1.41421f : 1.0f;
            int ni = nz * w_ + nx;
            // Road preference: stepping onto a non-road cell costs 1.2x, so a
            // parallel road is worth up to ~20% of detour. Roads keep the base
            // cost, which leaves the octile heuristic admissible unchanged.
            if (roads_ && !roadAt(nx, nz)) step *= 1.2f;
            float ng = gAt(size_t(n.idx)) + step;
            if (ng < gAt(size_t(ni))) {
                touch(size_t(ni), ng, n.idx);
                open.push_back({ng + hcost(nx, nz), ni});
                std::push_heap(open.begin(), open.end(), cmp);
            }
        }
    }
    if (pathStamp_[size_t(goal)] != pathGen_ || pathFrom_[size_t(goal)] < 0) return {};

    std::vector<std::pair<int, int>> cells;
    for (int i = goal; i >= 0; i = pathFrom_[size_t(i)]) {
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

// Cache cap: evict the least-recently-used field(s) when full, rather than wiping
// all of them (clear-all rebuilt every live field EVERY tick once goals outnumbered
// the cap and stalled the sim to single-digit fps). Fields are ~3 bytes/cell, so a
// generous cap holds every live goal block; the linear LRU scan is microseconds
// against a multi-ms build.
static constexpr size_t kFlowCap = 512;

static void evictFlowLru(std::map<long long, FlowField>& cache) {
    while (cache.size() >= kFlowCap) {
        auto oldest = cache.begin();
        for (auto i = std::next(cache.begin()); i != cache.end(); ++i)
            if (i->second.used < oldest->second.used) oldest = i;
        cache.erase(oldest);
    }
}

bool World::flowKeyFor(const UnitType* type, float gx, float gz, FlowKey& out) const {
    const NavGrid& grid = navFor(type);
    if (grid.empty()) return false;
    int cx = std::clamp(int(gx) / 16, 0, grid.width() - 1);
    int cz = std::clamp(int(gz) / 16, 0, grid.height() - 1);
    int domain = type ? int(type->domain) : 0;
    // Footprint-class the field: a 4x4 unit and a 1x1 unit want different fields
    // (the big one can't cross the same 1-cell gaps), but all units of a size share.
    int foot = type ? std::clamp(std::max(type->footX, type->footZ), 1, 15) : 1;
    // Quantize the goal to a 2x2-cell block and build toward the block centre: the
    // field goes flat near the goal and movers home straight in on their order's
    // exact (x,z), so nearby goals can share one field. Without this, per-unit
    // scatter/chase goals each claimed their own 16px cell and blew the cache cap
    // -- measured ~300x slower ticks (full-map Dijkstras rebuilt every tick) once
    // live goals exceeded it. The memo stays a pure function of (domain, foot,
    // block), identical on every peer.
    int q = int(flowQuantShift_);      // block = 2^q cells (crowd-adaptive)
    int mask = ~((1 << q) - 1), half = 1 << (q - 1);
    int qcx = cx & mask, qcz = cz & mask;
    out.key = (domain * 16LL + foot) * 100000000LL +
              (long long)(qcz * grid.width() + qcx);
    out.grid = &grid;
    out.bx = float(qcx + half) * 16.0f;   // block centre
    out.bz = float(qcz + half) * 16.0f;
    out.foot = foot;
    return true;
}

const FlowField* World::flowFor(const UnitType* type, float gx, float gz) const {
    FlowKey k;
    if (!flowKeyFor(type, gx, gz, k)) return nullptr;
    auto it = flowCache_.find(k.key);
    if (it != flowCache_.end()) {
        it->second.used = tickCounter_;
        return it->second.ready() ? &it->second : nullptr;
    }
    evictFlowLru(flowCache_);
    FlowField ff;
    if (g_phase) { auto _b0=std::chrono::steady_clock::now(); ff.build(*k.grid, k.bx, k.bz, k.foot);
        g_flowMs += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-_b0).count(); ++g_flowN; }
    else ff.build(*k.grid, k.bx, k.bz, k.foot);
    ff.used = tickCounter_;
    auto& stored = flowCache_[k.key];
    stored = std::move(ff);
    return stored.ready() ? &stored : nullptr;
}

// Batch-build the flow fields this tick's movers/chasers are about to request and
// that miss the cache, in PARALLEL, before the mover loop runs. The trigger case: a
// building placement invalidates K fields, and next tick K movers re-request them --
// serially that was K full-map Dijkstras in one tick. Lockstep-safe because a field
// is a pure function of (grid, goal block, foot): workers write only their own
// FlowField, the grids' lazy clearance is forced up to date on this thread first so
// workers only read, the join happens before any consumer runs, and cache insertion
// is serial in unit-index (miss-discovery) order. Fields that fail to build are
// cached too (same not-ready semantics as flowFor). A single miss just builds
// synchronously here -- no thread is worth one build.
void World::prefetchFlows() {
    std::vector<FlowKey> misses;
    auto want = [&](const UnitType* t, float gx, float gz) {
        FlowKey k;
        if (!flowKeyFor(t, gx, gz, k)) return;
        if (flowCache_.count(k.key)) return;
        for (const auto& m : misses)
            if (m.key == k.key) return;
        misses.push_back(k);
    };
    for (const auto& u : units_) {
        if (!u.alive() || !u.type || u.type->canFly || u.orders.empty()) continue;
        const Order& o = u.orders.front();
        if (o.flow && o.targetId == 0) want(u.type, o.x, o.z);
        // Chasers periodically ask for their target's field (reachability gate).
        else if (o.targetId > 0)
            if (const Unit* t = unit(o.targetId); t && t->alive())
                want(u.type, t->x, t->z);
    }
    if (misses.size() < 2) return;   // 0/1: the inline flowFor path handles it
    auto _b0 = std::chrono::steady_clock::now();
    for (const auto& m : misses) m.grid->ensureClearance();   // workers must only read
    std::vector<FlowField> built(misses.size());
    // BOUNDED parallelism: a fixed pool of workers (<= hardware threads) pulls builds
    // off a shared atomic cursor. The old code spawned ONE std::thread PER miss -- a
    // 5000-unit, 8-AI battle can miss ~450 fields in a tick, so it spawned ~450 threads
    // EVERY tick and the creation/oversubscription cost dwarfed the (cheap) Dijkstras.
    // Each field is an independent pure function, so the result is identical regardless
    // of how the work is split; insertion below stays serial in miss order.
    unsigned hw = serialFlow_ ? 1u : std::thread::hardware_concurrency();
    unsigned nth = std::min<unsigned>(hw ? hw : 1u, unsigned(misses.size()));
    std::atomic<size_t> cursor{0};
    auto worker = [&] {
        for (size_t i = cursor.fetch_add(1, std::memory_order_relaxed); i < misses.size();
             i = cursor.fetch_add(1, std::memory_order_relaxed))
            built[i].build(*misses[i].grid, misses[i].bx, misses[i].bz, misses[i].foot);
    };
    std::vector<std::thread> th;
    th.reserve(nth - 1);
    for (unsigned k = 1; k < nth; ++k) th.emplace_back(worker);
    worker();                          // the calling thread participates
    for (auto& t : th) t.join();
    for (size_t i = 0; i < misses.size(); ++i) {
        evictFlowLru(flowCache_);
        built[i].used = tickCounter_;
        flowCache_[misses[i].key] = std::move(built[i]);
    }
    if (g_phase) {
        g_flowMs += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - _b0).count();
        g_flowN += int(misses.size());
    }
}

void World::invalidateFlows(int cx, int cz, int w, int h) {
    for (auto it = flowCache_.begin(); it != flowCache_.end();) {
        long long hi = it->first / 100000000LL;
        int domain = int(hi / 16), foot = int(hi % 16);
        // Only nav_ (Ground) ever changes after setup; other domains' fields hold.
        if (domain != int(UnitType::Domain::Ground)) { ++it; continue; }
        // A failed build (no fitting goal) may succeed after an unblock: retry it.
        if (!it->second.ready()) { it = flowCache_.erase(it); continue; }
        // Pad by the footprint reach (+1 for the freshly-connectable frontier): a
        // walkability change can only alter fits()/adjacency within that band. If no
        // reachable cell of the field lies inside, neither blocking (nothing routed
        // there) nor unblocking (still sealed off) can change its dist_/dir_ -- the
        // kept field is bit-identical to a fresh build, so the memo stays pure and
        // every peer that holds this entry makes the same keep/evict call.
        int pad = foot + 1;
        if (it->second.touchesReachable(cx - pad, cz - pad,
                                        cx + w - 1 + pad, cz + h - 1 + pad))
            it = flowCache_.erase(it);
        else
            ++it;
    }
}

// A unit's footprint size in cells (square approximation) for footprint-aware nav.
static int footCells(const UnitType* t) {
    return t ? std::clamp(std::max(t->footX, t->footZ), 1, 15) : 1;
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
        auto path = grid.findPath(u->x, u->z, x, z, footCells(u->type));
        if (!path.empty()) {
            for (const auto& o : path) u->orders.push_back(o);
            return;
        }
    }
    u->orders.push_back({x, z, 0});
}

bool World::pathExists(const UnitType* type, float gx, float gz, float fx, float fz) const {
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
        auto path = grid.findPath(t->x, t->z, x, z, footCells(t->type));
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
            u.orders.erase(u.orders.begin());
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
    if (u.cargo.empty()) u.orders.erase(u.orders.begin());
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

void World::patrolTo(int unitId, float x, float z, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canMove) return;
    size_t before = queue ? u->orders.size() : 0;
    order(unitId, x, z, queue);
    // Mark every waypoint of this move as a looping, engage-en-route patrol leg; a
    // chain of patrolTo calls then cycles the unit through all of them.
    for (size_t i = before; i < u->orders.size(); ++i) {
        u->orders[i].patrol = true;
        u->orders[i].attackMove = true;
    }
}

void World::orderWait(int unitId, float seconds, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type) return;
    if (!queue) u->orders.clear();
    Order o;
    o.x = u->x;
    o.z = u->z;
    o.wait = seconds > 0 ? seconds : 0.0f;
    u->orders.push_back(o);
}

void World::orderWaitAttack(int unitId, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type) return;
    if (!queue) u->orders.clear();
    Order o;
    o.x = u->x;
    o.z = u->z;
    o.waitAttack = true;
    u->orders.push_back(o);
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
    // Self-destruct: TOGGLE a 5s countdown. Arming a live unit starts the timer;
    // pressing again while it counts down cancels it. Expiry (in tick) drops hp
    // to zero with an explosion death; kill credit is skipped.
    Unit* u = unit(unitId);
    if (!u || !u->alive()) return;
    u->selfDestructT = (u->selfDestructT < 0.0f) ? 5.0f : -1.0f;
}

void World::setWeapon(int unitId, int slot) {
    Unit* u = unit(unitId);
    if (!u || !u->type) return;
    int n = int(u->type->weapons.size());
    if (n > 0) {
        u->weaponSlot = std::clamp(slot, 0, n - 1);
        u->weaponAuto = false;   // the player has taken the choice over
    }
}

void World::setStance(int unitId, int stance) {
    Unit* u = unit(unitId);
    if (!u || !u->alive()) return;
    u->stance = std::clamp(stance, 0, 2);
}

void World::setCloak(int unitId, bool on) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canCloak) return;
    u->cloakOn = on;
}

void World::setActive(int unitId, bool on) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->onOffable) return;
    u->active = on;
}

void World::setSquad(int unitId, int squad) {
    Unit* u = unit(unitId);
    if (!u || !u->alive()) return;
    u->squad = int8_t(std::clamp(squad, -10, 10));   // 0 none, +N group N, -N formation N
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
    {
        HitFx hf{hx, hz, &w, primary ? primary->type : nullptr};
        if (primary) { hf.victimId = primary->id; hf.damage = w.damageVs(primary->type); }
        const Unit* from = fromId ? unit(fromId) : nullptr;
        hf.fromX = from ? from->x : hx;
        hf.fromZ = from ? from->z : hz;
        hits_.push_back(hf);
    }
    // Attacker's veteran attack multiplier boosts damage dealt (retail scales the
    // attacker's attack stat by vetMul); the victim's boosts armour (below).
    const Unit* attacker = fromId ? unit(fromId) : nullptr;
    // Attack multiplier = veterancy × live aura buff (AdjustAttack).
    float atkMul = attacker ? attacker->vetMul() * attacker->atkBuff : 1.0f;
    // Damage + status one victim, honouring veterancy, auras and immunities.
    auto hurt = [&](Unit& e, float scale) {
        if (e.stonedFor > 0) return;   // petrified units are impervious
        // subtype=mindcontrol (the Taros Mind Mage's Individual + Area Mind
        // Control): the shot CONVERTS the victim instead of hurting it. Retail
        // encodes eligibility in the weapon's own [DAMAGE] table -- it zeroes
        // monarch/god/dragon/fort/factory/naval/lodestone -- so "damage > 0
        // against this category" is exactly "can be charmed", which damageVs()
        // already computes. Conversion is permanent, like contact capture.
        if (w.mindControl) {
            // Retail's charm roll (icd 0x52da10), gates in this exact order so the
            // RNG stream can never diverge between peers. Note the [DAMAGE] table is
            // the ACQUISITION filter -- it decides what you may TARGET -- so a
            // Monarch caught inside an area charm is stopped by the commander gate
            // here, not by its zeroed damage row.
            if (e.embarked()) return;                 // safe inside a transport
            if (e.type->commander) return;            // Monarchs are never charmed
            if (e.type->cantBeCaptured) return;
            if (e.hp <= 0) return;
            if (atUnitCap(fromPlayer)) return;        // no room on the new side
            // Chance rises with the victim's veterancy -- a green unit is ~80%, a
            // 10-star veteran is capped at 99%. Scaled by the area falloff so the
            // rim of an Area Mind Control is less reliable than its centre.
            int chance = std::min(99, (int(e.veteran) + 16) * 5);
            if (burnRand(100) >= int(float(chance) * std::clamp(scale, 0.0f, 1.0f)))
                return;                               // it shrugged the spell off
            captureUnit(e, fromPlayer);
            return;
        }
        if (benchmarkMode() && e.type && e.type->commander) return;   // benchmark: Monarchs are invincible
        // base × attacker-attack (↑) ÷ victim-armour (↓); armour = veterancy × aura.
        float armour = std::max(e.vetMul() * e.armBuff, 0.01f);
        float dealt = w.damageVs(e.type) * atkMul / armour * scale;
        e.hp -= dealt;
        if (e.hp <= 0) {
            e.overkill = std::max(e.overkill, -e.hp);          // retail severity input
            e.deathType = uint8_t(w.dmgType);                  // 3 = explosion -> gib
        }
        if (fromId) e.lastHitBy = fromId;
        if (w.status != Weapon::Status::None && e.type) {
            bool immune =
                (w.status == Weapon::Status::Frozen && e.type->cantBeFrozen) ||
                (w.status == Weapon::Status::Stoned && e.type->cantBeStoned);
            if (!immune) {
                if (w.status == Weapon::Status::Paralyzed) {
                    e.paralyzedFor = std::max(e.paralyzedFor, w.statusDur);
                    e.speed = 0;
                } else {
                    // Retail petrify/freeze (icd 0x51a61a): HP zeroes INSTANTLY
                    // regardless of amount -- the victim dies on the spot and
                    // the death edge places its stone=/frozen= STATUE (blocking,
                    // permanent, resurrectable back to life). The old temporary
                    // stoned/frozen debuff was our pre-RE guess.
                    bool freeze = w.status == Weapon::Status::Frozen;
                    if (freeze) e.frozenFor = 1.0f; else e.stonedFor = 1.0f;
                    e.hp = 0;
                    e.deathType = freeze ? 15 : 14;
                    e.speed = 0;
                    static const bool kStatLog = std::getenv("TAK_BURNLOG") != nullptr;
                    if (kStatLog)
                        std::fprintf(stderr, "statue kill: %s %s at %.0f,%.0f\n",
                                     e.type->id.c_str(), freeze ? "frozen" : "stoned",
                                     e.x, e.z);
                }
            }
        }
    };
    if (primary) hurt(*primary, 1.0f);
    // Features under fire (retail icd 0x529dc0 -> DamageFeature 0x4961a0):
    // every feature head cell within aoe/2 of the impact -- the impact cell
    // exempt from the radius check, so a no-splash weapon (Fire Breath, aoe 0)
    // still strikes the cell it hits. A firestarter hit on a flamable feature
    // IGNITES it instead of damaging it that hit; everything else accumulates
    // weapon damage, and at the def's `damage` value the feature is destroyed
    // -- swapped to its `featuredead` stage (npcwreck chains) or removed.
    // Deterministic in every peer's sim (state + RNG hashed). Must run BEFORE
    // the aoe<=0 early-out below.
    // unitsonly=1: the effect touches UNITS only, never the scenery. Retail sets it
    // on the big area spells (Earthen/Wind Wave, Death Aura, Energy Blast, both
    // Mind Controls) precisely so a 500px nuke doesn't also level every forest and
    // mana-bearing prop inside it.
    if (!featTypes_.empty() && terW_ > 0 && !w.unitsOnly) {
        float r = std::max(w.aoe * 0.5f, 8.0f);
        int icx = int(hx) / 16, icz = int(hz) / 16;
        int rc = int(r) / 16 + 1;
        for (int dz = -rc; dz <= rc; ++dz)
            for (int dx = -rc; dx <= rc; ++dx) {
                int cx = icx + dx, cz = icz + dz;
                if (cx < 0 || cz < 0 || cx >= terW_ || cz >= terH_) continue;
                auto it = featureIdx_.find(cz * terW_ + cx);
                if (it == featureIdx_.end()) continue;
                Feature& f = features_[it->second];
                if (!f.alive || f.type < 0) continue;
                if (!(dx == 0 && dz == 0)) {   // impact cell is struck regardless
                    float fdx = f.x - hx, fdz = f.z - hz;
                    if (fdx * fdx + fdz * fdz > r * r) continue;
                }
                const FeatType& ft = featTypes_[size_t(f.type)];
                if (ft.indestructible) continue;
                if (w.fireStarter && ft.flamable && !f.burn && ft.hasBurnAnim) {
                    igniteFeature(f);
                    continue;   // ignition replaces the damage this hit
                }
                if (ft.hp <= 0) continue;   // no damage value: indestructible-by-damage
                f.dmg += w.damage;
                if (f.dmg >= ft.hp) swapFeature(f, ft.deadType);
            }
    }
    if (w.aoe <= 0) return;
    // Splash the surrounding enemies, scaled from full at the centre to `edge`
    // at the rim (so an area weapon actually hits a crowd, per its FBI aoe).
    // Retail's splash radius is areaofeffect/2, and the falloff is QUADRATIC:
    //   frac = (1 - d/r)^2 * (1 - edge) + edge
    // We used the full aoe as the radius with a linear ramp, which made every
    // splash weapon in the game twice as wide as retail and far too strong at the
    // rim -- and it disagreed with our own FEATURE pass below, which already halved
    // it. Both halves of applyHit now use the same radius.
    const float r = w.aoe * 0.5f;
    int splashed = 0;
    forEachNear(hx, hz, r, [&](int idx) {
        Unit& e = units_[size_t(idx)];
        if (!e.alive() || e.embarked() || !e.type || allied(e.player, fromPlayer)) return;
        if (&e == primary) return;   // already took the direct hit
        float dx = e.x - hx, dz = e.z - hz;
        float d = std::sqrt(dx * dx + dz * dz);
        if (d >= r) return;
        float t = 1.0f - d / r;
        float scale = t * t * (1.0f - w.edge) + w.edge;
        hurt(e, scale);
        ++splashed;
    });
    static const bool kLog = std::getenv("TAK_SPLASHLOG") != nullptr;
    if (splashed && kLog)
        std::fprintf(stderr, "splash %s aoe=%.0f hit %d extra\n",
                     w.name.c_str(), r, splashed);
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
    u.justFired = true;
    // Remote Effect: nothing travels. The spell materialises at the AIMED GROUND
    // POINT and lands after builduptime -- so walking aside doesn't dodge an
    // Earthquake, you have to leave its (up to 500px) radius. decaytime is the
    // visual fade after the hit.
    if (w.kind == Weapon::Kind::Remote) {
        PendingEffect e;
        e.w = &w;
        e.x = target.x; e.z = target.z;
        e.player = u.player; e.fromId = u.id;
        switch (w.remote) {
            case Weapon::RemoteKind::Earthquake:
                // Pulses every `shakeduration` from impact, right through buildup
                // AND decay -- for the drake's Earthquake the single pulse actually
                // lands inside the decay window.
                e.period = std::max(w.shakeDur, 0.1f);
                e.at = e.period;
                e.endAt = w.buildUp + w.decay;
                break;
            case Weapon::RemoteKind::Hailstorm: {
                // The rain: buildup/decay don't apply. It starts falling shortly
                // after the cast and pulses particlespersecond times a second for
                // `duration` -- so the Acolyte's Hail Shower is ~12 small hits, not
                // one 70-damage tap.
                float pps = w.particlesPerSec > 0 ? w.particlesPerSec : 5.0f;
                e.period = 1.0f / pps;
                e.at = 20.0f / 30.0f;                     // retail's ~20-tick lead-in
                e.endAt = 20.0f / 30.0f + std::max(w.duration, e.period);
                break;
            }
            case Weapon::RemoteKind::MindCtl:
            case Weapon::RemoteKind::Freeze:
                // One sweep when the channel completes -- and killing the caster
                // mid-channel ABORTS it, which is real counterplay against a Mind
                // Mage winding up an area charm.
                e.at = w.buildUp;
                e.endAt = w.buildUp + w.decay;
                e.casterGated = true;
                break;
            case Weapon::RemoteKind::Plain:
            default:
                e.at = w.buildUp;
                e.endAt = w.buildUp + w.decay;
                break;
        }
        pendingEffects_.push_back(e);
        return;
    }
    // Wandering: spawn a roaming storm at the aim point. It drifts for `duration`,
    // grinding whatever it passes over, instead of landing one tap.
    if (w.kind == Weapon::Kind::Wandering) {
        // The storm is born just IN FRONT OF THE CASTER facing the aim point -- it
        // is not delivered to the target. It then drifts along that launch heading
        // for good (a storm never re-aims), weaving as it goes, so the caster is
        // aiming a slow moving hazard rather than placing one.
        Storm s;
        float dx = target.x - u.x, dz = target.z - u.z;
        float dl = std::max(detmath::len(dx, dz), 1e-3f);
        s.dirX = dx / dl; s.dirZ = dz / dl;
        s.w = &w;
        s.x = u.x + s.dirX * 32.0f;
        s.z = u.z + s.dirZ * 32.0f;
        s.player = u.player; s.fromId = u.id;
        s.arm = w.buildUp;            // wind-up: visible and moving, but harmless
        s.left = w.duration > 0 ? w.duration : 6.0f;
        s.nextVary = 0.0f;            // roll the first wander offset immediately
        storms_.push_back(s);
        return;
    }
    if (w.melee || w.beam || w.projVel <= 0) {
        // Instant hit: a melee swing, a hitscan bolt, or a Line-of-Sight beam (the
        // drake's Fire Breath -- a sustained flame emission, not a lobbed shot). The
        // damage lands now along the sightline; the flame stream itself is a
        // client-side visual driven by emitTime, so no traveling projectile spawns.
        applyHit(w, target.x, target.z, u.player, u.id, &target);
        return;
    }
    // Dropped: the bomb is RELEASED, not fired. It leaves the bomber with only the
    // bomber's own forward drift and falls onto the ground beneath -- so it lands
    // where the bomber IS, and a bomber has to overfly its target (see the release
    // gate in tickCombat). Detonates on landing wherever it ended up, hit or miss.
    if (w.kind == Weapon::Kind::Dropped) {
        Projectile b;
        b.x = u.x;
        b.z = u.z;
        float dv = w.projVel * kTick / 30.0f;   // a slow release speed, not a shot
        b.vx = detmath::sin(u.heading) * dv;
        b.vz = detmath::cos(u.heading) * dv;
        b.damage = w.damage;
        b.wsrc = &w;
        b.targetId = target.id;
        b.fromPlayer = u.player;
        b.fromId = u.id;
        b.fx = w.fx;
        b.life = kBombFall;      // time to fall from cruise altitude
        b.flight = kBombFall;    // the viewer arcs it down over the same window
        projectiles_.push_back(b);
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
    // Fuel. A dumb shot only needs to reach where it was aimed. A GUIDED one is
    // fuelled from the weapon's RANGE instead (retail fixes its expiry tick at
    // launch as one full range of travel), so a homer that has to curve after a
    // fleeing target doesn't run dry halfway -- fuelling it from the launch
    // distance would make the chase it exists for fizzle. Out of fuel is a dud
    // either way: no damage, no splash.
    p.life = (w.kind == Weapon::Kind::Guided ? std::max(w.range, dist) : dist) / vel + 0.5f;
    p.flight = dist / vel;
    projectiles_.push_back(p);
}

// Retail melee ignores its TDF `range` entirely: MeleeWeapon::inRange (icd
// @0x52b980) passes when the two units' footprint boxes are within half a cell
// (8px) of TOUCHING on both axes -- edge adjacency, not a radial distance.
// (Ranged weapons use the base test, icd @0x530580: 2D centre-to-centre,
// inclusive.) Treating melee `range` as radial left Zhon beasts (range 250)
// halting 15 cells out and biting air, and the Sailor (range 10) unable to ever
// reach a legal firing distance past the 13px separation floor.
static bool meleeInRange(const UnitType* a, const UnitType* b, float dx, float dz) {
    return std::abs(dx) < 8.0f * float(a->footX + b->footX) + 8.0f &&
           std::abs(dz) < 8.0f * float(a->footZ + b->footZ) + 8.0f;
}

void World::tickCombat(Unit& u, float dt) {
    for (auto& r : u.reloads)
        if (r > 0) r -= dt;
    if (!u.active) return;   // onoffable unit powered down: no acquisition/fire

    // Auto-acquire: idle armed units engage the nearest enemy in reach;
    // attack-movers and patrollers interrupt their route to fight. A PASSIVE unit
    // (stance 2, hold fire) never auto-acquires -- it only fights when ordered.
    // A builder mid-job -- constructing a site, repairing, reclaiming, holding
    // queued builds, or producing from a queue -- stays on task: it does NOT
    // auto-acquire a nearby enemy and wander off to fight while it should build.
    bool busyBuilding = u.buildSiteId != 0 || u.repairId != 0 || u.reclaimId != 0 ||
                        !u.buildOrders.empty() ||
                        (u.type->canMove && !u.buildQueue.empty());   // mobile conjurer producing
    bool acquiring = !busyBuilding && u.stance != 2 &&
                     (u.orders.empty() ||
                      (u.orders.front().targetId == 0 &&
                       (u.orders.front().attackMove || u.orders.front().patrol)) ||
                      u.orders.front().guard);
    // Can any of this unit's weapons deal real damage to `e`? (noairweapon gates
    // flyers.) Requiring damage > 0 stops an army from piling onto -- and firing
    // forever at -- a building/target its weapons cannot scratch (a per-category
    // damage.<cat>=0 override), which never dies so the attack order never clears.
    auto canDamage = [&](const Unit& e) {
        for (const auto& wp : u.type->weapons)
            if (!(e.type->canFly && wp.noAir) && wp.damageVs(e.type) > 0.0f) return true;
        return false;
    };
    auto canTarget = [&](const Unit& e) {
        if (e.cloaked) return false;   // cloaked units are invisible to auto-acquire
        return canDamage(e);
    };
    // Auto-acquisition is staggered across ticks by unit id: an idle armed unit
    // rescans for a target every kAcqStride ticks (~0.13s at 30Hz), not every
    // tick. That turns the dense-crowd O(n^2) neighbour scan into O(n^2/stride)
    // -- the dominant sim cost when thousands of idle armed units pile together.
    // Deterministic (id+tick, identical on every lockstep peer), so no desync. The
    // stride widens with the crowd (acqStride_, set at tick start) so massive battles
    // rescan less often -- the acquisition cost scales sub-linearly instead of O(n).
    bool acqTurn = (uint32_t(u.id) + tickCounter_) % acqStride_ == 0;
    if (acquiring && u.type->weapon.damage > 0 && acqTurn) {
        float ar = u.type->maxRange() + 90;
        int best = 0;
        float bestD = ar * ar;
        // maneuverleashlength limits how far an IDLE defender will chase from its
        // post. It must NOT apply while attack-moving/patrolling — those orders
        // mean "advance and engage everything en route", so an army that has
        // travelled far from its spawn still acquires (incl. just-conjured foes).
        // An OFFENSIVE unit (stance 0) ignores its leash entirely and chases freely.
        float leash2 = (u.stance != 0 && u.orders.empty() && u.type->leash > 0)
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
        if (best) u.orders.insert(u.orders.begin(), {0, 0, best});
    }
    if (u.orders.empty() || u.orders.front().targetId == 0) return;

    if (u.orders.front().guard) {
        Order& o = u.orders.front();
        Unit* t = unit(o.targetId);
        if (!t || !t->alive()) {
            u.orders.erase(u.orders.begin());
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
        u.orders.erase(u.orders.begin());
        return;
    }
    // Give up a target this unit can neither damage NOR capture, instead of
    // firing at it forever -- it will never die or convert. Covers a converter
    // (0-damage charm weapon) ordered onto a cantBeCaptured building, and any
    // unit whose weapons all do 0 to the target's category. Capturable targets
    // are exempt: a charmer keeps contact until the capture branch converts it.
    bool canConvert = u.type->canCapture && target->type &&
                      !target->type->cantBeCaptured && !allied(u.player, target->player);
    if (target->type && !canDamage(*target) && !canConvert) {
        u.orders.erase(u.orders.begin());
        return;
    }
    float dx = target->x - u.x, dz = target->z - u.z;
    float dist = std::sqrt(dx * dx + dz * dz);
    int slot = u.type->weapons.empty()
                   ? 0 : std::clamp(u.weaponSlot, 0, int(u.type->weapons.size()) - 1);
    // Automatic weapon selection. Retail scores every weapon it could use against
    // this target as roughly (distance^2 + noise) / damageVs(target) and takes the
    // cheapest -- skipping any weapon that does NO damage to the target's category
    // -- which in practice means "the hardest-hitting weapon you can actually use
    // right now" (icd 0x4129b6, the fire-at-will scan). We had no selection at all:
    // a multi-weapon unit always fired slot 0, so Elsin spent whole battles casting
    // Lightning while his Meteor and Earthen Wave -- and his entire mana pool --
    // went untouched. 34 shipped units carry more than one weapon.
    // The player's own pick (Ctrl+W) switches weaponAuto off and is then obeyed.
    if (u.weaponAuto && u.type->weapons.size() > 1) {
        float bestScore = -1.0f;
        for (size_t i = 0; i < u.type->weapons.size(); ++i) {
            const Weapon& c = u.type->weapons[i];
            if (c.damageVs(target->type) <= 0.0f) continue;            // can't hurt it
            if (c.noAir && target->type && target->type->canFly) continue;
            if (dist > c.range || dist < c.minRange) continue;          // out of its band
            if (c.manaCost > 0 && u.type->maxMana > 0 && u.mana < c.manaCost) continue;
            if (u.reloads[i] > 0) continue;                             // still reloading
            float score = c.damageVs(target->type);
            if (score > bestScore) { bestScore = score; slot = int(i); }
        }
        // Nothing usable from here yet (still closing, or everything is reloading):
        // approach on the LONGEST-ranged weapon that could work, so a caster whose
        // reach lives in a later slot -- Elsin's Meteor outranges his Lightning --
        // walks to the right distance instead of closing past it.
        if (bestScore < 0) {
            float far = -1.0f;
            for (size_t i = 0; i < u.type->weapons.size(); ++i) {
                const Weapon& c = u.type->weapons[i];
                if (c.damageVs(target->type) <= 0.0f) continue;
                if (c.noAir && target->type && target->type->canFly) continue;
                if (c.range > far) { far = c.range; slot = int(i); }
            }
        }
    }
    const Weapon* sel = slot < int(u.type->weapons.size()) ? &u.type->weapons[slot] : nullptr;
    float best = sel ? sel->range : u.type->maxRange();
    // A bomber does not shoot from range -- it flies OVER and lets go, because the
    // bomb lands beneath the release point. Its effective reach is therefore how
    // close it must be for the blast to still cover the target, which makes it
    // close to overhead and turns its FBI `range` into an approach cue.
    if (sel && sel->kind == Weapon::Kind::Dropped)
        best = std::max(sel->aoe * 0.5f, 48.0f);
    // Range is to the target's footprint EDGE, not its centre. A building's centre is
    // deep inside a blocked footprint, so a centre-distance check leaves a short-range
    // attacker grinding the edge (never "in range") or a flyer buried inside it. For a
    // STRUCTURE target, pad the effective range by its footprint half-extent plus a
    // small margin for where nav actually halts at the edge (mirrors the builder reach
    // in tickConstruction). Mobile targets keep centre-distance, so unit-vs-unit combat
    // is unchanged.
    float pad = target->type->maxVel <= 0.0f
                    ? 8.0f * float(std::max(target->type->footX, target->type->footZ)) + 24.0f
                    : 0.0f;
    float reach = best + pad;
    // Melee: retail closes to footprint adjacency (see meleeInRange above);
    // `range` and LoS play no part.
    bool adj = sel && sel->melee && target->type &&
               meleeInRange(u.type, target->type, dx, dz);
    // Ranged units need a clear line to shoot; a wall between them means close
    // in / reposition rather than firing through it (melee & flyers are exempt).
    bool needLoS = best > 64.0f && !u.type->canFly &&
                   target->type && !target->type->canFly;
    // LoS gates ONLY in-range firing and in-range repositioning; an out-of-range
    // chaser advances regardless (the `dist > best*0.95` move test below
    // short-circuits before `los` is read, and the fire gate is never reached out of
    // range). So defer the raycast until we're actually close enough for it to
    // matter -- identical result, but a marching army (the bulk of a big battle, all
    // out of range) stops paying for a per-tick line-of-sight cast it never uses.
    bool los = true;
    if (needLoS && dist <= reach * 0.95f)
        los = nav_.losBetween(u.x, u.z, target->x, target->z,
                              std::max(u.type->footX, u.type->footZ) / 2,
                              std::max(target->type->footX, target->type->footZ) / 2);
    if (!u.type->canMove && dist > reach) {     // static units can't chase
        u.orders.erase(u.orders.begin());
        return;
    }
    if ((sel && sel->melee) ? !adj
                            : (dist > reach * 0.95f || (!los && u.type->canMove))) {
        // Advance toward the target, steering around impassable terrain.
        u.repathLeft -= dt;
        Order& o = u.orders.front();
        o.x = target->x;
        o.z = target->z;
        const NavGrid& grid = navFor(u.type);
        if (!u.type->canFly && !grid.empty() && u.repathLeft <= 0) {
            u.repathLeft = 0.7f;
            // Same guards as the movement repath sites: charge the shared per-tick
            // pathBudget_ (a chasing crowd otherwise aligns dozens of full A*s in
            // one tick -- this was the last unguarded full-grid pathfinder), and
            // give up early on a flow-unreachable target (an unreachable goal makes
            // A* flood the unit's ENTIRE reachable region every 0.7s). Both checks
            // are deterministic: fixed budget in unit-index order, and the flow
            // memo is a pure function shared by every peer.
            const FlowField* ff = flowFor(u.type, target->x, target->z);
            bool hopeless = ff && grid.walkable(int(u.x) / 16, int(u.z) / 16) &&
                            !ff->reachable(u.x, u.z);
            if (!hopeless && pathBudget_ > 0) {
                --pathBudget_;
                auto _p0 = std::chrono::steady_clock::now();
                auto path = grid.findPath(u.x, u.z, target->x, target->z, footCells(u.type));
                if (g_phase) { g_pathMs += std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - _p0).count(); ++g_pathN; }
                if (!path.empty()) {
                    o.x = path.front().x;
                    o.z = path.front().z;
                }
            }   // over budget or hopeless: keep steering at the target directly
        }
        return;   // movement handled by the normal move logic
    }
    // In range: stop and face the target.
    u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
    float want = detmath::atan2(dx, dz);
    float diff = angleDiff(want, u.heading);
    float maxTurn = u.type->turnRate * dt;
    u.heading += std::clamp(diff, -maxTurn, maxTurn);
    // Fire the selected weapon when the target is in its [minrange, range] band,
    // within aimtolerance, has a clear shot, and (unless noairweapon) may hit air.
    if (sel && !(sel->noAir && target->type && target->type->canFly) &&
        (sel->melee || los) && u.reloads[slot] <= 0 &&
        (sel->melee ? adj : dist <= (sel->kind == Weapon::Kind::Dropped ? best : sel->range) + pad) &&
        dist >= sel->minRange &&
        // A bomb is let go, not aimed: a bomber releases once it is over the target
        // rather than having to line its nose up first (which a hovering flyer with
        // momentum can rarely hold inside a 5-degree window anyway).
        (sel->kind == Weapon::Kind::Dropped ||
         std::abs(diff) < std::max(sel->aimTol, 0.03f)))
        fire(u, *target, slot);
    // cancapture: a charmer converts the target after sustained contact (~3s) or
    // once it is worn down, rather than killing it.
    if (u.type->canCapture && target->type && !target->type->cantBeCaptured &&
        !allied(u.player, target->player)) {
        u.captureProg += dt;
        if (u.captureProg > 3.0f || target->hp < target->type->maxHp * 0.25f) {
            captureUnit(*target, u.player);
            u.captureProg = 0;
            u.orders.erase(u.orders.begin());   // done with this one
        }
    }
}

// Switch a unit's allegiance: contact charm (cancapture) and mind-control weapons
// both land here, so a converted unit behaves identically either way -- it drops
// its old orders, comes up at half health if it was nearly dead, and forgets who
// last hit it (so the new owner isn't credited a kill on its own unit).
void World::captureUnit(Unit& t, int newPlayer) {
    if (!t.type || t.player == newPlayer) return;
    // Move the unit between the two owners' live counts NOW, the way spawn() does.
    // The counts only re-sync from a full walk at end of tick, so without this an
    // area mind control -- which converts a whole blob inside ONE applyHit -- reads
    // the same stale count for every victim and can carry its new owner far past
    // the lobby unit cap.
    if (t.alive()) {
        if (t.player >= 0 && t.player < int(players_.size()) &&
            players_[size_t(t.player)].unitCount > 0)
            players_[size_t(t.player)].unitCount--;
        if (newPlayer >= 0 && newPlayer < int(players_.size()))
            players_[size_t(newPlayer)].unitCount++;
    }
    t.player = newPlayer;
    t.orders.clear();
    t.hp = std::max(t.hp, t.type->maxHp * 0.5f);
    t.lastHitBy = 0;
    t.squad = 0;             // no longer in its old owner's control group
    t.buildQueue.clear();    // and not still producing for them
    t.buildProgress = 0;
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
    // Water structures (Veruna's Sea Fort / Floating Tower) encode a shoreline
    // footprint in their yardmap: 'w' cells are the slipway that MUST sit over water
    // (where the ships launch), solid cells ('o'/'c'/'C') the land-side base, '.' is
    // off-footprint. Honouring that stops a naval building from being placed on dry
    // land (all its 'w' cells would fail the water test) AND lets it sit correctly on
    // a coast (which the old whole-footprint-on-land check wrongly rejected). Only
    // yardmaps that actually contain 'w' take this path, so every land building keeps
    // its exact previous placement rule (and hash).
    bool waterYard = false;
    if (!type->yardMap.empty())
        for (char c : type->yardMap) if (c == 'w' || c == 'W') { waterYard = true; break; }
    if (waterYard) {
        for (int j = 0; j < type->footZ; ++j)
            for (int i = 0; i < type->footX; ++i) {
                char c = type->yardMap[size_t(j) * type->footX + i];
                if (c == 'w' || c == 'W') {
                    if (!navWater_.walkable(cx + i, cz + j)) return false;   // slipway needs water
                } else if (c == '.' || c == ' ') {
                    continue;                                                // not part of the footprint
                } else if (!nav_.walkable(cx + i, cz + j)) {
                    return false;                                           // land base needs land
                }
            }
    } else {
        for (int j = 0; j < type->footZ; ++j)
            for (int i = 0; i < type->footX; ++i)
                if (!grid.walkable(cx + i, cz + j)) return false;
    }
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
    // A mobile builder only: a building may carry canMove=1 in its FBI but can never
    // place structures, and a site still under construction is not yet a builder.
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || b->type->isStructure() ||
        b->underConstruction)
        return 0;
    if (atUnitCap(b->player)) return 0;   // at the unit cap: can't start a new build
    if (atTypeCap(b->player, type)) return 0;   // totalallowed: already fielding one
    if (!canPlace(type, x, z)) return 0;
    int8_t bsquad = b->squad;   // capture before spawn may realloc units_
    int id = spawn(type, x, z, 3.14159f, b->player);
    Unit* site = unit(id);
    site->underConstruction = true;
    site->beingBuilt = true;   // its builder owns it this tick (no instant decay)
    site->hp = type->maxHp * 0.05f;
    // Auto-join: a conjured MOBILE unit inherits the builder's squad (a building never does).
    if (bsquad && !type->isStructure()) site->squad = bsquad;
    if (!type->canMove) {
        blockFootprint(nav_, *type, x, z, true);
        invalidateFlows(int(x) / 16 - type->footX / 2, int(z) / 16 - type->footZ / 2,
                        type->footX, type->footZ);
    }
    b = unit(builderId);   // spawn may have reallocated units_
    b->buildSiteId = id;
    b->buildStuckT = 0; b->buildStuckD = 1e30f;   // fresh job: reset the reach watchdog
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
    b->reclaimId = 0;            // a fresh move/attack/stop drops any reclaim job
    b->reclaimQueue.clear();
    b->repairId = 0;             // ...and any repair job
    if (b->buildSiteId) {
        Unit* site = unit(b->buildSiteId);
        // A site that never actually started building is just a ghost — remove
        // it so its marker/ghost doesn't linger.
        if (site && site->underConstruction && !site->buildBegun) {
            if (site->type && !site->type->canMove) {
                blockFootprint(nav_, *site->type, site->x, site->z, false);
                invalidateFlows(int(site->x) / 16 - site->type->footX / 2,
                                int(site->z) / 16 - site->type->footZ / 2,
                                site->type->footX, site->type->footZ);
            }
            site->underConstruction = false;
            site->deadFor = 1000.0f;   // fully gone (painter skips deadFor>=4)
        }
        b->buildSiteId = 0;
    }
}

void World::assist(int builderId, int siteId) {
    Unit* b = unit(builderId);
    Unit* site = unit(siteId);
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || b->type->isStructure() ||
        b->underConstruction)
        return;
    if (!site || !site->alive() || !site->underConstruction ||
        !allied(site->player, b->player))
        return;
    // Latch onto the existing site; tickConstruction walks there and resumes at
    // this builder's rate (buildTime / workerTime) from the site's current HP.
    b->buildSiteId = siteId;
    b->buildStuckT = 0; b->buildStuckD = 1e30f;   // fresh job: reset the reach watchdog
    order(builderId, site->x, site->z + float(site->type->footZ) * 8 + 24, false);
}

void World::addFeature(int id, float x, float z, float manaYield, float work,
                       int fx, int fz, bool blocks, int type) {
    featureIdx_[id] = features_.size();
    Feature f{id, x, z, fx, fz, manaYield,
              std::max(work, 1.0f), std::max(work, 1.0f), blocks, true};
    f.type = type;
    features_.push_back(f);
}

const Feature* World::feature(int id) const {
    auto it = featureIdx_.find(id);
    return it == featureIdx_.end() ? nullptr : &features_[it->second];
}

const Feature* World::featureAt(float x, float z) const {
    if (terW_ <= 0) return nullptr;
    return feature((int(z) / 16) * terW_ + int(x) / 16);
}

bool World::featureAliveAt(float x, float z) const {
    if (terW_ <= 0) return true;
    int cx = int(x) / 16, cz = int(z) / 16;
    const Feature* f = feature(cz * terW_ + cx);
    return !f || f->alive;   // decorative (untracked) cells are always "alive"
}

// --- Feature burning (retail mechanic; icd 0x494b40 StartBurning, 0x495110
// BurnSpread, 0x495300 BurnOut). Retail synced fire as host-authority events;
// our lockstep equivalent runs it deterministically in every peer's sim. The
// retail burnweapon ("TreeBurn") is DEAD DATA -- the engine wants a [BurnWeapon]
// subsection which no shipped feature has, so burning never damages units, and
// neither does ours. Burn duration in retail is the burn-anim GAF length; the
// sim can't read art, so a fixed 5s stands in (typical tree-burn length).
static constexpr int kBurnTicks = 150;

// Swap a feature IN PLACE to another stage of its chain (featureburnt on
// burn-out, featuredead on destruction; retail places the replacement neutral
// -- our features carry no owner). newType < 0 = the feature is simply gone.
void World::swapFeature(Feature& f, int newType) {
    f.burn = 0;
    f.dmg = 0;
    if (f.blocks)   // old stage's footprint frees first
        nav_.block(int(f.x) / 16 - f.fx / 2, int(f.z) / 16 - f.fz / 2,
                   f.fx, f.fz, false);
    if (newType < 0) { f.alive = false; f.blocks = false; f.type = -1; return; }
    const FeatType& nt = featTypes_[size_t(newType)];
    f.type = newType;
    f.fx = nt.fx; f.fz = nt.fz;
    f.blocks = nt.blocking;
    if (f.blocks)
        nav_.block(int(f.x) / 16 - f.fx / 2, int(f.z) / 16 - f.fz / 2,
                   f.fx, f.fz, true);
    f.manaYield = nt.energy;
    f.work = f.workFull = std::max(nt.energy, 60.0f);
}

void World::igniteFeature(Feature& f) {
    if (f.burn || !f.alive || f.type < 0) return;
    const FeatType& ft = featTypes_[size_t(f.type)];
    if (!ft.flamable || !ft.hasBurnAnim) return;
    static const bool kBurnLog = std::getenv("TAK_BURNLOG") != nullptr;
    if (kBurnLog)
        std::fprintf(stderr, "ignite %s at %.0f,%.0f (spark %d)\n",
                     ft.name.c_str(), f.x, f.z, ft.sparkTicks);
    f.burn = 1;
    // Retail spread timer: sparktime30/2 + rand(sparktime30/2), one LCG draw.
    int half = std::max(ft.sparkTicks / 2, 1);
    f.spreadIn = half + burnRand(half);
    f.burnLeft = kBurnTicks;
}

void World::tickBurning() {
    if (featTypes_.empty()) return;
    // Index-based: igniteFeature never reallocates (spread only flips state).
    for (size_t i = 0; i < features_.size(); ++i) {
        Feature& f = features_[i];
        if (!f.alive || !f.burn) continue;
        // Spread fires ONCE per burning feature (retail slot+0x44 never reloads):
        // a 7x7 box around the head cell, each flamable neighbour rolls
        // rand(100) < its own spreadchance. (Retail's downwind spark phase moves
        // <1 cell at shipped wind speeds -- omitted.)
        if (f.spreadIn > 0 && --f.spreadIn == 0) {
            int cx = int(f.x) / 16, cz = int(f.z) / 16;
            for (int dz = -3; dz <= 3; ++dz)
                for (int dx = -3; dx <= 3; ++dx) {
                    if (dx == 0 && dz == 0) continue;
                    int nx = cx + dx, nz = cz + dz;
                    if (nx < 0 || nz < 0 || nx >= terW_ || nz >= terH_) continue;
                    auto it = featureIdx_.find(nz * terW_ + nx);
                    if (it == featureIdx_.end()) continue;
                    Feature& nf = features_[it->second];
                    if (!nf.alive || nf.burn || nf.type < 0) continue;
                    const FeatType& nft = featTypes_[size_t(nf.type)];
                    if (!nft.flamable || !nft.hasBurnAnim) continue;
                    if (burnRand(100) < nft.spreadChance) igniteFeature(nf);
                }
        }
        if (--f.burnLeft <= 0)
            // Burn-out: swap to the featureburnt stage IN PLACE (same id/cell --
            // the client watches f.type to swap art), or die outright.
            swapFeature(f, featTypes_[size_t(f.type)].burntType);
    }
}

// Reclaim rate: work is consumed at a flat rate (retail ties reclaim duration to
// the feature's `energy`, not the builder's worktime). ~2s for a 250-value tree.
static constexpr float kReclaimRate = 120.0f;   // work units per second

void World::reclaim(int builderId, int featureId, bool queue) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !b->type || !b->type->isBuilder ||
        !b->type->canMove || !b->type->canReclaim)
        return;
    // Negative target = a CORPSE (dead-unit record: bodies, statues, building
    // rubble), so the right-drag box and clicks can clear wrecks like features.
    float tx, tz;
    if (featureId < 0) {
        const Unit* c = unit(-featureId);
        // Accept anything dead or dying this tick whose def can be reclaimed
        // (the walk takes longer than the death anim anyway).
        if (!c || c->id == builderId || !c->type || (c->alive() && c->hp > 0))
            return;
        int ct = c->corpseStatue >= 0 ? c->corpseStatue : corpseTypeOf(c->type);
        if (ct < 0 || !featTypes_[size_t(ct)].reclaimable) return;
        tx = c->x; tz = c->z;
        static const bool kRcLog = std::getenv("TAK_BURNLOG") != nullptr;
        if (kRcLog)
            std::fprintf(stderr, "corpse-reclaim ORDER: b%d -> %s (%d)\n",
                         builderId, c->type->id.c_str(), -featureId);
    } else {
        const Feature* f = feature(featureId);
        if (!f || !f->alive) return;
        tx = f->x; tz = f->z;
    }
    if (b->reclaimId == 0 && b->reclaimQueue.empty() && !queue) {
        b->reclaimId = featureId;
        order(builderId, tx, tz, false);   // walk to it; tickReclaim takes over
    } else {
        b->reclaimQueue.push_back(featureId);
    }
}

void World::tickReclaim(Unit& b, float dt) {
    auto advance = [&] {
        // Pull the next still-valid target off the queue, or go idle.
        // Negative ids are corpses (dead-unit records), positive are features.
        b.reclaimId = 0;
        while (!b.reclaimQueue.empty()) {
            int nid = b.reclaimQueue.front();
            b.reclaimQueue.erase(b.reclaimQueue.begin());
            if (nid < 0) {
                const Unit* nc = unit(-nid);
                if (nc && nc->type && !nc->alive() && nc->deadFor < nc->corpseUntil) {
                    b.reclaimId = nid;
                    order(b.id, nc->x, nc->z, false);
                    break;
                }
            } else {
                const Feature* nf = feature(nid);
                if (nf && nf->alive) { b.reclaimId = nid; order(b.id, nf->x, nf->z, false); break; }
            }
        }
    };
    if (b.reclaimId < 0) {
        // Ordered corpse reclaim: walk to the body/wreck and consume it. Yields
        // the corpse def's energy (0 for everything shipped) -- this is about
        // clearing rubble and denying resurrection, not income.
        Unit* c = unit(-b.reclaimId);
        if (!c || !c->type || c->deadFor >= c->corpseUntil) { advance(); return; }
        if (c->alive()) {
            // hp<=0 = dying THIS tick (the death edge may run after us): hold
            // the job. A genuinely healthy target is an invalid order: drop it.
            if (c->hp > 0) advance();
            return;
        }
        // Body still mid-death-anim: stand by until it settles (statues settle
        // instantly).
        if (c->deadFor < (c->corpseStatue >= 0 ? 0.0f : 4.0f)) return;
        float dx = c->x - b.x, dz = c->z - b.z;
        float reach = 24.0f + 8.0f * float(std::max(c->type->footX, c->type->footZ)) +
                      (b.type->buildDist > 0 ? b.type->buildDist : 0.0f);
        if (dx * dx + dz * dz > reach * reach) return;   // still walking there
        b.speed = 0;
        int ct = c->corpseStatue >= 0 ? c->corpseStatue : corpseTypeOf(c->type);
        const FeatType* cd = ct >= 0 ? &featTypes_[size_t(ct)] : nullptr;
        float step = kReclaimRate * dt;
        // Proportional drip against the INITIAL work (= max(energy,60), set at
        // death). Shipped corpse defs all have energy 0, so this grants nothing.
        if (cd && cd->energy > 0 && c->corpseWork > 0)
            players_[size_t(b.player)].mana +=
                cd->energy * std::min(step, c->corpseWork) /
                std::max(cd->energy, 60.0f);
        c->corpseWork -= step;
        if (c->corpseWork <= 0) {
            c->deadFor = 1000.0f;   // consumed
            if (c->corpseBlocks) {
                c->corpseBlocks = false;
                blockFootprint(nav_, *c->type, c->x, c->z, false);
                invalidateFlows(int(c->x) / 16 - c->type->footX / 2,
                                int(c->z) / 16 - c->type->footZ / 2,
                                c->type->footX, c->type->footZ);
            }
            static const bool kRecLog = std::getenv("TAK_BURNLOG") != nullptr;
            if (kRecLog)
                std::fprintf(stderr, "corpse reclaimed: %s at %.0f,%.0f\n",
                             c->type->id.c_str(), c->x, c->z);
            advance();
        }
        return;
    }
    auto it = featureIdx_.find(b.reclaimId);
    if (it == featureIdx_.end()) { advance(); return; }
    Feature& f = features_[it->second];
    if (!f.alive) { advance(); return; }   // someone else got it (RECLAIMFAILED)
    float dx = f.x - b.x, dz = f.z - b.z;
    float reach = 24.0f + 8.0f * float(std::max(f.fx, f.fz)) +
                  (b.type->buildDist > 0 ? b.type->buildDist : 0.0f);
    if (dx * dx + dz * dz > reach * reach) return;   // still walking there
    b.orders.clear();
    b.speed = 0;
    float want = detmath::atan2(dx, dz);   // face the feature (deterministic)
    float turn = std::clamp(angleDiff(want, b.heading), -b.type->turnRate * dt,
                            b.type->turnRate * dt);
    b.heading += turn;
    float d = std::min(f.work, kReclaimRate * dt);
    f.work -= d;
    players_[size_t(b.player)].mana +=
        f.manaYield * (d / f.workFull) * players_[size_t(b.player)].manaMult;   // drip (income-cheat scaled)
    if (f.work <= 0) {
        f.alive = false;
        if (f.blocks) {   // free the ground cells it occupied (setupMatch blocked nav_)
            nav_.block(int(f.x) / 16 - f.fx / 2, int(f.z) / 16 - f.fz / 2, f.fx, f.fz, false);
            invalidateFlows(int(f.x) / 16 - f.fx / 2, int(f.z) / 16 - f.fz / 2, f.fx, f.fz);
        }
        advance();
    }
}

void World::repair(int builderId, int targetId, bool queue) {
    (void)queue;
    Unit* b = unit(builderId);
    Unit* t = unit(targetId);
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || !b->type->canMove) return;
    if (!t || !t->alive() || !t->type || t->id == b->id || t->underConstruction ||
        !allied(t->player, b->player) || t->hp >= t->type->maxHp)
        return;
    b->repairId = targetId;
    order(builderId, t->x, t->z, false);   // walk to it; tickRepair takes over
}

// A builder repairs a damaged friendly: restores HP at the same rate it would build
// the unit (buildTime / workerTime), draining the owner's mana proportional to the
// HP restored -- pauses if the mana runs out.
void World::tickRepair(Unit& b, float dt) {
    Unit* t = unit(b.repairId);
    if (!b.type) { b.repairId = 0; return; }
    if (!t || !t->alive() || !t->type || t->underConstruction || t->embarked() ||
        !allied(t->player, b.player) || t->hp >= t->type->maxHp) {
        b.repairId = 0;
        return;
    }
    float dx = t->x - b.x, dz = t->z - b.z;
    float half = 16.0f * float(std::max(t->type->footX, t->type->footZ)) / 2;
    float reach = std::max(half + 40.0f,
                           b.type->buildDist > 0 ? b.type->buildDist + half : 0.0f);
    if (dx * dx + dz * dz > reach * reach) {
        if (b.orders.empty()) order(b.id, t->x, t->z, false);   // (re)walk toward it
        return;
    }
    b.orders.clear();
    b.speed = 0;
    float want = detmath::atan2(dx, dz);
    b.heading += std::clamp(angleDiff(want, b.heading), -b.type->turnRate * dt,
                            b.type->turnRate * dt);
    float total = t->type->buildTime / std::max(b.type->workerTime, 0.01f);
    Player& tm = players_[size_t(b.player)];
    float cost = t->type->buildCost * dt / std::max(total, 0.01f);
    if (tm.mana < cost) return;   // can't afford: pause the repair
    tm.mana -= cost;
    t->hp = std::min(t->type->maxHp, t->hp + t->type->maxHp * dt / std::max(total, 0.01f));
    if (t->hp >= t->type->maxHp) b.repairId = 0;
}

void World::startDisco(int player) {
    if (player < 0 || player >= int(players_.size())) return;
    auto& p = players_[size_t(player)];
    // One emote at a time: ignore if this player is already dancing OR headbanging
    // (so you can't stack or restart them). Deterministic across peers.
    if (p.discoLeft <= 0 && p.headbangLeft <= 0) p.discoLeft = 10.0f;
}

bool World::discoActive(int player) const {
    return player >= 0 && player < int(players_.size()) &&
           players_[size_t(player)].discoLeft > 0;
}

void World::startHeadbang(int player) {
    if (player < 0 || player >= int(players_.size())) return;
    auto& p = players_[size_t(player)];
    if (p.discoLeft <= 0 && p.headbangLeft <= 0) p.headbangLeft = 10.0f;
}

bool World::headbangActive(int player) const {
    return player >= 0 && player < int(players_.size()) &&
           players_[size_t(player)].headbangLeft > 0;
}

void World::tickConstruction(Unit& b, float dt) {
    Unit* site = unit(b.buildSiteId);
    if (!site || !site->alive() || !site->underConstruction) {
        b.buildSiteId = 0;
        return;
    }
    site->beingBuilt = true;   // a builder is assigned (walking or working): no decay
    float dx = site->x - b.x, dz = site->z - b.z;
    float half = 16.0f * float(std::max(site->type->footX, site->type->footZ)) / 2;
    // Reach = the builder's FBI builddistance (to the site edge) when it has one,
    // else the default footprint-derived range.
    float reach = std::max(half + 40.0f,
                           b.type->buildDist > 0 ? b.type->buildDist + half : 0.0f);
    float gd = dx * dx + dz * dz;
    if (gd > reach * reach) {                          // out of range: still walking there
        // Give-up watchdog: if the builder gets no closer (~20px, squared 400) for ~8s,
        // it can't reach the site -- abandon it and pop the next queued build. Uses the
        // same fixed-dt / squared-distance idiom as the movement goalStuck watchdog, so
        // it stays deterministic (buildStuck* are non-hashed scratch, like goalStuck*).
        if (gd < b.buildStuckD - 400.0f) {             // real progress: reset the timer
            b.buildStuckD = gd; b.buildStuckT = 0;
        } else if ((b.buildStuckT += dt) > 8.0f) {
            b.buildStuckT = 0; b.buildStuckD = 1e30f;
            int bid = b.id;
            // Drop an un-started ghost site (as cancelBuilds does) so its marker and
            // blocked footprint don't linger.
            if (!site->buildBegun) {
                if (site->type && !site->type->canMove) {
                    blockFootprint(nav_, *site->type, site->x, site->z, false);
                    invalidateFlows(int(site->x) / 16 - site->type->footX / 2,
                                    int(site->z) / 16 - site->type->footZ / 2,
                                    site->type->footX, site->type->footZ);
                }
                site->underConstruction = false;
                site->deadFor = 1000.0f;
            }
            b.buildSiteId = 0;
            // Advance to the next queued build (startBuild may realloc units_).
            while (Unit* nb = unit(bid)) {
                if (nb->buildOrders.empty()) break;
                BuildOrder o = nb->buildOrders.front();
                nb->buildOrders.erase(nb->buildOrders.begin());
                if (startBuild(bid, o.type, o.x, o.z) != 0) break;
            }
        }
        return;
    }
    b.buildStuckT = 0; b.buildStuckD = 1e30f;          // in range: reset the watchdog
    site->buildBegun = true;   // in range: the site starts materialising now
    b.orders.clear();
    b.speed = 0;
    // Face what we're building/conjuring: turn toward the site at the unit's turn
    // rate (a building has turnRate 0, so it simply doesn't rotate).
    float want = detmath::atan2(dx, dz);
    float turn = std::clamp(angleDiff(want, b.heading), -b.type->turnRate * dt,
                            b.type->turnRate * dt);
    b.heading += turn;
    float total = site->type->buildTime / std::max(b.type->workerTime, 0.01f);
    // Record the current build rate so an interrupted conjure decays at this speed.
    site->conjureRate = site->type->maxHp * 0.95f / std::max(total, 0.01f);
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
            nb->buildOrders.erase(nb->buildOrders.begin());
            if (startBuild(bid, o.type, o.x, o.z) != 0) break;
        }
    }
}

void World::decayConstruction(Unit& u, float dt) {
    // No builder worked this conjure this tick: it "un-conjures", losing HP at the
    // rate it was last built, and vanishes at zero (no corpse -- it was never
    // finished). A site that never materialised falls back to its nominal rate.
    float rate = u.conjureRate > 0 ? u.conjureRate
                 : u.type->maxHp * 0.95f / std::max(u.type->buildTime, 0.01f);
    u.hp -= rate * dt;
    if (u.hp > 0) return;
    if (!u.type->canMove) {
        blockFootprint(nav_, *u.type, u.x, u.z, false);
        invalidateFlows(int(u.x) / 16 - u.type->footX / 2, int(u.z) / 16 - u.type->footZ / 2,
                        u.type->footX, u.type->footZ);
    }
    u.underConstruction = false;
    u.deadFor = 1000.0f;   // fully gone (painter skips deadFor>=4), no death anim
}

void World::train(int builderId, const UnitType* type, int count) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !type) return;
    for (int i = 0, n = std::max(1, count); i < n; ++i) b->buildQueue.push_back(type);
}

void World::dequeue(int builderId, const UnitType* type, int count) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !type) return;
    // Remove up to `count` copies of `type`, newest first (from the back), so
    // subtracting cancels the most-recently-queued rather than the in-progress front.
    for (int removed = 0; removed < count; ++removed) {
        int idx = -1;
        for (int i = int(b->buildQueue.size()) - 1; i >= 0; --i)
            if (b->buildQueue[size_t(i)] == type) { idx = i; break; }
        if (idx < 0) break;
        if (idx == 0) b->buildProgress = 0;   // canceling the in-progress front
        b->buildQueue.erase(b->buildQueue.begin() + idx);
    }
    // If the queue no longer holds a type set to infinite-repeat, stop repeating it
    // so the count actually reaches zero instead of refilling next tick.
    if (b->repeatType == type) {
        bool any = false;
        for (const auto* q : b->buildQueue) if (q == type) { any = true; break; }
        if (!any) b->repeatType = nullptr;
    }
}

int World::queuedCount(int builderId, const UnitType* type) const {
    const Unit* b = unit(builderId);
    if (!b || !type) return 0;
    int n = 0;
    for (const auto* q : b->buildQueue) if (q == type) ++n;
    return n;
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
    // Projecting every aura through a dense crowd every tick is the top sim cost in a
    // massive battle (~50ms at 30k units). Buffs change slowly -- they take ~0.5s to
    // fade -- so above a crowd threshold refresh them every kAuraStride ticks instead
    // of every tick: a ~0.1s update granularity that's imperceptible in a huge melee
    // but cuts the aura cost by the stride. Below the threshold it runs every tick, so
    // normal play stays byte-identical. Deterministic (tick-count + unit-count gated,
    // identical on every peer). The relax scales with the stride so a buff still fades
    // over the same wall-clock ~0.5s after its projector stops refreshing it.
    uint32_t stride = units_.size() > 8000 ? 3u : 1u;
    if (tickCounter_ % stride != 0) return;   // buffs held between refreshes
    // Every unit's buffs relax back toward 1.0; aura projectors then refresh the
    // units in their radius, so a buff holds while in range and fades on leaving.
    float relax = std::min(1.0f, 2.0f * dt * float(stride));   // catch up skipped ticks
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

void World::tickAbilities(float dt) {
    // A corpse is a dead unit whose body still lies on the field: the death
    // anim has finished (4s) and decomposetime hasn't expired. Whether it can
    // be RAISED again is the corpse def's `resurrectable` (81 of the 150
    // shipped corpse defs; reclaiming works on any corpse).
    auto isCorpse = [](const Unit& c) {
        // Statues stand from the instant of death (retail: severity 0, no Dying
        // anim); ordinary bodies appear after the 4s death animation.
        float from = c.corpseStatue >= 0 ? 0.0f : 4.0f;
        return c.type && !c.alive() && c.deadFor >= from && c.deadFor < c.corpseUntil;
    };
    auto corpseDef = [&](const Unit& c) -> const FeatType* {
        int ct = c.corpseStatue >= 0 ? c.corpseStatue : corpseTypeOf(c.type);
        return ct >= 0 ? &featTypes_[size_t(ct)] : nullptr;
    };
    auto retire = [&](Unit& c) {
        c.deadFor = 1000.0f;
        if (c.corpseBlocks) {
            c.corpseBlocks = false;
            blockFootprint(nav_, *c.type, c.x, c.z, false);
        }
    };
    struct Revive { const UnitType* type; float x, z; int player; bool animate; };
    std::vector<Revive> revives;
    const float kR = 56.0f;
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        if (!u.alive() || !u.type || u.underConstruction || u.incapacitated() ||
            !u.orders.empty()) {
            if (u.reviveTarget) u.reviveTarget = 0;   // interrupted: channel drops
            continue;
        }
        bool caster = u.type->canResurrect || (u.type->canAnimate && u.type->animateType);
        if (!caster && !u.type->canReclaim) continue;

        // A revive is a CHANNEL, not an instant (retail order-0xA task states:
        // work = target buildtime / caster workertime, x0.3 for animate; mana
        // drains across the channel at the same total as the build cost).
        if (u.reviveTarget) {
            Unit* c = unit(u.reviveTarget);
            if (!c || !isCorpse(*c)) { u.reviveTarget = 0; continue; }
            float dx = c->x - u.x, dz = c->z - u.z;
            if (dx * dx + dz * dz > kR * kR * 4) { u.reviveTarget = 0; continue; }
            bool animate = u.reviveMode == 2;
            const UnitType* out = animate ? u.type->animateType : c->type;
            float totalMana = out->buildCost * (animate ? 0.3f : 1.0f);
            float inc = totalMana * dt / std::max(u.reviveTotal, 0.01f);
            Player& tm = players_[size_t(u.player)];
            if (tm.mana < inc) continue;   // starved: the channel stalls, not drops
            tm.mana -= inc;
            u.reviveLeft -= dt;
            if (u.reviveLeft <= 0) {
                revives.push_back({out, c->x, c->z, u.player, animate});
                retire(*c);
                u.reviveTarget = 0;
            }
            continue;
        }

        // Corpses are dead (not in the spatial grid), so scan directly.
        for (size_t j = 0; j < units_.size(); ++j) {
            Unit& c = units_[j];
            if (j == i || !isCorpse(c)) continue;
            float dx = c.x - u.x, dz = c.z - u.z;
            if (dx * dx + dz * dz > kR * kR) continue;
            const FeatType* cd = corpseDef(c);
            if (!cd) continue;
            if (u.type->canResurrect && c.player == u.player && cd->resurrectable) {
                // Channel length: buildtime / workertime seconds (retail
                // 0x4201e9), mana drained over it; unit returns at 10% HP.
                u.reviveTarget = c.id;
                u.reviveMode = 1;
                u.reviveTotal = u.reviveLeft =
                    std::max(c.type->buildTime / std::max(u.type->workerTime, 0.01f),
                             0.5f);
                break;
            } else if (u.type->canAnimate && u.type->animateType &&
                       cd->resurrectable) {
                // Animate (tarpries animatetype=MONGHOUL): ANY resurrectable
                // corpse -- friend or foe -- rises as the caster's creature,
                // at 0.3x the work and FULL HP (retail 0x420257/0x4206ba).
                u.reviveTarget = c.id;
                u.reviveMode = 2;
                u.reviveTotal = u.reviveLeft =
                    std::max(u.type->animateType->buildTime /
                                 std::max(u.type->workerTime, 0.01f) * 0.3f,
                             0.5f);
                break;
            } else if (u.type->canReclaim && cd->reclaimable) {
                // Retail yield = the corpse def's energy -- 0 for every shipped
                // corpse. You reclaim bodies to DENY resurrection, not for mana.
                players_[size_t(u.player)].mana += cd->energy;
                retire(c);
            }
        }
    }
    for (const auto& r : revives) {
        int id = spawn(r.type, r.x, r.z, 3.14159f, r.player);
        // Retail HP: resurrect returns the unit at 10% (min 1); an animated
        // creature rises at FULL health (icd 0x420666-0x4206ba).
        if (Unit* nu = unit(id))
            if (!r.animate) nu->hp = std::max(nu->type->maxHp * 0.1f, 1.0f);
        static const bool kRevLog = std::getenv("TAK_BURNLOG") != nullptr;
        if (kRevLog)
            std::fprintf(stderr, "%s: %s for p%d at %.0f,%.0f\n",
                         r.animate ? "animate" : "resurrect",
                         r.type->id.c_str(), r.player, r.x, r.z);
    }
}

bool World::sightClear(int ux, int uz, float eyeH, int tx, int tz) const {
    if (heights_.empty()) return true;
    auto H = [&](int x, int z) { return float(heights_[size_t(z) * hW_ + x]); };
    float tH = H(tx, tz);
    float dxf = float(tx - ux), dzf = float(tz - uz);
    float D = std::sqrt(dxf * dxf + dzf * dzf);
    if (D < 1.5f) return true;                    // adjacent cell: always visible
    // Walk the cells between unit and target (integer DDA); a cell blocks the view
    // if its ground rises above the straight eye->target sight line at that point.
    // Terrain must rise this far ABOVE the sight line to block it. Tuned (with the
    // eye height below) so walls/hills/cliffs cast shadows but small rock clutter
    // does not -- on athri cay real walls are height 60-220, so an obstacle must
    // clear ~eye+margin ~= 56 to block. Live-tunable via TAK_FOG_MARGIN.
    static float MARGIN = [] {
        const char* e = std::getenv("TAK_FOG_MARGIN"); return e ? float(std::atof(e)) : 16.0f;
    }();
    int steps = int(D);
    for (int i = 1; i < steps; ++i) {
        float t = float(i) / D;
        int x = int(std::lround(float(ux) + dxf * t));
        int z = int(std::lround(float(uz) + dzf * t));
        if (x < 0 || z < 0 || x >= hW_ || z >= hH_) continue;
        float lineH = eyeH + (tH - eyeH) * t;
        if (H(x, z) > lineH + MARGIN) return false;   // terrain pokes above the line
    }
    return true;
}

void World::updateVisibility() {
    if (nav_.empty()) return;
    if (visPlayer_ < 0) return;   // headless referee: nothing renders, no fog needed
    if (vis_.empty()) {
        visW_ = nav_.width();
        visH_ = nav_.height();
        vis_.assign(size_t(visW_) * visH_, 0);
    }
    // Demote last pass's visible cells: to EXPLORED (1, dimmed but remembered) normally, or
    // straight to hidden (0) when fog memory is off, so they go dark again once out of sight.
    for (auto& v : vis_)
        if (v == 2) v = fogExplored_ ? 1 : 0;
    // Eye above the unit's ground cell: sees over small bumps, not over real
    // walls/hills. Paired with the sight-line MARGIN in sightClear so small rock
    // clutter stops casting fog shadows. Live-tunable via TAK_FOG_EYE.
    static float EYE = [] {
        const char* e = std::getenv("TAK_FOG_EYE"); return e ? float(std::atof(e)) : 40.0f;
    }();
    // PASS 1a (serial, cheap): gather every revealer's stamp parameters, and collect the
    // set of NEW LoS masks that need computing (deduped via an empty cache placeholder --
    // the ray-march that fills them runs in parallel in pass 1b). Bound the cache up front;
    // clearing mid-pass would desync the placeholder dedup. Fog is client-only display (the
    // referee returned above), so NONE of this is hashed.
    if (visMaskCache_.size() >= 8192) visMaskCache_.clear();
    struct Reveal { int cx, cz, r, rRadar2; bool los; uint64_t key; };
    struct Miss   { uint64_t key; int cx, cz, r, rRadar2; };
    std::vector<Reveal> reveals;
    std::vector<Miss>   misses;
    reveals.reserve(units_.size());
    for (const auto& u : units_) {
        // Shared team vision: every allied, built, living unit reveals fog.
        if (!u.alive() || !u.type || u.underConstruction || !allied(u.player, visPlayer_))
            continue;
        // Reveal to the greater of sight and radar range. Radar sees THROUGH terrain, so
        // the LoS test applies only to the sight area not already covered by radar; a unit
        // with radar >= sight does zero LoS work.
        int rSight = int(u.type->sight) / 16;
        int rRadar = int(u.type->radar) / 16;
        int r = std::max(rSight, rRadar) + 1;
        int rRadar2 = rRadar * rRadar;
        int cx = int(u.x) / 16, cz = int(u.z) / 16;
        bool losBlocks = !heights_.empty() && cx >= 0 && cz >= 0 && cx < hW_ && cz < hH_ &&
                         rSight > rRadar;   // only worth testing where sight exceeds radar
        uint64_t key = 0;
        if (losBlocks) {
            // LoS mask key: the visible-cell set for (sight, radar, cell) is a pure function
            // of the immutable heightmap -- computed ONCE, then re-stamped while a unit of
            // this class stands on the cell.
            key = (uint64_t(uint32_t(rSight)) << 52) | (uint64_t(uint32_t(rRadar)) << 40) |
                  (uint64_t(uint32_t(cx)) << 20) | uint32_t(cz);
            // emplace an empty placeholder: .second==true means this key is new this pass,
            // so it needs the ray-march (and dedups repeats of the same class+cell).
            if (visMaskCache_.emplace(key, std::vector<uint32_t>{}).second)
                misses.push_back({key, cx, cz, r, rRadar2});
        }
        reveals.push_back({cx, cz, r, rRadar2, losBlocks, key});
    }
    // PASS 1b (PARALLEL): the O(r^3) LoS ray-march for each new mask -- the expensive part
    // a moving army keeps retriggering. Each computes independently, reading only the
    // immutable heightmap into a local buffer (no shared-map writes); we install them
    // serially afterwards. This is what makes fog cheap at scale for a seated player too.
    if (!misses.empty()) {
        std::vector<std::vector<uint32_t>> built(misses.size());
        auto march = [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const Miss& m = misses[i];
                std::vector<uint32_t>& mask = built[i];
                float eyeH = float(heights_[size_t(m.cz) * hW_ + m.cx]) + EYE;
                for (int dz = -m.r; dz <= m.r; ++dz)
                    for (int dx = -m.r; dx <= m.r; ++dx) {
                        int dd = dx * dx + dz * dz;
                        if (dd > m.r * m.r) continue;
                        int x = m.cx + dx, z = m.cz + dz;
                        if (x < 0 || z < 0 || x >= visW_ || z >= visH_) continue;
                        // Inside radar range: revealed unconditionally. Beyond it, LoS-gated.
                        if (dd > m.rRadar2 && !sightClear(m.cx, m.cz, eyeH, x, z)) continue;
                        mask.push_back(uint32_t(size_t(z) * visW_ + x));
                    }
            }
        };
        size_t mn = misses.size();
        unsigned hw = serialFlow_ ? 1u : std::thread::hardware_concurrency();
        unsigned nth = std::min<unsigned>(hw ? hw : 1u, unsigned((mn + 15) / 16));  // heavy: ~16/thread
        if (nth <= 1) {
            march(0, mn);
        } else {
            size_t chunk = (mn + nth - 1) / nth;
            std::vector<std::thread> th;
            th.reserve(nth - 1);
            for (unsigned k = 1; k < nth; ++k) {
                size_t bb = std::min(mn, size_t(k) * chunk), ee = std::min(mn, bb + chunk);
                if (bb < ee) th.emplace_back(march, bb, ee);
            }
            march(0, std::min(mn, chunk));
            for (auto& t : th) t.join();
        }
        for (size_t i = 0; i < mn; ++i) visMaskCache_[misses[i].key] = std::move(built[i]);
    }
    // PASS 2 (parallel): stamp vis_[i]=2 for every revealer's cells (cached mask or circle
    // fill). It only READS the now-warm cache and writes the constant 2, so overlapping
    // writes from different threads store the SAME value -- a benign race, no locks needed.
    auto stamp = [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const Reveal& rv = reveals[i];
            if (!rv.los) {
                for (int dz = -rv.r; dz <= rv.r; ++dz)
                    for (int dx = -rv.r; dx <= rv.r; ++dx) {
                        if (dx * dx + dz * dz > rv.r * rv.r) continue;
                        int x = rv.cx + dx, z = rv.cz + dz;
                        if (x < 0 || z < 0 || x >= visW_ || z >= visH_) continue;
                        vis_[size_t(z) * visW_ + x] = 2;
                    }
                continue;
            }
            auto mi = visMaskCache_.find(rv.key);   // guaranteed present after pass 1
            if (mi != visMaskCache_.end())
                for (uint32_t idx : mi->second) vis_[idx] = 2;
        }
    };
    size_t n = reveals.size();
    unsigned hw = serialFlow_ ? 1u : std::thread::hardware_concurrency();
    unsigned nth = std::min<unsigned>(hw ? hw : 1u, unsigned((n + 255) / 256));  // >=256/thread
    if (nth <= 1) {
        stamp(0, n);
    } else {
        size_t chunk = (n + nth - 1) / nth;
        std::vector<std::thread> th;
        th.reserve(nth - 1);
        for (unsigned k = 1; k < nth; ++k) {
            size_t bb = std::min(n, size_t(k) * chunk), ee = std::min(n, bb + chunk);
            if (bb < ee) th.emplace_back(stamp, bb, ee);
        }
        stamp(0, std::min(n, chunk));
        for (auto& t : th) t.join();
    }
    ++visGen_;   // renderer: fog content may have changed; re-upload once
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
    // At the per-player unit cap: hold the finished unit inside the building (no mana
    // spent) until a slot frees, rather than popping the queue and spawning it.
    if (atUnitCap(u.player)) return;
    // Same for the FBI totalallowed cap (one dragon/god/juggernaut per player): hold
    // the finished unit until the one already fielded dies, instead of spawning a
    // second. The mana is already spent, so it emerges the moment a slot frees.
    if (atTypeCap(u.player, t)) return;
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
    u.buildQueue.erase(u.buildQueue.begin());
    // spawn() may reallocate units_, invalidating `u`; capture the id and re-fetch.
    int producerId = u.id, player = u.player;
    int id = spawn(t, sx, sz, 3.14159f, player);
    order(id, sx + float((id % 5) - 2) * 22, sz + 60, false);
    if (Unit* pu = unit(producerId)) {
        pu->justBuilt = id;
        // Auto-join: a squad-member producer's new MOBILE unit joins its squad.
        if (pu->squad && t && !t->isStructure())
            if (Unit* nu = unit(id)) nu->squad = pu->squad;
        // Infinite build: re-queue so the next one starts once this one clears.
        if (pu->buildQueue.empty() && pu->repeatType)
            pu->buildQueue.push_back(pu->repeatType);
    }
}

void World::tick(float dt) {
    ++tickCounter_;
    {
        auto _b = std::chrono::steady_clock::now();
        tickBurning();   // feature fire: spread + burn-out (deterministic, hashed)
        if (g_phase) g_burnMs += std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - _b).count();
    }
    // Benchmark: fire the staged spawn plan at its scheduled ticks. Deterministic -- both
    // the client sim and the referee run the identical plan (built in setupMatch), so
    // lockstep holds. It IS hashed (real sim state), which is fine: every peer agrees.
    while (benchCursor_ < benchPlan_.size() && benchPlan_[benchCursor_].tick <= tickCounter_) {
        for (const auto& bs : benchPlan_[benchCursor_].units)
            spawn(bs.type, bs.x, bs.z, 0, bs.player);
        ++benchCursor_;
    }
    std::chrono::steady_clock::time_point _tk0, _sep0;
    if (g_phase) { _tk0 = std::chrono::steady_clock::now();
                   g_tcomb = g_flowMs = g_pathMs = 0; g_flowN = g_pathN = 0;
                   g_visMs = g_burnMs = g_gridMs = 0; }
    // Cap A* repaths per tick: a big group that jams while moving can trip the
    // blocked/stuck watchdogs en masse, and hundreds of path searches in one tick
    // stall the sim. Deferred units retry a later tick. Deterministic (fixed budget,
    // unit-index order), so lockstep peers stay in sync.
    pathBudget_ = 24;
    for (auto& u : units_) { u.justFired = false; u.justBuilt = 0; }
    if (mission_ || scenario_) justDied_.clear();   // deaths this tick, fed to mission/scenario below
    hits_.clear();   // per-tick weapon impacts (drained by the viewer for sounds/fx)
    clock_ += dt;    // wall-clock since the match started (for god timing)
    // Cosmetic disco emote countdown (Shift+D). Deterministic across peers but not
    // hashed -- drives client-side monarch dancing only.
    for (auto& tm : players_) {
        if (tm.discoLeft > 0) tm.discoLeft = std::max(0.0f, tm.discoLeft - dt);
        if (tm.headbangLeft > 0) tm.headbangLeft = std::max(0.0f, tm.headbangLeft - dt);
    }

    // Economy: recompute income/storage, apply income.
    for (auto& tm : players_) { tm.income = 0; tm.storage = 0; }
    std::vector<int> godPriests(players_.size(), 0);
    for (auto& u : units_) {
        // A unit under construction contributes no economy until it finishes -- a
        // half-built lodestone must not add its mana income or storage capacity yet.
        if (!u.alive() || !u.type || u.underConstruction) continue;
        auto& tm = players_[size_t(u.player)];
        tm.income += u.type->income;
        tm.storage += u.type->storage;
        if (u.type->attractsGods) godPriests[size_t(u.player)]++;
    }
    // Difficulty income cheat: scale the summed income so the boost flows through the
    // mana accrual below, allied surplus sharing, and god-favour alike. manaMult is 1
    // for everyone but an Absurd AI, so this is an exact no-op (x1.0) otherwise.
    for (auto& tm : players_) tm.income *= tm.manaMult;
    // Apply income, then share the economy across allies: mana that would
    // overflow a player's storage flows to teammates that still have headroom,
    // so a maxed-out ally feeds the team instead of wasting mogrium. It is truly
    // wasted only when the whole team is capped. Deterministic -- collected and
    // handed out in player-index order. A solo/FFA player (a team of one) has no
    // teammate to receive the surplus, so this reduces exactly to the old clamp.
    for (auto& tm : players_) tm.mana += tm.income * dt;
    const int np = int(players_.size());
    auto cap = [&](int i) { return std::max(players_[size_t(i)].storage, 100.0f); };
    bool teamDone[kMaxPlayers] = {};
    for (int lead = 0; lead < np; ++lead) {
        int team = players_[size_t(lead)].team;
        if (team < 0 || team >= kMaxPlayers || teamDone[team]) continue;
        teamDone[team] = true;
        float pool = 0;   // surplus above caps, gathered from the whole team
        for (int i = 0; i < np; ++i)
            if (players_[size_t(i)].team == team && players_[size_t(i)].mana > cap(i)) {
                pool += players_[size_t(i)].mana - cap(i);
                players_[size_t(i)].mana = cap(i);
            }
        for (int i = 0; i < np && pool > 0; ++i)
            if (players_[size_t(i)].team == team) {
                float give = std::min(cap(i) - players_[size_t(i)].mana, pool);
                if (give > 0) { players_[size_t(i)].mana += give; pool -= give; }
            }
        // Any pool left (every member capped) is wasted, as before.
    }
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
    // Construction: clear the per-tick "worked" flag, let builders add HP (which
    // re-sets it on the sites they walk to / work), then bleed any orphaned conjure
    // that no builder touched this tick until it dies and vanishes.
    for (auto& u : units_)
        if (u.alive() && u.underConstruction) u.beingBuilt = false;
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        if (u.alive() && u.type && u.buildSiteId) tickConstruction(u, dt);
        else if (u.alive() && u.type && u.reclaimId) tickReclaim(u, dt);
        else if (u.alive() && u.type && u.repairId) tickRepair(u, dt);
    }
    for (auto& u : units_)
        if (u.alive() && u.type && u.underConstruction && !u.beingBuilt)
            decayConstruction(u, dt);

    visTimer_ -= dt;
    if (visTimer_ <= 0) {
        // Fog recompute period widens with the crowd: 0.25s normally, up to 0.5s in a huge
        // battle. Fog is the dominant per-0.25s render-thread cost at scale, and a slightly
        // slower reveal is imperceptible -- but it halves the periodic spike RATE. Fog is
        // client-only display (never hashed), so this pacing has no lockstep effect.
        visTimer_ = std::clamp(0.25f + float(units_.size()) / 8000.0f, 0.25f, 0.5f);
        {
        auto _v = std::chrono::steady_clock::now();
        updateVisibility();
        if (g_phase) g_visMs += std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - _v).count();
    }
    }

    // Projectiles.
    for (auto& p : projectiles_) {
        // Guided (FBI type=Guided): steer toward the target's CURRENT position,
        // clamped to the weapon's turnrate, so a homing shot (Tracking Arrow, Ball
        // Lightning, the dragons' fireballs) chases a target that keeps walking
        // instead of flying through where it used to be. Retail's answer to kiting;
        // without it these 576-9000 damage shots simply missed anything mobile.
        // A homer whose target is gone just flies on straight and fizzles.
        if (p.wsrc && p.wsrc->kind == Weapon::Kind::Guided && p.wsrc->turnRate > 0) {
            if (const Unit* gt = unit(p.targetId); gt && gt->alive() && !gt->embarked()) {
                float speed = detmath::len(p.vx, p.vz);
                if (speed > 0.01f) {
                    float cur = detmath::atan2(p.vx, p.vz);
                    float want = detmath::atan2(gt->x - p.x, gt->z - p.z);
                    float d = angleDiff(want, cur);
                    float maxTurn = p.wsrc->turnRate * dt;
                    float nh = cur + std::clamp(d, -maxTurn, maxTurn);
                    p.vx = detmath::sin(nh) * speed;
                    p.vz = detmath::cos(nh) * speed;
                }
            }
        }
        float ox = p.x, oz = p.z;        // segment start (before this step)
        p.x += p.vx * dt;
        p.z += p.vz * dt;
        p.life -= dt;
        p.age += dt;
        Unit* t = unit(p.targetId);
        // A falling bomb is ABOVE everything until it lands, so it takes no
        // in-flight collision -- it detonates once, on the ground, below.
        bool bomb = p.wsrc && p.wsrc->kind == Weapon::Kind::Dropped;
        if (!bomb && t && t->alive() && !t->embarked()) {
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
                else if (!(benchmarkMode() && t->type && t->type->commander)) {
                    t->hp -= p.damage;
                    if (t->hp <= 0) {
                        t->overkill = std::max(t->overkill, -t->hp);
                        t->deathType = p.wsrc ? uint8_t(p.wsrc->dmgType) : 1;
                    }
                }
                p.life = -1;
            }
        }
    }
    // A released bomb detonates the moment it reaches the ground, wherever it has
    // drifted to -- unlike a fired shot, which is a dud if it runs out of life.
    // Whatever it lands ON takes the direct hit: several bombs carry no
    // areaofeffect at all (tarbeak's Egg Bomb), and a splash-only detonation would
    // make those deal nothing whatsoever.
    for (size_t bi = 0; bi < projectiles_.size(); ++bi) {
        const Projectile bp = projectiles_[bi];
        if (bp.life > 0 || !bp.wsrc || bp.wsrc->kind != Weapon::Kind::Dropped) continue;
        Unit* under = nullptr;
        float bestD = 1e30f;
        forEachNear(bp.x, bp.z, 40.0f, [&](int idx) {
            Unit& e = units_[size_t(idx)];
            if (!e.alive() || e.embarked() || !e.type || allied(e.player, bp.fromPlayer)) return;
            float dx = e.x - bp.x, dz = e.z - bp.z;
            float d = dx * dx + dz * dz;
            if (d < bestD) { bestD = d; under = &e; }
        });
        applyHit(*bp.wsrc, bp.x, bp.z, bp.fromPlayer, bp.fromId, under);
    }
    std::erase_if(projectiles_, [](const Projectile& p) { return p.life <= 0; });

    rebuildGrid();   // spatial hash for this tick (combat acquire + separation)

    // Auto-acquire re-scan period, widened with the crowd: target acquisition is the
    // dominant sim cost in a huge battle (each idle armed unit scans its neighbourhood
    // every acqStride_ ticks), so as unit counts climb we rescan LESS often -- a 0.5s
    // re-acquire delay is imperceptible in a 5000-unit melee but roughly halves the
    // acquisition cost there. Deterministic: derived from the live-unit count, which is
    // identical on every lockstep peer.
    {
        uint32_t live = 0;
        for (const auto& u : units_) if (u.alive() && u.type) ++live;
        acqStride_ = std::clamp<uint32_t>(4 + live / 700, 4, 16);
        // Flow-goal quantization coarsens with the crowd too: 2x2 cell blocks normally,
        // up to 8x8 in a massive battle. Fewer distinct goal blocks means far fewer
        // full-map flow-field builds per tick (the dominant cost at 10k+ units), at the
        // price of homing onto a coarser goal centre before steering to the exact order
        // point -- imperceptible at that scale. Deterministic (live count).
        flowQuantShift_ = std::clamp<uint32_t>(1 + live / 3000, 1, 3);
    }

    prefetchFlows();   // batch-build this tick's missing flow fields on threads

    // Formations (Unit::squad < 0): each tick, compute the group's centre + slowest
    // member speed, then walk idle stragglers back toward the centre so they congregate.
    // The mover below paces grouped members to `slowest`; members behind the centre
    // (relative to the goal) keep their own speed to catch up. Deterministic: the sums
    // accumulate in unit-index order and use only basic arithmetic + detmath::len.
    constexpr float kFormBehind = 48.0f;   // sprint if this far behind the group (goal-relative)
    constexpr float kFormRejoin = 140.0f;  // an idle member this far from the centre rejoins
    struct FormAgg { double sx = 0, sz = 0; int n = 0; float slowest = 1e9f; };
    FormAgg forms[kMaxPlayers][11] = {};   // [player][1..10]; slot 0 unused
    auto formOf = [&](const Unit& u) -> FormAgg* {
        if (u.squad >= 0 || u.player < 0 || u.player >= kMaxPlayers) return nullptr;
        return &forms[u.player][-u.squad];
    };
    for (const auto& u : units_)
        if (u.alive() && u.type && u.squad < 0)
            if (FormAgg* f = formOf(u)) {
                f->sx += u.x; f->sz += u.z; ++f->n;
                f->slowest = std::min(f->slowest, u.type->maxVel);
            }
    for (auto& u : units_) {
        if (!u.alive() || !u.type || u.squad >= 0 || !u.orders.empty()) continue;
        if (u.type->isStructure() || u.underConstruction) continue;   // buildings don't rejoin
        FormAgg* f = formOf(u);
        if (!f || f->n <= 1) continue;
        float cx = float(f->sx / f->n), cz = float(f->sz / f->n);
        if (detmath::len(u.x - cx, u.z - cz) > kFormRejoin) order(u.id, cx, cz, false);
    }

    for (auto& u : units_) {
        if (!u.type) continue;
        if (!u.alive()) {
            u.deadFor += dt;
            // CorpseAdjustX/Z: the wreck art sits offset from the building's
            // centre (arakeep corpseadjustz=2). Shift the record -- and its nav
            // block -- once, the moment the death anim ends and the corpse
            // appears (retail places the corpse feature at the adjusted cell).
            if (u.deadFor >= 4.0f && u.deadFor - dt < 4.0f &&
                u.deadFor < u.corpseUntil &&
                (u.type->corpseAdjX != 0 || u.type->corpseAdjZ != 0)) {
                if (u.corpseBlocks) blockFootprint(nav_, *u.type, u.x, u.z, false);
                u.x = std::clamp(u.x + float(u.type->corpseAdjX) * 16.0f,
                                 8.0f, float(terW_) * 16.0f - 8.0f);
                u.z = std::clamp(u.z + float(u.type->corpseAdjZ) * 16.0f,
                                 8.0f, float(terH_) * 16.0f - 8.0f);
                if (u.corpseBlocks) blockFootprint(nav_, *u.type, u.x, u.z, true);
            }
            // The body decomposed (or was never a corpse): fully gone. Records
            // explicitly retired at 1000 stay put.
            if (u.deadFor >= u.corpseUntil && u.deadFor < 999.0f) {
                u.deadFor = 1000.0f;
                if (u.corpseBlocks) {   // blocking wreck finally clears the ground
                    u.corpseBlocks = false;
                    blockFootprint(nav_, *u.type, u.x, u.z, false);
                    invalidateFlows(int(u.x) / 16 - u.type->footX / 2,
                                    int(u.z) / 16 - u.type->footZ / 2,
                                    u.type->footX, u.type->footZ);
                }
            }
            continue;
        }
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
            if (mission_ || scenario_) justDied_.push_back(u.id);
            // [EXPLODEAS]: the unit detonates its own death weapon where it stands
            // (Kamikaze Rat, Grenadier, Fire Demon, Balloon...). Credit the blast to
            // the dying unit's OWNER so it can't hurt its own side (applyHit skips
            // allies) and so kills count for the player who built it. Deferred to a
            // queue: applyHit walks units_ and can kill others, and we are mid-sweep
            // over units_ -- draining after the loop keeps that safe and keeps the
            // order deterministic (sweep order, which is id order).
            if (u.type->hasExplodeAs)
                deathBlasts_.push_back({&u.type->explodeAs, u.x, u.z, u.player, u.id});
            // Corpse window: the body lies reclaimable (and, if its corpse def
            // says so, resurrectable) until decomposetime runs out. Gibbed
            // (overkill >= maxHp -- placeholder severity rule pending the icd
            // Killed RE) or corpse-less units vanish with the death anim.
            {
                // Retail severity (icd 0x512610): ((overkill% + HP% one second
                // before death) / 2), clamped 1..100. Passed to the COB Killed.
                float okPct = u.overkill * 100.0f / std::max(u.type->maxHp, 1.0f);
                u.severity = uint8_t(std::clamp((okPct + float(u.hpPct1s)) * 0.5f,
                                                1.0f, 100.0f));
                // Dying while petrified/frozen leaves the FBI stone=/frozen=
                // STATUE feature instead of the corpse (retail deathType 0xE/0xF
                // path, 0x512d2a) -- blocking, permanent, and resurrectable
                // (raising a statue un-petrifies the unit).
                u.corpseStatue = u.stonedFor > 0 ? statueTypeOf(u.type, false)
                              : u.frozenFor > 0 ? statueTypeOf(u.type, true) : -1;
                static const bool kStatLog2 = std::getenv("TAK_BURNLOG") != nullptr;
                if (kStatLog2 && (u.stonedFor > 0 || u.frozenFor > 0))
                    std::fprintf(stderr, "statue edge: %s statue=%d stoned=%.1f\n",
                                 u.type->id.c_str(), u.corpseStatue, u.stonedFor);
                int ct = u.corpseStatue >= 0 ? u.corpseStatue : corpseTypeOf(u.type);
                // Retail gib rule (icd 0x512610): deathType = the killing blow's
                // FBI damagetype; 3 (explosion) makes Killed refuse the corpse
                // and EXPLODE every piece. An unfinished conjure never leaves a
                // corpse (corpseType forced 0 at 0x5127f5). Statues place
                // unconditionally.
                bool gib = (u.deathType == 3 || u.underConstruction) &&
                           u.corpseStatue < 0;
                if (ct >= 0 && !gib) {
                    if (u.corpseStatue < 0 && isWater(u.x, u.z)) {
                        // Retail water graves sink and fade in seconds, never
                        // decompose, never get reclaimed (icd 0x512fbe).
                        u.corpseUntil = 4.0f + 2.5f;
                    } else {
                        int d30 = featTypes_[size_t(ct)].decomposeTicks;
                        // decomposetime 0 = never rots (building wrecks linger
                        // until reclaimed, like retail).
                        u.corpseUntil = d30 > 0 ? 4.0f + float(d30) / 30.0f : 1e9f;
                    }
                } else {
                    u.corpseUntil = 4.0f;
                }
                u.corpseWork = std::max(ct >= 0 ? featTypes_[size_t(ct)].energy : 0.0f,
                                        60.0f);   // ~0.5s minimum consume time
                // A dead structure frees its nav footprint -- unless its wreck
                // BLOCKS (arakeep_dead blocking=1; a destroyed wall's ARAWALL
                // feature likewise), which keeps the cells occupied until the
                // wreck is reclaimed or rots. (Fixes a long-standing gap:
                // destroyed buildings never unblocked at all.)
                if (u.type->isStructure()) {
                    bool wreckBlocks =
                        u.corpseUntil > 4.0f &&
                        ct >= 0 && featTypes_[size_t(ct)].blocking;
                    if (wreckBlocks) {
                        u.corpseBlocks = true;
                    } else {
                        blockFootprint(nav_, *u.type, u.x, u.z, false);
                        invalidateFlows(int(u.x) / 16 - u.type->footX / 2,
                                        int(u.z) / 16 - u.type->footZ / 2,
                                        u.type->footX, u.type->footZ);
                    }
                }
            }
            u.deadFor = 0; u.orders.clear(); u.speed = 0; continue;
        }

        // Retail 1-second HP-percent samples (unit+0x110/+0x111): the Killed
        // severity reads the PREVIOUS sample, i.e. your health ~1s before death.
        if (tickCounter_ % 30 == 0) {
            u.hpPct1s = u.hpPctCur;
            u.hpPctCur = uint8_t(std::clamp(u.hp / std::max(u.type->maxHp, 1.0f)
                                            * 100.0f, 0.0f, 100.0f));
        }
        // Status timers count down; HP regenerates (healtime); mana recharges.
        if (u.frozenFor > 0) u.frozenFor = std::max(0.0f, u.frozenFor - dt);
        if (u.stonedFor > 0) u.stonedFor = std::max(0.0f, u.stonedFor - dt);
        if (u.paralyzedFor > 0) u.paralyzedFor = std::max(0.0f, u.paralyzedFor - dt);
        if (u.selfDestructT >= 0.0f) {   // armed self-destruct: tick down, then blow up
            u.selfDestructT -= dt;
            if (u.selfDestructT <= 0.0f) {
                u.selfDestructT = -1.0f;
                u.hp = 0; u.lastHitBy = 0; u.deathType = 3;   // explosion death (gib)
            }
        }
        if (u.type->healTime > 0 && u.hp < u.type->maxHp)
            u.hp = std::min(u.type->maxHp, u.hp + dt / u.type->healTime);
        if (u.type->maxMana > 0 && u.mana < u.type->maxMana)
            u.mana = std::min(u.type->maxMana, u.mana + u.type->manaRegen * dt);
        if (u.orders.empty()) { u.homeX = u.x; u.homeZ = u.z; }   // leash anchor

        // Cloaking: drains player mana; an enemy within mincloakdistance forces a
        // decloak, and so does running dry of mana.
        if (u.type->canCloak && u.cloakOn) {
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
        } else if (u.cloaked) {
            u.cloaked = false;   // toggled off (or no longer a cloaker): decloak now
        }

        if (u.underConstruction) continue;   // silent until finished
        if (u.embarked()) {                  // riding a transport
            Unit* t = unit(u.inTransport);
            if (t && t->alive()) { u.x = t->x; u.z = t->z; }
            else { if (mission_ || scenario_) justDied_.push_back(u.id); u.deadFor = 0; }  // transport lost with all hands
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
                // Melee holds at footprint adjacency (must agree with tickCombat's
                // gates, else a unit standing at contact is dragged back into
                // walking by the radial test below).
                int slot = u.type->weapons.empty()
                               ? 0 : std::clamp(u.weaponSlot, 0, int(u.type->weapons.size()) - 1);
                const Weapon* sel =
                    slot < int(u.type->weapons.size()) ? &u.type->weapons[slot] : nullptr;
                if (sel && sel->melee && t->type)
                    return meleeInRange(u.type, t->type, dx, dz);
                // Pad by a structure target's footprint half-extent so "holding in
                // range" agrees with tickCombat's fire gate (else a unit stopped at a
                // building's edge gets dragged back into moving).
                float pad = t->type->maxVel <= 0.0f
                    ? 8.0f * float(std::max(t->type->footX, t->type->footZ)) + 24.0f : 0.0f;
                if (std::sqrt(dx * dx + dz * dz) > (u.type->maxRange() + pad) * 0.95f) return false;
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
        } else if (u.orders.front().wait > 0.0f) {
            // SetMission "w N": hold position while the scripted wait counts down.
            u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
            u.orders.front().wait -= dt;
            if (u.orders.front().wait <= 0.0f) u.orders.erase(u.orders.begin());
        } else if (u.orders.front().waitAttack) {
            // SetMission "wa": ambush -- hold until a non-allied unit is within sight,
            // then release to the next order (usually an attack).
            u.speed = std::max(0.0f, u.speed - u.type->brake * dt);
            float sight = u.type->sight > 0 ? u.type->sight : 200.0f;
            bool threat = false;
            for (const auto& e : units_)
                if (e.alive() && !e.embarked() && e.type && !allied(e.player, u.player)) {
                    float dx = e.x - u.x, dz = e.z - u.z;
                    if (dx * dx + dz * dz <= sight * sight) { threat = true; break; }
                }
            if (threat) u.orders.erase(u.orders.begin());
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
                    u.orders.erase(u.orders.begin());
                    if (done.patrol) u.orders.push_back(done);
                }
                continue;
            }
            float want = detmath::atan2(dx, dz);
            if (o.flow) {
                // Steer along the shared flow field; near the goal (or in an
                // unreachable pocket) the field goes flat and we home straight in.
                const FlowField* ff = flowFor(u.type, o.x, o.z);
                float fx = 0, fz = 0;
                if (ff) ff->dirAt(u.x, u.z, fx, fz);
                if (fx != 0 || fz != 0) want = detmath::atan2(fx, fz);
            }
            float diff = angleDiff(want, u.heading);
            float maxTurn = u.type->turnRate * dt;
            u.heading += std::clamp(diff, -maxTurn, maxTurn);

            // Brake into the waypoint if it's the last one; slow for big turns.
            float target = u.type->maxVel;
            // Formation pacing: a pure-move member keeps to the group's slowest speed,
            // UNLESS it's behind the centre relative to the goal (a straggler), in which
            // case it sprints at its own speed to catch up (see the FormAgg pass above).
            if (u.squad < 0 && o.targetId == 0) {
                if (FormAgg* f = formOf(u); f && f->n > 1) {
                    float cx = float(f->sx / f->n), cz = float(f->sz / f->n);
                    float gx = u.orders.back().x, gz = u.orders.back().z;
                    float uToGoal = detmath::len(u.x - gx, u.z - gz);
                    float cToGoal = detmath::len(cx - gx, cz - gz);
                    if (uToGoal <= cToGoal + kFormBehind) target = std::min(target, f->slowest);
                }
            }
            // Terrain speed factors, retail-style (icd 0x51be12): ROAD first --
            // whole footprint on road cells -- else shallow WATER, never both
            // (a road bridge over the river keeps the road bonus).
            if (!u.type->canFly) {
                if (u.type->roadMult != 1.0f &&
                    onRoad(u.x, u.z, u.type->footX, u.type->footZ)) {
                    target *= u.type->roadMult;
                    static const bool kRoadLog = std::getenv("TAK_ROADLOG") != nullptr;
                    if (kRoadLog) {
                        static int logged = 0;
                        if (logged < 5)
                            std::fprintf(stderr, "road boost: %s x%.2f (%d)\n",
                                         u.type->id.c_str(), u.type->roadMult, ++logged);
                    }
                } else if (u.type->waterMult != 1.0f && !depth_.empty()) {
                    int cx = int(u.x) / 16, cz = int(u.z) / 16;
                    if (cx >= 0 && cz >= 0 && cx < terW_ && cz < terH_ &&
                        depth_[size_t(cz) * terW_ + cx] > 0)
                        target *= u.type->waterMult;
                }
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

            float mx = detmath::sin(u.heading) * u.speed * dt;
            float mz = detmath::cos(u.heading) * u.speed * dt;
            // Collide ground/water units with the nav grid so they can't walk
            // through walls and buildings (pathfinding routes around, but direct
            // steering in combat did not). Slide along a blocked axis.
            if (u.type->canFly) {
                u.x += mx; u.z += mz;
            } else {
                const NavGrid& g = navFor(u.type);
                auto free = [&](float nx, float nz) {
                    // Footprint-aware, and the SAME grid the pathfinder used, so a unit
                    // never stalls on a cell its own path routed it through.
                    return g.empty() || g.fits(int(nx) / 16, int(nz) / 16, footCells(u.type));
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
                            auto _p0=std::chrono::steady_clock::now(); auto path = g.findPath(u.x, u.z, tx, tz, footCells(u.type)); if(g_phase){ g_pathMs += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-_p0).count(); ++g_pathN; }
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
                float moved = detmath::len(u.x - u.stuckX, u.z - u.stuckZ);
                if (moved > 11.0f) {
                    u.stuckFor = 0; u.stuckX = u.x; u.stuckZ = u.z;
                } else {
                    u.stuckFor += dt;
                    if (u.stuckFor > 1.0f) {
                        u.stuckFor = 0; u.stuckX = u.x; u.stuckZ = u.z;
                        const NavGrid& g = navFor(u.type);
                        auto free = [&](float nx, float nz) {
                            return g.empty() || g.fits(int(nx) / 16, int(nz) / 16, footCells(u.type));
                        };
                        float px = detmath::cos(u.heading), pz = -detmath::sin(u.heading);
                        float s = (u.id & 1) ? 1.0f : -1.0f;
                        for (float d : {20.0f, 34.0f}) {
                            if (free(u.x + px * s * d, u.z + pz * s * d)) {
                                u.x += px * s * d * 0.5f; u.z += pz * s * d * 0.5f; break;
                            }
                            if (free(u.x - px * s * d, u.z - pz * s * d)) {
                                u.x -= px * s * d * 0.5f; u.z -= pz * s * d * 0.5f; break;
                            }
                        }
                        // Guard emptiness: the fully-blocked branch above may have
                        // just u.orders.clear()'d this unit (gave up an unreachable
                        // goal), and back()/front() on an empty deque is undefined --
                        // it computes a wild pointer that segfaults on some heap
                        // layouts (the release referee sim) while reading harmless
                        // garbage on others, which is exactly how this surfaced as an
                        // 8-AI server crash. A unit with no order has nothing to repath.
                        if (!g.empty() && !u.orders.empty() && u.orders.front().targetId == 0) {
                            float tx = u.orders.back().x, tz = u.orders.back().z;
                            const FlowField* ff = flowFor(u.type, tx, tz);
                            bool onWalkable = g.walkable(int(u.x) / 16, int(u.z) / 16);
                            if (ff && onWalkable && !ff->reachable(u.x, u.z)) {
                                u.orders.clear();   // unreachable -> give up, no A*
                            } else if (pathBudget_ > 0) {
                                --pathBudget_;
                                auto _p0=std::chrono::steady_clock::now(); auto path = g.findPath(u.x, u.z, tx, tz, footCells(u.type)); if(g_phase){ g_pathMs += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-_p0).count(); ++g_pathN; }
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

        // Progress-based unstick: a ground unit heading to a POINT (move / fight-move,
        // targetId == 0) that hasn't gotten meaningfully closer to it for ~2s is
        // force-repathed with A* -- flow steering can dead-end at a terrain chokepoint
        // or in the unit's own base without ever tripping the fully-blocked path, so a
        // lone scout could sit forever. The A* route (which we know exists when the
        // goal is reachable) replaces the order and steers it around. Fliers and
        // target-locked (attack) orders are exempt; idle units reset the tracker.
        if (!u.type->canFly && !u.orders.empty()) {
            const Order& fo = u.orders.front();
            bool pointMove = fo.targetId == 0 && !fo.load && !fo.unload &&
                             fo.wait <= 0.0f && !fo.waitAttack;
            if (pointMove) {
                float gx = u.orders.back().x, gz = u.orders.back().z;
                float gd = (u.x - gx) * (u.x - gx) + (u.z - gz) * (u.z - gz);
                if (gd < u.goalStuckD - 400.0f) {        // >20px closer -> real progress
                    u.goalStuckD = gd; u.goalStuckT = 0;
                } else {
                    u.goalStuckT += dt;
                    if (u.goalStuckT > 2.0f && pathBudget_ > 0) {
                        u.goalStuckT = 0; u.goalStuckD = gd; --pathBudget_;
                        const NavGrid& g = navFor(u.type);
                        if (!g.empty()) {
                            auto path = g.findPath(u.x, u.z, gx, gz, footCells(u.type));
                            if (!path.empty()) {
                                u.orders.clear();
                                for (const auto& wp : path) u.orders.push_back(wp);
                            }
                        }
                    }
                }
            } else {
                u.goalStuckD = 1e30f; u.goalStuckT = 0;
            }
        } else if (u.orders.empty()) {
            u.goalStuckD = 1e30f; u.goalStuckT = 0;
        }
    }

    // Remote Effect spells channelling toward their landing point. The effect is
    // pinned to the ground where it was aimed, so it lands whether or not the
    // original target moved -- you dodge an Earthquake by leaving its radius, not
    // by taking a step. Ticked before the death-blast drain so a kill it causes is
    // swept next tick like any other.
    for (size_t i = 0; i < pendingEffects_.size();) {
        PendingEffect& e = pendingEffects_[i];
        // A charm/freeze channel dies with its caster: kill the Mind Mage during
        // its wind-up and nothing is converted.
        if (e.casterGated) {
            const Unit* c = e.fromId ? unit(e.fromId) : nullptr;
            if (!c || !c->alive()) {
                pendingEffects_.erase(pendingEffects_.begin() + std::ptrdiff_t(i));
                continue;
            }
        }
        e.at -= dt;
        e.endAt -= dt;
        bool pulse = e.at <= 0.0f;
        if (pulse) {
            if (e.period > 0.0f) e.at += e.period;
            else e.at = 1e9f;          // single-pulse: never again
        }
        bool done = e.endAt <= 0.0f;
        const PendingEffect cur = e;   // applyHit walks/kills units_; copy first
        if (done) pendingEffects_.erase(pendingEffects_.begin() + std::ptrdiff_t(i));
        else ++i;
        if (pulse && cur.w) applyHit(*cur.w, cur.x, cur.z, cur.player, cur.fromId, nullptr);
    }
    // Wandering storms: drift along the launch heading, weave, and grind whatever
    // they touch. The FBI `damage` is a PER-TICK rate, not a per-hit figure -- the
    // Tornado's 50 is 1500/sec inside its small radius (it wears a unit down over
    // its 9 seconds), while a god vortex's 12500 is simply death to stand in, which
    // its `monarch = 0.01` row scales back so a Monarch has a few seconds to escape.
    for (size_t i = 0; i < storms_.size();) {
        Storm& s = storms_[i];
        if (!s.w) { storms_.erase(storms_.begin() + std::ptrdiff_t(i)); continue; }
        // Wander offset: NOT an angle. maxvariation is a jitter half-width in
        // pixels per tick, applied PERPENDICULAR to the launch direction (x gets
        // |dirZ|, z gets |dirX|), so the storm weaves across its own path while
        // still advancing. Re-rolled every variationtime on the sim's Lehmer RNG,
        // so every peer weaves identically.
        s.nextVary -= dt;
        if (s.nextVary <= 0.0f) {
            s.nextVary += s.w->variationTime > 0 ? s.w->variationTime : 2.0f;
            float mv = s.w->maxVariation;
            if (mv > 0) {
                float vx = mv * std::abs(s.dirZ), vz = mv * std::abs(s.dirX);
                s.jitX = (float(burnRand(2001)) / 1000.0f - 1.0f) * vx;
                s.jitZ = (float(burnRand(2001)) / 1000.0f - 1.0f) * vz;
            }
        }
        float vel = s.w->projVel > 0 ? s.w->projVel : 50.0f;
        s.x += s.dirX * vel * dt + s.jitX;
        s.z += s.dirZ * vel * dt + s.jitZ;
        s.x = std::clamp(s.x, 0.0f, float(terW_) * 16.0f);
        s.z = std::clamp(s.z, 0.0f, float(terH_) * 16.0f);
        // builduptime is a harmless wind-up: the storm is already visible and
        // moving, which is the only warning a victim gets to walk out of its path.
        if (s.arm > 0.0f) { s.arm -= dt; ++i; continue; }
        s.left -= dt;
        if (s.left <= 0.0f) { storms_.erase(storms_.begin() + std::ptrdiff_t(i)); continue; }
        const Storm hit = s;   // applyHit walks/kills units_; copy what we need
        static const bool kStormLog = std::getenv("TAK_STORMLOG") != nullptr;
        if (kStormLog) std::fprintf(stderr, "storm t=%u at %.0f,%.0f jit=%.1f,%.1f left=%.1f\n",
                                    tickCounter_, hit.x, hit.z, hit.jitX, hit.jitZ, hit.left);
        applyHit(*hit.w, hit.x, hit.z, hit.player, hit.fromId, nullptr);
        ++i;
    }
    // Drain the [EXPLODEAS] death blasts queued by the sweep above. Done here, after
    // the loop, because applyHit can kill further units (chain-detonating a pack of
    // Kamikaze Rats) and must not mutate units_ while it is being walked. A blast may
    // queue more blasts; the index walk picks those up in the same tick, capped so a
    // pathological chain can't spin forever. Fully deterministic (id-ordered).
    for (size_t i = 0; i < deathBlasts_.size() && i < 4096; ++i) {
        const DeathBlast b = deathBlasts_[i];
        if (b.w) applyHit(*b.w, b.x, b.z, b.player, b.fromId, nullptr);
    }
    deathBlasts_.clear();

    if (g_phase) _sep0 = std::chrono::steady_clock::now();
    // Separation: push overlapping mobile units apart. The spatial hash limits
    // each unit to its ~3x3 neighbourhood, so this is O(n) not O(n^2). Each pair
    // is handled once (by the lower index), so the result matches the old loop.
    {
        auto _g = std::chrono::steady_clock::now();
        rebuildGrid();   // units moved this tick; rebuild for accurate neighbours
        if (g_phase) g_gridMs += std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - _g).count();
    }
    tickAbilities(dt);   // reclaim / resurrect corpses (uses the fresh grid)
    tickAuras(dt);       // AdjustArmor/Attack stat auras (uses the fresh grid)
    constexpr float kSep = 13.0f;
    auto ok = [&](const Unit& u, float nx, float nz) {
        const NavGrid& g = navFor(u.type);
        return g.empty() || g.walkable(int(nx) / 16, int(nz) / 16);
    };
    // Density cap: in a MASSIVE battle (units piled far denser than they separate),
    // the 3x3 neighbourhood of a cell can hold hundreds of units, making this O(n^2)
    // within the pile. The separation push is dominated by the nearest few, so above a
    // crowd threshold each unit interacts with at most kSepCap neighbours -- bounding
    // the pathological case while normal/moderate battles (below the threshold) keep the
    // exact all-pairs behaviour. Deterministic (crowd count + fixed grid order).
    uint32_t liveSep = 0;
    for (const auto& u : units_) if (u.alive() && u.type) ++liveSep;
    int sepCap = liveSep > 4000 ? 24 : 0;   // 0 = uncapped
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& a = units_[i];
        // isStructure(), NOT canmove: the Keep/castle/smith family ships
        // canmove=1 with zero velocity, and a canmove gate let fresh spawns
        // (e.g. a resurrection at a corpse beside a building) SHOVE the
        // building -- which the unstick pass then walked right off its
        // footprint. Buildings never separate.
        if (!a.alive() || a.embarked() || !a.type || !a.type->canMove ||
            a.type->isStructure() || a.type->canFly) continue;
        forEachNearCapped(a.x, a.z, kSep, sepCap, [&](int j) {
            if (size_t(j) <= i) return;   // handle each pair once, and skip self
            Unit& b = units_[size_t(j)];
            if (!b.alive() || b.embarked() || !b.type || !b.type->canMove ||
                b.type->isStructure() || b.type->canFly) return;
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
        // Same isStructure guard as separation: a canmove=1 building sits on
        // its own blocked footprint, so the unstick would march it away.
        if (!u.alive() || u.embarked() || !u.type || !u.type->canMove ||
            u.type->isStructure() || u.type->canFly)
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
            std::fprintf(stderr, "SIMPHASE tick=%.1fms combat=%.1f sep=%.1f flow=%.1f(x%ld) path=%.1f(x%ld) vis=%.1f burn=%.1f grid=%.1f other=%.1f units=%d\n",
                         ttot, g_tcomb, tsep, g_flowMs, g_flowN, g_pathMs, g_pathN,
                         g_visMs, g_burnMs, g_gridMs,
                         ttot - g_tcomb - tsep - g_flowMs - g_pathMs - g_visMs -
                             g_burnMs - g_gridMs, alive);
        }
    }
    // Campaign mission runner: feed this tick's build/death events into the "god"
    // script, then advance it (VM + triggers + win/lose). Runs on every peer (build
    // & death events queue VM threads; the spawns/orders happen in step()), so the
    // mission stays in lockstep with no relayed actions.
    if (mission_) {
        for (auto& u : units_)
            if (u.justBuilt) mission_->unitBuilt(*this, u.justBuilt);
        for (int id : justDied_) mission_->unitDied(*this, id);
        mission_->step(*this, dt);
    }
    // Scenario (.crt) trigger runner: same lockstep contract as the mission
    // runner -- built identically on every peer, ticked here, hashed below.
    if (scenario_) {
        for (int id : justDied_) scenario_->unitDied(*this, id);
        scenario_->step(*this, dt);
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
        // All three reload timers, not just the primary: they now decide WHICH
        // weapon the auto-selector fires, so a drift in any of them would change
        // behaviour.
        for (float rl : u.reloads) mixf(rl);
        mixf(u.selfDestructT);   // self-destruct countdown drives a deterministic death
        // Stance / cloak-intent / active gate auto-acquire, cloaking and firing, so a
        // divergence in them must fault directly rather than diffusing into positions.
        mix(uint64_t(uint32_t(u.stance)));
        mix(uint64_t((u.cloakOn ? 1u : 0u) | (u.active ? 2u : 0u)));
        mix(uint64_t(uint32_t(u.repairId)));   // build-power target -> HP/mana divergence
        mix(uint64_t(uint32_t(int32_t(u.squad))));   // control squad: formation<0 drives movement
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
    // Remote Effect spells mid-channel and wandering storms mid-roam are live sim
    // state that outlives a tick, so a divergence in either must show up here.
    mix(uint64_t(pendingEffects_.size()));
    for (const auto& e : pendingEffects_) {
        mix(uint64_t(uint32_t(e.player)));
        mixf(e.x); mixf(e.z); mixf(e.at);
    }
    mix(uint64_t(storms_.size()));
    for (const auto& s : storms_) {
        mix(uint64_t(uint32_t(s.player)));
        mixf(s.x); mixf(s.z); mixf(s.left); mixf(s.arm);
        // The wander offset and its timer decide the whole path, and each re-roll
        // advances the shared burn RNG -- fold them so a drift surfaces here rather
        // than as a mystery divergence seconds later.
        mixf(s.jitX); mixf(s.jitZ); mixf(s.nextVary);
    }
    for (const auto& t : players_) {
        mixf(t.mana);
        mixf(t.godFavor);
        // Team assignment drives sim behaviour (splash/acquire/auras) but is set
        // from setup -- fold it in so a lobby/config mismatch faults immediately
        // as a desync instead of diverging mysteriously.
        mix(uint64_t(uint32_t(t.team)));
    }
    // Reclaimable features: fold a cheap signature so a reclaim divergence faults as
    // a desync directly (mana/nav already reflect it indirectly). Order-independent.
    uint64_t fAlive = 0, fWork = 0;
    for (const auto& f : features_)
        if (f.alive) {
            ++fAlive;
            uint32_t w; std::memcpy(&w, &f.work, 4);
            uint32_t dm; std::memcpy(&dm, &f.dmg, 4);
            fWork ^= (uint64_t(w) << 1) ^ uint64_t(uint32_t(f.id)) ^
                     // burning/damage state: type swaps and timers are sim state
                     (uint64_t(uint32_t(f.type + 1)) << 17) ^
                     (uint64_t(f.burn) << 33) ^ (uint64_t(uint32_t(f.burnLeft)) << 40) ^
                     (uint64_t(dm) << 9);
        }
    mix(fAlive);
    mix(fWork);
    mix(uint64_t(burnRng_));   // burn-RNG stream position must agree
    if (mission_) mission_->foldHash(h);   // mission triggers/vars/outcome are lockstep state
    if (scenario_) scenario_->foldHash(h); // scenario flags/timers/outcome are lockstep state
    for (uint8_t d : forcedDefeat_) { h ^= (d + 1u); h *= 1099511628211ULL; }
    return h;
}

// Mission runner ownership -- ctor/dtor defined here where MissionScript is a complete
// type, so no other TU instantiates the unique_ptr<MissionScript> deleter.
World::World() = default;
World::~World() = default;
void World::setMission(std::unique_ptr<MissionScript> m) {
    mission_ = std::move(m);
    if (mission_) mission_->start(*this);   // queue the Start script (runs on the first tick)
}
int World::missionOutcome() const { return mission_ ? mission_->outcome() : 0; }

void World::setScenario(std::unique_ptr<ScenarioScript> s) {
    scenario_ = std::move(s);
    if (scenario_) scenario_->start(*this);
}
void World::forceDefeat(int player) {
    if (player < 0) return;
    if (int(forcedDefeat_.size()) <= player) forcedDefeat_.resize(size_t(player) + 1, 0);
    forcedDefeat_[size_t(player)] = 1;
}

int World::updateOutcome() {
    // A player is defeated when it has no living units. Compute per-player
    // alive counts, then per-team. This runs on every sim (referee included),
    // so all peers conclude win/defeat on the same tick.
    std::vector<int> aliveByPlayer(players_.size(), 0);
    // Living Monarch (commander unit) per player, for the monarch-loss rule.
    std::vector<int> monarchByPlayer(players_.size(), 0);
    for (const auto& u : units_)
        if (u.alive() && u.type &&
            u.player >= 0 && u.player < int(players_.size())) {
            ++aliveByPlayer[size_t(u.player)];
            if (u.type->commander) ++monarchByPlayer[size_t(u.player)];
        }
    if (hadMonarch_.size() != players_.size()) hadMonarch_.assign(players_.size(), 0);
    for (int p = 0; p < int(players_.size()); ++p) {
        if (monarchByPlayer[size_t(p)] > 0) hadMonarch_[size_t(p)] = 1;
        // No living units OR -- when the Monarch is NOT expendable -- a player who
        // once fielded a Monarch has now lost it. Both are deterministic and computed
        // identically on every peer + the referee, so win/defeat agree in lockstep.
        bool monarchDead = !monarchExpendable_ && hadMonarch_[size_t(p)] &&
                           monarchByPlayer[size_t(p)] == 0;
        bool forced = p < int(forcedDefeat_.size()) && forcedDefeat_[size_t(p)];
        players_[size_t(p)].defeated = (aliveByPlayer[size_t(p)] == 0) || monarchDead || forced;
        players_[size_t(p)].unitCount = aliveByPlayer[size_t(p)];   // re-sync the cap count
    }

    // Count DISTINCT teams that still have a living unit (robust to any team id,
    // not just 0..n-1): a surviving player counts its team once -- the first time
    // that team appears among survivors. If exactly one team remains it wins.
    // A player counts as surviving only if NOT defeated -- so a monarch-loss
    // elimination (units still alive but the Monarch is dead) removes them from the
    // running just like being wiped out.
    int survivingTeam = -1, survivingCount = 0;
    for (int p = 0; p < int(players_.size()); ++p) {
        if (players_[size_t(p)].defeated) continue;
        int tm = players_[size_t(p)].team;
        bool firstOfTeam = true;
        for (int q = 0; q < p; ++q)
            if (!players_[size_t(q)].defeated && players_[size_t(q)].team == tm) {
                firstOfTeam = false;
                break;
            }
        if (firstOfTeam) { ++survivingCount; survivingTeam = tm; }
    }
    winningTeam_ = (survivingCount == 1) ? survivingTeam : -1;
    return winningTeam_;
}

} // namespace tak::sim
