#include "sim/sim.h"
#include "sim/retailtransport.h"
#include "sim/retailanimationqueries.h"
#include "sim/retailaim.h"
#include "sim/retailhweffectdata.h"
#include "sim/retailstorm.h"
#include "sim/retailguided.h"

#include <type_traits>

#include "hpi/hpi.h"
#include "gaf/nimbus.h"
#include "gaf/animationtiming.h"
#include "sim/detmath.h"
#include "sim/mission.h"
#include "sim/scenario.h"
#include "sim/retailreach.h"
#include "sim/retailaipatrol.h"
#include "sim/retailplacement.h"
#include "tdf/tdf.h"
#include "tdo/tdo.h"

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
static thread_local double g_visMs = 0, g_burnMs = 0, g_gridMs = 0;   // finer "other" split
static thread_local double g_tcomb = 0, g_scriptMs = 0, g_moveMs = 0;

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
            m.footX = int(c.numberOr("FootprintX", 0));
            m.footZ = int(c.numberOr("FootprintZ", 0));
            m.maxSlope = float(c.numberOr("MaxSlope", 255));
            m.maxWaterSlope = float(c.numberOr("MaxWaterSlope", 255));
            m.maxWaterDepth = float(c.numberOr("MaxWaterDepth", 255));
            m.minWaterDepth = float(c.numberOr("MinWaterDepth", 0));
            m.transportLandEligible=c.numberOr("MinWaterDepth",-10000)<0;
            m.badSlope=int(c.numberOr("BadSlope",m.maxSlope/2));
            m.badWaterSlope=std::min(int(c.numberOr("BadWaterSlope",m.maxWaterSlope/2)),int(m.maxWaterSlope));
            m.maxSlope=std::min(m.maxSlope,m.maxWaterSlope);
            m.badSlope=std::min(m.badSlope,m.maxSlope);
            m.badMaxWaterDepth=int(c.numberOr("BadMaxWaterDepth",32768));
            m.badMinWaterDepth=int(c.numberOr("BadMinWaterDepth",32768));
            moveClasses_[name] = m;
        }
    } catch (const std::exception&) {}   // no MOVEINFO -> units keep FBI defaults
}

void TypeRegistry::loadDir(const hpi::Vfs& vfs, const std::string& prefix) {
    const auto nimbus = gaf::factionNimbus(vfs);
    std::map<std::string,std::vector<uint16_t>> stormTimings;
    auto stormTiming = [&](const std::string& name) -> const std::vector<uint16_t>& {
        auto [it, added] = stormTimings.try_emplace(name);
        if (added) it->second = gaf::animationTiming(vfs,name);
        return it->second;
    };
    if(!effectsLoaded_) {
        for(const auto& path:vfs.list("gamedata/effects")) {
            if(lower(std::filesystem::path(path).extension().string())!=".tdf")continue;
            const auto bytes=vfs.read(path);
            const auto root=tdf::parseText(std::string(bytes.begin(),bytes.end()),path);
            for(const auto& [name,node]:root.orderedChildren())
                lightningEffects_.try_emplace(std::string(name),
                    std::make_shared<const RetailLightningDefinition>(retailLightningDefinition(*node)));
        }
        effectsLoaded_=true;
    }
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
            t.orientation=uint16_t(int32_t(info->numberOr("orientation",0)));
            t.buildAngle=uint16_t(int32_t(info->numberOr("buildangle",0)));
            t.side = info->valueOr("side", "");
            if (const auto it = nimbus.find(lower(t.side)); it != nimbus.end())
                t.hasNimbusArt = !it->second.empty();
            // Buildings often declare canmove=1; bmcode (0 = building,
            // 1 = mobile unit) is the authoritative mobility flag.
            t.canMove = info->numberOr("canmove", 0) != 0 &&
                        info->numberOr("bmcode", 1) != 0;
            t.buildMovementCode=uint8_t(int32_t(info->numberOr("bmcode",1)));
            t.maxVel = Fixed::fromRetailNumber(info->numberOr("maxvelocity", 0));
            // 4bfd8e: absent moverate1/2 both default to twice maxvelocity.
            t.animationMoveRate1=info->value("moverate1")
                ? Fixed::fromRetailNumber(info->numberOr("moverate1",0))
                : Fixed::raw(int32_t(uint32_t(t.maxVel.v)*2u));
            t.animationMoveRate2=info->value("moverate2")
                ? Fixed::fromRetailNumber(info->numberOr("moverate2",0))
                : Fixed::raw(int32_t(uint32_t(t.maxVel.v)*2u));
            // Velocity is px/tick (*kTick => px/s); acceleration and braking are
            // px/tick^2, so they need kTick^2. Using kTick left accel 30x too
            // small, so high-maxVel flyers never reached speed and all crawled.
            t.accel = Fixed::fromRetailNumber(info->numberOr("acceleration", 0.5));
            t.brake = Fixed::fromRetailNumber(info->numberOr("brakerate", 0.5));
            // FBI turnrate is COB angle units per tick.
            float tr = float(info->numberOr("turnrate", 500));
            t.turnRate = int32_t(tr);   // bam/tick, exactly as the FBI holds it
            // Native +0x190 is an unsigned word, with zero for an omitted key.
            t.turnInPlaceRate = uint16_t(int32_t(info->numberOr("turninplacerate", 0)));
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
            t.gate = (int(info->numberOr("gate",0))&1)!=0;
            {
                std::string ym = info->valueOr("yardmap", "");
                std::erase_if(ym, [](char c) { return c == ' ' || c == '\t'; });
                // A stray 'S' (not a full footprint map) marks a lodestone that
                // must sit on a mana deposit.
                t.onMana = ym.find('S') != std::string::npos ||
                           ym.find('s') != std::string::npos;
                t.sacredIncome=ym.find('S')!=std::string::npos;
                if (int(ym.size()) == t.footX * t.footZ) t.yardMap = ym;
            }
            // Every lodestone (Lodestone / Divine Lodestone) must sit on a mana
            // deposit, even ones whose FBI omits the 'S' yardmap (e.g. crelode).
            if (t.id.find("lode") != std::string::npos ||
                t.id.find("mana") != std::string::npos)
                t.onMana = true;
            t.canTransport = info->numberOr("cantransport", 0) != 0;
            t.transportCap = uint16_t(int(info->numberOr("transportcapacity",0)));
            t.transportSizeCap = uint16_t(int(info->numberOr("transportsizecapacity",0)));
            t.maxTransportSize = uint16_t(int(info->numberOr("transportsize",0)));
            t.transportDist = uint16_t(int(info->numberOr("transportdistance", 0)));
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
            t.noShadow = info->numberOr("noshadow", 0) != 0;
            t.floater = info->numberOr("floater", 0) != 0;
            t.upright = info->numberOr("upright", 0) != 0;
            t.canHover = info->numberOr("canhover", 0) != 0;
            t.ghost = info->numberOr("ghost", 0) != 0;
            t.waterline = int(info->numberOr("waterline", 0));
            t.veteranModel = lower(info->valueOr("veteranmodel", ""));
            t.bodyType = lower(info->valueOr("bodytype", "default"));
            // Extended stats.
            t.healTime = float(info->numberOr("healtime", 0));
            t.leash = float(info->numberOr("maneuverleashlength", 0));
            // Standing orders, derived the way retail's parser does (icd 0x4c005d):
            // standingunitorder is a front-end that expands into the two fields
            // that actually drive behaviour, and its ABSENCE is a distinct case --
            // the sentinel default 3 falls through to standingmoveorder /
            // standingfireorder (both default 2), i.e. roam + fire at will.
            {
                int suo = int(info->numberOr("standingunitorder", 3)) & 3;
                t.defaultStandingOrder = uint8_t(suo);
                if (suo == 2)      { t.defaultMove = 1; t.defaultFire = 2; }
                else if (suo == 1) { t.defaultMove = 0; t.defaultFire = 2; }
                else if (suo == 0) { t.defaultMove = 0; t.defaultFire = 0; }
                else {
                    t.defaultMove = uint8_t(int(info->numberOr("standingmoveorder", 2)) & 3);
                    t.defaultFire = uint8_t(int(info->numberOr("standingfireorder", 2)) & 3);
                }
            }
            t.waterMult = Fixed::fromRetailNumber(info->numberOr("watermultiplier",
                                info->numberOr("watermultipliser", 1)));
            // Exact key only: the icd's parser knows no typo fallback, so
            // verpult's "roadmultplier" never counted in retail either. The
            // retail default is ~1.2 (16.16 0x13333), NOT 1.0.
            t.roadMult = info->value("roadmultiplier")
                       ? Fixed::fromRetailNumber(info->numberOr("roadmultiplier", 1.2))
                       : Fixed::raw(0x13333);
            // Retail's per-type movement time scale, UnitType+0x249 (icd 0x4bfc5e-
            // 0x4bfd34): ticks to cross half a cell at the type's best speed. The
            // best speed is maxvelocity times the better of the two terrain
            // multipliers, floored at 1.0; maxvelocity <= 0 pins the scale at 255.
            // All in the same 16.16 the parser used, truncating like its ftol.
            {
                const int64_t mult = std::max<int64_t>(
                    std::max(t.waterMult.v, t.roadMult.v), 0x10000);
                const int64_t best = (mult * t.maxVel.v) >> 16;   // 16.16 px/tick
                t.halfCellTicks = best <= 0
                    ? 255
                    : int32_t(std::clamp<int64_t>((int64_t(8) << 16) / best, 1, 255));
            }
            t.maxWaterDepth = float(info->numberOr("maxwaterdepth", 0));
            t.maxSlope = float(info->numberOr("maxslope", 255));
            // Retail keeps this in 3 bits (icd 0x4c09e8: `& 7`, shifted into
            // UnitType+0x264 bits 21..23), so 0..7 seconds is the whole range.
            // Default 2, not 0: when the key is absent retail's parser clears
            // bit 23 and sets bit 22 of the three-bit field (icd 0x4c0a1f),
            // which leaves 2. No shipped FBI declares the key, so 2 is what
            // every unit in the game actually uses.
            t.selfDestructCountdown =
                std::clamp(int(info->numberOr("selfdestructcountdown", 2)), 0, 7);
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
            // Boolean in retail too: the parser does `and eax,1`.
            t.fireAtWillRandom = (int(info->numberOr("fireatwillrandom", 0)) & 1) != 0;
            t.attractsGods = info->numberOr("attractsgods", 0) != 0;
            t.weaponSwitching = info->numberOr("weaponswitching", 0) != 0;
            t.onOffable = info->numberOr("onoffable", 0) != 0;
            t.activateWhenBuilt = info->numberOr("activatewhenbuilt", 1) != 0;
            t.cantBeStoned = info->numberOr("cantbestoned", 0) != 0;
            t.cantBeFrozen = info->numberOr("cantbefrozen", 0) != 0;
            // Retail forces both on Monarchs (icd: def bit18 -> cantbestoned +
            // cantbefrozen) -- else petrify would be an instant Monarch kill.
            if (t.commander) t.cantBeStoned = t.cantBeFrozen = true;
            t.cantBeCaptured = info->numberOr("cantbecaptured", 0) != 0;
            t.cantBeTransported = info->numberOr("cantbetransported", 0) != 0;
            t.transportSize = uint16_t(int(info->numberOr("transportedsize",0)));
            {   // bloodcolor1 = "r g b"
                std::string bc = info->valueOr("bloodcolor1", "");
                int r = 150, g = 30, b = 10;
                if (std::sscanf(bc.c_str(), "%d %d %d", &r, &g, &b) >= 1) {
                    t.blood[0] = uint8_t(std::clamp(r, 0, 255));
                    t.blood[1] = uint8_t(std::clamp(g, 0, 255));
                    t.blood[2] = uint8_t(std::clamp(b, 0, 255));
                }
                for(unsigned color=0;color<3;++color) {
                    const auto text=info->valueOr("bloodcolor"+std::to_string(color+1),"");
                    int red=t.blood[0],green=t.blood[1],blue=t.blood[2];
                    std::sscanf(text.c_str(),"%d %d %d",&red,&green,&blue);
                    t.bloodColors[color]=0xff000000u |
                        (uint32_t(std::clamp(red,0,255))<<16) |
                        (uint32_t(std::clamp(green,0,255))<<8) |
                        uint32_t(std::clamp(blue,0,255));
                }
            }
            t.canFly = info->numberOr("canfly", 0) != 0;
            t.transportLandEligible=info->numberOr("minwaterdepth",-10000)<0;
            std::string mc = lower(info->valueOr("movementclass", ""));
            if (mc.rfind("water", 0) == 0) t.domain = UnitType::Domain::Water;
            else if (mc.rfind("hover", 0) == 0) t.domain = UnitType::Domain::Hover;
            // Inherit terrain limits from the MOVEINFO movement class (the real
            // source of slope/water limits; most FBIs don't carry their own).
            if (auto mci = moveClasses_.find(mc); mci != moveClasses_.end()) {
                t.maxSlope = mci->second.maxSlope;
                t.maxWaterSlope = mci->second.maxWaterSlope;
                t.maxWaterDepth = mci->second.maxWaterDepth;
                t.minWaterDepth = mci->second.minWaterDepth;
                t.transportLandEligible=mci->second.transportLandEligible;
                t.badSlope=mci->second.badSlope;t.badWaterSlope=mci->second.badWaterSlope;
                t.badMaxWaterDepth=mci->second.badMaxWaterDepth;t.badMinWaterDepth=mci->second.badMinWaterDepth;
                // The movement class WINS over the FBI, which is retail's precedence
                // and is safe here: no shipped unit declares both.
                if (mci->second.footX > 0) t.footX = mci->second.footX;
                if (mci->second.footZ > 0) t.footZ = mci->second.footZ;
            }
            // 4c0e58: omitted/zero transportedsize uses the final movement footprint.
            if (!t.transportSize) t.transportSize=uint16_t(t.footX*t.footZ);
            // RAW, no divisor. Retail adds cruisealt straight onto the terrain
            // height byte to get the flyer's world Y (icd 0x4e42ee:
            // `eax = groundHeight + cruisealt`, then <<16 into the unit's Y),
            // so it is in the same units as the heightmap -- and our terrain
            // lift already matches retail's height/2. The `/ 4` sitting here
            // was an undocumented render fudge with no other reader (nothing in
            // the sim consumes cruiseAlt), and it put flyers at a quarter of
            // their height: 18.75px of lift for a cruisealt of 150 where retail
            // gives 75. It also pulled every flyer's shadow four times too close,
            // since the shadow offset is alt/4 off the same number.
            t.cruiseAlt = float(info->numberOr("cruisealt", 0));
            t.hoverAttack = info->numberOr("hoverattack", 0) != 0;
            t.hoverAttackDistance = int(info->numberOr("hoverattackdistance", 0));
            t.hoverAttackAltitude = int(info->numberOr("hoverattackaltitude", t.cruiseAlt));
            t.bankScale = Fixed::fromRetailNumber(info->numberOr("bankscale", 0.5));
            t.canSetStance = int(info->numberOr("unitstandorders", 1)) != 0;
            {
                std::string dmt = lower(info->valueOr("defaultmissiontype", ""));
                t.wanders = dmt == "standby_wander";
                t.vtolStandby = dmt == "vtol_standby";
            }
            t.pitchScale = Fixed::fromRetailNumber(info->numberOr("pitchscale", t.canFly?0.0:0.5));
            // One weapon block -> a Weapon. Shared by WEAPON1..3 and by
            // [EXPLODEAS] (the death blast), which is the same block shape.
            auto parseWeapon = [this,&stormTiming](const tdf::Node* w) {
                Weapon wp;
                wp.name = w->valueOr("name", "");
                // Native WeaponType+0xc8 bit 0x20 drives the Airstrike cursor
                // when it is the active weapon; it is distinct from subtype=Dropped.
                wp.cursorAirstrike = w->numberOr("dropped", 0) != 0;
                wp.range = float(w->numberOr("range", 0));
                wp.hoverAttack = w->numberOr("hoverattack", 0) != 0;
                wp.hoverAttackDistance = int(w->numberOr("hoverattackdistance", 0));
                wp.hoverAttackAltitude = int(w->numberOr("hoverattackaltitude", 0));
                wp.reload = float(w->numberOr("reloadtime", 1));
                wp.projVel = float(w->numberOr("weaponvelocity", 0));
                // See Weapon::subSteps. Integer ceil: no shipped weaponvelocity is a
                // multiple of 480, so this is bit-identical to retail's fixed-point
                // chain on every shipped weapon (a mod landing exactly on 480n would
                // differ by one sub-step).
                wp.subSteps = wp.projVel > 0.0f
                                  ? int((wp.projVel + 479.0f) / 480.0f) : 1;
                if (wp.subSteps < 1) wp.subSteps = 1;
                wp.noLead = w->numberOr("dontleadtargets", 0) != 0;
                wp.gravityAdj = float(w->numberOr("gravityadjustment", 1.0));
                wp.lobPreferred = w->numberOr("lobpreferred", 0) != 0;
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
                // Subtype selects the flame emitter, independently of hit-effect art.
                wp.beam = lower(w->valueOr("type", "")) == "line of sight";
                const auto flameSubtype=lower(w->valueOr("subtype", ""));
                wp.straight=wp.beam && flameSubtype.empty();
                wp.lightning=wp.beam && flameSubtype=="lightning";
                wp.hwEffectName=hwe;
                if(wp.lightning) {
                    const auto effect=lightningEffects_.find(hwe);
                    if(effect!=lightningEffects_.end())wp.lightningEffect=effect->second;
                }
                if(wp.beam) {
                    if(flameSubtype=="fire")wp.flameKind=0;
                    else if(flameSubtype=="bluefire")wp.flameKind=1;
                    else if(flameSubtype=="dieselflame")wp.flameKind=2;
                }
                wp.groundBounce=w->numberOr("groundbounce",0)!=0;
                wp.waterWeapon=w->numberOr("waterweapon",0)!=0;
                wp.emitTime = int32_t(w->numberOr("emittime", 0));   // already ticks in the FBI
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
                    wp.buildUpTicks = int32_t(w->numberOr("builduptime", 0) * 30.0);
                    wp.decay = float(w->numberOr("decaytime", 0));
                    wp.duration = float(w->numberOr("duration", 0));
                    wp.durationTicks = int32_t(w->numberOr("duration", 0) * 30.0);
                    // maxvariation is NOT an angle: it is the wander jitter's
                    // half-width in PIXELS PER TICK (2..8 across the shipped
                    // storms, which dwarfs their 45..80 px/s drift -- that is what
                    // makes the path genuinely wander rather than curve).
                    wp.maxVariation = int32_t(w->numberOr("maxvariation", 0));
                    wp.variationTime = float(w->numberOr("variationtime", 0));
                    wp.variationTicks = int32_t(w->numberOr("variationtime", 0) * 30.0);
                    wp.unitsOnly = w->numberOr("unitsonly", 0) != 0;
                    wp.particlesPerSec = int32_t(w->numberOr("particlespersecond", 0));
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
                // aimtolerance stays in COB angle units -- the same unit as Bam, and
                // the same unit the firing test compares in. Ballistic weapons lob an
                // arc (viewer draws it); soundhitclass = impact sound.
                wp.aimTol = int32_t(w->numberOr("aimtolerance",
                                    w->numberOr("aimtolerence", 1024)));
                wp.ballistic = lower(w->valueOr("type", "")) == "ballistic";
                wp.soundHit = lower(w->valueOr("soundhitclass",
                                               w->valueOr("soundhit", "")));
                // Projectile art (display only).
                wp.weaponArt = lower(w->valueOr("weaponart", ""));
                wp.shotModel = lower(w->valueOr("model", ""));
                if (auto dot = wp.shotModel.rfind(".3do"); dot != std::string::npos)
                    wp.shotModel.erase(dot);          // some FBIs spell the extension
                wp.veteranShotModel = lower(w->valueOr("veteranmodel", ""));
                if (auto dot = wp.veteranShotModel.rfind(".3do");
                    dot != std::string::npos)
                    wp.veteranShotModel.erase(dot);
                wp.veteranLevel = int32_t(w->numberOr("veteranlevel", 0));
                wp.nimbus = w->numberOr("nimbus", 0) != 0;
                {
                    // innercolor/middlecolor/outercolor are "R G B" triples that
                    // tint a lightning bolt's core, body and halo.
                    auto rgb = [&](const char* key, uint8_t out[3]) {
                        const std::string* v = w->value(key);
                        if (!v) return false;
                        int c[3] = {255, 255, 255};
                        std::sscanf(v->c_str(), "%d %d %d", &c[0], &c[1], &c[2]);
                        for (int i = 0; i < 3; ++i)
                            out[i] = uint8_t(std::clamp(c[i], 0, 255));
                        return true;
                    };
                    bool a = rgb("innercolor", wp.inner);
                    bool b = rgb("middlecolor", wp.middle);
                    bool c = rgb("outercolor", wp.outer);
                    wp.hasBoltColor = a || b || c;
                }
                // spinheading is in COB angle units per second.
                // RAW COB ANGLE UNITS, as retail stores it (readInt). Scaling by kCobAngle here
                // -- which is 2*pi/65536, about 9.6e-5 -- truncated the whole field to zero
                // the moment it became an int, silently stopping every shot from spinning.
                // The conversion to degrees belongs at the renderer, which is the only reader.
                wp.spinRate = int32_t(w->numberOr("spinheading", 0));
                wp.shotSpin={uint16_t(int32_t(w->numberOr("spinroll",0))),uint16_t(wp.spinRate),
                    uint16_t(int32_t(w->numberOr("spinpitch",0)))};
                // shadowgaf is always "shadows"; shadowart names the sequence in it.
                wp.shadowArt = lower(w->valueOr("shadowart", ""));
                wp.shotArt = lower(w->valueOr("shotart", ""));
                {
                    std::string lm = lower(w->valueOr("lightmap", ""));
                    wp.lightMap = lm == "small" ? 1 : lm == "medium" ? 2
                                : lm == "large" ? 3 : 0;
                }
                wp.wanderStart = lower(w->valueOr("wanderstartart", ""));
                wp.wanderLoop = lower(w->valueOr("wanderloopart", ""));
                wp.wanderEnd = lower(w->valueOr("wanderendart", ""));
                if(wp.kind==Weapon::Kind::Wandering) {
                    wp.wanderStartTicks=stormTiming(wp.wanderStart);
                    wp.wanderLoopTicks=stormTiming(wp.wanderLoop);
                    wp.wanderEndTicks=stormTiming(wp.wanderEnd);
                }
                wp.explosionClass = lower(w->valueOr("explosionclass", ""));
                wp.waterExplosionClass = lower(w->valueOr("waterexplosionclass", ""));
                wp.radiusArt[0] = lower(w->valueOr("radiusart0", ""));
                wp.radiusArt[1] = lower(w->valueOr("radiusart1", ""));
                wp.radiusArt[2] = lower(w->valueOr("radiusart2", ""));
                wp.ringCount = int(w->numberOr("ringcount", 0));
                wp.ringDelay = float(w->numberOr("ringdelay", 0.2));
                wp.ringDur = float(w->numberOr("ringduration", 1.0));
                wp.spriteCount = int(w->numberOr("spritecount", 24));
                wp.shakeMag = int32_t(w->numberOr("shakemagnitude", 0));
                wp.shakeDur = float(w->numberOr("shakeduration", 0));
                wp.fireStarter = w->numberOr("firestarter", 0) != 0;
                {
                    std::string dtp = lower(w->valueOr("damagetype", "normal"));
                    wp.dmgType = dtp == "fire" ? 2 : dtp == "explosion" ? 3
                               : dtp == "paralyzer" ? 4 : 1;
                }
                wp.minRange = int32_t(w->numberOr("minrange", 0));
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
            t.hasPrimaryWeaponBlock = root.child("WEAPON1") != nullptr;
            for (int slot = 1; slot <= 3; ++slot) {
                const auto* w = root.child("WEAPON" + std::to_string(slot));
                if (!w) continue;
                Weapon wp = parseWeapon(w);
                t.weaponAirstrikeCursor[size_t(slot-1)] = wp.cursorAirstrike;
                if (wp.damage > 0) {
                    t.weaponNativeSlotForLocal[t.weapons.size()] = uint8_t(slot-1);
                    t.weapons.push_back(wp);
                }
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
                        au.radius = int32_t(a->numberOr("radius", 200));
                        au.edge = float(a->numberOr("edgeeffectiveness", 1));
                        t.auras.push_back(au);
                    }
                }
            }
            // Target-category tokens for weapons' per-category damage: split
            // `category`, plus `damagecategory` and `tedclass`, all lowercased.
            {
                t.damageCategory = lower(info->valueOr("damagecategory", ""));
                std::string cat = lower(info->valueOr("category", "")) + " " +
                                  t.damageCategory + " " +
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
    // Script/model failures must not silently remove a valid unit definition.
    for (auto& [id,t]:types_) {
        if (!t.groundModelLoaded) {
            const auto path="objects3d/"+t.id+".3do";
            if (vfs.has(path)) {
                const auto model=tdo::load(vfs.read(path));
                t.modelTop=retailModelTop(model.root);
                t.sightHeight=uint8_t(uint32_t(t.modelTop)>>16);
                const auto& root=model.root;
                if (root.selectionPrimitive>=0 && size_t(root.selectionPrimitive)<root.primitives.size()) {
                    const auto& indices=root.primitives[size_t(root.selectionPrimitive)].indices;
                    if (indices.size()>=4) {
                        RetailGroundSupport support;
                        for (size_t i=0;i<4;++i) {
                            const auto& point=root.verticesRaw.at(indices[i]);
                            support[i]={std::bit_cast<int32_t>(0u-uint32_t(point[0])),
                                        std::bit_cast<int32_t>(0u-uint32_t(point[2]))};
                        }
                        t.projectileQuad=support;
                        if (!t.canFly && !t.isStructure() && !t.upright && !t.floater)
                            t.groundSupport=support;
                    }
                }
            }
            t.groundModelLoaded=true;
        }
        if (t.simulationScript) continue;
        {
            const auto scriptPath="scripts/"+t.id+".cob";
            if (vfs.has(scriptPath)) {
                auto script=std::make_shared<cob::File>(cob::load(vfs.read(scriptPath),scriptPath));
                t.simulationScript=script;
                const auto modelPath="objects3d/"+t.id+".3do";
                if(vfs.has(modelPath)) {
                    auto model=tdo::load(vfs.read(modelPath));
                    t.scriptPieceCenters.resize(script->pieces.size());
                    auto& productionModel=t.productionModel;
                    auto append=[&](auto&& self,const tdo::Object& object,int parent)->void {
                        int piece=-1;
                        for (size_t i=0;i<script->pieces.size();++i)
                            if (lower(script->pieces[i])==lower(object.name)) { piece=int(i); break; }
                        const int index=int(productionModel.size());
                        productionModel.push_back({object.offsetRaw,parent,piece});
                        auto& emissionModel=productionModel.back();
                        emissionModel.emissionVertexCount=uint8_t(std::min(size_t(2),object.verticesRaw.size()));
                        for(size_t i=0;i<emissionModel.emissionVertexCount;++i)
                            emissionModel.emissionVertices[i]=object.verticesRaw[i];
                        if(piece>=0) {
                            RetailPieceBounds bounds;
                            for(auto vertex:object.verticesRaw) {
                                for(int axis:{0,2})vertex[size_t(axis)]=std::bit_cast<int32_t>(0u-uint32_t(vertex[size_t(axis)]));
                                bounds.add(vertex);
                            }
                            t.scriptPieceCenters[size_t(piece)]=bounds.center();
                        }
                        for (const auto& child:object.children) self(self,child,index);
                    };
                    append(append,model.root,-1);
                }
                if(t.producesUnits() && script->scriptIndex("QueryBuildInfo")>=0)
                    t.productionScript=std::move(script);
            }
        }
    }
    // Intern here rather than asking callers to remember: loadDir is called once per
    // archive (unitscb then units) and this rebuilds from scratch each time, so it is
    // idempotent and cannot be left stale by a second load.
    internCategories();
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

int World::spawn(const UnitType* type, float x, float z, std::optional<float> heading, int player) {
    Unit u;
    u.groundGradeTick=tickCounter_;
    if (type) {                      // the type's standing orders are the unit's
        u.standingOrder = type->defaultStandingOrder;
        u.moveState = type->defaultMove;
        u.fireState = type->defaultFire;
        // The displayed stance is whichever button retail would light up.
        u.stance = u.fireState == 0 ? 2 : (u.moveState == 0 ? 1 : 0);
    }
    // Clamp to a valid player slot: every players_[u.player] index downstream is
    // unchecked, so an out-of-range owner would read/write past the vector.
    if (player < 0 || player >= int(players_.size())) {
        std::fprintf(stderr, "sim: spawn player %d out of range [0,%d) -> clamped to 0\n",
                     player, int(players_.size()));
        player = 0;
    }
    if (retailAllocation_) {
        if (!retailEntityPools_) throw std::runtime_error("restored entity pools absent");
        const auto [first,capacity]=(*retailEntityPools_)[size_t(player)];
        std::vector<bool> occupied(size_t(capacity),false);
        int allocated=0;
        for (const auto& member:units_) {
            if (member.id>=first && member.id<first+capacity && member.deadFor<kRetiredTicks) {
                occupied[size_t(member.id-first)]=true;++allocated;
            }
        }
        const int slot=retailAllocateSlot(capacity,allocated,[&](int index){return occupied[size_t(index)];},
            [&]{return crtRand(0x5121dd);});
        if (slot<0) throw std::runtime_error("restored entity pool exhausted");
        u.id=first+slot;
        if (unit(u.id)) throw std::runtime_error("restored entity slot reuse lifecycle is not ported");
    } else u.id=nextId_++;
    players_[size_t(player)].unitCount++;   // keep the cap count exact within a tick
    players_[size_t(player)].built++;        // end-of-game "Units" column
    u.player = player;
    if (type && type->canSetStance && players_[size_t(player)].defensiveAi) {
        u.moveState=0;u.fireState=2;u.stance=1;u.standingOrder=1;
    }
    u.type = type;
    if (retailAllocation_ && players_[size_t(player)].buildCache) {
        const auto& entries=players_[size_t(player)].buildCache->entries;
        auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.type==type;});
        if (entry!=entries.end() && entry->construction) u.constructionEmitter=entry->construction->emitter;
    }
    // 511ba2..511cbb initializes both angle fields before speed variation.
    const uint32_t spread=type ? type->buildAngle : 0;
    const uint16_t birthHeading=uint16_t(32768u+(type ? type->orientation : 0u)-spread/2+gameRand(spread));
    u.variationPhase=uint16_t(gameRand(65536));
    u.baseSpeed = type ? type->maxVel : Fixed();
    if (u.baseSpeed > Fixed()) {
        u.baseSpeed = individualSpeed(u.baseSpeed, gameRand(201));
    }
    u.x = Fixed::fromFloat(x);   // boundary: callers still speak float
    u.z = Fixed::fromFloat(z);
    u.heading = heading ? bamFromRadians(*heading) : retailHeadingToPort(birthHeading);
    u.tickStartHeadingBam=uint16_t(u.heading.v);
    u.groundMoveTick=tickCounter_;
    if (type && !type->canFly) u.groundY=surfaceHeight(u,tickCounter_,&u.groundPitch,&u.groundRoll);
    if (type && type->canFly) {
        u.flightSectorX=u.x.v>>23;u.flightSectorZ=u.z.v>>23;
        const int cx=std::clamp(u.x.floorInt()/16,0,std::max(0,terW_-1));
        const int cz=std::clamp(u.z.floorInt()/16,0,std::max(0,terH_-1));
        u.flightY=Fixed::fromInt(heights_.empty() ? 0 : heights_[size_t(cz)*terW_+cx]);
        u.flightNavigation={{u.x.v,u.flightY.v,u.z.v},{},portHeadingToRetail(u.heading)};
    }
    u.hp = Fixed::fromFloat(type ? type->maxHp : 100.0f);
    u.mana = type ? type->maxMana : 0;   // casters start with a full pool
    u.active = type ? type->activateWhenBuilt : true;
    u.homeX = Fixed::fromFloat(x);
    u.homeZ = Fixed::fromFloat(z);
    units_.push_back(u);
    ++spawnGeneration_;
    bodyIndexValid_=false;
    if (type && type->script()) {
        auto [it,inserted]=unitScripts_.try_emplace(u.id,*type->script());
        (void)inserted;
        if (unitScriptById_.size()<=size_t(u.id)) unitScriptById_.resize(size_t(u.id)+1,nullptr);
        unitScriptById_[size_t(u.id)]=&it->second;
        it->second.state.vm.start(*type->script(),type->script()->scriptIndex("Create"));
    }
    return u.id;
}

Unit* World::unit(int id) {
    // Ordinary matches append sequential IDs, allowing an O(1) lookup. This is called per unit
    // every tick in combat (unit(targetId)); the old linear scan made a big battle
    // O(n^2) and stalled the sim to single-digit fps. The scan stays as a fallback
    // for restored retail slots, whose IDs are selected from free player pools.
    if (id >= 1 && size_t(id) <= units_.size()) {
        Unit& u = units_[size_t(id) - 1];
        if (u.id == id) return &u;
    }
    for (auto& u : units_)
        if (u.id == id) return &u;
    return nullptr;
}

Fixed World::surfaceHeight(const Unit& u,uint32_t clock,uint16_t* pitch,uint16_t* roll) const {
    if (!u.type || u.type->canFly || heights_.empty()) return u.groundY;
    const auto& type=*u.type;
    auto terrain=[&](int32_t x,int32_t z) {
        return retailTerrainHeight(x,z,terW_,terH_,[&](int cx,int cz) {
            return heights_[size_t(cz)*size_t(terW_)+size_t(cx)];
        });
    };
    if (type.upright) return Fixed::raw(retailUprightHeight(terrain(u.x.v,u.z.v),
        type.canHover,uint8_t(seaLevel_),uint8_t(type.waterline)));
    if (type.floater) return Fixed::raw(retailFloatingHeight(uint8_t(seaLevel_),uint8_t(type.waterline)));
    // Structures have no moving support controller. Their birth position still
    // needs the terrain Y supplied by native placement (511f50 copies XYZ).
    if (type.isStructure()) return Fixed::fromInt(terrain(u.x.v,u.z.v));
    if (!type.groundSupport) return u.groundY;
    Fixed effective=u.baseSpeed;
    if (u.groundTerrainFlags&0x800) effective=effective*type.roadMult;
    else if (u.groundTerrainFlags&0x1000) effective=effective*type.waterMult;
    return Fixed::raw(retailSupportedHeight(u.x.v,u.z.v,u.groundY.v,portHeadingToRetail(u.heading),
        *type.groundSupport,terrain,[&](size_t corner,int h) {
            return type.canHover && u.alive()?
                retailHoverHeightCorner(h,uint8_t(seaLevel_),corner,clock,u.variationPhase,
                    u.speed.v,effective.v,tickCounter_-u.groundMoveTick):h;
        },type.bankScale.v,type.pitchScale.v,pitch,roll));
}

void World::updateGroundTerrainFlags(Unit& u) const {
    u.groundTerrainFlags=onRoad(u.x,u.z,u.type->footX,u.type->footZ)?0x800:0;
    if (u.groundY.floorInt()<seaLevel_) u.groundTerrainFlags|=0x1000;
}

Fixed World::groundTerrainMultiplier(const Unit& u) const {
    return u.groundTerrainFlags&0x800?u.type->roadMult:
           u.groundTerrainFlags&0x1000?u.type->waterMult:Fixed::fromInt(1);
}

void World::brakeGround(Unit& u,std::optional<Bam> facing) {
    const Fixed multiplier=groundTerrainMultiplier(u);
    u.turnReqBam=0;
    u.speed=fxMin(retailGroundSpeedCap(u.baseSpeed*multiplier,u.groundPitch,u.groundSpeedMode),
        fxMax(Fixed(),u.speed-u.type->brake*multiplier));
    if (facing && u.speed==Fixed()) {
        const int32_t diff=retailTurnRequest(*facing,u.heading);
        // 4d91b0 scales the unsigned 16-bit pivot rate, truncates toward zero,
        // then retains its low word before clamping the signed request.
        const int32_t rate=uint16_t(int64_t(uint16_t(u.type->turnInPlaceRate))*multiplier.v/65536);
        u.heading=u.heading+Bam(std::clamp(diff,-rate,rate));
        u.turnReqBam=diff;
    }
    const auto step=retailGroundStep(u.heading,u.speed);
    commitGroundStep(u,step.s,step.c);
}

SinCos World::steerGround(Unit& u,RetailSteeringPoint start,RetailSteeringPoint end,
                         RetailSteeringPoint next,Fixed maximum) {
    const Fixed multiplier=groundTerrainMultiplier(u);
    const auto aim=retailSteeringPoint(u.x,u.z,start,end,u.groundMovementMode);
    // Convert the native direction explicitly: coincident points request
    // native heading zero, which is a half-turn in World's heading convention.
    const int direction=retailDirection(u.x-aim.x,u.z-aim.z).v;
    const auto wanted=retailHeadingToPort(direction);
    const int32_t diff=retailTurnRequest(wanted,u.heading);
    const uint16_t rate=uint16_t(int64_t(uint16_t(u.type->turnRate))*multiplier.v/65536);
    u.heading=u.heading+Bam(std::clamp(diff,-int32_t(rate),int32_t(rate)));
    u.turnReqBam=diff;
    const Fixed delta=retailGroundAcceleration(u.x,u.z,start,end,next,u.heading,u.speed,
        maximum*multiplier,u.type->accel*multiplier,u.type->brake*multiplier,uint16_t(rate),
        u.groundMovementMode==0 ? &aim : nullptr,u.groundMovementMode==0 ? direction : -1);
    u.speed=fxMin(retailGroundSpeedCap(maximum*multiplier,u.groundPitch,u.groundSpeedMode),
                  fxMax(Fixed(),u.speed+delta));
    return retailGroundStep(u.heading,u.speed);
}

void World::commitGroundStep(Unit& u,Fixed dx,Fixed dz) {
    if (dx==Fixed() && dz==Fixed()) return;
    u.groundMoveTick=tickCounter_;
    const int fx=u.type->footX,fz=u.type->footZ;
    const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
    const Fixed px=u.x+dx,pz=u.z+dz;
    const int nx=footprintOrigin(px,fx),nz=footprintOrigin(pz,fz);
    if (nx==ox && nz==oz) { u.x=px;u.z=pz;updateBodyIndex(u);return; }
    bool clear;
    if (!mapPlacementCells_.empty()) clear=mobilePlacement(u,nx,nz,false);
    else {
        // Legacy terrain-only harnesses have no feature plane. Loaded matches
        // use the authoritative rectangular map/body placement above.
        const auto& grid=navFor(u.type);const int foot=std::clamp(std::max(fx,fz),1,15);
        clear=(grid.empty() || grid.fits(footprintCell(px,foot),footprintCell(pz,foot),foot)) &&
            cellFree(px,pz,u.id,foot);
    }
    if (clear) {
        u.bodyBlockStreak=0;
        refreshMovingSearchBody(u);
        if (occW_>0) {
            for (int z=oz;z<oz+fz;++z) for (int x=ox;x<ox+fx;++x)
                if (x>=0 && z>=0 && x<occW_ && z<occH_) {
                    auto& owner=occ_[size_t(z)*occW_+x];
                    if (owner==u.id) owner=0;
                }
            for (int z=nz;z<nz+fz;++z) for (int x=nx;x<nx+fx;++x)
                if (x>=0 && z>=0 && x<occW_ && z<occH_) occ_[size_t(z)*occW_+x]=u.id;
        }
        u.x=px;u.z=pz;
    } else {
        u.bodyBlockStreak=std::min(u.bodyBlockStreak+1,2);
        const bool repeated=u.bodyBlockStreak>=2;
        u.x=footprintClamp(u.x,px,fx,repeated);
        u.z=footprintClamp(u.z,pz,fz,repeated);
        Fixed base=u.baseSpeed;
        if (u.groundTerrainFlags&0x800) base=base*u.type->roadMult;
        else if (u.groundTerrainFlags&0x1000) base=base*u.type->waterMult;
        u.speed=fxMin(u.speed,Fixed::raw(base.v/(repeated?5:2)));
    }
    updateBodyIndex(u);
}

bool World::mobilePlacement(const Unit& subject,int x,int z,bool allowMoving) const {
    const auto& type=*subject.type;
    if (mapPlacementCells_.size()!=size_t(hW_)*hH_ || mapPlacementCells_.empty())
        throw std::runtime_error("mobile placement requires loaded map feature data");
    // A placement query observes one immutable body layout. Reuse its rectangle
    // across all footprint cells instead of rescanning the army for each cell.
    // Build lazily: out-of-map requests return before asking for any cells.
    SearchBodyRect bodies;
    auto cellAt=[&](int cx,int cz) {
        const auto& source=mapPlacementCells_.at(size_t(cz)*hW_+cx);
        RetailPlacementCell result;
        result.low=source.low;
        result.high=std::max({heights_[size_t(cz)*hW_+cx],heights_[size_t(cz)*hW_+cx+1],
                             heights_[size_t(cz+1)*hW_+cx],heights_[size_t(cz+1)*hW_+cx+1]});
        result.feature=source.feature;
        if (source.feature<0xfffa || source.feature==0xfffe) {
            const int ox=cx-(source.feature==0xfffe?source.backX:0);
            const int oz=cz-(source.feature==0xfffe?source.backZ:0);
            const auto index=mapPlacementCells_.at(size_t(oz)*hW_+ox).feature;
            bool blocking=index<mapPlacementTypes_.size() && mapPlacementTypes_[index].blocking;
            // Placement only observes the resolved feature's blocking bit.
            result.feature=blocking?0:0xffff;
        }
        if (bodies.cells.empty()) bodies=searchBodyRect(x,z,type.footX,type.footZ);
        const auto* body=bodies.cells[size_t(cz-z)*type.footX+cx-x];
        if (body) result.entity=uint16_t(body->id);
        return result;
    };
    const int maximum=type.canFly || type.domain!=UnitType::Domain::Ground
        ? 10000 : (type.maxWaterDepth>0?type.maxWaterDepth:20);
    const int minimum=type.domain==UnitType::Domain::Water
        ? (type.minWaterDepth>0?type.minWaterDepth:13) : -10000;
    return retailMobilePlacement(x,z,type.footX,type.footZ,hW_,hH_,seaLevel_,maximum,minimum,
        type.maxSlope,type.maxWaterSlope,uint16_t(subject.id),allowMoving,1,cellAt,
        [](uint16_t){return 0x20u;},[&](uint16_t id) {
            const auto* u=unit(id);
            return RetailPlacementEntity{u!=nullptr,u && u->alive(),u && !u->type->isStructure(),id};
        });
}

void World::setMapPlacementFeatures(const std::vector<uint16_t>& raw,
                                    std::vector<RetailMapFeatureType> types) {
    mapPlacementTypes_=std::move(types);
    mapPlacementCells_.assign(size_t(hW_)*hH_,{});
    if (raw.size()!=mapPlacementCells_.size())
        throw std::runtime_error("map placement feature dimensions differ from terrain");
    auto cell=[&](int x,int z)->RetailMapFeatureCell& {
        return mapPlacementCells_.at(size_t(z)*hW_+x);
    };
    for (int z=0;z<hH_;++z) for (int x=0;x<hW_;++x) {
        auto& c=cell(x,z);
        c.height=heights_[size_t(z)*hW_+x];
        c.low=std::min({c.height,heights_[size_t(z)*hW_+std::min(x+1,hW_-1)],
            heights_[size_t(std::min(z+1,hH_-1))*hW_+x],
            heights_[size_t(std::min(z+1,hH_-1))*hW_+std::min(x+1,hW_-1)]});
        // 50f789: roads set the separate terrain flag, not the feature index.
        if (raw[size_t(z)*hW_+x]==0xfffc) c.feature=0xfffc;
    }
    for (int z=0;z<hH_;++z) for (int x=0;x<hW_;++x) {
        const uint16_t index=raw[size_t(z)*hW_+x];
        if (index>=mapPlacementTypes_.size()) continue;
        retailMapInstall(hW_,hH_,x,z,index,mapPlacementTypes_,cell,[](int,int,uint16_t){});
    }
    if (hW_>=2 && hH_>=16)
        retailMapBoundary(hW_,hH_,hH_*16,seaLevel_,false,cell);
}

void World::setTerrain(const std::vector<uint8_t>& heights, int w, int h, int seaLevel,
                       const std::vector<uint16_t>* features) {
    mapPlacementCells_.clear();mapPlacementTypes_.clear();corpseFootprints_.clear();
    paths_.clear();
    searchGrades_.clear(); activeSearchGrade_=-1;
    // The fog worker reads heights_; never let a reload pull the map out from under it.
    if (visRunning_) { visWorker_.join(); visRunning_ = false; visDone_.store(false); }
    // EVERY cached LoS mask is a function of the terrain we are about to replace, and
    // its contents are flat cell indices computed against the OLD visW_/visH_. Keeping
    // them across a map change is not merely stale fog: load a smaller map and those
    // indices address past the end of visBack_. The cache key is (sight, radar, cx, cz)
    // with no terrain identity in it, so nothing else would ever invalidate them.
    visMaskCache_.clear();
    visHavePass_ = false;   // the next pass is a first pass again (see updateVisibility)
    seaLevel_ = seaLevel;
    noSeaLevelTrigger_ = false;
    heights_ = heights;   // keep raw heights for fog line-of-sight
    hW_ = w; hH_ = h;
    explorationHeights_=retailExplorationHeights(w,h,uint8_t(seaLevel),
        [&](int x,int z){return heights_[size_t(z)*w+x];});
    navigationExplored_.assign(explorationHeights_.size(),0);
    restoredNavigationViewer_=-1;
    for (auto& u:units_) u.sightFootprint={};
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
    // Passability, retail's rule (see NavGrid::Limits). One grid per DOMAIN still,
    // with each domain's limits taken from the movement class that dominates it in
    // the shipped data: GROUND2/GROUND3 (67+20 of the 125 movers) for ground,
    // WATER* for water, HOVER2 for hover. Per-CLASS grids -- so a Catapult's
    // MaxSlope 15 differs from a Swordsman's 30 -- are the next step; this leg is
    // about the rule's SHAPE, which is where the connectivity was being lost.
    {
        NavGrid::Limits ground;      // GROUND2 / GROUND3
        ground.maxSlope = 30; ground.maxWaterSlope = 30;
        ground.maxWaterDepth = 20; ground.minWaterDepth = -10000;
        nav_ = NavGrid(heights, w, h, seaLevel, ground);

        NavGrid::Limits water;       // WATER* declare no limits: retail's defaults,
        water.maxSlope = 255; water.maxWaterSlope = 255;   // except they must be IN
        water.maxWaterDepth = 10000; water.minWaterDepth = 13;   // water to float.
        navWater_ = NavGrid(heights, w, h, seaLevel, water);

        NavGrid::Limits hover;       // HOVER2: crosses land and water alike
        hover.maxSlope = 30; hover.maxWaterSlope = 255;
        hover.maxWaterDepth = 10000; hover.minWaterDepth = -10000;
        navHover_ = NavGrid(heights, w, h, seaLevel, hover);
        // Direct naval shots see the water surface, not the seabed. Flatten
        // submerged heights before computing slopes so even a steep seabed or
        // shallow shoreline cannot become an artificial firing wall. Raised
        // land and the shared feature/building overlay still block direct shots.
        auto surface=heights;
        for (auto& height:surface) height=uint8_t(std::max(int(height),seaLevel));
        NavGrid::Limits sight=hover;
        navalSight_=NavGrid(surface,w,h,seaLevel,sight);
        // One obstacle overlay, shared by every grid. Buildings, blocking features
        // and wrecks go here rather than into a single grid's cells.
        obst_.assign(size_t(w) * size_t(h), 0);
        nav_.setObstacles(&obst_);
        navWater_.setObstacles(&obst_);
        navHover_.setObstacles(&obst_);
        navalSight_.setObstacles(&obst_);
        navClasses_.clear();
        navIdx_.clear();
    }
    // Projected cliff faces are rendering geometry, not extra navigation walls.
    // The retail Hunter capture traverses cells the former seven-row projection
    // heuristic blocked. Terrain limits and explicit feature blockers decide here.
    nav_.setRoads(&roads_);   // ground domain only: marches prefer highways
    // 0xFFFC cells are hard blockers (retail rates them impassable for every
    // movement domain AND rejects building placement on them -- icd 0x508190 /
    // 0x507705; they sit under castle wall/gate/dock art baked into terrain).
    if (features && features->size() == size_t(w) * size_t(h))
        for (int z = 0; z < h; ++z)
            for (int x = 0; x < w; ++x)
                if ((*features)[size_t(z) * w + x] == 0xFFFC)
                    // The SHARED overlay, not each grid's own cells. block() writes
                    // cells_, and buildNavClasses() -- which runs after setTerrain and
                    // builds the grids navFor() actually hands every unit in a real
                    // match -- constructs its grids from heights alone. Stamping the
                    // legacy grids published this to nobody from 9b037c6 onward, and
                    // these are the cells under castle wall, gate and dock art: units
                    // walked straight through them. 2538 cells on Inner Circle.
                    obst_[size_t(z) * size_t(w) + size_t(x)] = 1;
    // The overlay changed after setObstacles(), so clearance derived from it is
    // stale. The per-class grids are built after this point and start clean.
    nav_.markClearanceDirty();
    navWater_.markClearanceDirty();
    navHover_.markClearanceDirty();
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
    int cx = footprintOrigin(x, t.footX), cz = footprintOrigin(z, t.footZ);
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




void NavGrid::block(int cx, int cz, int w, int h, bool blocked) {
    for (int z = cz; z < cz + h; ++z)
        for (int x = cx; x < cx + w; ++x)
            if (x >= 0 && z >= 0 && x < w_ && z < h_)
                cells_[size_t(z) * w_ + x] = blocked ? 0 : 1;
    ++version_;   // walkability changed: component caches over this grid are stale
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
// cells (footCells clamps there), so every value >= 15 answers identically -- and
// the clamp is what bounds how far a walkability edit can propagate (see block()).
static constexpr uint16_t kClearMax = 15;

// Largest all-walkable square with each cell as its min (bottom-left) corner, by the
// classic DP from the far corner inward, saturated at kClearMax. Deterministic
// (pure function of cells_).
void NavGrid::rebuildClearance() const {
    clear_.assign(size_t(w_) * size_t(h_), 0);
    for (int z = h_ - 1; z >= 0; --z)
        for (int x = w_ - 1; x >= 0; --x) {
            if (!walkable(x, z)) continue;   // blocked (cells_ or the overlay) -> 0
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
void NavGrid::overlayChanged(int cx, int cz, int w, int h) {
    ++version_;   // walkability changed: component caches over this grid are stale
    if (!clearDirty_ && !clear_.empty())
        updateClearanceRect(cx, cz, w, h);
    else
        clearDirty_ = true;
}

void NavGrid::updateClearanceRect(int cx, int cz, int w, int h) const {
    int x0 = std::max(0, cx - int(kClearMax));
    int z0 = std::max(0, cz - int(kClearMax));
    int x1 = std::min(w_ - 1, cx + w - 1);
    int z1 = std::min(h_ - 1, cz + h - 1);
    for (int z = z1; z >= z0; --z)
        for (int x = x1; x >= x0; --x) {
            uint16_t v = 0;
            if (walkable(x, z)) {
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

NavGrid::NavGrid(const std::vector<uint8_t>& heights, int w, int h, int sea,
                 const Limits& lim)
    : w_(w), h_(h) {
    cells_.assign(size_t(w) * h, 1);
    terrainPenalty_.assign(size_t(w)*h,0);
    auto H = [&](int x, int z) {
        return int(heights[size_t(std::min(z, h - 1)) * size_t(w) + size_t(std::min(x, w - 1))]);
    };
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x) {
            // The cell's own quad, anchored down-right -- retail's exact sample set.
            int a = H(x, z), b = H(x + 1, z), c = H(x, z + 1), d = H(x + 1, z + 1);
            int lo = std::min(std::min(a, b), std::min(c, d));
            int hi = std::max(std::max(a, b), std::max(c, d));
            const int badSlope=lim.badSlope<0 ? lim.maxSlope/2 : lim.badSlope;
            const int badWaterSlope=lim.badWaterSlope<0 ? lim.maxWaterSlope/2 : lim.badWaterSlope;
            const int badMax=lim.badMaxWaterDepth==32768 ? lim.maxWaterDepth : lim.badMaxWaterDepth;
            const int badMin=lim.badMinWaterDepth==32768 ? lim.minWaterDepth : lim.badMinWaterDepth;
            auto grade=[&](bool road) {
                return retailTerrainGrade(lo,hi,sea,lim.maxWaterDepth,lim.minWaterDepth,badMax,badMin,
                    lim.maxSlope,badSlope,lim.maxWaterSlope,badWaterSlope,road);
            };
            const int ordinary=grade(false),road=grade(true);
            cells_[size_t(z)*w+x]=uint8_t(ordinary!=0);
            terrainPenalty_[size_t(z)*w+x]=uint8_t((ordinary==4 ? 1 : 0)|(road==4 ? 2 : 0));
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

bool NavGrid::lineFits(int x0, int z0, int x1, int z1, int foot) const {
    // Like lineClear, but asks whether a body of `foot` cells FITS at every cell the
    // line crosses -- and, on a diagonal step, at both cells it passes between, so a
    // shortcut can never squeeze a body through a corner the mover would be stopped by
    // (the movers themselves refuse diagonal corner-cutting; see the step loop in the
    // search). Integer Bresenham, so it is exact and identical on every peer.
    int dx = std::abs(x1 - x0), dz = -std::abs(z1 - z0);
    int sx = x0 < x1 ? 1 : -1, sz = z0 < z1 ? 1 : -1, err = dx + dz;
    for (int guard = 0; guard < 8192; ++guard) {
        if (!fits(x0, z0, foot)) return false;
        if (x0 == x1 && z0 == z1) return true;
        const int e2 = 2 * err;
        const bool stepX = e2 >= dz, stepZ = e2 <= dx;
        if (stepX && stepZ) {   // diagonal: both orthogonal neighbours must fit too
            if (!fits(x0 + sx, z0, foot) || !fits(x0, z0 + sz, foot)) return false;
        }
        if (stepX) { err += dz; x0 += sx; }
        if (stepZ) { err += dx; z0 += sz; }
    }
    return false;
}

// Floor-divide by the 16-unit cell size. A plain `/ 16` truncates toward zero, so
// every coordinate in the first cell left of the origin would land in cell 0 along
// with the first cell right of it.
static inline int navCellOf(int world) { return world >= 0 ? world / 16 : -((-world + 15) / 16); }

bool NavGrid::segmentFits(float wx0, float wz0, float wx1, float wz1, int foot) const {
    if (empty()) return true;

    // Integer world units. The sub-CELL position is what this exists to preserve;
    // sub-unit precision buys nothing here and integers keep the DDA exact.
    const int phase = foot % 2 ? 0 : 8;
    const int ix0 = Fixed::fromFloat(wx0).floorInt() + phase;
    const int iz0 = Fixed::fromFloat(wz0).floorInt() + phase;
    const int ix1 = Fixed::fromFloat(wx1).floorInt() + phase;
    const int iz1 = Fixed::fromFloat(wz1).floorInt() + phase;
    int cx = navCellOf(ix0), cz = navCellOf(iz0);
    const int ex = navCellOf(ix1), ez = navCellOf(iz1);

    if (!fits(cx, cz, foot)) return false;
    if (cx == ex && cz == ez) return true;

    const int dx = ix1 - ix0, dz = iz1 - iz0;
    const int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    const int stepZ = dz > 0 ? 1 : (dz < 0 ? -1 : 0);
    const long long adx = dx < 0 ? -dx : dx, adz = dz < 0 ? -dz : dz;

    // Distance to the first cell boundary on each axis, as a numerator over |d|.
    // Leaving them as a fraction (rather than dividing) keeps this in integers.
    long long tx = 0, tz = 0;
    if (stepX > 0)      tx = (long long)((cx + 1) * 16 - ix0);
    else if (stepX < 0) tx = (long long)(ix0 - cx * 16);
    if (stepZ > 0)      tz = (long long)((cz + 1) * 16 - iz0);
    else if (stepZ < 0) tz = (long long)(iz0 - cz * 16);

    for (int guard = 0; guard < 8192; ++guard) {
        if (cx == ex && cz == ez) return true;
        // Which boundary comes first? Compare tx/adx against tz/adz by cross-
        // multiplying, so no division and no float rounding enters the decision.
        const bool moveX = stepX != 0 && (stepZ == 0 || tx * adz < tz * adx);
        const bool moveZ = stepZ != 0 && (stepX == 0 || tz * adx < tx * adz);
        if (!moveX && !moveZ) {
            // Exactly through a corner. The body passes between two cells, and both
            // must admit it -- the same rule the movers enforce and lineFits applies
            // on a diagonal, so a shortcut cannot squeeze through a corner the unit
            // would be stopped by.
            if (!fits(cx + stepX, cz, foot) || !fits(cx, cz + stepZ, foot)) return false;
            cx += stepX; cz += stepZ;
            tx += 16; tz += 16;
        } else if (moveX) {
            cx += stepX;
            tx += 16;
        } else {
            cz += stepZ;
            tz += 16;
        }
        if (!fits(cx, cz, foot)) return false;
    }
    return false;
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









// A unit's footprint size in cells (square approximation) for footprint-aware nav.
static int footCells(const UnitType* t) {
    return t ? std::clamp(std::max(t->footX, t->footZ), 1, 15) : 1;
}

void World::replaceLeg(Unit& u, const std::vector<Order>& path) {
    if (u.orders.empty() || path.empty()) return;
    const size_t end = currentLeg(u.orders);
    const Order tmpl = u.orders[end];        // keep attack-move / patrol / flags
    std::vector<Order> next;
    next.reserve(path.size() + u.orders.size() - end - 1);
    for (size_t i = 0; i < path.size(); ++i) {
        Order o = path[i];
        if (i || !o.hasSegment) {
            o.segmentX = i ? path[i - 1].x : u.x;
            o.segmentZ = i ? path[i - 1].z : u.z;
        }
        o.hasSegment = true;
        o.attackMove = tmpl.attackMove;
        o.patrol = tmpl.patrol;
        o.targetId = tmpl.targetId;
        o.guard = tmpl.guard;
        o.load = tmpl.load;
        o.unload = tmpl.unload;
        o.transportPickup = tmpl.transportPickup;
        o.transportUnloadReleasePending = tmpl.transportUnloadReleasePending;
        o.transportUnloadTransferDeferred = tmpl.transportUnloadTransferDeferred;
        o.autoTarget = tmpl.autoTarget;
        o.issuedTick = tmpl.issuedTick;
        o.goal = (i + 1 == path.size());
        o.navigationExhausted=false;
        o.navigationConsumed=false;
        if (o.goal) {
            o.clickX = tmpl.clickX; o.clickZ = tmpl.clickZ;
            o.groundMission = tmpl.groundMission;
            o.transportUnloadApproach = tmpl.transportUnloadApproach;
            o.mission = tmpl.mission;
            o.transportMission = tmpl.transportMission;
            o.transportTicks = tmpl.transportTicks;
            o.transportPassenger = tmpl.transportPassenger;
            o.transportApproachAttempts = tmpl.transportApproachAttempts;
            o.transportX = tmpl.transportX; o.transportY = tmpl.transportY; o.transportZ = tmpl.transportZ;
            o.missionRadius = tmpl.missionRadius;
            o.missionTarget = tmpl.missionTarget;
            o.groundResponse = tmpl.groundResponse;
            o.controller = tmpl.controller;
            o.park = tmpl.park;
            o.buildType = tmpl.buildType;
            o.buildRectangle = tmpl.buildRectangle;
            o.buildX = tmpl.buildX; o.buildZ = tmpl.buildZ;
        }
        next.push_back(o);
    }
    next.insert(next.end(), u.orders.begin() + long(end) + 1, u.orders.end());
    u.orders.swap(next);

}

void World::dropLeg(Unit& u) {
    if (u.orders.empty()) return;
    // The request belongs to the departing movement controller, not to the
    // unit's next order. Retail retires it when that controller is replaced
    // (4d4d40 -> navigator -> 415f30), before any later route delivery.
    cancelPath(u);
    const size_t end = currentLeg(u.orders);
    if (u.orders[end].groundMission || u.orders[end].buildRectangle) {
        // Detachment keeps a recent admission, otherwise clears it to zero.
        // A -1 sentinel suppresses the next mission's native retry RNG draw.
        if (u.orders[end].controller && uint32_t(u.routeStamp)<=tickCounter_-6u) u.routeStamp=0;
    } else u.routeStamp=-1;
    u.orders.erase(u.orders.begin(), u.orders.begin() + long(end) + 1);
}

// A move/attack/patrol order aimed at a PRODUCTION BUILDING sets its rally instead:
// the building cannot go anywhere itself, but the units it makes can. Returns true
// when the order was consumed as a rally, so the caller stops there.
//
// The orders are stored raw and never enter the mover -- see Unit::rally. `queue`
// appends, so a player can lay out "move here, then fight-move there, then patrol"
// and every unit off the line follows the whole plan.
bool World::setRally(Unit& u, const Order& o, bool queue) {
    if (!u.type || !u.type->producesUnits()) return false;
    if (!queue) u.rally.clear();
    u.rally.push_back(o);
    u.rally.back().goal = true;          // each issued rally step is its own leg
    if (u.rally.size() > 32) u.rally.erase(u.rally.begin());   // bound it
    return true;
}

static void resetGroundSegment(const Unit& u,Order& goal) {
    // 4e2500/4e2820 retain the mission point separately from the navigator's
    // footprint-aligned endpoint. A retry must use the mission, not a partial
    // route's last corner, when constructing its new direct segment.
    if (!goal.missionTarget) goal.missionTarget=std::pair{goal.x,goal.z};
    goal.x=footprintWaypoint(footprintCell(goal.missionTarget->first,u.type->footX),u.type->footX);
    goal.z=footprintWaypoint(footprintCell(goal.missionTarget->second,u.type->footZ),u.type->footZ);
    goal.segmentX=Fixed::fromInt(u.x.floorInt());
    goal.segmentZ=Fixed::fromInt(u.z.floorInt());
    goal.hasSegment=true;
    goal.navigationConsumed=false;
}

static bool retainGroundRoute(const Unit& u,const Order& goal) {
    const auto target=goal.missionTarget.value_or(std::pair{goal.x,goal.z});
    const Fixed gx=footprintWaypoint(footprintCell(target.first,u.type->footX),u.type->footX);
    const Fixed gz=footprintWaypoint(footprintCell(target.second,u.type->footZ),u.type->footZ);
    // 4e5563..4e55e9 uses signed wrapped fixed-point differences, x87 double
    // square roots truncated to integers, then a signed doubled-distance test.
    const auto distance=[&](Fixed x,Fixed z) {
        const int32_t dx=std::bit_cast<int32_t>(uint32_t(x.v)-uint32_t(gx.v));
        const int32_t dz=std::bit_cast<int32_t>(uint32_t(z.v)-uint32_t(gz.v));
        return uint32_t(int64_t(std::sqrt(double(dx)*dx+double(dz)*dz)));
    };
    const uint32_t endpoint=distance(Fixed::fromInt(int16_t(goal.x.floorInt())),
                                     Fixed::fromInt(int16_t(goal.z.floorInt())));
    return std::bit_cast<int32_t>(endpoint*2u)<std::bit_cast<int32_t>(distance(u.x,u.z));
}

void World::order(int unitId, float x, float z, bool queue) {
    Unit* u = unit(unitId);
    // isStructure(), NOT canMove: the Keep and both Taros/Veruna walls declare
    // canmove=1 with no velocity (the CLAUDE.md gotcha). Gating on canMove let a
    // BUILDING accept a move order -- it could not go anywhere, but the mover still
    // turned its heading toward the goal, so you could spin a keep by right-clicking.
    if (!u || !u->alive() || !u->type) return;
    if (u->type->isStructure()) { setRally(*u, Order{Fixed::fromFloat(x), Fixed::fromFloat(z), 0}, queue); return; }
    // A replacement owns a fresh search, even for another click in the same
    // cell. Cadence retries within the same order may retain search progress.
    if (!queue) {
        cancelPath(*u);
        u->orders.clear();
    }
    auto markGoal = [&] {
        if (u->orders.empty()) return;
        u->orders.back().goal = true;
        u->orders.back().issuedTick = tickCounter_;
    };
    if (u->type->canFly) {
        u->orders.push_back({Fixed::fromFloat(x), Fixed::fromFloat(z), 0});
        markGoal();
        return;
    }
    // Retail move validation/submission retains the requested world point even
    // on blocked terrain. The search and mission radius determine the reachable
    // approach; choosing a nearby walkable cell here changes the search's goal.
    // The navigator starts with a direct segment while its search is pending.
    u->orders.push_back({Fixed::fromFloat(x), Fixed::fromFloat(z), 0});
    u->orders.back().clickX = Fixed::fromFloat(x);
    u->orders.back().clickZ = Fixed::fromFloat(z);
    u->orders.back().groundMission = true;
    // 4d6c40 retains the point-move definition flags when a point is supplied.
    u->orders.back().mission.flags = 0x3000400u;
    u->orders.back().controller = ++nextMovementController_;
    // The interim navigator segment starts at integer world coordinates.
    // Captured retail routes preserve this anchor until the path is replaced.
    if (u->orders.size() == 1) resetGroundSegment(*u,u->orders.back());
    markGoal();
    // Only ask for a route when this order is the one the unit is about to
    // WALK. Requests are keyed by unit id, so asking for a queued order cancels
    // the pending request for the leg in progress and then installs the queued
    // destination's route into that leg -- the unit sets off for the last thing
    // you queued and skips everything before it. Orders behind the current one
    // get their route when they become current, via the retry sweep in tick().
    if (!queue || u->orders.size() == 1) {
        // Replacing the controller cancels its work, but does not bypass the
        // fifteen-tick admission interval after a very recent search.
        if (u->routeStamp<0 || uint32_t(u->routeStamp)<=tickCounter_-6u) u->routeStamp=0;
        requestPath(*u, x, z);
    }
}

// Drop any path search still queued for this unit. Call it wherever a unit's order
// list is REPLACED wholesale and nothing immediately re-requests, because a search
// outlives the orders that asked for it: the PathService keys on the unit id, not on
// the order it came from.
//
// A stale route is not merely wasted work. When it lands, replaceLeg() rebuilds the
// current leg out of its waypoints -- and currentLeg() falls through to the LAST order
// when nothing carries `goal`, so for a bare command list the leg it rewrites is the
// command itself. replaceLeg carries only movement flags across, so `load` and `unload`
// are dropped: the transport quietly stops being a transport and drives off along a
// route to wherever it was going before. Both were reachable by changing your mind
// while a route was in flight.
void World::cancelPath(Unit& u) {
    static const bool kPqLog = std::getenv("TAK_PATHLOG") != nullptr;
    if (kPqLog) std::printf("[pq] t=%u id=%d CANCEL\n", tickCounter_, u.id);
    paths_.cancel(u.id);
    // A new destination earns the cheap tracer again. This deliberately does NOT live in
    // requestPath: that is also the once-a-second re-anchor, so clearing there wiped the
    // detour history on every re-ask and the fallback could never accumulate -- the
    // trigger fired and was forgotten within the same second, and the benchmark showed
    // not a single number moving.
}

Order* World::groundMissionOrder(Unit& u) {
    if (!u.type || u.type->canFly || u.orders.empty()) return nullptr;
    const auto plain = [](const Order& o) {
        return !o.targetId && !o.attackMove && !o.patrol && !o.guard && !o.load &&
               (!o.unload || o.transportUnloadApproach) &&
               !o.buildType && !o.reclaimFeat && !o.repairTarget && !o.wait && !o.waitAttack;
    };
    if (!plain(u.orders.front())) return nullptr;
    Order& goal = u.orders[currentLeg(u.orders)];
    return goal.groundMission && plain(goal) ? &goal : nullptr;
}

Order* World::navigationMissionOrder(Unit& u) {
    if (auto* goal=groundMissionOrder(u)) return goal;
    if (!u.type || u.type->canFly || u.orders.empty()) return nullptr;
    auto& goal=u.orders[currentLeg(u.orders)];
    return goal.buildRectangle || goal.load ? &goal : nullptr;
}

bool World::groundMissionAccepts(const Unit& u,const Order& goal) {
    const int radius=std::bit_cast<int32_t>(goal.missionRadius+4u);
    const RetailCircleGoal circle{footprintOrigin(goal.missionTarget ? goal.missionTarget->first : goal.x,u.type->footX),
        footprintOrigin(goal.missionTarget ? goal.missionTarget->second : goal.z,u.type->footZ),radius,retailCircleRadiusSquared(radius)};
    return goal.park && goal.park->ring
        ? goal.park->ring->accepts(footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ))
        : circle.accepts(footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ));
}

int World::flightGround(const Unit& u) const {
    // 5066f0 relinks the center sector only when the footprint changes cells.
    // Each tile stores the maximum of its four corners, including the far edge.
    if (heights_.empty()) return 0;
    const int sx=std::clamp(u.flightSectorX,0,(terW_-1)/8);
    const int sz=std::clamp(u.flightSectorZ,0,(terH_-1)/8);
    int height=seaLevel_;
    for (int cz=std::max(0,(sz-1)*8);cz<std::min(terH_,(sz+2)*8+1);++cz)
        for (int cx=std::max(0,(sx-1)*8);cx<std::min(terW_,(sx+2)*8+1);++cx)
            height=std::max(height,int(heights_[size_t(cz)*terW_+cx]));
    return height;
}

static bool plainFlightPatrol(const Unit& u) {
    return u.type && u.type->canFly && !u.type->isBuilder && u.type->weapons.empty() &&
        !u.orders.empty() && (u.orders.front().patrol || u.orders.front().flightMoveMission) && !u.orders.front().targetId;
}

void World::tickFlightPatrol(Unit& u) {
    // A newly activated patrol may append its return anchor. Keep the
    // dispatcher's reference to the current mission stable during that append.
    u.orders.reserve(u.orders.size()+1);
    struct Host {
        World& w; Unit& u;
        bool enabled() const { return u.alive() && !u.incapacitated() && !u.underConstruction && !u.embarked(); }
        RetailMissionState* head() { return plainFlightPatrol(u) ? &u.orders.front().mission : nullptr; }
        bool hasNext(const RetailMissionState&) const { return u.orders.size()>1; }
        void idle() {}
        uint32_t random(int n) { return w.pathRand(uint32_t(n)); }
        void remove(RetailMissionState&) { u.orders.erase(u.orders.begin()); }
        void rotate(RetailMissionState&) {
            const auto order=u.orders.front();
            u.orders.erase(u.orders.begin()); u.orders.push_back(order);
        }
        void clear() { u.orders.clear(); }
        int handle(RetailMissionState& m,uint32_t events) {
            if ((m.flags&0x8000000u) && random(10)==0) m.flags&=~0x8000000u;
            if (u.orders.front().flightMoveMission) {
                auto& order=u.orders.front();
                if (m.stage==0) {
                    if (auto script=w.unitScripts_.find(u.id);script!=w.unitScripts_.end() && !script->second.activated) {
                        script->second.activated=true;w.notifyUnitScript(u,"Activate");
                    }
                    w.notifyUnitScript(u,"BeginFlight");
                    m.pending&=~0x700u; return 1;
                }
                if (m.stage==1) {
                    order.x=footprintWaypoint(footprintCell(order.x,u.type->footX),u.type->footX);
                    order.z=footprintWaypoint(footprintCell(order.z,u.type->footZ),u.type->footZ);
                    order.flightGoal=RetailFlightGoal{{order.x.v,u.flightY.v,order.z.v},0x30,0,int16_t((random(5)+5)*16)};
                    m.waitMask=0x700;m.sleep(w.tickCounter_,random(10)+5);return 1;
                }
                if (m.stage==2) {
                    if (events&0x700) return 1;
                    m.waitMask=0x700;m.sleep(w.tickCounter_,random(10)+5);return 2;
                }
                if (m.stage==3 && u.orders.size()>1) return 5;
                throw std::runtime_error("unsupported terminal flight move mission");
            }
            if (m.stage==0) {
                const bool anchored=std::any_of(u.orders.begin(),u.orders.end(),[](const Order& o) {return (o.mission.flags&0x4000u)!=0;});
                if (!anchored) {
                    Order anchor;anchor.x=u.x;anchor.z=u.z;anchor.goal=anchor.patrol=true;
                    anchor.mission.flags=0x1000412u;anchor.issuedTick=w.tickCounter_;
                    u.orders.push_back(anchor);
                }
                m.flags|=0x4000u;
                if (auto script=w.unitScripts_.find(u.id);script!=w.unitScripts_.end() && !script->second.activated) {
                    script->second.activated=true;w.notifyUnitScript(u,"Activate");
                }
                w.notifyUnitScript(u,"BeginFlight");
                m.pending&=~0x700u; return 1;
            }
            if (m.stage==1) {
                auto& order=u.orders.front();
                const RetailFlightVector waypoint{order.x.v,u.flightY.v,order.z.v};
                order.flightGoal=retailFlightPatrolGoal({u.x.v,u.flightY.v,u.z.v},waypoint,
                    u.type->maxVel.v,[&](int n) { return random(n); });
                m.sleep(w.tickCounter_,1);
                return 1;
            }
            if (m.stage==2) {
                if (events&0x700) { m.stage=1; return 6; }
                m.waitMask|=0x700;
                m.sleep(w.tickCounter_,random(10)+5);
                return 2;
            }
            return 7;
        }
    } host{*this,u};
    retailDispatchMissions(tickCounter_,u.missionEvents,host);
}

void World::tickFlightMovement(Unit& u, bool persistent) {
    auto& order=u.orders.front();
    if (order.guard) {
        const auto* target=unit(order.targetId);
        if (target && fxLen(target->x-u.x,target->z-u.z)<=Fixed::fromInt(70)) return;
    }
    u.flightGroundMode=2;
    const int cruise=flightGround(u)+u.type->cruiseAlt;
    if (!order.flightGoal) {
        RetailFlightGoal goal;
        goal.point={order.x.v,Fixed::fromInt(cruise).v,order.z.v};
        order.flightGoal=goal;
    }
    auto& goal=*order.flightGoal;
    // Pickup pursues the passenger during approach, then uses a separate
    // departure point during transfer, just like unloading.
    const bool pickupDeparture=order.transportPickup && order.transportMission.stage==2;
    if (!order.patrol && !order.unload && !pickupDeparture) {
        goal.point.x=order.x.v; goal.point.z=order.z.v;
    }
    const bool pickupPursuit=order.transportPickup && order.transportMission.stage==1;
    if (!(goal.flags&8) && !pickupPursuit) goal.point.y=Fixed::fromInt(std::min(cruise,511)).v;
    const RetailFlightVector position{u.x.v,u.flightY.v,u.z.v};
    u.flightNavigation=retailFlightNavigation(position,u.flightNavigation.destination,goal.point,
        u.flightNavigation.heading,Fixed::fromInt(cruise).v,false,(goal.flags&0x40)!=0,goal.heading);
    // 524c10 posts arrival for a satisfied controller. 4e41b0 keeps
    // passenger pursuit alive; its mission consumes the event next tick.
    if(pickupPursuit && goal.accepts(position))order.transportMission.pending|=0x100;
    if((order.unload || pickupDeparture) && goal.accepts(position)) {
        // Native point controllers post arrival, then detach. Pursuit above
        // retains its passenger reference and therefore posts only arrival.
        order.transportMission.pending|=0x500;
        order.flightGoal.reset();
        tickFlightBody(u);
        return;
    }
    if (!persistent && goal.accepts(position)) {
        if (order.landing) {
            u.missionEvents|=0x100;
            u.orders.erase(u.orders.begin());
            return;
        }
        const bool flyingBuild=u.retailBuild && u.retailBuild->flying;
        if (!flyingBuild && (order.buildType || order.reclaimFeat || order.repairTarget)) return;
        // Point controllers used by both move and patrol are nonpersistent:
        // arrival posts 0x100 and releasing the controller posts 0x400.
        if (flyingBuild) u.retailBuild->mission.pending|=0x500;
        else if (plainFlightPatrol(u)) order.mission.pending|=0x500;
        else {
            Order done=order;
            u.orders.erase(u.orders.begin());
            if (done.patrol) { done.flightGoal.reset(); u.orders.push_back(done); }
            return;
        }
    }
    tickFlightBody(u);
}

void World::tickRetainedFlightMovement(Unit& u) {
    if (!u.retainedFlightGoal) return;
    auto& goal=*u.retainedFlightGoal;
    u.flightGroundMode=2;
    const int cruise=flightGround(u)+u.type->cruiseAlt;
    const RetailFlightVector position{u.x.v,u.flightY.v,u.z.v};
    const bool arrived=u.retainedFlightControllerActive && goal.accepts(position);
    if (arrived) {
        // The controller's arrival and release notifications are now unit
        // events: the VTOL_UNLOAD mission has already detached from them.
        u.missionEvents|=0x500;
        u.retainedFlightControllerActive=false;
    } else {
        u.flightNavigation=retailFlightNavigation(position,u.flightNavigation.destination,goal.point,
            u.flightNavigation.heading,Fixed::fromInt(cruise).v,false,(goal.flags&0x40)!=0,goal.heading);
    }
    tickFlightBody(u);
    notifyFlightOccupancy(u);
    if (!u.retainedFlightControllerActive && u.speed<=Fixed() &&
        u.flightVelocity.x==0 && u.flightVelocity.y==0 && u.flightVelocity.z==0)
        u.retainedFlightGoal.reset();
}

void World::tickFlightBody(Unit& u) {
    const RetailFlightVector position{u.x.v,u.flightY.v,u.z.v};
    if (u.baseSpeed<=Fixed()) { u.flightVelocity={}; u.speed=Fixed(); return; }
    const uint16_t heading=portHeadingToRetail(u.heading);
    const auto previousVelocity=u.flightVelocity;
    u.flightVelocity=retailFlightVelocity(u.flightVelocity,position,
        u.flightNavigation.destination,u.flightNavigation.velocity,u.speed.v,u.baseSpeed.v,
        u.type->accel.v,u.type->brake.v,heading);
    const int diff=int16_t(uint16_t(u.flightNavigation.heading-heading));
    u.heading=u.heading+Bam(std::clamp(diff,-u.type->turnRate,u.type->turnRate));
    u.turnReqBam=diff;
    const auto delta=[](int32_t next,int32_t previous) {
        return std::bit_cast<int32_t>(uint32_t(next)-uint32_t(previous));
    };
    const auto attitude=retailFlightAttitude(u.flightAcceleration,
        {delta(u.flightVelocity.x,previousVelocity.x),delta(u.flightVelocity.y,previousVelocity.y),
         delta(u.flightVelocity.z,previousVelocity.z)},portHeadingToRetail(u.heading),
        u.type->bankScale.v,u.type->pitchScale.v,8155); // native map initialization 50f240
    u.groundRoll=attitude[0];u.groundPitch=attitude[1];
    const auto& v=u.flightVelocity;
    u.speed=Fixed::raw(int32_t(std::sqrt((double(v.z)*v.z+double(v.y)*v.y)+double(v.x)*v.x)));
    const int oldX=footprintOrigin(u.x,u.type->footX),oldZ=footprintOrigin(u.z,u.type->footZ);
    u.x+=Fixed::raw(v.x); u.z+=Fixed::raw(v.z); u.flightY+=Fixed::raw(v.y);
    updateBodyIndex(u);
    if (oldX!=footprintOrigin(u.x,u.type->footX) || oldZ!=footprintOrigin(u.z,u.type->footZ)) {
        u.flightSectorX=u.x.v>>23;u.flightSectorZ=u.z.v>>23;
    }
}

void World::tickGuardNoMove(Unit& u) {
    const bool eligible=u.guardNoMoveAllowed && u.type->isStructure() && !u.underConstruction &&
        u.buildQueue.empty() && !u.buildSiteId &&
        (u.orders.empty() || u.orders.front().autoTarget);
    if (!eligible) { u.guardNoMoveActive=false; return; }
    if (!u.guardNoMoveActive) { u.guardNoMoveState={}; u.guardNoMoveActive=true; }
    struct Host {
        World& w; Unit& u;
        bool enabled() const { return u.alive() && !u.incapacitated() && u.guardNoMoveActive; }
        RetailMissionState* head() { return &u.guardNoMoveState; }
        bool hasNext(const RetailMissionState&) const { return false; }
        void idle() {}
        uint32_t random(int n) { return w.pathRand(n); }
        void remove(RetailMissionState&) { u.guardNoMoveActive=false; }
        void rotate(RetailMissionState&) {}
        void clear() { u.guardNoMoveActive=false; }
        int handle(RetailMissionState& m,uint32_t) {
            return retailGuardNoMove(m,w.tickCounter_,true,!u.type->weapons.empty(),
                [&](int n){return random(n);},[&]{ w.acquireTarget(u,true); });
        }
    } host{*this,u};
    retailDispatchMissions(tickCounter_,u.missionEvents,host);
}

void World::tickGroundMission(Unit& u) {
    // Other mission kinds continue through their existing handlers.
    if (!u.orders.empty() && !u.orders.front().landing) {
        u.standbyActive=false; u.landing.reset();
    }
    struct Host {
        World& w; Unit& u;
        uint64_t replaceController=0;
        bool completeUnloadApproach=false,abortUnloadApproach=false;
        bool enabled() const { return u.alive() && !u.underConstruction && !u.embarked(); }
        bool canStandby() const {
            return u.standbyAllowed && u.type && (!u.type->canFly || u.type->vtolStandby) && !u.type->isStructure() &&
                   !u.type->wanders && !u.buildSiteId && !u.repairId && !u.reclaimId &&
                   u.buildQueue.empty() && !u.repeatType && u.orders.empty() && !u.retainedFlightGoal;
        }
        RetailMissionState* head() {
            if (u.landing) return &u.landing->mission;
            if (auto* o = groundMissionOrder(u)) {
                if (o->transportUnloadApproach) {
                    if (completeUnloadApproach || abortUnloadApproach ||
                        o->transportUnloadReleasePending || o->transportMission.stage>1)
                        return nullptr;
                    return &o->transportMission;
                }
                return &o->mission;
            }
            return canStandby() && u.standbyActive ? &u.standbyState : nullptr;
        }
        bool hasNext(const RetailMissionState&) const { return currentLeg(u.orders)+1 < u.orders.size(); }
        void idle() {
            if (canStandby()) {
                u.standbyState = {};
                if (!u.type->canFly) u.standbyState.flags=0x5012000u;
                u.standbyActive = true;
            }
        }
        uint32_t random(int n) { return w.pathRand(uint32_t(n)); }
        void remove(RetailMissionState& m) {
            if (&m == &u.standbyState) u.standbyActive = false;
            else if (u.landing && &m==&u.landing->mission) {
                std::erase_if(u.orders,[](const Order& o){return o.landing;}); u.landing.reset();
            }
            else if (auto* o=groundMissionOrder(u); o && o->transportUnloadApproach &&
                    &m==&o->transportMission) abortUnloadApproach=true;
            else w.dropLeg(u);
        }
        void rotate(RetailMissionState&) {
            const Order goal = u.orders[currentLeg(u.orders)];
            w.dropLeg(u); u.orders.push_back(goal);
        }
        void clear() { w.cancelPath(u); u.orders.clear(); u.routeStamp=-1; u.standbyActive=false; u.landing.reset(); }
        int handle(RetailMissionState& m, uint32_t events) {
            if (u.landing && &m==&u.landing->mission)
                return retailLanding(*u.landing,events,{u.x.v,u.flightY.v,u.z.v},portHeadingToRetail(u.heading),
                    int16_t(u.type->footX),int16_t(u.type->footZ),true,u.type->canFly,*this);
            if (&m==&u.standbyState && u.type->canFly)
                return retailVtolStandby(m,w.tickCounter_,true,true,u.flightGroundMode==2,
                    [&](int n){return random(n);},[&]{ initialize(); },
                    [&]{return w.acquireTarget(u,true);},[&]{u.landing.emplace();return true;});
            if (&m == &u.standbyState)
                return retailStandby(m,w.tickCounter_,true,[&](int n) { return random(n); },
                    [&] { w.cancelPath(u); }, [&] { return w.acquireTarget(u,true); });
            auto& goal = u.orders[currentLeg(u.orders)];
            if(goal.transportUnloadApproach) {
                const auto target=goal.missionTarget.value_or(std::pair{goal.x,goal.z});
                const bool inRange=retailTransportInRange((target.first-u.x).v,
                    (target.second-u.z).v,uint16_t(u.type->transportDist));
                const int result=retailTransportUnloadApproach(m,goal.transportApproachAttempts,
                    w.tickCounter_,events,inRange,!u.type->isStructure() && u.type->maxVel>Fixed(),[&] {
                        w.cancelPath(u);
                        goal.controller=++w.nextMovementController_;
                        goal.navigationExhausted=false;goal.navigationConsumed=false;
                        // A polled surface-unload retry re-anchors the navigator
                        // directly at the mission site while the replacement search
                        // runs. Keep the requested site separate from any partial
                        // route endpoint, as 4e2500 does when it rebuilds the circle.
                        resetGroundSegment(u,goal);
                        m.pending&=~0x700u;
                        if(uint32_t(u.routeStamp)<=w.tickCounter_-6u)u.routeStamp=0;
                        w.requestPath(u,goal.x.toFloat(),goal.z.toFloat());
                    });
                // A blocked transfer resets the combined unload to stage 1.
                // It still needs this handler to re-route if the carrier has
                // drifted out of range, but it must not fold a second unload
                // into itself (there is no queued unload behind it).
                if(result==1 && !goal.unload)completeUnloadApproach=true;
                return result;
            }
            if (goal.park) {
                auto& park=*goal.park;
                const Unit* target=w.unit(park.target);
                const bool valid=target && target->alive();
                return retailGroundPark(m,events,true,park.target!=0,valid,
                    hasNext(m) && !u.incapacitated() && !u.embarked(),int16_t(u.type->footX),
                    int16_t(valid ? target->type->footX : 0),int16_t(valid ? target->type->footZ : 0),
                    park.padding,park.permanent,park.attempts,[&](int n){return random(n);},
                    [&]{park.target=0;},[&](bool useTarget,int outer,int inner) {
                        const Unit& center=useTarget ? *target : u;
                        park.ring=RetailRingGoal{footprintOrigin(center.x,u.type->footX),
                            footprintOrigin(center.z,u.type->footZ),inner,outer,retailCircleRadiusSquared(outer)};
                        const auto point=park.ring->navigationPoint(u.x,u.z,u.type->footX,u.type->footZ);
                        goal.x=point.x;goal.z=point.z;
                        goal.segmentX=Fixed::fromInt(u.x.floorInt());goal.segmentZ=Fixed::fromInt(u.z.floorInt());
                        goal.hasSegment=true;goal.navigationExhausted=false;goal.navigationConsumed=false;goal.controller=++w.nextMovementController_;
                        m.pending&=~0x3700u;w.cancelPath(u);
                        if (uint32_t(u.routeStamp)<=w.tickCounter_-6u) u.routeStamp=0;
                        w.requestPath(u,goal.x.toFloat(),goal.z.toFloat());
                    });
            }
            std::optional<Order> auxiliary;
            auto& response=goal.groundResponse;
            const RetailGroundPoint position{int16_t(u.x.floorInt()),int16_t(u.z.floorInt())};
            const uint16_t leash=uint16_t(std::clamp(u.type->leash,0,65535));
            const int result=retailGroundMove(m,goal.missionRadius,w.tickCounter_,events,
                int16_t(u.type->footX),u.embarked(),response.mode,uint8_t(u.moveState),[&](int n) { return random(n); },
                [&](uint32_t) {
                    w.cancelPath(u);
                    const bool wasActive=!goal.navigationExhausted;
                    goal.controller = ++w.nextMovementController_;
                    m.pending &= ~0x3700u;
                    const bool retain=currentLeg(u.orders)>0 && retainGroundRoute(u,goal);
                    const bool direct=!retain && (!(m.flags&0x10400000u) ||
                        (!wasActive && (m.flags&0x10000000u)));
                    goal.navigationExhausted=!retain && !direct;
                    if (direct) {
                        resetGroundSegment(u,goal);
                        // Erasing corners here would invalidate the dispatcher's
                        // mission reference before it advances the stage.
                        replaceController=goal.controller;
                    }
                    // Controller reset also invalidates stamps older than six
                    // ticks, with the executable's unsigned comparison.
                    if (uint32_t(u.routeStamp)<=w.tickCounter_-6u) u.routeStamp=0;
                    // 4e54e0 cancels the old search, then 4e4f50(1) marks
                    // the replacement controller pending. Retaining the stamp
                    // alone loses this request until some later retry happens.
                    w.requestPath(u,goal.x.toFloat(),goal.z.toFloat());
                },[&] {
                    // Target scoring and retaliation eligibility still use the
                    // World's combat host; the mission owns when to poll it.
                    int target=w.findTarget(u,true,true);
                    if (!target && u.fireState!=0 && u.lastHitBy) {
                        const Unit* enemy=w.unit(u.lastHitBy);
                        if (enemy && enemy->alive() && !enemy->embarked() && enemy->type &&
                            !w.allied(u.player,enemy->player) && !enemy->cloaked &&
                            fxLen(enemy->x-u.x,enemy->z-u.z)<=Fixed::fromFloat(u.type->maxRange()+90)) {
                            for (const auto& weapon:u.type->weapons)
                                if (!(enemy->type->canFly && weapon.noAir) && weapon.damageVs(enemy->type)>0) {
                                    target=enemy->id; break;
                                }
                        }
                    }
                    if (!target) return false;
                    const auto* enemy=w.unit(target);
                    if (response.mode>0 && u.moveState==1 &&
                        retailGroundDistance({int16_t(enemy->x.floorInt()),int16_t(enemy->z.floorInt())},
                                             response.goal)>=leash) return false;
                    if (response.mode<0) response.origin=position;
                    auxiliary.emplace(); auxiliary->targetId=target;
                    auxiliary->goal=true; auxiliary->autoTarget=response.mode>=0;
                    w.cancelPath(u); u.routeStamp=-1;
                    goal.controller=0; m.pending&=~0x3700u;
                    return true;
                },[&] {
                    if (!retailGroundReturnNeeded(position,response,leash)) return true;
                    auxiliary.emplace(); auto& back=*auxiliary;
                    back.x=Fixed::fromInt(response.origin.x); back.z=Fixed::fromInt(response.origin.z);
                    back.goal=back.groundMission=true; back.missionRadius=leash/4;
                    back.groundResponse.mode=1; back.groundResponse.goal=response.origin;
                    back.segmentX=Fixed::fromInt(position.x); back.segmentZ=Fixed::fromInt(position.z);
                    back.hasSegment=true;back.navigationExhausted=false;back.navigationConsumed=false; back.controller=++w.nextMovementController_;
                    w.cancelPath(u); u.routeStamp=-1;
                    return true;
                });
            // Inserting can invalidate m and goal. The dispatcher re-fetches
            // its head after result 4, so all parent updates must precede it.
            if (auxiliary) u.orders.insert(u.orders.begin(),*auxiliary);
            return result;
        }
        void initialize() {} // weapon aim reset has no separate state in this host
        int velocityPercent() {
            if (u.baseSpeed<=Fixed()) return 0;
            const auto& v=u.flightVelocity;
            const int32_t speed=int32_t(std::sqrt(double(v.x)*v.x+double(v.z)*v.z));
            return std::clamp(int(double(speed)*100/u.baseSpeed.v+0.5),0,100);
        }
        bool landable(RetailFlightVector point) {
            const auto& grid=w.navFor(u.type);
            if (grid.empty()) return true;
            const int foot=footCells(u.type);
            const Fixed x=Fixed::raw(point.x),z=Fixed::raw(point.z);
            return grid.fits(footprintCell(x,foot),footprintCell(z,foot),foot) && w.cellFree(x,z,u.id,foot);
        }
        int groundHeight(RetailFlightVector point) {
            if (w.heights_.empty()) return 0;
            const int x=std::clamp(Fixed::raw(point.x).floorInt()/16,0,w.terW_-1);
            const int z=std::clamp(Fixed::raw(point.z).floorInt()/16,0,w.terH_-1);
            return w.heights_[size_t(z)*w.terW_+x];
        }
        void touchdown() { ++u.flightLandingCallbackSerial; }
        void deactivate() { u.active=false; }
        void finish() {
            if(u.flightGroundMode!=1) {
                const auto attitude=retailFlightAttitude(u.flightAcceleration,{},portHeadingToRetail(u.heading),
                    u.type->bankScale.v,u.type->pitchScale.v,8155);
                u.groundRoll=attitude[0];u.groundPitch=attitude[1];
            }
            u.flightGroundMode=1;u.flightVelocity={};u.speed=Fixed();
        }
        void install(RetailFlightGoal goal) {
            std::erase_if(u.orders,[](const Order& o){return o.landing;});
            Order order; order.x=Fixed::raw(goal.point.x); order.z=Fixed::raw(goal.point.z);
            order.flightGoal=goal; order.landing=true; u.orders.insert(u.orders.begin(),order);
        }
    } host{*this,u};
    retailDispatchMissions(tickCounter_,u.missionEvents,host);
    if(host.abortUnloadApproach) {
        dropLeg(u);
        if(!u.orders.empty() && u.orders.front().unload)
            u.orders.erase(u.orders.begin());
    } else if(host.completeUnloadApproach) {
        const size_t approachIndex=currentLeg(u.orders);
        auto unloadIt=std::find_if(u.orders.begin()+long(approachIndex+1),u.orders.end(),
            [](const Order& order){return order.unload;});
        if(approachIndex<u.orders.size() && unloadIt!=u.orders.end() && !u.cargo.empty()) {
            // The transfer-range callback starts passenger delivery before the
            // navigator reaches its tighter unload-circle goal. Retail keeps that
            // route moving while the beam runs, so fold the unload action onto its
            // current goal instead of dropping the approach controller here.
            const Order requested=*unloadIt;
            auto& unload=u.orders[approachIndex];
            unload.unload=true;
            unload.transportY=requested.transportY;
            const int passengerId=u.cargo.front();
            const Unit* passenger=unit(passengerId);
            if(passenger && passenger->alive() && passenger->inTransport==u.id) {
                unload.transportPassenger=passengerId;
                unload.transportX=requested.x;unload.transportZ=requested.z;
                unload.transportTicks=0;unload.transportApproachAttempts=1;
                unload.transportMission=RetailMissionState{2};
                unload.transportMission.sleep(tickCounter_,1);
                unload.transportUnloadTransferDeferred=false;
                u.orders.erase(unloadIt);
            } else {
                dropLeg(u);
                if(!u.orders.empty() && u.orders.front().unload)
                    u.orders.erase(u.orders.begin());
            }
        } else {
            dropLeg(u);
        }
    }
    if (host.replaceController && !u.orders.empty()) {
        const size_t end=currentLeg(u.orders);
        if (u.orders[end].controller==host.replaceController)
            u.orders.erase(u.orders.begin(),u.orders.begin()+long(end));
    }
}

RetailCostSearch::Costs World::searchCosts(const Unit& u) const {
    return retailPathCosts(uint16_t(u.type->turnRate),u.type->footX,
                           u.type->roadMult.v,u.type->waterMult.v,u.groundTerrainFlags,
                           u.type->floater && u.type->minWaterDepth>0);
}

void World::refreshSearchRequest(int id,PathCell& start,int& heading,RetailCostSearch::Costs& costs) const {
    const Unit* u=unit(id);
    if (!u || !u->type) return;
    start={footprintCell(u->x,u->type->footX),footprintCell(u->z,u->type->footZ)};
    heading=portHeadingToRetail(u->heading);
    costs=searchCosts(*u);
}

bool World::admitSearchRequest(int id,uint32_t now) {
    Unit* u=unit(id);
    if (!u || !u->alive()) return false;
    // 4e54a0 stamps a pending request before execution, at least 15 ticks
    // after the preceding admission. Suspended work bypasses this callback.
    uint32_t stamp=u->routeStamp<0 ? 0u : uint32_t(u->routeStamp);
    if (!retailAdmitNavigator(true,now,stamp)) return false;
    u->routeStamp=std::bit_cast<int32_t>(stamp);
    return true;
}

bool World::requestPath(Unit& u, float x, float z) {
    static const bool kPqLog = std::getenv("TAK_PATHLOG") != nullptr;
    if (!pathService_) return false;
    if (!u.type || u.type->canFly || u.type->isStructure()) return false;
    const auto* mission = navigationMissionOrder(u);
    auto target = mission && mission->missionTarget ? *mission->missionTarget :
        std::pair{Fixed::fromFloat(x),Fixed::fromFloat(z)};
    if (mission && mission->buildRectangle) {
        // A partial route can end away from the build perimeter. Re-query the
        // controller rather than treating that stored endpoint as the goal.
        const auto [cx,cz]=mission->buildRectangle->navigationCell(
            footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ));
        target={Fixed::fromInt(cx*16+u.type->footX*8),Fixed::fromInt(cz*16+u.type->footZ*8)};
    }
    const NavGrid& g = navFor(u.type);
    if (g.empty()) return false;
    const PathCell from{footprintCell(u.x,u.type->footX),footprintCell(u.z,u.type->footZ)};
    const PathCell to{footprintCell(target.first,u.type->footX),footprintCell(target.second,u.type->footZ)};
    // Distance alone says nothing about intervening terrain or bodies. Even a
    // two-cell move may need a detour; cancelling those requests also cancelled
    // every retry after the mover hit the obstacle.
    if (from.x == to.x && from.z == to.z) {
        if (kPqLog) std::printf("[pq] t=%u id=%d NEAR-CANCEL to=(%.0f,%.0f)\n",
                                tickCounter_, u.id, x, z);
        paths_.cancel(u.id); return false;
    }
    if (kPqLog) std::printf("[pq] t=%u id=%d QUEUED to=(%.0f,%.0f)\n", tickCounter_, u.id, x, z);
    // The 5x budget class is the PLAYER's, not the request's: retail flags player
    // slots (+0x24e7) and the scheduler weighs whole players. `commander` stood here
    // as our stand-in for that flag and is gone with it.
    const int32_t tolerance = mission ? std::bit_cast<int32_t>(mission->missionRadius + 4u) : 4;
    const bool newlyPending=!paths_.pending(u.id);
    paths_.request(u.id, from, to, g.width(), g.height(),
                   target.first, target.second,
                   u.player, /*priority=*/(humanMask_ >> (unsigned(u.player) & 31)) & 1,
                   /*tolCells=*/u.type->halfCellTicks > 0 ? 50 / u.type->halfCellTicks : 0,
                   portHeadingToRetail(u.heading),searchCosts(u),{u.type->footX/2,u.type->footZ/2},
                   tolerance, mission ? mission->controller : 0,
                   mission ? retailCircleRadiusSquared(tolerance) : 0,
                   u.orders.empty() ? std::nullopt : u.orders[currentLeg(u.orders)].buildRectangle,
                   mission && mission->park ? mission->park->ring : std::nullopt);
    // 4e4f50 clears prior outcome bits only when first marking a request pending.
    if (newlyPending) u.routeCrowded=u.routeTraffic=u.routeFailed=u.routeDetour=false;
    return true;
}

// Label the connected components of the cells a `foot`-wide unit can occupy, using
// the same adjacency the movers use (8-neighbour, fits(), no diagonal corner-cut),
// so "same component" means a body of this size can actually walk between them. Rebuilt
// only when the grid's walkability version moves; one fill serves every goal.
// Blocking cells can only SPLIT components, never merge them -- so if no split
// happened, every existing label is still correct and the only edit needed is to clear
// the cells that stopped fitting. Proving "no split" does not need the whole map: take
// the cells that stopped fitting, look at the still-fitting cells around them, and ask
// whether cells that shared a label before still reach one another WITHOUT crossing the
// edit. If they do, any route that ran through the edit can be rerouted locally, so
// global connectivity is unchanged. If the local search cannot show it (the reroute may
// exist but leave the window, or the corridor really was severed), we give up and let
// the next query rebuild from scratch -- conservative, never wrong.
//
// The window has to cover every cell whose fits() answer the edit can move. fits(cx,cz)
// reads clearance at (cx-off, cz-off), and a blocked cell perturbs clearance up to
// kClearMax cells up-left of it, so the reach is kClearMax + foot in each direction.
// Padded by one and clamped to the map.
bool World::tryIncrementalBlock(const NavGrid& g, int foot, CompGrid& cg,
                                int x0, int z0, int x1, int z1) const {
    const int w = g.width(), h = g.height();
    if (cg.w != w || cg.h != h || cg.raw_.empty()) return false;
    const int off = foot / 2;
    const int margin = int(kClearMax) + foot + 2;
    const int wx0 = std::max(0, x0 - margin), wz0 = std::max(0, z0 - margin);
    const int wx1 = std::min(w - 1, x1 + off + 2), wz1 = std::min(h - 1, z1 + off + 2);
    if (wx1 < wx0 || wz1 < wz0) return false;
    const int ww = wx1 - wx0 + 1, wh = wz1 - wz0 + 1;

    // Current fits() over the window, once per cell (see components() for why).
    std::vector<uint8_t> okw(size_t(ww) * size_t(wh));
    for (int z = wz0; z <= wz1; ++z)
        for (int x = wx0; x <= wx1; ++x)
            okw[size_t(z - wz0) * size_t(ww) + size_t(x - wx0)] = g.fits(x, z, foot) ? 1 : 0;
    auto okAt = [&](int x, int z) -> bool {
        if (x < wx0 || z < wz0 || x > wx1 || z > wz1) return false;   // outside: unknown
        return okw[size_t(z - wz0) * size_t(ww) + size_t(x - wx0)] != 0;
    };
    auto labelAt = [&](int x, int z) -> int32_t {
        return cg.raw_[size_t(z) * size_t(w) + size_t(x)];
    };

    // Cells that stopped fitting. A cell that STARTED fitting means this was not a pure
    // block after all (or the entry was already stale) -- bail rather than guess.
    std::vector<int> removed;
    for (int z = wz0; z <= wz1; ++z)
        for (int x = wx0; x <= wx1; ++x) {
            const bool had = labelAt(x, z) >= 0, has = okAt(x, z);
            if (had && !has) removed.push_back(int(size_t(z) * size_t(w) + size_t(x)));
            else if (!had && has) return false;
        }
    if (removed.empty()) return true;   // nothing this footprint can even notice

    static const int dcx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dcz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    // Same connectivity rule as the full fill, including the no-corner-cutting test.
    auto stepOk = [&](int cx, int cz, int k) -> bool {
        const int nx = cx + dcx[k], nz = cz + dcz[k];
        if (!okAt(nx, nz)) return false;
        if (k >= 4 && (!okAt(cx + dcx[k], cz) || !okAt(cx, cz + dcz[k]))) return false;
        return true;
    };

    // Still-passable cells touching the edit: these carry the connectivity that the
    // removed cells used to provide.
    std::vector<int> boundary;
    for (int idx : removed) {
        const int cx = idx % w, cz = idx / w;
        for (int k = 0; k < 8; ++k) {
            const int nx = cx + dcx[k], nz = cz + dcz[k];
            if (okAt(nx, nz)) boundary.push_back(int(size_t(nz) * size_t(w) + size_t(nx)));
        }
    }
    if (boundary.empty()) {          // the edit was self-contained: just clear it
        for (int idx : removed) cg.raw_[size_t(idx)] = -1;
        return true;
    }

    // Flood the still-passable cells inside the window, so each boundary cell gets a
    // LOCAL component id. A cell that touches the window edge is marked "escapes": its
    // component may continue outside, where we cannot see it, so it cannot be used to
    // prove connectivity either way.
    std::vector<int32_t> loc(size_t(ww) * size_t(wh), -1);
    std::vector<int> stack;
    int32_t nloc = 0;
    for (int idx : boundary) {
        const int bx = idx % w, bz = idx / w;
        if (loc[size_t(bz - wz0) * size_t(ww) + size_t(bx - wx0)] != -1) continue;
        const int32_t id = nloc++;
        loc[size_t(bz - wz0) * size_t(ww) + size_t(bx - wx0)] = id;
        stack.push_back(idx);
        while (!stack.empty()) {
            const int cur = stack.back(); stack.pop_back();
            const int cx = cur % w, cz = cur / w;
            for (int k = 0; k < 8; ++k) {
                if (!stepOk(cx, cz, k)) continue;
                const int nx = cx + dcx[k], nz = cz + dcz[k];
                int32_t& l = loc[size_t(nz - wz0) * size_t(ww) + size_t(nx - wx0)];
                if (l != -1) continue;
                l = id;
                stack.push_back(int(size_t(nz) * size_t(w) + size_t(nx)));
            }
        }
    }

    // Cells that shared a component before the edit must still share a local one. If two
    // of them landed in different local components the edit may have split them -- or a
    // reroute may exist outside the window where this search cannot see it. Either way
    // we cannot prove safety, so give up and rebuild fully.
    // Group by ROOT, not by raw label. After an unblock merge two cells can share a
    // component while still carrying the different ids they were first given; grouping
    // by raw label would see two unrelated ids, find each trivially self-consistent,
    // and conclude nothing was severed -- leaving the cache asserting a connection the
    // edit just cut. Reachability would then depend on whether a passage had ever been
    // open, which is history, not geometry. (Found by review; reproduced by opening and
    // re-closing a gate in a full-width wall in ONE blockCells call each way. Closing it
    // in three calls hides the bug, because then cells sharing a raw label straddle the
    // split and the check trips for the wrong reason.)
    std::map<int32_t, int32_t> firstLoc;   // pre-edit component root -> local component
    for (int idx : boundary) {
        const int bx = idx % w, bz = idx / w;
        const int32_t pre = cg.root(labelAt(bx, bz));
        if (pre < 0) continue;
        const int32_t lc = loc[size_t(bz - wz0) * size_t(ww) + size_t(bx - wx0)];
        auto it = firstLoc.find(pre);
        if (it == firstLoc.end()) firstLoc.emplace(pre, lc);
        else if (it->second != lc) return false;   // possible split -- do it properly
    }

    for (int idx : removed) cg.raw_[size_t(idx)] = -1;
    return true;
}

// Clearing cells can only MERGE components, so unlike a block there is nothing to
// prove -- but the merge must not cost a relabel. Newly passable cells take an adjacent
// component's id (or a fresh one if they stand alone), and any OTHER components they now
// touch are united with it in the alias table. Two cells are in the same component iff
// their roots match, which is the only thing either caller asks.
bool World::tryIncrementalUnblock(const NavGrid& g, int foot, CompGrid& cg,
                                  int x0, int z0, int x1, int z1) const {
    const int w = g.width(), h = g.height();
    if (cg.w != w || cg.h != h || cg.raw_.empty()) return false;
    const int off = foot / 2;
    const int margin = int(kClearMax) + foot + 2;
    const int wx0 = std::max(0, x0 - margin), wz0 = std::max(0, z0 - margin);
    const int wx1 = std::min(w - 1, x1 + off + 2), wz1 = std::min(h - 1, z1 + off + 2);
    if (wx1 < wx0 || wz1 < wz0) return false;
    const int ww = wx1 - wx0 + 1, wh = wz1 - wz0 + 1;

    std::vector<uint8_t> okw(size_t(ww) * size_t(wh));
    for (int z = wz0; z <= wz1; ++z)
        for (int x = wx0; x <= wx1; ++x)
            okw[size_t(z - wz0) * size_t(ww) + size_t(x - wx0)] = g.fits(x, z, foot) ? 1 : 0;
    auto okAt = [&](int x, int z) -> bool {
        if (x < wx0 || z < wz0 || x > wx1 || z > wz1)
            return x >= 0 && z >= 0 && x < w && z < h && g.fits(x, z, foot);
        return okw[size_t(z - wz0) * size_t(ww) + size_t(x - wx0)] != 0;
    };

    // Cells that became passable. A cell that STOPPED fitting means this was not a pure
    // unblock -- bail and let the next query rebuild.
    std::vector<int> added;
    for (int z = wz0; z <= wz1; ++z)
        for (int x = wx0; x <= wx1; ++x) {
            const bool had = cg.raw_[size_t(z) * size_t(w) + size_t(x)] >= 0;
            const bool has = okAt(x, z);
            if (!had && has) added.push_back(int(size_t(z) * size_t(w) + size_t(x)));
            else if (had && !has) return false;
        }
    if (added.empty()) return true;

    static const int dcx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dcz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    auto stepOk = [&](int cx, int cz, int k) -> bool {
        const int nx = cx + dcx[k], nz = cz + dcz[k];
        if (!okAt(nx, nz)) return false;
        if (k >= 4 && (!okAt(cx + dcx[k], cz) || !okAt(cx, cz + dcz[k]))) return false;
    return true;
    };

    // Ascending cell order, so the ids handed out do not depend on iteration accidents.
    // Two passes: give every new cell an id first (neighbouring new cells can then adopt
    // each other's), then unite across every passable step.
    for (int idx : added) {
        const int cx = idx % w, cz = idx / w;
        int32_t take = -1;
        for (int k = 0; k < 8; ++k) {
            if (!stepOk(cx, cz, k)) continue;
            const int nx = cx + dcx[k], nz = cz + dcz[k];
            const int32_t nl = cg.raw_[size_t(nz) * size_t(w) + size_t(nx)];
            if (nl >= 0) { take = cg.root(nl); break; }
        }
        if (take < 0) {                       // an island of its own
            take = int32_t(cg.alias.size());
            cg.alias.push_back(take);
        }
        cg.raw_[size_t(idx)] = take;
    }
    for (int idx : added) {
        const int cx = idx % w, cz = idx / w;
        const int32_t mine = cg.raw_[size_t(idx)];
        for (int k = 0; k < 8; ++k) {
            if (!stepOk(cx, cz, k)) continue;
            const int nx = cx + dcx[k], nz = cz + dcz[k];
            const int32_t nl = cg.raw_[size_t(nz) * size_t(w) + size_t(nx)];
            if (nl >= 0) cg.unite(mine, nl);
        }
    }
    return true;
}

const World::CompGrid* World::components(const NavGrid& g, int foot) const {
    if (g.empty()) return nullptr;
    auto& cg = compCache_[{&g, foot}];
    if (cg.ver == g.version() && cg.w == g.width() && cg.h == g.height()) return &cg;
    g.ensureClearance();
    const int w = g.width(), h = g.height();
    cg.ver = g.version(); cg.w = w; cg.h = h;
    cg.raw_.assign(size_t(w) * size_t(h), -1);
    cg.alias.clear();   // a fresh labelling needs no aliasing: every id is its own root
    static const int dcx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dcz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    // Evaluate the footprint predicate ONCE per cell. The fill asks "does a body of
    // this size fit here?" from every adjacent cell, so each cell was tested about
    // eight times over, and fits() is not free: for foot > 1 it re-checks the dirty
    // flag, bounds-checks, and indexes the clearance grid. One flat pass up front
    // turns ~8*w*h fits() calls into w*h of them plus byte loads -- measured 7.49ms
    // -> 5.66ms per relabel on the Ulasem 8-AI benchmark (853 relabels either way).
    std::vector<uint8_t> ok(size_t(w) * size_t(h));
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x)
            ok[size_t(z) * size_t(w) + size_t(x)] = g.fits(x, z, foot) ? 1 : 0;
    std::vector<int> stack;
    int32_t next = 0;
    for (int z0 = 0; z0 < h; ++z0)
        for (int x0 = 0; x0 < w; ++x0) {
            size_t seed = size_t(z0) * size_t(w) + size_t(x0);
            if (cg.raw_[seed] != -1 || !ok[seed]) continue;
            const int32_t id = next++;
            cg.alias.push_back(id);          // identity; unblock edits may union later
            cg.raw_[seed] = id;
            stack.push_back(int(seed));
            while (!stack.empty()) {
                int idx = stack.back(); stack.pop_back();
                int cx = idx % w, cz = idx / w;
                // The four ORTHOGONAL fits are computed once and reused. The
                // diagonal corner-cut rule asks about exactly those same cells, so
                // the original loop tested each of them twice -- up to sixteen
                // fits() calls per cell popped instead of eight.
                //
                // Strictly less work, but do not expect it to show up in a profile:
                // at -O2 the whole sim costs ~26ms across 1800 ticks, so a relabel
                // is already lost in the noise. (It IS visible in an -O0 debug
                // build, which is what mislead an earlier pass into calling this a
                // 100ms stall -- measure optimisations on the build users run.)
                //
                // dcx/dcz index 0..3 are +x,-x,+z,-z; 4..7 are the diagonals, each
                // the combination of two of those. Same predicate, same traversal
                // order, same labels -- only the redundant calls go.
                bool orth[4];
                for (int k = 0; k < 4; ++k) {
                    const int nx = cx + dcx[k], nz = cz + dcz[k];
                    orth[k] = nx >= 0 && nz >= 0 && nx < w && nz < h &&
                              ok[size_t(nz) * size_t(w) + size_t(nx)];
                }
                for (int k = 0; k < 8; ++k) {
                    int nx = cx + dcx[k], nz = cz + dcz[k];
                    if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
                    if (k < 4) {
                        if (!orth[k]) continue;
                    } else {
                        if (!ok[size_t(nz) * size_t(w) + size_t(nx)]) continue;
                        // dcx[k] is +/-1 and dcz[k] is +/-1: the two orthogonal steps
                        // that make up this diagonal are entries (dcx[k]>0?0:1) and
                        // (dcz[k]>0?2:3) of the table above.
                        if (!orth[dcx[k] > 0 ? 0 : 1] || !orth[dcz[k] > 0 ? 2 : 3])
                            continue;                // no diagonal corner-cutting
                    }
                    size_t ni = size_t(nz) * size_t(w) + size_t(nx);
                    if (cg.raw_[ni] != -1) continue;
                    cg.raw_[ni] = id;
                    stack.push_back(int(ni));
                }
            }
        }
    return &cg;
}


bool World::pathExists(const UnitType* type, float gx, float gz, float fx, float fz) const {
    // Answered from connectivity, not a distance field. This used to build a full-map
    // Dijkstra (~40ms) for a single yes/no, and that WAS the per-second AI hitch:
    // measured ai=40.7ms of which pathExists=40.7ms for one build. The component
    // labelling costs one flood fill per grid+footprint and serves every goal until
    // the terrain changes.
    const NavGrid& grid = navFor(type);
    if (grid.empty()) return false;
    const int foot = type ? std::clamp(std::max(type->footX, type->footZ), 1, 15) : 1;
    const CompGrid* cg = components(grid, foot);
    if (!cg || cg->empty()) return false;
    const int w = cg->w, h = cg->h;
    auto labelAt = [&](float wx, float wz) -> int32_t {
        int cx = footprintCell(wx, foot), cz = footprintCell(wz, foot);
        if (cx < 0 || cz < 0 || cx >= w || cz >= h) return -1;
        return cg->componentAt(cx, cz);
    };
    // Nearest label to a point whose own cell has none: spiral out to the first cell
    // a body of this size fits in. Used for BOTH ends -- see below.
    auto nearestLabel = [&](float wx, float wz) -> int32_t {
        int cx = std::clamp(footprintCell(wx, foot), 0, w - 1);
        int cz = std::clamp(footprintCell(wz, foot), 0, h - 1);
        for (int r = 1; r < 24; ++r)
            for (int j = -r; j <= r; ++j)
                for (int i = -r; i <= r; ++i) {
                    if (std::max(std::abs(i), std::abs(j)) != r) continue;
                    int nx = cx + i, nz = cz + j;
                    if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
                    int32_t l = cg->componentAt(nx, nz);
                    if (l >= 0) return l;
                }
        return -1;
    };
    // The asker may not FIT where it stands -- shoved by a crowd, clipped into a
    // corner, squeezed past a building -- while still being on walkable ground. That
    // is a transient position, not a verdict about the goal, so resolve it to the
    // nearest cell the body does fit exactly as the goal is resolved below. Returning
    // false here instead made the movement watchdogs read "goal unreachable" and DROP
    // the order, so a unit that got briefly wedged abandoned its march for good.
    int32_t from = labelAt(fx, fz);
    if (from < 0) from = nearestLabel(fx, fz);
    if (from < 0) return false;
    int32_t to = labelAt(gx, gz);
    if (to < 0) {
        // A goal the unit cannot occupy resolves to the nearest cell it can, so
        // asking about a spot on a wall still answers usefully.
        to = nearestLabel(gx, gz);
    }
    return to >= 0 && to == from;
}

bool World::scriptYardOpen(int unitId) const {
    const auto it=unitScripts_.find(unitId);
    return it!=unitScripts_.end() && it->second.yardOpen;
}

bool World::canLoadInto(int unitId,int transportId) const {
    const Unit* u=unit(unitId);const Unit* t=unit(transportId);
    if (!u || !t || u==t || !u->alive() || !t->alive() || u->embarked() || t->embarked() ||
        !u->type || !t->type || u->underConstruction || t->underConstruction) return false;
    if (u->type->isStructure() || u->type->canFly || u->type->cantBeTransported ||
        !t->type->canTransport || u->player!=t->player) return false;
    // 51a099: a surface carrier cannot board a water-only passenger. A flying
    // carrier skips this restriction (other type/capacity restrictions still apply).
    if (!t->type->canFly && !u->type->transportLandEligible) return false;
    // 51a0af: the top of the passenger must protrude above the water.
    if (int64_t(u->groundY.v)+u->type->modelTop<=int64_t(uint8_t(seaLevel_))*65536) return false;
    uint32_t count=0,used=0;
    for (int id:t->cargo) if (const Unit* c=unit(id);c && c->alive() && c->inTransport==t->id) {
        ++count;used+=uint16_t(c->type->transportSize);
    }
    return retailTransportCapacity(uint16_t(u->type->transportSize),uint16_t(t->type->maxTransportSize),
        count,uint16_t(t->type->transportCap),used,uint16_t(t->type->transportSizeCap));
}

void World::loadInto(int unitId, int transportId) {
    if (!canLoadInto(unitId,transportId)) return;
    Unit* u=unit(unitId);
    u->orders.clear();
    cancelPath(*u);          // a route for the orders just discarded would eat the load
    Order o;
    o.targetId = transportId;
    o.load = true;
    o.goal = true;
    o.transportMission={};
    u->orders.push_back(o);
    Unit* carrier=unit(transportId);
    {
        // GROUND_PICKUP and VTOL_PICKUP both own a reciprocal carrier mission.
        // Only its active passenger may enter the transfer stage.
        if(carrier->orders.empty() || !carrier->orders.front().transportPickup) {
            carrier->orders.clear();cancelPath(*carrier);
        }
        if(std::none_of(carrier->orders.begin(),carrier->orders.end(),
                [&](const Order& pending){return pending.transportPickup && pending.targetId==unitId;})) {
            Order pickup;pickup.load=true;pickup.transportPickup=true;pickup.goal=true;
            pickup.transportMission={};
            pickup.targetId=unitId;pickup.x=u->x;pickup.z=u->z;
            carrier->orders.push_back(pickup);
        }
    }
}

void World::unloadAt(int transportId, float x, float z, Fixed destinationY) {
    Unit* t = unit(transportId);
    if (!t || !t->alive() || t->cargo.empty()) return;
    t->orders.clear();
    // A search for the orders just discarded is still queued, and cases 1 and 3 below
    // re-request nothing -- so it would land on the bare unload and rewrite it.
    cancelPath(*t);
    // Retail routes surface carriers to a circle centered on the exact drop point,
    // with radius transportdistance-34 (408e84/408d50). VTOL_UNLOAD installs the
    // same-radius flight controller before it steps out for transfer. Keep that
    // mission on the active flying order so the carrier does not overshoot to the
    // exact drop point before its unload handler wakes.
    const bool inRange=retailTransportInRange((Fixed::fromFloat(x)-t->x).v,
        (Fixed::fromFloat(z)-t->z).v,uint16_t(t->type->transportDist));
    if (!inRange && t->type->canFly) {
        Order approach;
        approach.x=Fixed::fromFloat(x);approach.z=Fixed::fromFloat(z);
        approach.goal=true;approach.unload=true;approach.transportUnloadApproach=true;
        approach.transportMission=RetailMissionState{1};
        approach.transportY=destinationY;
        approach.missionTarget=std::pair{approach.x,approach.z};
        t->orders.push_back(std::move(approach));
        return;
    }
    if (!inRange) {
        // Keep the approach as its own order. replaceLeg rebuilds the active route
        // from waypoints; the unload flag must stay queued behind that routed leg.
        Order mv;
        mv.x = Fixed::fromFloat(x);
        mv.z = Fixed::fromFloat(z);
        mv.goal = true;
        if (!t->type->canFly) {
            mv.groundMission=true;
            mv.transportUnloadApproach=true;
            mv.transportMission=RetailMissionState{1};
            mv.missionTarget=std::pair{Fixed::fromFloat(x),Fixed::fromFloat(z)};
            // The shared circle-goal helper adds four to the stored radius.
            mv.missionRadius=uint32_t(t->type->transportDist-34)-4u;
        }
        t->orders.push_back(mv);
    }
    Order o;
    o.x = Fixed::fromFloat(x);
    o.z = Fixed::fromFloat(z);
    o.unload = true;
    o.transportY = destinationY;
    t->orders.push_back(o);
}

bool World::tickTransport(Unit& u, float dt) {
    (void)dt;
    size_t pickupLeg=u.orders.front().load ? currentLeg(u.orders) : 0;
    if(!u.orders.front().load && !u.orders.front().unload && !u.orders.empty()) {
        const size_t leg=currentLeg(u.orders);
        if(leg<u.orders.size() && u.orders[leg].unload &&
           u.orders[leg].transportUnloadApproach)pickupLeg=leg;
    }
    Order& o = u.orders[pickupLeg];
    auto removeUnloadLeg=[&] {
        const size_t end=std::min(pickupLeg+1,u.orders.size());
        u.orders.erase(u.orders.begin(),u.orders.begin()+long(end));
    };
    if(o.unload && o.transportUnloadTransferDeferred) {
        o.transportUnloadTransferDeferred=false;
        return true;
    }
    if(o.transportPickup) {
        auto removePickup=[&] {
            cancelPath(u);
            u.orders.erase(u.orders.begin(),u.orders.begin()+long(pickupLeg)+1);
        };
        Unit* passenger=unit(o.targetId);
        if(!passenger || !canLoadInto(passenger->id,u.id) || passenger->orders.empty() ||
            !passenger->orders.front().load || passenger->orders.front().targetId!=u.id) {
            removePickup();return true;
        }
        if(u.type->canFly || !o.controller) {o.x=passenger->x;o.z=passenger->z;}
        auto& mission=o.transportMission;
        auto advancePickup=[&] {
            if(u.type->canFly) {
                if(o.flightGoal)tickFlightMovement(u,true);
                else tickFlightBody(u);
                notifyFlightOccupancy(u);
            } else brakeGround(u);
            return true;
        };
        auto advanceApproach=[&] {
            if(!u.type->canFly)return false; // the ground navigator keeps moving
            o.flightGoal=retailPickupPursuitGoal({passenger->x.v,passenger->groundY.v,passenger->z.v},
                uint16_t(u.type->transportDist));
            tickFlightMovement(u,true);notifyFlightOccupancy(u);
            return true;
        };
        // Both native carrier handlers initialize, then wait one tick before
        // approach. The passenger's Move_Seek_Pickup never owns this timer.
        if(mission.stage==0) {
            if(u.type->transportDist<(u.type->canFly ? 16:51)) {
                removePickup();return true;
            }
            mission.stage=1;mission.sleep(tickCounter_,1);
            if(u.type->canFly)notifyUnitScript(u,"BeginFlight");
            return advancePickup();
        }
        if(tickCounter_>=mission.deadline) {
            mission.deadline=0xffffffffu;mission.pending|=1;
        }
        const uint32_t events=(u.missionEvents|mission.pending)&mission.waitMask;
        if(mission.waitMask && !events)
            return mission.stage==1 && (mission.waitMask&0x700u) ? advanceApproach() : advancePickup();
        u.missionEvents&=~events;mission.pending&=~events;mission.waitMask=0;
        if(mission.stage==2) {
            const int result=retailPickupTransfer(mission,o.transportTicks,o.transportApproachAttempts,
                tickCounter_,u.type->canFly,passenger->speed.v,[&] {
                    transportEffects_.push_back({tickCounter_,
                        {passenger->x.v,(passenger->type->canFly ? passenger->flightY : passenger->groundY).v,passenger->z.v},
                        {u.x.v,(u.type->canFly ? u.flightY : u.groundY).v,u.z.v}});
                });
            if(result==8) {
                removePickup();return true;
            }
            if(result!=1)return advancePickup();
            // Stage 3 attaches immediately when the fifteenth effect wait ends.
            // The carrier owns completion even when it updates before its cargo.
            cancelPath(*passenger);
            passenger->inTransport=u.id;
            u.cargo.push_back(passenger->id);
            passenger->orders.clear();passenger->speed=Fixed();
            advancePickup();
            passenger->x=u.x;passenger->z=u.z;
            updateBodyIndex(*passenger);refreshMovingSearchBody(*passenger);
            removePickup();
            return true;
        }
        if(retailTransportInRange((passenger->x-u.x).v,(passenger->z-u.z).v,uint16_t(u.type->transportDist))) {
            mission.stage=2;mission.sleep(tickCounter_,1);
            o.transportTicks=0;o.transportApproachAttempts=0;
            o.transportPassenger=passenger->id;
            o.transportX=passenger->x;o.transportZ=passenger->z;
            passenger->missionEvents|=0x80;cancelPath(u);
            o.controller=0;o.navigationExhausted=true;
            if(u.type->canFly) {
                mission.pending&=~0x3700u;
                o.flightGoal=retailUnloadStepOut({u.x.v,u.flightY.v,u.z.v},
                    portHeadingToRetail(u.heading),uint16_t(u.type->transportDist));
            }
            const bool handled=advancePickup();
            if(pickupLeg)u.orders.erase(u.orders.begin(),u.orders.begin()+long(pickupLeg));
            return handled;
        }
        if(retailPickupApproachAborted(u.type->canFly,!u.type->isStructure(),events,passenger->speed.v)) {
            removePickup();return true;
        }
        // Native 408a50/41ab16 scans the owner's unit array for other
        // reciprocal passengers already in range, then draws random(count).
        // Put that pickup ahead of the distant mission, which resumes afterward.
        std::vector<std::pair<int,size_t>> nearby;
        for(size_t i=pickupLeg+1;i<u.orders.size();++i) {
            const auto& pending=u.orders[i];
            if(!pending.transportPickup)continue;
            const Unit* candidate=unit(pending.targetId);
            if(!candidate || !canLoadInto(candidate->id,u.id) || candidate->orders.empty())continue;
            const auto& request=candidate->orders.front();
            if(!request.load || request.targetId!=u.id)continue;
            if(retailTransportInRange((candidate->x-u.x).v,(candidate->z-u.z).v,
                    uint16_t(u.type->transportDist)))nearby.emplace_back(candidate->id,i);
        }
        if(!nearby.empty()) {
            // Unit ids preserve the native unit-array order, independent of
            // the order in which the player issued the boarding commands.
            std::sort(nearby.begin(),nearby.end());
            const size_t index=nearby[pathRand(uint32_t(nearby.size()))].second;
            o.transportMission.stage=0;o.transportMission.waitMask=0;
            o.controller=0;o.navigationExhausted=true;
            Order pickup=std::move(u.orders[index]);
            u.orders.erase(u.orders.begin()+index);
            if(pickupLeg)u.orders.erase(u.orders.begin(),u.orders.begin()+long(pickupLeg));
            u.orders.insert(u.orders.begin(),std::move(pickup));
            cancelPath(u);
            return true;
        }
        retailPickupApproachWait(mission,tickCounter_,u.type->canFly,
            [&](uint32_t n){return pathRand(n);});
        if(!u.type->canFly) {
            o.missionTarget=std::pair{passenger->x,passenger->z};
            // Circle setup adds four to the stored mission radius. Pickup
            // passes transportdistance-16 to the native circle constructor.
            o.missionRadius=uint32_t(u.type->transportDist-16)-4u;
            o.controller=++nextMovementController_;
            mission.pending&=~0x3700u;
            const bool retain=pickupLeg && retainGroundRoute(u,o);
            o.navigationExhausted=false;
            if(!retain)resetGroundSegment(u,o);
            cancelPath(u);
            if(uint32_t(u.routeStamp)<=tickCounter_-6u)u.routeStamp=0;
            if(!retain && pickupLeg)u.orders.erase(u.orders.begin(),u.orders.begin()+long(pickupLeg));
            const auto& goal=u.orders[currentLeg(u.orders)];
            requestPath(u,goal.x.toFloat(),goal.z.toFloat());
            return false;
        }
        // Native controller installation clears stale movement events. Do this
        // at the mission poll, not during the pursuit's per-tick target refresh.
        mission.pending&=~0x3700u;
        return advanceApproach();
    }
    if (o.load) {
        Unit* t=unit(o.targetId);
        auto remove=[&] {
            cancelPath(u);
            u.orders.erase(u.orders.begin(),u.orders.begin()+long(pickupLeg)+1);
        };
        auto& mission=o.transportMission;
        bool discardPrefix=false,request=false;
        auto detach=[&] {
            cancelPath(u);o.controller=0;o.navigationExhausted=true;
            discardPrefix=true;request=false;
        };
        auto approach=[&] {
            o.missionTarget=std::pair{t->x,t->z};
            o.missionRadius=uint32_t(t->type->transportDist-16)-4u;
            o.controller=++nextMovementController_;
            mission.pending&=~0x3700u;
            const bool retain=pickupLeg && retainGroundRoute(u,o);
            o.navigationExhausted=false;
            if(!retain)resetGroundSegment(u,o);
            cancelPath(u);
            if(uint32_t(u.routeStamp)<=tickCounter_-6u)u.routeStamp=0;
            discardPrefix=!retain;request=true;
        };
        for(unsigned calls=0;calls<100;++calls) {
            if(tickCounter_>=mission.deadline) {
                mission.deadline=0xffffffffu;mission.pending|=1;
            }
            const uint32_t events=(u.missionEvents|mission.pending)&mission.waitMask;
            if(mission.waitMask && !events)break;
            u.missionEvents&=~events;mission.pending&=~events;mission.waitMask=0;
            // Native eligibility and reciprocal-queue checks run in the
            // handler, after dispatch wakes it. Retiring a carrier mission
            // does not directly erase another unit's sleeping passenger order.
            if(!canLoadInto(u.id,o.targetId)) {remove();return false;}
            if(std::none_of(t->orders.begin(),t->orders.end(),
                    [](const Order& p){return p.transportPickup;})) {remove();return false;}
            const int result=retailPassengerPickup(mission,o.transportApproachAttempts,
                tickCounter_,events,!t->type->canFly,approach,detach);
            if(result==1)++mission.stage;
            else if(result!=2 && result!=4) {remove();return false;}
        }
        const bool moving=o.controller!=0;
        if(discardPrefix && pickupLeg)
            u.orders.erase(u.orders.begin(),u.orders.begin()+long(pickupLeg));
        if(request) {
            const auto& goal=u.orders[currentLeg(u.orders)];
            requestPath(u,goal.x.toFloat(),goal.z.toFloat());
        }
        if(!moving) {brakeGround(u);return true;}
        return false;
    }
    // Check the chosen point, as 408f16/41b292 do. A blocked point does
    // not authorize a different landing cell; passengers disperse after release.
    const Fixed unloadX=o.transportUnloadApproach && o.missionTarget ?
        o.missionTarget->first : o.x;
    const Fixed unloadZ=o.transportUnloadApproach && o.missionTarget ?
        o.missionTarget->second : o.z;
    const bool surfaceRouteActive=!u.type->canFly && o.transportUnloadApproach &&
        (o.controller || ((o.transportMission.stage>1 || o.transportUnloadReleasePending) &&
                          u.speed>Fixed()));
    const bool inRange=retailTransportInRange((unloadX-u.x).v,(unloadZ-u.z).v,
                                               uint16_t(u.type->transportDist));
    if (!u.type->canFly) {
        if (!inRange) { o.transportTicks=0;return false; }
        if (!surfaceRouteActive) u.speed=Fixed();
    }
    auto advanceCarrier=[&] {
        if (u.type->canFly && !u.orders.empty() && u.orders.front().unload) {
            // The carrier's controller continues during transfer and retry.
            // The beam destination and the flight destination are distinct.
            if(u.orders.front().flightGoal)tickFlightMovement(u,true);
            else tickFlightBody(u);
            notifyFlightOccupancy(u);
        }
        // Retail starts the passenger beam as soon as the carrier enters
        // transportdistance, but its surface navigator continues to the smaller
        // unload-circle radius. Let the normal ground mover run during that overlap.
        return !surfaceRouteActive;
    };
    std::erase_if(u.cargo,[&](int id) {
        const Unit* c=unit(id);return !c || !c->alive() || c->inTransport!=u.id;
    });
    if (u.cargo.empty()) {
        if (o.transportUnloadReleasePending) {
            if (tickCounter_ < o.transportMission.deadline) return advanceCarrier();
            if (o.controller) return advanceCarrier();
            const bool surfaceCoasting=!u.type->canFly && u.speed>Fixed();
            o.transportUnloadReleasePending=false;
            if (u.type->canFly && o.flightGoal && u.cargo.empty() &&
                pickupLeg+1==u.orders.size()) {
                u.retainedFlightGoal=o.flightGoal;
                u.retainedFlightControllerActive=true;
                removeUnloadLeg();
                // Retail retires VTOL_UNLOAD, then still calls the mover with
                // its independent step-out controller on this same tick.
                return false;
            }
            removeUnloadLeg();return !surfaceCoasting;
        }
        if (surfaceRouteActive) return advanceCarrier();
        removeUnloadLeg();return true;
    }
    Unit* c=unit(u.cargo.front());const int id=c->id;
    if (o.transportPassenger!=id) {
        o.transportPassenger=id;o.transportX=unloadX;o.transportZ=unloadZ;o.transportTicks=0;
        o.transportMission=RetailMissionState{uint8_t(u.type->canFly ? 1 : 2)};
        o.transportApproachAttempts=u.type->canFly ? 0 : 1;
        if (u.type->canFly && !o.flightGoal) notifyUnitScript(u,"BeginFlight");
    }
    auto& mission=o.transportMission;
    if(tickCounter_>=mission.deadline) {
        mission.deadline=0xffffffffu;mission.pending|=1;
    }
    const uint32_t events=(u.missionEvents|mission.pending)&mission.waitMask;
    if(mission.waitMask && !events)return advanceCarrier();
    u.missionEvents&=~events;mission.pending&=~events;mission.waitMask=0;
    if (mission.stage==1) {
        if (u.type->canFly) {
            // Both approach and departure install a new native controller.
            mission.pending&=~0x3700u;
            if (!inRange) {
                o.flightGoal=RetailFlightGoal{{o.x.v,u.flightY.v,o.z.v},0x30,0,
                    int16_t(u.type->transportDist-34)};
                mission.waitMask=0x700;mission.sleep(tickCounter_,pathRand(6)+6);
                return advanceCarrier();
            }
            o.flightGoal=retailUnloadStepOut({u.x.v,u.flightY.v,u.z.v},
                portHeadingToRetail(u.heading),uint16_t(u.type->transportDist));
        }
        ++o.transportApproachAttempts;mission.stage=2;
        mission.sleep(tickCounter_,1);return advanceCarrier();
    }
    const int cx=int16_t(uint32_t(unloadX.v)>>20),cz=int16_t(uint32_t(unloadZ.v)>>20);
    auto placement=[&](bool allowMoving) {
        if (!mapPlacementCells_.empty()) return mobilePlacement(*c,cx,cz,allowMoving);
        // Terrain-only harnesses have no native feature plane. Keep their
        // terrain grid, but distinguish movable bodies for the second query.
        const int foot=footCells(c->type);
        if (!navFor(c->type).fits(cx+foot/2,cz+foot/2,foot)) return false;
        const auto bodies=searchBodyRect(cx,cz,c->type->footX,c->type->footZ);
        for (const Unit* body:bodies.cells) if (body && body->id!=id &&
                (!allowMoving || body->type->isStructure())) return false;
        return true;
    };
    const int result=retailUnloadTransfer(mission,o.transportTicks,o.transportApproachAttempts,
        tickCounter_,true,placement,[&] {
            transportEffects_.push_back({tickCounter_,
                {o.transportX.v,o.transportY.v,o.transportZ.v},
                {u.x.v,(u.type->canFly ? u.flightY : u.groundY).v,u.z.v}});
        },[] {});
    if (result==1) { ++mission.stage;return advanceCarrier(); }
    if (result==8 || result==7) { removeUnloadLeg();return true; }
    if (result==9) {
        if (pickupLeg+1<u.orders.size()) removeUnloadLeg();
        else {
            mission=RetailMissionState{uint8_t(u.type->canFly ? 1 : 2)};
            mission.flags|=0x400000u;mission.sleep(tickCounter_,pathRand(30)+15);
            o.transportTicks=0;o.transportApproachAttempts=u.type->canFly ? 0 : 1;
        }
        return advanceCarrier();
    }
    if (mission.stage!=4) return advanceCarrier();
    c->x=o.transportX;c->z=o.transportZ;c->inTransport=0;
    c->groundY=surfaceHeight(*c,tickCounter_,&c->groundPitch,&c->groundRoll);
    updateBodyIndex(*c);refreshMovingSearchBody(*c);
    rebuildOccupancy();
    std::erase(u.cargo,id);
    {
        const Unit* next=nullptr;uint32_t remaining=0;
        for (int aboard:u.cargo) if (const Unit* passenger=unit(aboard);
                passenger && passenger->alive() && passenger->inTransport==u.id) {
            if (!next) next=passenger;
            ++remaining;
        }
        const uint32_t first=pathRand(3),second=pathRand(3);
        Order park;park.goal=park.groundMission=true;park.park.emplace();
        park.park->permanent=u.incapacitated() ? 1 : 0;
        park.park->padding=retailUnloadParkPadding(first,second,remaining,
            int16_t(next ? next->type->footX : 0),int16_t(next ? next->type->footZ : 0));
        park.mission.flags=0x200u;
        c->orders.push_back(park);
    }
    o.transportPassenger=0;
    if (u.cargo.empty()) {
        // Both GROUND_UNLOAD and VTOL_Unload leave their now-empty mission in
        // the queue until the following one-tick sleep expires.
        o.transportUnloadReleasePending=true;
        mission.stage=0;
        mission.waitMask=1;mission.deadline=tickCounter_+1;
        mission.pending=0x500u;
    } else {
        o.transportTicks=0;
    }
    return advanceCarrier();
}

void World::attackMove(int unitId, float x, float z, bool queue) {
    Unit* u = unit(unitId);
    // isStructure(), NOT canMove: the Keep and both Taros/Veruna walls declare
    // canmove=1 with no velocity (the CLAUDE.md gotcha). Gating on canMove let a
    // BUILDING accept a move order -- it could not go anywhere, but the mover still
    // turned its heading toward the goal, so you could spin a keep by right-clicking.
    if (!u || !u->alive() || !u->type) return;
    if (u->type->isStructure()) {
        Order o{Fixed::fromFloat(x), Fixed::fromFloat(z), 0};
        o.attackMove = true;
        setRally(*u, o, queue);
        return;
    }
    size_t before = queue ? u->orders.size() : 0;
    order(unitId, x, z, queue);
    for (size_t i = before; i < u->orders.size(); ++i) u->orders[i].attackMove = true;
}

void World::patrol(int unitId, float x, float z) {
    Unit* u = unit(unitId);
    // isStructure(), NOT canMove: the Keep and both Taros/Veruna walls declare
    // canmove=1 with no velocity (the CLAUDE.md gotcha). Gating on canMove let a
    // BUILDING accept a move order -- it could not go anywhere, but the mover still
    // turned its heading toward the goal, so you could spin a keep by right-clicking.
    if (!u || !u->alive() || !u->type) return;
    if (u->type->isStructure()) {
        // The rally patrols between the building and the clicked point: a unit off the
        // line walks out and then loops, which is what a patrol rally means.
        //
        // The return leg stops just OUTSIDE the producer, not at its centre. A building
        // occupies its whole footprint, so a return waypoint on its own position is a
        // cell no ground unit can stand in: produced units walked to the clicked point
        // and then pushed against the factory instead of looping, until the
        // no-headway watchdog discarded the leg. Step out along the patrol line by the
        // building's half-width plus a cell, which is the direction they are coming
        // back from anyway.
        float rx = u->x.toFloat(), rz = u->z.toFloat();
        {
            const float dx = x - u->x.toFloat(), dz = z - u->z.toFloat();
            const float len = detmath::len(dx, dz);
            if (len > 1.0f) {
                const float out = float(std::max(u->type->footX, u->type->footZ)) * 8.0f + 16.0f;
                rx = u->x.toFloat() + dx / len * out;
                rz = u->z.toFloat() + dz / len * out;
            }
        }
        Order b{Fixed::fromFloat(x), Fixed::fromFloat(z), 0};   b.patrol = true; b.attackMove = true;
        Order a{Fixed::fromFloat(rx), Fixed::fromFloat(rz), 0}; a.patrol = true; a.attackMove = true;
        setRally(*u, b, /*queue=*/false);
        setRally(*u, a, /*queue=*/true);
        return;
    }
    u->orders.clear();
    cancelPath(*u);          // the route for the replaced orders is not this patrol's
    Order a;
    a.x = u->x; a.z = u->z; a.patrol = true; a.attackMove = true;
    Order b;
    b.x = Fixed::fromFloat(x); b.z = Fixed::fromFloat(z); b.patrol = true; b.attackMove = true;
    // These are two complete legs, not intermediate points of one route.
    // Without boundaries, currentLeg selects the return point at our feet.
    a.goal = b.goal = true;
    u->orders.push_back(b);
    u->orders.push_back(a);
}

void World::patrolTo(int unitId, float x, float z, bool queue) {
    Unit* u = unit(unitId);
    // isStructure(), NOT canMove: the Keep and both Taros/Veruna walls declare
    // canmove=1 with no velocity (the CLAUDE.md gotcha). Gating on canMove let a
    // BUILDING accept a move order -- it could not go anywhere, but the mover still
    // turned its heading toward the goal, so you could spin a keep by right-clicking.
    if (!u || !u->alive() || !u->type) return;
    if (u->type->isStructure()) {
        Order o{Fixed::fromFloat(x), Fixed::fromFloat(z), 0};
        o.patrol = true;
        o.attackMove = true;
        setRally(*u, o, queue);
        return;
    }
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
    if (!queue) {
        cancelPath(*u);
        u->orders.clear();
    }
    Order o;
    o.x = u->x;
    o.z = u->z;
    o.wait = seconds > 0 ? int32_t(seconds * kTick + 0.5f) : 0;
    u->orders.push_back(o);
}

void World::orderWaitAttack(int unitId, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type) return;
    if (!queue) {
        cancelPath(*u);
        u->orders.clear();
    }
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
    if (!queue) {
        u->orders.clear();
        cancelPath(*u);
        u->routeStamp = -1;
    }
    Order o;
    o.targetId = targetId;
    o.guard = true;
    o.goal = true;
    o.x = t->x;
    o.z = t->z;
    u->orders.push_back(o);
}

void World::stop(int unitId) {
    Unit* u = unit(unitId);
    if (!u) return;
    u->orders.clear();
    u->landing.reset(); u->standbyActive=false;
    cancelPath(*u);          // stop means stop: do not finish routing where it was going
    // Stop also halts a conjurer: cancel the infinite loop and drain the queue.
    u->repeatType = nullptr;
    u->buildQueue.clear();
    u->buildProgress = 0;
    u->productionSiteId = 0;
}

void World::destroy(int unitId) {
    // Self-destruct: TOGGLE the countdown. Arming a live unit starts the timer;
    // pressing again while it counts down cancels it. On expiry (in tick) the
    // unit leaves your command -- it fades, without an explosion, without a
    // wreck, and without giving anyone kill credit.
    //
    // The length is the type's `selfdestructcountdown` in SECONDS. Retail packs
    // it into three bits of UnitType+0x264 (0x4c09e8 masks it & 7, shifts 21)
    // and the mission at 0x4017e0 steps it once a second.
    //
    // Nothing in the shipped data declares that key -- not one of the 558 FBIs
    // across every archive -- which is exactly why the DEFAULT matters, and the
    // default is not zero. When the key is absent the parser clears bit 23 and
    // sets bit 22 (0x4c0a1f), leaving the field at 2. Emulating the mission
    // confirms it: at 2 it reschedules twice at 30 ticks, expires, and applies
    // its 30000 damage on the run after -- two seconds, which is what the game
    // gives you. See tools/re/emuself.py.
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type) return;
    const float len = float(u->type->selfDestructCountdown);
    u->selfDestructT = (u->selfDestructT < 0) ? int32_t(len * kTick + 0.5f) : -1;
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
    // A type with no stance cannot be given one. The HUD already hides the buttons;
    // this is the authoritative half, so a modified client cannot set a stance on
    // something retail would not offer it for.
    if (u->type && !u->type->canSetStance) return;
    if (!u->orders.empty() && u->orders.front().autoTarget) {
        // A stance change ends the autonomous engagement immediately. Explicit
        // player attacks and the underlying queued movement orders remain valid.
        dropLeg(*u);cancelPath(*u);u->routeStamp=-1;
    }
    u->stance = std::clamp(stance, 0, 2);
    u->standingOrder = uint8_t(2-u->stance);
    // Retail's Standing_UnitOrder setter (icd 0x5198a0) writes the move AND fire
    // fields together, and note what it CANNOT write: move 2, the unlimited-chase
    // roam state. A unit that spawned roaming (its type sets no standingunitorder)
    // gives that up permanently the first time the player touches these buttons.
    // That is retail's behaviour, not an oversight of ours.
    switch (u->stance) {
        case 0: u->moveState = 1; u->fireState = 2; break;   // Offensive
        case 1: u->moveState = 0; u->fireState = 2; break;   // Defensive
        default: u->moveState = 0; u->fireState = 0; break;  // Passive
    }
}

void World::setCloak(int unitId, bool on) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->canCloak) return;
    u->cloakOn = on;
}

void World::setActive(int unitId, bool on) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->onOffable) return;
    if (u->active==on) return;
    u->active = on;
    if (u->type->gate) {
        // 51e4d0 notifies the script immediately on an active-bit edge.
        if (auto script=unitScripts_.find(u->id);script!=unitScripts_.end())
            script->second.activated=on;
        notifyUnitScript(*u,on ? "Activate" : "Deactivate");
    }
}

void World::setSquad(int unitId, int squad) {
    Unit* u = unit(unitId);
    if (!u || !u->alive()) return;
    u->squad = int8_t(std::clamp(squad, -10, 10));   // 0 none, +N group N, -N formation N
}

void World::attack(int unitId, int targetId, bool queue) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type) return;
    // A production building sends its OUTPUT at the target rather than shooting it
    // itself -- and it may well have no weapon, so this is checked before the
    // weapon gate below.
    if (u->type->isStructure() && u->type->producesUnits()) {
        Order o{Fixed(), Fixed(), targetId};
        setRally(*u, o, queue);
        return;
    }
    if (u->type->weapon.damage <= 0) return;
    if (!queue) {
        u->orders.clear();
        cancelPath(*u);
        u->routeStamp = -1;
    }
    u->orders.push_back({Fixed(), Fixed(), targetId});
    u->orders.back().goal = true;                 // an attack order IS its own leg
    u->orders.back().issuedTick = tickCounter_;
}


// Intern every category token to a small int and resolve each weapon's overrides
// against it. Done ONCE after loading, never afterwards: several rooms tick in
// parallel over one shared registry, so anything lazily written here would race.
//
// Deterministic: types_ is a name-sorted std::map and dmgVs a string-keyed map, so
// both are walked in the same order on every peer and the same token always gets the
// same id. Order within a weapon's override list is preserved, which is what keeps
// category precedence identical -- damageVs returns the FIRST category that matches.
void TypeRegistry::internCategories() {
    catIds_.clear();
    auto idOf = [&](const std::string& tok) {
        auto it = catIds_.find(tok);
        if (it != catIds_.end()) return it->second;
        const int id = int(catIds_.size());
        catIds_.emplace(tok, id);
        return id;
    };
    for (auto& [name, t] : types_) {
        t.catIds.clear();
        t.catIds.reserve(t.categories.size());
        for (const auto& c : t.categories) t.catIds.push_back(idOf(c));
    }
    // Weapon overrides resolve against the SAME table. A token no type ever declares
    // interns here too rather than being dropped: it simply never matches, exactly as
    // the string lookup never matched it.
    for (auto& [name, t] : types_)
        for (auto& w : t.weapons) {
            w.dmgVsIds.clear();
            w.dmgVsIds.reserve(w.dmgVs.size());
            for (const auto& [k, v] : w.dmgVs) w.dmgVsIds.emplace_back(idOf(k), v);
        }
}

float Weapon::damageVs(const UnitType* t) const {
    // A per-category DAMAGE entry is a MULTIPLIER on `default`, not a damage figure.
    // This returned the entry directly, which is how the Barracks became unkillable:
    // arakeep is damagecategory=factory, the Crusades swordsman declares factory=0.5,
    // and 0.5 was read as half a hit point. Against 17162 HP with healtime=1.25 (0.8
    // HP/s of regen) the building out-healed a besieging army by three orders of
    // magnitude -- every arrow visibly connecting, the health bar never moving.
    //
    // The data says multiplier plainly once you look at all of it. Across both
    // datasets the values cluster on 0.04/0.08/0.2/0.25/0.5/0.75/1.1/1.25/1.5/2/3 --
    // 0.25 and 0.5 alone are 129 of the 249 entries in unitscb. Read as absolute
    // damage those are fractions of one HP, i.e. every override in the game would
    // mean "this weapon does nothing", which is not a balance system. Read as
    // multipliers they are exactly the expected table: siege weapons strong against
    // structures (arasmith factory=2.0, fort=2.0), infantry weak against them
    // (factory=0.5), an assassin devastating against soft targets (araspy human=6,
    // tier1=6), anti-air multiplied against flyers (verball airship=4). The clincher
    // is npcemen, a campaign duellist with buri=100 against Lord Buriash (2940 HP):
    // as a multiplier that is the scripted one-shot kill the mission wants, as
    // absolute damage it is 100 and the duel takes thirty hits.
    if (t && !dmgVsIds.empty()) {
        // Integer compares over two short vectors, instead of a string-keyed tree walk
        // per candidate. Same traversal order, so the same category wins.
        for (int c : t->catIds)
            for (const auto& [k, v] : dmgVsIds)
                if (k == c) return damage * v;
        return damage;
    }
    // Fallback for a registry that was never interned (tools, tests): the original
    // lookup, so behaviour does not depend on whether internCategories() ran.
    if (t && !dmgVs.empty())
        for (const auto& c : t->categories) {
            auto it = dmgVs.find(c);
            if (it != dmgVs.end()) return damage * it->second;
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
        hf.fromX = from ? from->x.toFloat() : hx;
        hf.fromZ = from ? from->z.toFloat() : hz;
        hf.fromPlayer = fromPlayer;
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
            if (e.hp <= Fixed()) return;
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
        e.hp -= Fixed::fromFloat(dealt);
        if (e.hp <= Fixed()) {
            e.overkill = fxMax(e.overkill, -e.hp);          // retail severity input
            e.deathType = uint8_t(w.dmgType);                  // 3 = explosion -> gib
        }
        if (fromId) e.lastHitBy = fromId;
        if (w.status != Weapon::Status::None && e.type) {
            bool immune =
                (w.status == Weapon::Status::Frozen && e.type->cantBeFrozen) ||
                (w.status == Weapon::Status::Stoned && e.type->cantBeStoned);
            if (!immune) {
                if (w.status == Weapon::Status::Paralyzed) {
                    e.paralyzedFor = std::max(e.paralyzedFor, int32_t(w.statusDur * kTick + 0.5f));
                    e.speed = Fixed();
                } else {
                    // Retail petrify/freeze (icd 0x51a61a): HP zeroes INSTANTLY
                    // regardless of amount -- the victim dies on the spot and
                    // the death edge places its stone=/frozen= STATUE (blocking,
                    // permanent, resurrectable back to life). The old temporary
                    // stoned/frozen debuff was our pre-RE guess.
                    bool freeze = w.status == Weapon::Status::Frozen;
                    if (freeze) e.frozenFor = int32_t(kTick); else e.stonedFor = int32_t(kTick);
                    e.hp = Fixed();
                    e.deathType = freeze ? 15 : 14;
                    e.speed = Fixed();
                    static const bool kStatLog = std::getenv("TAK_BURNLOG") != nullptr;
                    if (kStatLog)
                        std::fprintf(stderr, "statue kill: %s %s at %.0f,%.0f\n",
                                     e.type->id.c_str(), freeze ? "frozen" : "stoned",
                                     e.x.toFloat(), e.z.toFloat());
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
                    float fdx = f.x.toFloat() - hx, fdz = f.z.toFloat() - hz;
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
        float dx = e.x.toFloat() - hx, dz = e.z.toFloat() - hz;
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

// Retail leads a MOVING unit target: the aim point is the target's centre plus its
// velocity times a fraction of the flight time (icd 0x51aa50). The fraction is the
// fixed-point literal 0xCCCC/65536, so retail deliberately under-leads by 20% --
// shots land slightly behind a runner rather than perfectly on it, which is what
// makes a siege shell miss a moving blob and crater instead.
// A ground-point target, a melee swing and a hitscan beam never lead; nor does a
// weapon carrying `dontleadtargets`.
static constexpr float kLeadFrac = float(0xCCCC) / 65536.0f;   // 0.79998779296875

static void leadAim(const Unit& shooter, const Unit& tgt, const Weapon& w,
                    float& ax, float& az) {
    ax = tgt.x.toFloat();
    az = tgt.z.toFloat();
    if (w.noLead || w.melee || w.beam || w.projVel <= 0.0f || tgt.speed.toFloat() == 0.0f) return;
    // Distance is the PRE-lead one, matching retail: a single pass, no iteration.
    float d = detmath::len((tgt.x - shooter.x).toFloat(), (tgt.z - shooter.z).toFloat());
    float t = kLeadFrac * d / w.projVel;
    // Velocity from heading x speed is exact -- it is literally what the mover
    // integrates each tick.
    const SinCos tsc = fxSinCos(tgt.heading);
    ax += tsc.s.toFloat() * tgt.speed.toFloat() * kTick * t;
    az += tsc.c.toFloat() * tgt.speed.toFloat() * kTick * t;
}

void World::fire(Unit& u, Unit& target, int slot,bool scriptTriggered) {
    const Weapon& w = u.type->weapons[size_t(slot)];
    // manapershot: a caster spends personal mana to fire; if it can't pay, the
    // shot doesn't happen (reload not consumed, so it fires the moment it can).
    if (w.manaCost > 0 && u.type->maxMana > 0) {
        if (!scriptTriggered && u.mana < w.manaCost) return;
        u.mana -= w.manaCost;
    }
    // Veterans reload faster (retail divides the cooldown by the veteran multiplier).
    const float rl = w.reload / std::max(u.vetMul(), 0.01f);
    if(!scriptTriggered) {u.reloads[slot] = int32_t(rl * kTick + 0.5f);u.fireAnimations|=uint32_t(1)<<slot;u.weaponAnimations.add(tak::RetailWeaponAnimation::Fire,slot);}   // seconds -> ticks, as retail stores it
    u.justFired = true;
    if (slot < 32) u.firedWeapons |= uint32_t(1) << slot;
    // Remote Effect: nothing travels. The spell materialises at the AIMED GROUND
    // POINT and lands after builduptime -- so walking aside doesn't dodge an
    // Earthquake, you have to leave its (up to 500px) radius. decaytime is the
    // visual fade after the hit.
    if (w.kind == Weapon::Kind::Remote) {
        PendingEffect e;
        e.w = &w;
        e.x = target.x; e.z = target.z;
        e.player = u.player; e.fromId = u.id;
        // shotart: this spell is DELIVERED. A visible shot flies to the aim point
        // first and the channel only starts when it lands, so the Acolyte lobs its
        // Earthquake rather than conjuring it under your feet. The shot is purely
        // cosmetic -- targetId 0 means it collides with nothing and simply expires
        // on arrival; the PendingEffect below carries all the actual damage.
        float deliver = 0.0f;
        if (!w.shotArt.empty() && w.projVel > 0) {
            float sdx = (target.x - u.x).toFloat(), sdz = (target.z - u.z).toFloat();
            float sdist = std::max(detmath::len(sdx, sdz), 1e-3f);
            const float svel = w.projVel;            // px/s from the FBI
            deliver = sdist / svel;
            Projectile s;
            s.x = u.x; s.z = u.z;
            s.vx = Fixed::fromFloat(sdx / sdist * svel / kTick);   // px per TICK
            s.vz = Fixed::fromFloat(sdz / sdist * svel / kTick);
            s.wsrc = &w;
            s.targetId = 0;              // cosmetic: hits nothing, just flies
            s.fromPlayer = u.player;
            s.fromId = u.id;
            s.fx = w.fx;
            s.life = int32_t(deliver * kTick + 0.5f);
            s.flight = int32_t(deliver * kTick + 0.5f);
            projectiles_.push_back(s);
        }
        switch (w.remote) {
            case Weapon::RemoteKind::Earthquake:
                // Pulses every `shakeduration` from impact, right through buildup
                // AND decay -- for the drake's Earthquake the single pulse actually
                // lands inside the decay window.
                e.period = int32_t(std::max(w.shakeDur, 0.1f) * kTick + 0.5f);
                e.at = e.period;
                e.endAt = int32_t((w.buildUp + w.decay) * kTick + 0.5f);
                break;
            case Weapon::RemoteKind::Hailstorm: {
                // The rain: buildup/decay don't apply. It starts falling shortly
                // after the cast and pulses particlespersecond times a second for
                // `duration` -- so the Acolyte's Hail Shower is ~12 small hits, not
                // one 70-damage tap.
                float pps = w.particlesPerSec > 0 ? float(w.particlesPerSec) : 5.0f;
                e.period = int32_t(kTick / pps + 0.5f);
                e.at = 20;                                // retail's ~20-tick lead-in, exactly
                e.endAt = 20 + std::max(int32_t(w.duration * kTick + 0.5f), e.period);
                break;
            }
            case Weapon::RemoteKind::MindCtl:
            case Weapon::RemoteKind::Freeze:
                // One sweep when the channel completes -- and killing the caster
                // mid-channel ABORTS it, which is real counterplay against a Mind
                // Mage winding up an area charm.
                e.at = int32_t(w.buildUp * kTick + 0.5f);
                e.endAt = int32_t((w.buildUp + w.decay) * kTick + 0.5f);
                e.casterGated = true;
                break;
            case Weapon::RemoteKind::Plain:
            default:
                e.at = int32_t(w.buildUp * kTick + 0.5f);
                e.endAt = int32_t((w.buildUp + w.decay) * kTick + 0.5f);
                break;
        }
        // The channel clock starts when the shot LANDS, not when it is thrown.
        e.at += int32_t(deliver * kTick + 0.5f);
        e.endAt += int32_t(deliver * kTick + 0.5f);
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
        const auto aim=queryWeaponAim(u.id,target.id,slot);
        const std::array<int32_t,3> point=aim ? aim->target :
            std::array<int32_t,3>{target.x.v,target.groundY.v,target.z.v};
        const int32_t raw=std::max(0,int32_t(double(w.projVel)*2184.5333333333333));
        s.substeps=std::max(1u,(uint32_t(raw)+0xfffffu)>>20);
        const auto launch=retailStormLaunch({u.x.v,(u.type->canFly ? u.flightY : u.groundY).v,u.z.v},
            point,uint32_t(raw)/s.substeps,s.substeps,w.maxVariation);
        s.w = &w;
        s.x=Fixed::raw(launch.position[0]);s.z=Fixed::raw(launch.position[2]);
        const int height=heights_.empty() ? 0 : retailTerrainHeight(s.x.v,s.z.v,terW_,terH_,[&](int x,int z) {
            return heights_[size_t(z)*size_t(terW_)+size_t(x)];
        });
        s.y=Fixed::fromInt(height);s.baseVelocity=launch.baseVelocity;s.variation=launch.variation;
        s.player = u.player; s.fromId = u.id;
        s.id = ++stormSeq_;
        s.start = tickCounter_ + 1;
        if(w.nimbus && u.type->hasNimbusArt)s.start += uint32_t(w.buildUpTicks);
        uint16_t heading=u.weaponAim[size_t(slot)].heading,pitch=u.weaponAim[size_t(slot)].pitch;
        if(!scriptTriggered && aim) {
            heading=aim->heading;pitch=aim->pitch;
        }
        s.wanderSeed=uint32_t(heading)|(uint32_t(pitch)<<16);
        storms_.push_back(s);
        return;
    }
    if(w.flameKind>=0) {
        const int32_t raw=int32_t(double(w.projVel)*2184.5333333333333);
        if(raw<=0)return;
        FlameShot flame;flame.weapon=&w;flame.owner=u.player;flame.fromId=u.id;flame.slot=slot;
        flame.position=queryUnitScriptPoint(u.id,false,slot);
        flame.muzzle=flame.position;
        flame.aimPoint=scriptTriggered ? u.weaponAimPoints[size_t(slot)] :
            std::array<int32_t,3>{target.x.v,(target.type->canFly ? target.flightY : target.groundY).v,target.z.v};
        flame.substeps=std::max(1u,(uint32_t(raw)+0xfffffu)>>20);
        flame.speed=uint32_t(raw)/flame.substeps;
        uint16_t heading=u.weaponAim[size_t(slot)].heading,pitch=u.weaponAim[size_t(slot)].pitch;
        if(!scriptTriggered)if(const auto aim=queryWeaponAim(u.id,target.id,slot)) {
            heading=aim->heading;pitch=aim->pitch;flame.aimPoint=aim->target;
        }
        heading=uint16_t(heading+portHeadingToRetail(u.heading));
        const int32_t horizontal=retailScaledCosine(pitch,int32_t(flame.speed));
        flame.velocity={retailScaledSine(heading,horizontal),
            std::bit_cast<int32_t>(0u-uint32_t(retailScaledSine(pitch,int32_t(flame.speed)))),
            retailScaledCosine(heading,horizontal)};
        flame.start=tickCounter_+1;flame.end=flame.start+uint32_t(w.emitTime);
        flames_.push_back(std::move(flame));
        return;
    }
    if(w.kind==Weapon::Kind::Guided && w.projVel>0) {
        const int32_t raw=int32_t(double(w.projVel)*2184.5333333333333);
        if(raw<=0)return;
        Projectile p;p.guided3d=true;p.wsrc=&w;p.fromId=u.id;p.fromPlayer=u.player;
        p.targetId=target.id;p.slot=slot;p.fx=w.fx;
        p.position=queryUnitScriptPoint(u.id,false,slot);p.muzzle=p.position;
        p.substeps=std::max(1u,(uint32_t(raw)+0xfffffu)>>20);
        const int32_t speed=int32_t(uint32_t(raw)/p.substeps);
        uint16_t heading=u.weaponAim[size_t(slot)].heading,pitch=u.weaponAim[size_t(slot)].pitch;
        std::optional<WeaponAimSolution> aim;
        if(!scriptTriggered)aim=queryWeaponAim(u.id,target.id,slot);
        if(aim) {heading=aim->heading;pitch=aim->pitch;}
        const auto launch=retailGuidedLaunch(p.position,portHeadingToRetail(u.heading),heading,pitch,speed);
        p.position=launch.position;p.velocity=launch.velocity;p.angles=launch.angles;
        // The native base mover observes this new shot again in the firing tick
        // and adds the body heading to the relative BAM yaw before its start tick.
        p.angles[1]=uint16_t(p.angles[1]+portHeadingToRetail(u.heading));
        const int32_t horizontal=retailScaledCosine(pitch,speed);
        const int32_t product=std::bit_cast<int32_t>(uint32_t(w.range)*uint32_t(speed));
        const int32_t denominator=horizontal ? horizontal : speed;
        const int64_t quotient=int64_t(product)/denominator;
        const uint32_t perTick=p.substeps*uint32_t(speed);
        const uint32_t lifetime=((uint32_t(quotient)<<16)+perTick)/perTick;
        p.start=tickCounter_+1;
        if(w.nimbus && u.type->hasNimbusArt)p.start+=uint32_t(w.buildUpTicks);
        p.end=p.start+lifetime;p.life=int32_t(lifetime);p.flight=int32_t(lifetime);
        p.x=Fixed::raw(p.position[0]);p.z=Fixed::raw(p.position[2]);
        projectiles_.push_back(p);
        return;
    }
    if((w.straight || w.lightning) && w.projVel>0) {
        const int32_t raw=int32_t(double(w.projVel)*2184.5333333333333);
        if(raw<=0)return;
        Projectile p;p.straight=true;p.wsrc=&w;p.fromId=u.id;p.fromPlayer=u.player;
        p.targetId=target.id;p.slot=slot;p.fx=w.fx;
        p.position=queryUnitScriptPoint(u.id,false,slot);
        p.muzzle=p.position;
        if(w.lightningEffect)p.lightningEffect=w.lightningEffect->initial;
        p.substeps=std::max(1u,(uint32_t(raw)+0xfffffu)>>20);
        const int32_t speed=int32_t(uint32_t(raw)/p.substeps);
        uint16_t heading=u.weaponAim[size_t(slot)].heading,pitch=u.weaponAim[size_t(slot)].pitch;
        if(!scriptTriggered)if(const auto aim=queryWeaponAim(u.id,target.id,slot)) {
            heading=aim->heading;pitch=aim->pitch;
        }
        p.angles={0,heading,uint16_t(0u-pitch)};
        heading=uint16_t(heading+portHeadingToRetail(u.heading));
        const int32_t horizontal=retailScaledCosine(pitch,speed);
        p.velocity={retailScaledSine(heading,horizontal),
            std::bit_cast<int32_t>(0u-uint32_t(retailScaledSine(pitch,speed))),
            retailScaledCosine(heading,horizontal)};
        const int32_t product=std::bit_cast<int32_t>(uint32_t(w.range)*uint32_t(speed));
        const int64_t quotient=int64_t(product)/(horizontal ? horizontal : speed);
        const uint32_t perTick=p.substeps*uint32_t(speed);
        p.start=tickCounter_+1;
        if(w.nimbus && u.type->hasNimbusArt)p.start+=uint32_t(w.buildUpTicks);
        p.end=p.start+((uint32_t(quotient)<<16)+perTick)/perTick;
        p.x=Fixed::raw(p.position[0]);p.z=Fixed::raw(p.position[2]);p.life=1;
        projectiles_.push_back(p);
        return;
    }
    if (w.melee || w.beam || w.projVel <= 0) {
        // Legacy delivery for the remaining classes; flames use the native
        // advancing front above. Other Line-of-Sight subclasses remain separate.
        applyHit(w, target.x.toFloat(), target.z.toFloat(), u.player, u.id, &target);
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
        // Retail solves the release velocity so the bomb arrives over the aim point
        // exactly as it finishes falling: vel = (aim - release) / fallTicks, with
        // ZERO vertical speed. weaponvelocity is never read here and neither is the
        // flyer's heading -- release point and timing do not change where the bomb
        // lands. (It LOOKS like inherited momentum only because a closing bomber is
        // already pointed at its target.)
        float bdx = (target.x - u.x).toFloat(), bdz = (target.z - u.z).toFloat();
        b.vx = Fixed::fromFloat(bdx / kBombFall / kTick);   // px per TICK
        b.vz = Fixed::fromFloat(bdz / kBombFall / kTick);
        b.wsrc = &w;
        b.targetId = target.id;
        b.fromPlayer = u.player;
        b.fromId = u.id;
        b.fx = w.fx;
        b.life = int32_t(kBombFall * kTick + 0.5f);   // ticks to fall from cruise altitude
        b.flight = int32_t(kBombFall * kTick + 0.5f);    // the viewer arcs it down over the same window
        projectiles_.push_back(b);
        return;
    }
    Projectile p;
    p.x = u.x;
    p.z = u.z;
    // Aim where the target WILL be, not where it is (see leadAim). The projectile
    // keeps targetId so the swept proximity test still resolves a direct hit; what
    // changes is that a shot at a runner now lands behind it and detonates there.
    float aimX = target.x.toFloat(), aimZ = target.z.toFloat();
    leadAim(u, target, w, aimX, aimZ);
    float dx = aimX - u.x.toFloat(), dz = aimZ - u.z.toFloat();
    float dist = std::max(std::sqrt(dx * dx + dz * dz), 1e-3f);
    const float vel = w.projVel;             // weaponvelocity, px/s
    p.vx = Fixed::fromFloat(dx / dist * vel / kTick);   // px per TICK, as retail stores it
    p.vz = Fixed::fromFloat(dz / dist * vel / kTick);
    p.wsrc = &w;
    // Retail BallisticWeapon::initShot selects and stores a model pointer at
    // launch, so later veterancy changes cannot change an in-flight bolt.
    p.projectileUsesVeteranModel = w.usesVeteranShotModel(u.veteran);
    // Keep the existing X/Z projectile and hit logic intact, but retain retail's
    // three-dimensional ballistic state for model-backed shots. The renderer uses
    // this state to place and orient the mesh; it never feeds damage or collision.
    if (w.ballistic && w.kind == Weapon::Kind::Normal && !w.melee &&
        !w.beam && !w.shotModel.empty()) {
        const double rawSpeed = double(w.projVel) * 2184.5333333333333;
        if (rawSpeed > 0.0 && rawSpeed < double(INT32_MAX)) {
            const int32_t raw = int32_t(rawSpeed);
            const uint32_t steps = std::max(1u, (uint32_t(raw) + 0xfffffu) >> 20);
            const auto aim = scriptTriggered ? std::optional<WeaponAimSolution>{} :
                                               queryWeaponAim(u.id, target.id, slot);
            const uint16_t heading = scriptTriggered ? u.weaponAim[size_t(slot)].heading :
                aim ? aim->heading : u.weaponAim[size_t(slot)].heading;
            const uint16_t pitch = scriptTriggered ? u.weaponAim[size_t(slot)].pitch :
                aim ? aim->pitch : u.weaponAim[size_t(slot)].pitch;
            const auto muzzle = queryUnitScriptPoint(u.id, false, slot);
            const auto native = retailBallisticLaunch(muzzle, portHeadingToRetail(u.heading),
                heading, pitch, raw / int32_t(steps));
            p.position = native.position;
            p.velocity = native.velocity;
            p.angles = native.angles;
            p.substeps = steps;
            p.ballistic3d = true;
        }
    }
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
    // A dumb shot is fuelled to EXACTLY its aim point, because that is where it now
    // detonates: the old half-second of slack would put an arapult's crater ~375px
    // (23 cells) beyond where the shell was drawn landing. A GUIDED one keeps the
    // slack and is fuelled from the weapon's RANGE instead.
    // CEIL, not truncate. A dumb shot is fuelled to exactly its aim point, so a
    // flight of 2.4 ticks rounded down to 2 expired one step short and the last
    // segment -- the one that reaches the target -- was never collision-tested.
    // Fast shots missed stationary targets and splash landed short.
    p.life = int32_t(std::ceil(kTick * (w.kind == Weapon::Kind::Guided
                                  ? (std::max(float(w.range), dist) / vel + 0.5f)
                                  : (dist / vel))));
    p.flight = int32_t(dist / vel * kTick + 0.5f);
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

// Naval shots cross both water and shore. Water depth is a movement restriction,
// not a projectile blocker; sight grids still consult the shared obstacle overlay.
// Ground-only combat retains its existing sight test and navigation is unchanged.
bool World::combatLineOfSight(const Unit& from,const Unit& to) const {
    const bool naval=from.type->domain==UnitType::Domain::Water ||
                     to.type->domain==UnitType::Domain::Water;
    return (naval?navalSight_:nav_).losBetween(from.x.toFloat(),from.z.toFloat(),to.x.toFloat(),to.z.toFloat(),
        std::max(from.type->footX,from.type->footZ)/2,
        std::max(to.type->footX,to.type->footZ)/2);
}

int World::findTarget(Unit& u, bool missionPoll, bool groundResponse) {
    // VTOL landing also clears active. Only switchable units interpret it as
    // power-off; ordinary landed flyers must still acquire and fire.
    if (u.type->onOffable && !u.active) return false;
    // Auto-acquire: idle armed units engage the nearest enemy in reach;
    // attack-movers and patrollers interrupt their route to fight. A PASSIVE unit
    // (stance 2, hold fire) never auto-acquires -- it only fights when ordered.
    // A builder mid-job -- constructing a site, repairing, reclaiming, holding
    // queued builds, or producing from a queue -- stays on task: it does NOT
    // auto-acquire a nearby enemy and wander off to fight while it should build.
    bool busyBuilding = u.buildSiteId != 0 || u.repairId != 0 || u.reclaimId != 0 ||
                        hasQueuedWork(u) ||
                        (!u.type->isStructure() && (!u.buildQueue.empty() || u.repeatType));
    // Auto-acquisition belongs to the FIRE order: only Fire At Will goes looking.
    // Hold Fire and Return Fire both wait to be handed a target (an explicit attack
    // order still works -- retail's hold-fire gate has a "forced" bypass for
    // exactly that).
    bool acquiring = !busyBuilding && u.fireState == 2 &&
                     (groundResponse || u.orders.empty() ||
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
    bool acqTurn = missionPoll || (uint32_t(u.id) + tickCounter_) % acqStride_ == 0;
    if (acquiring && u.type->weapon.damage > 0 && acqTurn) {
        // The +90 is approach margin: room to notice something and walk to it. A
        // unit whose move order forbids leaving has no use for it -- it should
        // acquire only what it can already shoot, or it would lock onto something
        // it will never reach and then drop it again next tick.
        float ar = u.type->maxRange() + (u.moveState == 0 ? 0.0f : 90.0f);
        int best = 0;
        float bestD = ar * ar;
        // fireatwillrandom: spread fire across whatever is in range instead of
        // every unit converging on the nearest body. Only while AUTO-acquiring --
        // an explicit attack order still goes exactly where it was pointed, which
        // is retail's gate too (its scorer is reached only from the fire-at-will
        // path, and every shipped unit is fire-at-will by default since no FBI
        // sets standingfireorder).
        const bool scatter = u.type->fireAtWillRandom;
        float bestScore = 1e30f;
        // maneuverleashlength limits how far an IDLE defender will chase from its
        // post. It must NOT apply while attack-moving/patrolling — those orders
        // mean "advance and engage everything en route", so an army that has
        // travelled far from its spawn still acquires (incl. just-conjured foes).
        // An OFFENSIVE unit (stance 0) ignores its leash entirely and chases freely.
        // ... while whether you may LEAVE to engage belongs to the move order:
        //   0 = never (shoot what wanders into range, but hold the post),
        //   1 = only within maneuverleashlength of where you were posted,
        //   2 = no limit.
        // The leash is measured from home, and an attack-move or patrol overrides
        // it entirely -- those orders mean "advance and engage everything en route".
        float leash2 = (u.moveState == 1 && u.orders.empty() && u.type->leash > 0)
                           ? u.type->leash * u.type->leash : 1e30f;
        // A ranged unit only auto-acquires enemies it can actually see, so it
        // doesn't charge a target hidden behind a wall (which caused pile-ups at
        // corners). Melee/flyers acquire regardless.
        bool ranged = u.type->maxRange() > 64.0f && !u.type->canFly;
        const bool lobber = u.type->lobs();   // shoots OVER obstacles: skip the LoS gate
        uint64_t enemies=~uint64_t(0);
        for (int player=0;player<numPlayers() && player<64;++player)
            if (allied(player,u.player)) enemies&=~(uint64_t(1)<<player);
        forEachNear(u.x.toFloat(), u.z.toFloat(), ar, [&](int idx) {
            const Unit& e = units_[size_t(idx)];
            if (!e.alive() || e.embarked() || allied(e.player, u.player) || !e.type) return;
            if (e.underConstruction) return;   // don't auto-react to a site still conjuring
            float dx = (e.x - u.x).toFloat(), dz = (e.z - u.z).toFloat();
            float d = dx * dx + dz * dz;
            // A scatter-firing unit still only considers what is in RANGE (retail
            // gathers by radius the same way), but inside that set it no longer
            // prefers the nearest -- so the early-out on distance is skipped.
            if (!scatter && d >= bestD) return;
            if (d >= ar * ar) return;
            float hx = (e.x - u.homeX).toFloat(), hz = (e.z - u.homeZ).toFloat();
            if (hx * hx + hz * hz > leash2) return;   // outside the leash
            // Range rejects most candidates. Damage/category lookup is pure,
            // so defer it until these cheap geometric checks have passed.
            if (!canTarget(e)) return;
            if (ranged && !e.type->canFly && !lobber &&
                !combatLineOfSight(u,e))
                return;                         // no clear shot: don't acquire it
            if (scatter) {
                // Retail's scorer, with the flag's substitution in place: n is
                // INT_MAX/damage instead of dist^2/damage, and the winner is the
                // lowest of rand(n)/2 + rand(n). Dividing by damage keeps the
                // draw biased toward whatever this weapon hits hardest, which is
                // the half of the original scoring the flag does NOT remove.
                float dmg = 0;
                for (const auto& wp : u.type->weapons)
                    if (!(e.type->canFly && wp.noAir))
                        dmg = std::max(dmg, wp.damageVs(e.type));
                if (dmg <= 0) return;
                uint32_t n = uint32_t(float(0x7FFFFFFF) / dmg);
                const auto firstRoll = fireRand(n);
                const auto secondRoll = fireRand(n);
                float score = float(firstRoll) / 2.0f + float(secondRoll);
                if (score >= bestScore) return;
                bestScore = score; best = e.id;
                return;
            }
            bestD = d; best = e.id;
        },enemies);
        return best;
    }
    return false;
}

bool World::acquireTarget(Unit& u,bool missionPoll) {
    const int target=findTarget(u,missionPoll);
    if (!target) return false;
    cancelPath(u); u.routeStamp=-1;
    u.orders.insert(u.orders.begin(),{Fixed(),Fixed(),target});
    u.orders.front().autoTarget=true; u.orders.front().goal=true;
    return true;
}

void World::tickCombat(Unit& u, float dt, bool& groundMovementHandled) {
    // One tick per tick. The referee always steps at exactly 1/kServerHz and game
    // speed changes the CADENCE, not dt, so a tick is the sim's real unit of time
    // and the countdown no longer depends on dt at all -- which is both retail's
    // representation and one less float in the checksum.
    // 52ae90 skips unselected and absent slots before updating their reload.
    // Independent multi-weapon units select all slots; switchers freeze the
    // inactive timers until the player selects those weapons again.
    const bool reloadAll=!u.type->weaponSwitching && u.type->weapons.size()>1;
    for(size_t slot=0;slot<std::min(size_t(3),u.type->weapons.size());++slot)
        if((reloadAll || int(slot)==u.weaponSlot) && u.reloads[slot]>0)--u.reloads[slot];
    if (u.type->onOffable && !u.active) return;   // explicitly powered down

    if (!u.standbyActive && !u.guardNoMoveActive) acquireTarget(u, false);
    const int activeTarget=!u.orders.empty() && !u.orders.front().guard ? u.orders.front().targetId : 0;
    if(u.scriptAimTarget && u.scriptAimTarget!=activeTarget)clearScriptWeaponTarget(u);
    const auto canDamage = [&](const Unit& e) {
        for (const auto& wp : u.type->weapons)
            if (!(e.type->canFly && wp.noAir) && wp.damageVs(e.type) > 0.0f) return true;
        return false;
    };
    if (u.orders.empty() || u.orders.front().targetId == 0) return;

    if (u.orders.front().guard) {
        Order& o = u.orders.front();
        Unit* t = unit(o.targetId);
        if (!t || !t->alive()) {
            dropLeg(u);
            cancelPath(u);
            u.routeStamp = -1;
            return;
        }
        Order& end = u.orders[currentLeg(u.orders)];
        end.x = t->x;
        end.z = t->z;
        float dx = (t->x - u.x).toFloat(), dz = (t->z - u.z).toFloat();
        if (dx * dx + dz * dz <= 70 * 70) {
            if (!u.type->canFly && !u.type->isStructure()) {
                brakeGround(u);
                groundMovementHandled=true;
            } else u.speed=fxMax(Fixed(),u.speed-u.type->brake);
        }
        return;   // movement walks toward o when out of reach
    }

    Unit* target = unit(u.orders.front().targetId);
    if (!target || !target->alive() ||
        (target->hp<=Fixed() && !(target->retailSite && target->underConstruction))) {
        // 51a9a0 retires a dying target during lookup, before a pending
        // script release can create a projectile against it.
        if(u.scriptAimTarget)clearScriptWeaponTarget(u);
        dropLeg(u);
        cancelPath(u);
        u.routeStamp = -1;
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
        dropLeg(u);
        cancelPath(u);
        u.routeStamp = -1;
        return;
    }
    if (u.scriptAimTarget!=target->id) {
        u.scriptAimTarget=target->id;
        // An attack must wake the airborne script pose too. Movement alone
        // cannot do this when a landed flyer acquires a target already in range.
        if (u.type->canFly) notifyUnitScript(u,"BeginFlight");
    }
    float dx = (target->x - u.x).toFloat(), dz = (target->z - u.z).toFloat();
    float dist = std::sqrt(dx * dx + dz * dz);
    int slot = u.type->weapons.empty()
                   ? 0 : std::clamp(u.weaponSlot, 0, int(u.type->weapons.size()) - 1);
    // Which weapon(s) this unit fights with. Retail splits multi-weapon units in
    // two, on the FBI `weaponswitching` flag (unitdef+0x264 bit 26):
    //   weaponswitching=1 (24 units, incl. every Monarch and dragon) -- ONE active
    //     weapon at a time, chosen by the PLAYER from the command panel and held
    //     until switched. There is no cleverness: it starts on WEAPON1 and stays
    //     there. (An earlier pass here had the sim auto-pick the biggest usable
    //     weapon, which made these units markedly stronger than retail.)
    //   no flag (10 units: araat, aragod, arasiege, creaeri, cregod, creiron,
    //     targod, verat, verball, vergod) -- every weapon fires INDEPENDENTLY, each
    //     with its own reload, its own range band and its own mana check, so an
    //     AA/ground turret really does work both barrels at once.
    // (`fireatwillrandom` is unrelated to this -- it scatters auto-acquired TARGET
    // choice; see UnitType::fireAtWillRandom.)
    bool allWeapons = !u.type->weaponSwitching && u.type->weapons.size() > 1;
    // Approach on the reach this unit actually fights at: the selected weapon for a
    // switcher, the longest of them when they all fire.
    const Weapon* sel = slot < int(u.type->weapons.size()) ? &u.type->weapons[slot] : nullptr;
    float best = allWeapons ? u.type->maxRange() : (sel ? sel->range : u.type->maxRange());
    // Range is to the target's footprint EDGE, not its centre. A building's centre is
    // deep inside a blocked footprint, so a centre-distance check leaves a short-range
    // attacker grinding the edge (never "in range") or a flyer buried inside it. For a
    // STRUCTURE target, pad the effective range by its footprint half-extent plus a
    // small margin for where nav actually halts at the edge (mirrors the builder reach
    // in tickConstruction). Mobile targets keep centre-distance, so unit-vs-unit combat
    // is unchanged.
    float pad = target->type->maxVel <= Fixed()
                    ? 8.0f * float(std::max(target->type->footX, target->type->footZ)) + 24.0f
                    : 0.0f;
    float reach = best + pad;
    // Melee: retail closes to footprint adjacency (see meleeInRange above);
    // `range` and LoS play no part.
    bool adj = sel && sel->melee && target->type &&
               meleeInRange(u.type, target->type, dx, dz);
    // Ranged units need a clear line to shoot; a wall between them means close
    // in / reposition rather than firing through it (melee & flyers are exempt).
    // A lobbing weapon arcs over what is in the way, which is the whole point of a
    // mortar. Our projectiles are still 2D, so this is an approximation of retail's
    // high-arc solve rather than the mechanism: it lets a lobber shoot over a cliff
    // it could not genuinely clear. Ported faithfully, the arc would decide.
    bool needLoS = best > 64.0f && !u.type->canFly &&
                   target->type && !target->type->canFly && !u.type->lobs();
    // LoS gates ONLY in-range firing and in-range repositioning; an out-of-range
    // chaser advances regardless (the `dist > best*0.95` move test below
    // short-circuits before `los` is read, and the fire gate is never reached out of
    // range). So defer the raycast until we're actually close enough for it to
    // matter -- identical result, but a marching army (the bulk of a big battle, all
    // out of range) stops paying for a per-tick line-of-sight cast it never uses.
    bool los = true;
    if (needLoS && dist <= reach * 0.95f)
        los = combatLineOfSight(u,*target);
    // A static unit cannot chase -- and neither may one whose move standing order
    // says hold position. Both drop an AUTO-acquired target that walks out of
    // reach; an explicitly ordered attack is unaffected, because being told to go
    // kill something is exactly the case the standing order does not govern.
    const bool mayChase = u.type->canMove &&
                          (u.moveState != 0 || !u.orders.front().autoTarget);
    if (!mayChase && dist > reach) {
        dropLeg(u);
        cancelPath(u);
        u.routeStamp = -1;
        return;
    }
    const bool hovering = u.type->canFly && (u.type->hoverAttack || (sel && sel->hoverAttack));
    if (hovering) {
        tickHoverAttack(u,*target,sel);
        groundMovementHandled=true; // the flight controller already committed movement
        dx=(target->x-u.x).toFloat(); dz=(target->z-u.z).toFloat();
        dist=std::sqrt(dx*dx+dz*dz);
    }
    if ((sel && sel->melee) ? !adj
                            : (dist > reach * 0.95f || (!los && mayChase))) {
        if (hovering) return;
        // Advance toward the target, steering around impassable terrain.
        if (u.repathLeft > 0) --u.repathLeft;
        Order& o = u.orders[currentLeg(u.orders)];
        o.x = target->x;
        o.z = target->z;
        // Refresh the leg's destination, preserving its intermediate waypoints.
        // The shared route service and retry cadence handle chases too.
        return;   // movement handled by the normal move logic
    }
    // The attack controller stops navigation and supplies a facing target.
    // Ground movement brakes and commits once; pivoting starts only at rest.
    const Bam want = u.type->canFly ? fxAtan2(Fixed::fromFloat(dx),Fixed::fromFloat(dz))
        : retailDirection(target->x-u.x,target->z-u.z);
    const int32_t diff = bamDiff(want, u.heading);
    if (!u.type->canFly && !u.type->isStructure()) {
        if (u.type->domain==UnitType::Domain::Water && !u.type->turnInPlaceRate) {
            // Ships commonly specify only turnrate. Once in firing range they
            // still need to face the target instead of waiting on a zero pivot rate.
            brakeGround(u);
            if (u.speed==Fixed()) {
                const int32_t turn=retailTurnRequest(want,u.heading);
                u.heading=u.heading+Bam(std::clamp(turn,-u.type->turnRate,u.type->turnRate));
                u.turnReqBam=turn;
            }
        } else brakeGround(u,want);
        groundMovementHandled=true;
    } else if (!hovering) {
        u.speed=fxMax(Fixed(),u.speed-u.type->brake);
        if (u.speed==Fixed()) {
            // Flyers use their flight turn rate while aiming too. Many have no
            // ground turn-in-place rate and otherwise remain stuck off-axis.
            const int32_t maxTurn=u.type->canFly ? u.type->turnRate : u.type->turnInPlaceRate;
            u.heading=u.heading+Bam(std::clamp(diff,-maxTurn,maxTurn));
            u.turnReqBam=diff;
        }
    }
    // Fire a weapon when the target is in ITS [minrange, range] band, within its
    // aimtolerance, with a clear shot, and (unless noairweapon) legal against air.
    // Each weapon is gated on its own terms, because a non-switching unit runs all
    // of them at once and they rarely share a range band.
    auto tryFire = [&](int sl) {
        const Weapon& sw = u.type->weapons[size_t(sl)];
        if (sw.noAir && target->type && target->type->canFly) return;
        if (!(sw.melee || los)) return;
        if (sw.melee ? !adj : dist > sw.range + pad) return;
        if (dist < sw.minRange) return;
        if(tickScriptWeapon(u,*target,sl))return;
        if(u.reloads[size_t(sl)]>0)return;
        // A bomb is let go, not aimed -- retail's Dropped weapon overrides its aim
        // virtual with a no-op, so the bomber is always considered on target (which
        // a hovering flyer with momentum could otherwise almost never satisfy).
        if (sw.kind != Weapon::Kind::Dropped &&
            std::abs(diff) >= std::max(sw.aimTol, int32_t(0.03f / kCobAngle))) return;
        fire(u, *target, sl);
    };
    if (!u.type->weapons.empty()) {
        if (allWeapons)
            for (int sl = 0; sl < int(u.type->weapons.size()); ++sl) tryFire(sl);
        else if (sel)
            tryFire(slot);
    }
    // cancapture: a charmer converts the target after sustained contact (~3s) or
    // once it is worn down, rather than killing it.
    if (u.type->canCapture && target->type && !target->type->cantBeCaptured &&
        !allied(u.player, target->player)) {
        ++u.captureProg;
        if (u.captureProg > 3 * int32_t(kTick) ||
            target->hp < Fixed::fromFloat(target->type->maxHp * 0.25f)) {
            captureUnit(*target, u.player);
            u.captureProg = 0;
            dropLeg(u);
            cancelPath(u);
            u.routeStamp = -1;
        }
    }
}

// Switch a unit's allegiance: contact charm (cancapture) and mind-control weapons
// both land here, so a converted unit behaves identically either way -- it drops
// its old orders, comes up at half health if it was nearly dead, and forgets who
// last hit it (so the new owner isn't credited a kill on its own unit).
void World::captureUnit(Unit& t, int newPlayer) {
    if (!t.type || t.player == newPlayer) return;
    // Already dead, just not swept yet (alive() reads deadFor, which tick() sets):
    // capturing would raise it back to half health and steal a corpse.
    if (t.hp <= Fixed()) return;
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
    gPlayersValid_=false;
    t.orders.clear();
    t.hp = fxMax(t.hp, Fixed::fromFloat(t.type->maxHp * 0.5f));
    t.lastHitBy = 0;
    t.squad = 0;             // no longer in its old owner's control group
    t.buildQueue.clear();    // and not still producing for them
    t.buildProgress = 0;
    t.productionSiteId = 0;
}

// Stamp every PARKED ground unit into the occupancy layer. O(n), one cell each:
// our movers are all one nav cell, and this deliberately does NOT go through
// NavGrid::block, whose clearance DP and cache invalidation (it bumps the grid
// version, staling every component labelling over it) would be ruinous at a
// per-tick cadence. Rebuilt wholesale in unit-index order so the last writer on a
// contested cell is the same on every peer.
void World::blockCells(int cx, int cz, int w, int h, bool blocked) {
    if (obst_.empty() || terW_ <= 0) return;
    int x0 = std::max(0, cx), z0 = std::max(0, cz);
    int x1 = std::min(terW_ - 1, cx + w - 1), z1 = std::min(terH_ - 1, cz + h - 1);
    bool any = false;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            uint8_t& o = obst_[size_t(z) * size_t(terW_) + size_t(x)];
            if (o != (blocked ? 1 : 0)) { o = blocked ? 1 : 0; any = true; }
        }
    if (!any) return;
    // Every grid reads the same overlay, so they all need their clearance redone
    // over the touched band. markDirty is cheap; the DP is lazy.
    const int rw = x1 - x0 + 1, rh = z1 - z0 + 1;
    // Which cached labellings are CURRENT right now: only those may be carried across
    // the edit incrementally. Captured before the version bump below invalidates them.
    std::vector<std::pair<std::pair<const NavGrid*, int>, uint64_t>> fresh;
    for (auto& [key, cg] : compCache_)
        if (key.first && cg.ver == key.first->version()) fresh.emplace_back(key, cg.ver);

    nav_.overlayChanged(x0, z0, rw, rh);
    navWater_.overlayChanged(x0, z0, rw, rh);
    navHover_.overlayChanged(x0, z0, rw, rh);
    for (auto& g : navClasses_) g.overlayChanged(x0, z0, rw, rh);
    for (auto& plane : searchGrades_) {
        refreshSearchRect(plane,x0,z0,rw,rh);
        plane.navVersion=plane.nav->version();
    }

    // A pure block can only split components, and a split is provable locally -- so most
    // building placements need not throw away a whole-map labelling that costs ~5.7ms to
    // rebuild. Entries that cannot be proven safe keep their old (now stale) version and
    // get rebuilt by the next query, exactly as before.
    for (const auto& [key, oldVer] : fresh) {
        auto it = compCache_.find(key);
        if (it == compCache_.end() || it->second.ver != oldVer) continue;
        const NavGrid* g = key.first;
        const bool carried = blocked
            ? tryIncrementalBlock(*g, key.second, it->second, x0, z0, x1, z1)
            : tryIncrementalUnblock(*g, key.second, it->second, x0, z0, x1, z1);
        if (carried) it->second.ver = g->version();   // carried across the edit
    }
}

void World::blockFoot(const UnitType& t, float x, float z, bool blocked) {
    int cx = footprintOrigin(x, t.footX), cz = footprintOrigin(z, t.footZ);
    if (t.yardMap.empty()) { blockCells(cx, cz, t.footX, t.footZ, blocked); return; }
    for (int j = 0; j < t.footZ; ++j)
        for (int i = 0; i < t.footX; ++i) {
            char c = t.yardMap[size_t(j) * t.footX + i];
            if (c == 'o' || c == 'O') blockCells(cx + i, cz + j, 1, 1, blocked);
        }
}

void World::buildNavClasses(const TypeRegistry& reg) {
    paths_.clear();
    searchGrades_.clear(); activeSearchGrade_=-1;
    navClasses_.clear();
    navIdx_.clear();
    if (heights_.empty() || terW_ <= 0) return;
    // Collapse the registry's types onto their distinct limit tuples. Ordered by the
    // tuple so the table is built identically on every peer regardless of map order.
    auto tupleOf = [](const UnitType& t) {
        NavGrid::Limits l;
        l.maxSlope = int(t.maxSlope);
        l.maxWaterSlope = int(t.maxWaterSlope);
        l.badSlope=t.badSlope;l.badWaterSlope=t.badWaterSlope;
        l.badMaxWaterDepth=t.badMaxWaterDepth;l.badMinWaterDepth=t.badMinWaterDepth;
        // Domain decides the sense of the water band, exactly as the three legacy
        // grids did: a ground unit may wade only so deep, a boat needs a minimum
        // depth to float, a hoverer ignores both.
        if (t.domain == UnitType::Domain::Water) {
            l.maxWaterDepth = 10000;
            l.minWaterDepth = int(t.minWaterDepth > 0 ? t.minWaterDepth : 13);
        } else if (t.domain == UnitType::Domain::Hover) {
            l.maxWaterDepth = 10000;
            l.minWaterDepth = -10000;
        } else {
            l.maxWaterDepth = int(t.maxWaterDepth > 0 ? t.maxWaterDepth : 20);
            l.minWaterDepth = -10000;
        }
        return l;
    };
    auto key = [](const NavGrid::Limits& l) {
        return std::make_tuple(l.maxSlope,l.maxWaterSlope,l.maxWaterDepth,l.minWaterDepth,
                              l.badSlope,l.badWaterSlope,l.badMaxWaterDepth,l.badMinWaterDepth);
    };
    std::map<std::tuple<int,int,int,int,int,int,int,int>,int> seen;
    for (const auto& [id, t] : reg.types()) {
        if (t.canFly) continue;             // flyers ignore the ground entirely
        NavGrid::Limits l = tupleOf(t);
        auto k = key(l);
        auto it = seen.find(k);
        if (it == seen.end()) {
            it = seen.emplace(k, int(navClasses_.size())).first;
            navClasses_.push_back(NavGrid(heights_, terW_, terH_, seaLevel_, l));
        }
        navIdx_[&t] = it->second;
    }
    // Obstacles are NOT baked in: every grid reads the one shared overlay, so a
    // building blocked before or after this call is seen by all of them.
    for (auto& g : navClasses_) { g.setObstacles(&obst_); g.setRoads(&roads_); }
}

// Parked bodies take precedence when importing an already-overlapping world.
bool World::unitHoldsCell(const Unit& u) const {
    return u.speed == Fixed();
}

bool World::canFollowTraffic(const Unit& self, const Unit& other) const {
    if (!self.type || other.speed <= Fixed() || other.speed < self.speed ||
        std::abs(int(portHeadingToRetail(other.heading)) - int(portHeadingToRetail(self.heading))) > kBamQuarterV)
        return false;
    // 0x4db77d..0x4db7ac: compare against BOTH our current speed and 3/4
    // of our terrain-adjusted base speed (0x421ae0). Without the latter,
    // two slowed units pressing into a wall become passable to one another.
    Fixed base = self.baseSpeed;
    if (self.groundTerrainFlags&0x800) base=base*self.type->roadMult;
    else if (self.groundTerrainFlags&0x1000) base=base*self.type->waterMult;
    return other.speed >= base * Fixed::raw(0xc000);
}

void World::setSearchCell(SearchGradePlane& plane,size_t index,uint8_t value) {
    const auto word=[&](uint8_t grade) -> uint64_t {
        if (!grade) return 0;
        uint64_t v=(uint64_t(index)<<4)|grade;
        v=(v^(v>>30))*0xbf58476d1ce4e5b9ull;
        v=(v^(v>>27))*0x94d049bb133111ebull;
        return v^(v>>31);
    };
    plane.checksum^=word(plane.cells[index])^word(value);
    plane.cells[index]=value;
}

bool World::searchGradeChecksumsValid() const {
    for (const auto& plane:searchGrades_) {
        uint64_t checksum=0;
        for (size_t index=0;index<plane.cells.size();++index) {
            const uint8_t grade=plane.cells[index];
            if (!grade) continue;
            uint64_t value=(uint64_t(index)<<4)|grade;
            value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
            value=(value^(value>>27))*0x94d049bb133111ebull;
            checksum^=value^(value>>31);
        }
        if (checksum!=plane.checksum) return false;
    }
    return true;
}

int World::mapFeatureGrade(int cx,int cz) const {
    const auto& cell=mapPlacementCells_[size_t(cz)*hW_+cx];
    uint16_t index=cell.feature;
    int ox=cx,oz=cz;
    if (index==0xfffe) {
        ox-=cell.backX;oz-=cell.backZ;
        index=mapPlacementCells_[size_t(oz)*hW_+ox].feature;
        if (index>=0xfffa) index=0xffff;
    }
    if (index==0xffff) return 7;
    if (index>=mapPlacementTypes_.size()) return 0;
    const auto& type=mapPlacementTypes_[index];
    return type.blocking ? (type.clearable ? 1 : 0) : 7;
}

void World::rebuildBodyIndex() const {
    bodyTilesW_=(hW_+7)/8; bodyTilesH_=(hH_+7)/8;
    bodyTiles_.resize(size_t(bodyTilesW_)*bodyTilesH_);
    for (auto& bucket:bodyTiles_) bucket.clear();
    bodyTileBounds_.assign(units_.size(),{-1,-1,-1,-1});
    bodyFootprints_.resize(units_.size());
    bodyIndexValid_=true;
    for (const auto& u:units_) updateBodyIndex(u);
}

void World::updateBodyIndex(const Unit& u) const {
    if (!bodyIndexEnabled_ || !bodyIndexValid_ || !u.type) return;
    const size_t index=size_t(&u-units_.data());
    if (index>=bodyTileBounds_.size()) { bodyIndexValid_=false;return; }
    const int x=footprintOrigin(u.x,u.type->footX),z=footprintOrigin(u.z,u.type->footZ);
    // Keep exact cell bounds even when movement stays in the same buckets.
    bodyFootprints_[index]={x,z,x+u.type->footX,z+u.type->footZ};
    const std::array<int,4> bounds={std::clamp(x/8,0,bodyTilesW_),std::clamp(z/8,0,bodyTilesH_),
        std::clamp((x+u.type->footX+7)/8,0,bodyTilesW_),
        std::clamp((z+u.type->footZ+7)/8,0,bodyTilesH_)};
    if (bounds==bodyTileBounds_[index]) return;
    bodyTileBounds_[index]=bounds;
    // Leave old memberships until the next tick. They are harmless false
    // positives; stamping retains the last eligible unit in vector order.
    for (int tz=bounds[1];tz<bounds[3];++tz)
        for (int tx=bounds[0];tx<bounds[2];++tx)
            bodyTiles_[size_t(tz)*bodyTilesW_+tx].push_back(int(index));
}

World::SearchBodyRect World::searchBodyRect(int x,int z,int w,int h) const {
    SearchBodyRect result{x,z,w,h,std::vector<const Unit*>(size_t(w)*h,nullptr)};
    auto stamp=[&](const Unit& u,const std::array<int,4>* bounds=nullptr) {
        if (!u.alive() || u.embarked() || !u.type || (u.type->canFly && u.flightGroundMode!=1)) return;
        const int ux=bounds ? (*bounds)[0] : footprintOrigin(u.x,u.type->footX);
        const int uz=bounds ? (*bounds)[1] : footprintOrigin(u.z,u.type->footZ);
        const int x0=std::max(x,ux),z0=std::max(z,uz);
        const int x1=std::min(x+w,ux+u.type->footX),z1=std::min(z+h,uz+u.type->footZ);
        for (int cz=z0;cz<z1;++cz) for (int cx=x0;cx<x1;++cx) {
            if (u.type->isStructure() && !u.type->yardMap.empty()) {
                const char yard=u.type->yardMap.at(size_t(cz-uz)*u.type->footX+cx-ux);
                if (yard=='.') continue;
                if (yard=='c' || yard=='C') {
                    const auto script=unitScripts_.find(u.id);
                    if (script!=unitScripts_.end() && script->second.yardOpen) continue;
                }
            }
            // Bucket visits need not follow unit order. Retain the last unit
            // in the original vector, exactly as the full scan does.
            auto& occupant=result.cells[size_t(cz-z)*w+cx-x];
            if (!occupant || occupant<&u) occupant=&u;
        }
    };
    if (bodyIndexEnabled_ && hW_>0 && hH_>0 && x>=0 && z>=0 && x+w<=hW_ && z+h<=hH_) {
        if (!bodyIndexValid_) rebuildBodyIndex();
        for (int tz=z/8;tz<(z+h+7)/8;++tz)
            for (int tx=x/8;tx<(x+w+7)/8;++tx) {
                const auto& bucket=bodyTiles_[size_t(tz)*bodyTilesW_+tx];
                for (int index:bucket) {
                    const auto& bounds=bodyFootprints_[size_t(index)];
                    // Reject misses from compact bounds before touching the
                    // larger unit/type records. Full-scan verification below
                    // deliberately derives its bounds from live coordinates.
                    // These pure comparisons can execute together instead of
                    // taking up to four dependent early-exit branches.
                    if ((bounds[0]>=x+w) | (bounds[1]>=z+h) |
                        (bounds[2]<=x) | (bounds[3]<=z)) continue;
                    stamp(units_[size_t(index)],&bounds);
                }
            }
#ifndef NDEBUG
        static const bool verify=std::getenv("TAK_VERIFY_BODY_INDEX")!=nullptr;
        if (verify) {
            const auto indexed=result.cells;
            std::fill(result.cells.begin(),result.cells.end(),nullptr);
            for (const auto& u:units_) stamp(u);
            if (result.cells!=indexed) throw std::runtime_error("body spatial index differs from full occupancy scan");
        }
#endif
    } else for (const auto& u:units_) stamp(u);
    return result;
}

bool World::gatePassageAt(int x,int z) const {
    for (const auto& gate:units_) {
        if (!gate.alive() || gate.embarked() || !gate.type || !gate.type->gate) continue;
        const auto& type=*gate.type;
        const int dx=x-footprintOrigin(gate.x,type.footX),dz=z-footprintOrigin(gate.z,type.footZ);
        if (dx<0 || dz<0 || dx>=type.footX || dz>=type.footZ || type.yardMap.empty()) continue;
        const char yard=type.yardMap.at(size_t(dz)*type.footX+dx);
        if (yard=='c' || yard=='C') return true;
    }
    return false;
}

bool World::gateWantsOpen(const Unit& gate) const {
    const int x=footprintOrigin(gate.x,gate.type->footX),z=footprintOrigin(gate.z,gate.type->footZ);
    const auto bodies=searchBodyRect(x-1,z-1,gate.type->footX+2,gate.type->footZ+2);
    for (const auto* body:bodies.cells) {
        if (!body || body==&gate || body->player!=gate.player || body->type->isStructure()) continue;
        if (body->speed.v) return true;
        const int bx=footprintOrigin(body->x,body->type->footX),bz=footprintOrigin(body->z,body->type->footZ);
        for (int dz=0;dz<body->type->footZ;++dz)
            for (int dx=0;dx<body->type->footX;++dx)
                if (gatePassageAt(bx+dx,bz+dz)) return true;
    }
    return false;
}

void World::tickAutomaticGates() {
    // Gate policy runs at simulation cadence. The skirmish AI scheduler is
    // intentionally independent of retail's planner; the proximity rule is not.
    for (auto& gate:units_) {
        if (!gate.alive() || gate.underConstruction || !gate.type || !gate.type->gate ||
            !players_[size_t(gate.player)].automaticGates) continue;
        setActive(gate.id,gateWantsOpen(gate));
    }
}

int World::rawSearchGrade(const SearchGradePlane& plane,int x,int z,int w,int h,
                          const SearchBodyRect* bodies) const {
    const auto& nav=*plane.nav;
    // The raw rectangle routine excludes the final map row/column, even
    // when the footprint would otherwise fit there (508918..508937).
    if (x<0 || z<0 || x+w>=nav.width() || z+h>=nav.height()) return 0;
    const auto localBodies=bodies ? SearchBodyRect{} : searchBodyRect(x,z,w,h);
    if (!bodies) bodies=&localBodies;
    const bool hasMapCells=mapPlacementCells_.size()==size_t(hW_)*hH_ && !mapPlacementCells_.empty();
    int grade=7;
    for (int dz=0;dz<h;++dz) for (int dx=0;dx<w;++dx) {
        const int cx=x+dx,cz=z+dz;
        if (hasMapCells) {
            const int featureGrade=mapFeatureGrade(cx,cz);
            if (!featureGrade) return 0;
            grade=std::min(grade,featureGrade);
        }
        const int terrain=nav.terrainGrade(cx,cz,!hasMapCells);
        if (!terrain) return 0;
        grade=std::min(grade,terrain);
        const Unit* body=bodies->cells[size_t(cz-bodies->z)*bodies->width+cx-bodies->x];
        if (body) {
            if (body->type->gate && !body->type->yardMap.empty()) {
                const int bx=cx-footprintOrigin(body->x,body->type->footX);
                const int bz=cz-footprintOrigin(body->z,body->type->footZ);
                const char yard=body->type->yardMap.at(size_t(bz)*body->type->footX+bx);
                // 506416 marks only c/C gate passages; 508b54 grades them
                // specially before the ordinary stationary-body rejection.
                if (yard=='c' || yard=='C') { grade=std::min(grade,3);continue; }
            }
            grade=std::min(grade,retailCachedBodyGrade(!body->type->isStructure(),
                body->id==plane.preparation.requestSlot,body->groundGradeTick,
                plane.preparation.recent,plane.preparation.stale));
            if (!grade) return 0;
        }
    }
    return grade;
}

void World::refreshSearchRect(SearchGradePlane& plane,int x,int z,int w,int h) {
    const int width=plane.nav->width(),height=plane.nav->height();
    const int x0=std::max(0,x-plane.footX),z0=std::max(0,z-plane.footZ);
    const int x1=std::min(width,x+w+1),z1=std::min(height,z+h+1);
    if (x1<=x0 || z1<=z0) return;
    const auto bodies=searchBodyRect(x0-1,z0-1,x1-x0+plane.footX+2,z1-z0+plane.footZ+2);
    for (int cz=z0;cz<z1;++cz) for (int cx=x0;cx<x1;++cx) {
        const int grade=retailCachedFootprintGrade(cx,cz,plane.footX,plane.footZ,
            [&](int qx,int qz,int qw,int qh) { return rawSearchGrade(plane,qx,qz,qw,qh,&bodies); });
        const size_t index=size_t(cz)*width+cx;
        setSearchCell(plane,index,uint8_t((plane.cells[index]&8)|grade));
    }
}

void World::ageSearchBody(SearchGradePlane& plane,const RetailGradeBody& body,bool stale) {
    RetailGradeContext c;
    c.width=plane.nav->width(); c.height=plane.nav->height();
    c.footX=plane.footX; c.footZ=plane.footZ;
    // The native routine expects a body overlapping its cache. Imported or
    // off-map bodies must not turn a clipped-empty border into an array read.
    if (body.x+body.footX+1<=0 || body.z+body.footZ+1<=0 ||
        body.x-c.footX>=c.width || body.z-c.footZ>=c.height) return;
    retailAgeGradeRect(c,body.x,body.z,body.footX,body.footZ,stale,
        [&](int x,int z) {
            if (x<0 || z<0 || x>=c.width || z>=c.height) return uint8_t(0);
            return plane.cells[size_t(z)*c.width+x];
        },
        [&](int x,int z,int grade) {
            if (x<0 || z<0 || x>=c.width || z>=c.height) return;
            const size_t index=size_t(z)*c.width+x;
            setSearchCell(plane,index,uint8_t((plane.cells[index]&8)|grade));
        });
}

void World::prepareSearchGrade(int id,bool lastRetry) {
    const Unit* requester=unit(id);
    if (!requester || !requester->type) return;
    const auto* nav=&navFor(requester->type);
    int index=0;
    for (;index<int(searchGrades_.size());++index) {
        const auto& plane=searchGrades_[size_t(index)];
        if (plane.nav==nav && plane.footX==requester->type->footX &&
            plane.footZ==requester->type->footZ) break;
    }
    if (index==int(searchGrades_.size())) {
        SearchGradePlane plane;
        plane.nav=nav; plane.footX=requester->type->footX; plane.footZ=requester->type->footZ;
        searchGrades_.push_back(std::move(plane));
    }
    auto& plane=searchGrades_[size_t(index)];
    if (plane.cells.empty() || plane.navVersion!=nav->version()) {
        plane.preparation={}; plane.checksum=0;
        plane.cells.assign(size_t(nav->width())*nav->height(),0);
        refreshSearchRect(plane,0,0,nav->width(),nav->height());
        plane.navVersion=nav->version();
    }
    activeSearchGrade_=index;
    std::vector<RetailGradeBody> bodies;
    RetailGradeBody request;
    for (const auto& u:units_) {
        if (!u.type || !u.alive() || u.embarked() || (u.type->canFly && u.flightGroundMode!=1)) continue;
        RetailGradeBody body{u.id,0x1000001u,u.groundGradeTick,!u.type->isStructure(),
            footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),
            u.type->footX,u.type->footZ};
        bodies.push_back(body);
        if (u.id==id) request=body;
    }
    std::sort(bodies.begin(),bodies.end(),[](const auto& a,const auto& b) { return a.slot<b.slot; });
    plane.preparation.prepare(tickCounter_,request,bodies,lastRetry,
        [&](const auto& body) { refreshSearchRect(plane,body.x,body.z,body.footX,body.footZ); },
        [&](const auto& body,bool stale) { ageSearchBody(plane,body,stale); });
}

void World::finishSearchGrade(int id) {
    if (activeSearchGrade_<0) return;
    auto& plane=searchGrades_[size_t(activeSearchGrade_)];
    RetailGradeBody body;
    const Unit* u=unit(id);
    if (u && u->alive() && u->type) body={u->id,0x1000001u,u->groundGradeTick,true,
        footprintOrigin(u->x,u->type->footX),footprintOrigin(u->z,u->type->footZ),
        u->type->footX,u->type->footZ};
    plane.preparation.finish(u ? &body : nullptr,
        [&](const auto& b,bool stale) { ageSearchBody(plane,b,stale); });
    activeSearchGrade_=-1;
}

int World::searchGrade(int id,int cx,int cz,int retry,PathCell start) const {
    const Unit* u=unit(id);
    if (!u || !u->type || activeSearchGrade_<0) return 0;
    const auto& plane=searchGrades_[size_t(activeSearchGrade_)];
    RetailGradeContext c;
    c.width=plane.nav->width(); c.height=plane.nav->height();
    c.footX=plane.footX; c.footZ=plane.footZ;
    c.startX=start.x-plane.footX/2; c.startZ=start.z-plane.footZ/2;
    c.player=u->player; c.retry=retry;
    c.specialBlockerProbe=players_[size_t(u->player)].automaticGates;
    return retailSearchGrade(c,cx-plane.footX/2,cz-plane.footZ/2,
        [&](int x,int z) {
            return retailSearchVisible(c,hW_,hH_,x,z,[&](int vx,int vz) {
                return navigationExplored_[size_t(vz)*(hW_/2)+vx];
            });
        },
        [&](int x,int z) { return plane.cells[size_t(z)*c.width+x]; },
        [&](int x,int z) { return cellScore(u->type,x+plane.footX/2,z+plane.footZ/2,id); },
        [&](int x,int z) {
            const auto bodies=searchBodyRect(x,z,1,1);
            const auto* body=bodies.cells[0];
            return body && body->type->gate && gatePassageAt(x,z) ? body->player : -1;
        });
}

void World::refreshMovingSearchBody(Unit& u) {
    const auto old=u.groundGradeTick;
    u.groundGradeTick=tickCounter_;
    for (auto& plane:searchGrades_)
        if (old<plane.preparation.recent && plane.preparation.requestSlot!=u.id)
            refreshSearchRect(plane,footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),
                              u.type->footX,u.type->footZ);
}

int World::cellScore(const UnitType* t, int cx, int cz, int selfId) const {
    return cellScoreWithBodies(t,cx,cz,selfId,nullptr);
}

int World::cellScoreWithBodies(const UnitType* t,int cx,int cz,int selfId,
                                const SearchBodyRect* snapshot) const {
    const NavGrid& g=navFor(t);
    const Unit* self=unit(selfId);
    const int x0=cx-t->footX/2,z0=cz-t->footZ/2;
    if (self && t->canFly && self->flightGroundMode==2) return 7;
    if (x0<0 || z0<0 || x0+t->footX>=g.width() || z0+t->footZ>=g.height()) return -1;
    const bool hasMapCells=mapPlacementCells_.size()==size_t(hW_)*hH_ && !mapPlacementCells_.empty();
    int score=7;
    for (int z=z0;z<z0+t->footZ;++z) for (int x=x0;x<x0+t->footX;++x) {
        // A feature can be considered for clearing by search, but the live
        // footprint cannot stand on it (507fb0); all terrain is checked first.
        if (hasMapCells && mapFeatureGrade(x,z)<7) return -1;
        const int terrain=g.liveTerrainGrade(x,z,!hasMapCells);
        if (terrain<0) return -1;
        score=std::min(score,terrain);
    }
    const auto bodies=snapshot ? SearchBodyRect{} : searchBodyRect(x0,z0,t->footX,t->footZ);
    if (!snapshot) snapshot=&bodies;
    // The retail scan stops at the first failing body in ascending ID order.
    // Find that body directly: traffic checks are pure, and repeated footprint
    // cells cannot change the minimum failing ID.
    const Unit* firstBlocker=nullptr;
    int blockedScore=score;
    for (int z=z0;z<z0+t->footZ;++z) for (int x=x0;x<x0+t->footX;++x) {
        const auto* body=snapshot->cells[size_t(z-snapshot->z)*snapshot->width+x-snapshot->x];
        if (!body || body->id==selfId || (firstBlocker && body->id>=firstBlocker->id)) continue;
        if (body->type->isStructure()) {
            firstBlocker=body; blockedScore=0;
        } else if (!self || !canFollowTraffic(*self,*body)) {
            firstBlocker=body; blockedScore=2;
        }
    }
    return blockedScore;
}

void World::rebuildOccupancy() {
    occW_ = terW_;
    occH_ = terH_;
    if (occW_ <= 0 || occH_ <= 0) {
        occ_.clear(); occW_ = occH_ = 0; return;
    }
    occ_.assign(size_t(occW_) * size_t(occH_), 0);
    // Rebuild in deterministic order, moving bodies first and parked ones last.
    // Subsequent steps update ownership immediately; two units cannot reserve
    // the same destination using a stale snapshot of this grid.
    for (int pass = 0; pass < 2; ++pass) {
        const bool wantParked = (pass == 1);
        for (const auto& u : units_) {
            if (!u.alive() || u.embarked() || !u.type) continue;
            if (u.type->canFly || u.type->isStructure()) continue;   // structures are in nav_
            if (u.underConstruction) continue;
            if (unitHoldsCell(u) != wantParked) continue;
            // Stamp the whole footprint, centre-anchored to match NavGrid::fits.
            const int fx=u.type->footX,fz=u.type->footZ;
            int cx = footprintOrigin(u.x, fx), cz = footprintOrigin(u.z, fz);
            for (int j = 0; j < fz; ++j)
                for (int i = 0; i < fx; ++i) {
                    int x = cx + i, z = cz + j;
                    if (x < 0 || z < 0 || x >= occW_ || z >= occH_) continue;
                    const size_t cell = size_t(z) * size_t(occW_) + size_t(x);
                    occ_[cell] = u.id;
                }
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
        minx = std::min(minx, float(u.x.toFloat())); maxx = std::max(maxx, float(u.x.toFloat()));
        minz = std::min(minz, float(u.z.toFloat())); maxz = std::max(maxz, float(u.z.toFloat()));
    }
    if (!any) { gW_ = gH_ = 0; return; }
    gOx_ = minx - gCell_;
    gOz_ = minz - gCell_;
    gW_ = int((maxx - minx) / gCell_) + 3;
    gH_ = int((maxz - minz) / gCell_) + 3;
    gHead_.assign(size_t(gW_) * gH_, -1);
    gPlayers_.assign(gHead_.size(),0);
    gPlayersValid_=true;
    gNext_.assign(units_.size(), -1);
    for (size_t i = 0; i < units_.size(); ++i) {
        const Unit& u = units_[i];
        if (!inGrid(u)) continue;
        int cx = std::clamp(int((u.x.toFloat() - gOx_) / gCell_), 0, gW_ - 1);
        int cz = std::clamp(int((u.z.toFloat() - gOz_) / gCell_), 0, gH_ - 1);
        int c = cz * gW_ + cx;
        gNext_[i] = gHead_[size_t(c)];
        gHead_[size_t(c)] = int(i);
        if (unsigned(u.player)<64) gPlayers_[size_t(c)]|=uint64_t(1)<<u.player;
        else gPlayersValid_=false; // unrepresentable owners require the full traversal
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
        // Proximity alone can accept an offset footprint that earns no mana.
        // Match the sacred-site coverage used by the economy calculation.
        if (type->sacredIncome && !sacredSites_.empty() &&
            sacredIncomeMultiplier(footprintOrigin(x,type->footX),
                                   footprintOrigin(z,type->footZ),type->footX,type->footZ)<=0)
            return false;
        float sx = manaSpots_[size_t(spot)].first, sz = manaSpots_[size_t(spot)].second;
        // One lodestone per deposit. Some Sacred Stones register as two adjacent
        // spots (~22-40px apart); a 44px exclusion merges those into one deposit
        // so a second lodestone can't squeeze onto the same stone.
        for (const auto& u : units_) {
            if (!u.alive() || !u.type || !u.type->onMana) continue;
            float dx = u.x.toFloat() - sx, dz = u.z.toFloat() - sz;
            if (dx * dx + dz * dz < 44.0f * 44.0f) return false;   // deposit taken
        }
    }
    // Check the domain-appropriate grid so water units (Kraken) require water
    // and land units require land, rather than always testing the ground grid.
    const NavGrid& grid = navFor(type);
    int cx = footprintOrigin(x, type->footX), cz = footprintOrigin(z, type->footZ);
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
    } else if (!type->yardMap.empty()) {
        // Honour the yardmap: a '.' cell is NOT part of the footprint and retail
        // runs no test on it at all (its per-cell mask is zero). We were testing
        // every cell of the bounding box, so a tree under one of the Keep's TWO
        // leading '.' rows -- 14 of its 84 cells -- denied a placement retail
        // allows. blockFootprint already honoured the yardmap, so nav and
        // placement disagreed about the same building.
        for (int j = 0; j < type->footZ; ++j)
            for (int i = 0; i < type->footX; ++i) {
                char c = type->yardMap[size_t(j) * type->footX + i];
                if (c == '.' || c == ' ') continue;
                if (!grid.walkable(cx + i, cz + j)) return false;
            }
    } else {
        for (int j = 0; j < type->footZ; ++j)
            for (int i = 0; i < type->footX; ++i)
                if (!grid.walkable(cx + i, cz + j)) return false;
    }
    if (type->isStructure()) {
        // Retail's building branch (507400) checks occupied footprint cells.
        // A radius based on the longest side rejects builders standing beside
        // a rectangular building after they reach its construction perimeter.
        const auto bodies=searchBodyRect(cx,cz,type->footX,type->footZ);
        for (size_t i=0;i<bodies.cells.size();++i) {
            if (!type->yardMap.empty() && type->yardMap[i]=='.') continue;
            if (bodies.cells[i]) return false;
        }
        return true;
    }
    for (const auto& u : units_) {
        if (!u.alive()) continue;
        float dx = u.x.toFloat() - x, dz = u.z.toFloat() - z;
        float min = 16.0f * float(std::max(type->footX, type->footZ)) / 2 + 12;
        if (dx * dx + dz * dz < min * min) return false;
    }
    return true;
}

bool World::clearableForPlacement(const UnitType* type, float x, float z,
                                  std::vector<int>& out) const {
    out.clear();
    if (!type || type->maxVel > Fixed()) return false;   // buildings only
    if (canPlace(type, x, z)) return true;            // nothing in the way already
    const NavGrid& grid = navFor(type);
    if (grid.empty()) return false;
    int cx = footprintOrigin(x, type->footX), cz = footprintOrigin(z, type->footZ);
    // Which live features could be cleared, indexed by the cells they cover.
    auto featureAt = [&](int gx, int gz) -> const Feature* {
        for (const auto& f : features_) {
            if (!f.alive || !f.blocks || f.work <= Fixed()) continue;   // not reclaimable
            int fx0 = f.x.floorInt() / 16 - f.fx / 2, fz0 = f.z.floorInt() / 16 - f.fz / 2;
            if (gx >= fx0 && gx < fx0 + f.fx && gz >= fz0 && gz < fz0 + f.fz) return &f;
        }
        return nullptr;
    };
    bool anyBlocked = false;
    for (int j = 0; j < type->footZ; ++j)
        for (int i = 0; i < type->footX; ++i) {
            if (!type->yardMap.empty()) {
                char c = type->yardMap[size_t(j) * type->footX + i];
                if (c == '.' || c == ' ') continue;   // not part of the footprint
            }
            int gx = cx + i, gz = cz + j;
            if (grid.walkable(gx, gz)) continue;                 // clear already
            if (!grid.terrainWalkable(gx, gz)) return false;     // the GROUND says no
            const Feature* f = featureAt(gx, gz);
            if (!f) return false;   // an obstacle that isn't a clearable doodad
            anyBlocked = true;
            if (std::find(out.begin(), out.end(), f->id) == out.end()) out.push_back(f->id);
        }
    if (!anyBlocked) return false;   // blocked by something the cell walk didn't see
    // A unit standing on the site still refuses, exactly as canPlace does -- clearing
    // doodads can't move a body, and retail refuses here too.
    for (const auto& u : units_) {
        if (!u.alive()) continue;
        float dx = u.x.toFloat() - x, dz = u.z.toFloat() - z;
        float min = 16.0f * float(std::max(type->footX, type->footZ)) / 2 + 12;
        if (dx * dx + dz * dz < min * min) return false;
    }
    return true;
}

int World::startBuild(int builderId, const UnitType* type, float x, float z,
                      Approach approach) {
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
    // Preserve retail birth orientation. Factory scripts query this heading to
    // counter-rotate their build pads into the fixed yard where appropriate.
    int id = spawn(type, x, z, std::nullopt, b->player);
    Unit* site = unit(id);
    site->underConstruction = true;
    site->beingBuilt = true;   // its builder owns it this tick (no instant decay)
    site->hp = Fixed::fromFloat(type->maxHp * 0.05f);
    // Auto-join: a conjured MOBILE unit inherits the builder's squad (a building never does).
    if (bsquad && !type->isStructure()) site->squad = bsquad;
    // Key off maxVel, NOT the FBI canmove flag: verwall and tarwall both declare
    // canmove=1 with no velocity, so a canMove gate left Veruna and Taros walls
    // blocking neither pathing nor line of sight while Aramon's and Creon's did.
    // (The CLAUDE.md gotcha, biting for real.)
    if (type->isStructure()) {
        blockFoot(*type, x, z, true);
    }
    b = unit(builderId);   // spawn may have reallocated units_
    if (players_[size_t(b->player)].retailResources) {
        initializeRetailSite(*site, builderId);
        RetailConstructionJob job;
        job.target=id;job.workerTime=b->type->workerTime;job.working=true;
        for (const auto& entry:players_[size_t(b->player)].buildCache->entries)
            if (entry.type==b->type && entry.construction) job.workerTime=entry.construction->workerTime;
        job.mission.stage=2;
        const uint16_t target=uint16_t(retailDirection(b->x-site->x,b->z-site->z).v);
        job.turnHeading=target;
        b->retailBuild=job;b->constructionHolding=true;
        if (auto script=unitScripts_.find(builderId);script!=unitScripts_.end()) {
            const auto& file=*b->type->script();
            script->second.state.startArguments(file,file.scriptIndex("StartBuilding"),
                {uint16_t(target-portHeadingToRetail(b->heading)),1,0,0},2);
        }
    }
    b->buildSiteId = id;
    b->buildStuckT = 0; b->buildStuckD = INT32_MAX;   // fresh job: reset the reach watchdog
    if (approach == Approach::Replace)
        order(builderId, x, z + float(type->footZ) * 8 + 24, false);
    return id;
}

void World::queueBuild(int builderId, const UnitType* type, float x, float z, bool queue) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || b->type->isStructure() ||
        b->underConstruction || !type) return;
    if (!canPlace(type, x, z)) return;
    // ONE queue. A build is an order like any other and lives in Unit::orders, in
    // the position the player clicked it -- which is what makes "move here, build
    // that, move there" mean what it says, and what lets the order line draw it.
    // Retail works the same way: its order list holds MobileBuild orders alongside
    // the moves, and the order-line iterator (icd 0x4d6340) dispatches per kind.
    // A non-queued build replaces the lot, exactly as a non-queued move does.
    if (!queue) {
        cancelPath(*b);
        cancelBuilds(builderId);
        b->orders.clear();
    }
    b->orders.push_back(makeBuildOrder(*b,type,Fixed::fromFloat(x),Fixed::fromFloat(z)));
}

Order World::makeBuildOrder(const Unit& builder,const UnitType* type,Fixed x,Fixed z) const {
    const Unit* b=&builder;
    Order o;
    o.buildX=footprintWaypoint(footprintCell(x,type->footX),type->footX);
    o.buildZ=footprintWaypoint(footprintCell(z,type->footZ),type->footZ);
    const int sx=footprintOrigin(o.buildX,type->footX),sz=footprintOrigin(o.buildZ,type->footZ);
    o.buildRectangle=RetailRectGoal{sx-b->type->footX,sx+type->footX,
                                  sz-b->type->footZ,sz+type->footZ};
    const auto [cx,cz]=o.buildRectangle->navigationCell(
        footprintOrigin(b->x,b->type->footX),footprintOrigin(b->z,b->type->footZ));
    o.x=Fixed::fromInt(cx*16+b->type->footX*8);
    o.z=Fixed::fromInt(cz*16+b->type->footZ*8);
    o.goal = true;
    o.issuedTick = tickCounter_;
    o.buildType = type;
    o.mission.flags=0x80508u; // MobileBuild definition with a supplied point.
    return o;
}

bool World::prepareBuildApproach(Unit& u) {
    if (u.orders.empty()) return false;
    auto& goal=u.orders[currentLeg(u.orders)];
    if (goal.buildRectangle && goal.mission.stage==0) {
        // A queued build computes its approach when it becomes active,
        // after earlier movement/build orders have changed our position.
        const auto [x,z]=goal.buildRectangle->navigationCell(
            footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ));
        goal.x=Fixed::fromInt(x*16+u.type->footX*8);
        goal.z=Fixed::fromInt(z*16+u.type->footZ*8);
        if (u.type->canFly) {
            // Flying construction works at builddistance from the site centre
            // (retail 41ef00), rather than at the ground footprint perimeter.
            // Choose the initial hover point on the builder's side of the site.
            const uint16_t angle=uint16_t(retailDirection(u.x-goal.buildX,u.z-goal.buildZ).v);
            const int32_t radius=Fixed::fromInt(u.type->buildDist>0 ? u.type->buildDist : 40).v;
            goal.x=goal.buildX+Fixed::raw(retailScaledSine(angle,radius));
            goal.z=goal.buildZ+Fixed::raw(retailScaledCosine(angle,radius));
            goal.flightGoal=RetailFlightGoal{{goal.x.v,0,goal.z.v},0x60,angle,0};
        }
        goal.segmentX=Fixed::fromInt(u.x.floorInt());
        goal.segmentZ=Fixed::fromInt(u.z.floorInt());
        goal.hasSegment=true;
        goal.mission.stage=1;
        goal.mission.waitMask=u.type->canFly ? 0x100 : 0x700;
        if (!u.type->canFly) {
            goal.controller=++nextMovementController_;
            goal.navigationExhausted=goal.navigationConsumed=false;
            goal.mission.pending&=~0x3700u;
            if (u.routeStamp<0 || uint32_t(u.routeStamp)<=tickCounter_-6u) u.routeStamp=0;
        }
        // Replacing the build controller preserves navigator cadence.
        // Its previous search stamp still governs this tick's retry.
        cancelPath(u);
        requestPath(u,goal.x.toFloat(),goal.z.toFloat());
    }
    if (!goal.buildRectangle) return false;
    if (u.type->canFly) return (goal.mission.pending & goal.mission.waitMask & 0x100)!=0;
    const uint32_t events=(u.missionEvents | goal.mission.pending) & goal.mission.waitMask;
    if (!events) return false;
    // 4d8450 consumes the approach events before MobileBuild enters placement.
    u.missionEvents&=~events;goal.mission.pending&=~events;goal.mission.waitMask=0;
    if (events&0x200) {
        if (!retailBuildWithinReach(u.x,u.z,goal.buildX,goal.buildZ,
                int16_t(u.type->footX),int16_t(u.type->footZ),
                int16_t(goal.buildType->footX),int16_t(goal.buildType->footZ),uint16_t(u.type->buildDist))) {
            dropLeg(u);
            tickGroundMission(u);
            return prepareBuildApproach(u);
        }
    }
    return true;
}

void World::cancelBuilds(int builderId) {
    Unit* b = unit(builderId);
    if (!b) return;
    // Drop every pending build from the order queue (see queueBuild): a fresh
    // move/attack/stop cancels queued construction.
    b->orders.erase(std::remove_if(b->orders.begin(), b->orders.end(),
                                   [](const Order& o) {
                                       return o.buildType != nullptr || o.reclaimFeat != 0 ||
                                              o.repairTarget != 0;
                                   }),
                    b->orders.end());
    b->reclaimId = 0;            // a fresh move/attack/stop drops any reclaim job
    b->repairId = 0;             // ...and any repair job
    if (b->buildSiteId) {
        Unit* site = unit(b->buildSiteId);
        // A site that never actually started building is just a ghost — remove
        // it so its marker/ghost doesn't linger.
        if (site && site->underConstruction && !site->buildBegun) {
            if (site->type && site->type->isStructure()) {
                blockFoot(*site->type, site->x.toFloat(), site->z.toFloat(), false);
            }
            site->underConstruction = false;
            site->deadFor = kRetiredTicks;   // fully gone (painter skips the corpse window)
        }
        b->buildSiteId = 0;
    }
}

void World::assist(int builderId, int siteId, bool queue) {
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
    b->buildStuckT = 0; b->buildStuckD = INT32_MAX;   // fresh job: reset the reach watchdog
    order(builderId, site->x.toFloat(), site->z.toFloat() + float(site->type->footZ) * 8 + 24, queue);
}

float World::sacredIncomeMultiplier(int x,int z,int fx,int fz) const {
    // 51d68e..51d7b6 scans x then z. Count positive sacred-site cells and
    // compare with the last encountered site's full footprint.
    const SacredSite* last=nullptr;
    int covered=0;
    for (int dx=0;dx<fx;++dx) for (int dz=0;dz<fz;++dz) {
        for (auto it=sacredSites_.rbegin();it!=sacredSites_.rend();++it) {
            if (x+dx<it->x || x+dx>=it->x+it->fx || z+dz<it->z || z+dz>=it->z+it->fz) continue;
            if (it->multiplier>0) { ++covered;last=&*it; }
            break;
        }
    }
    return last && covered>=last->fx*last->fz ? last->multiplier : 0;
}

// Corpse visuals/reclaim targets stay on the dead unit record. Navigation uses
// the feature's own footprint, and ownership prevents an old corpse retiring a
// newer feature placed at the same anchor.
void World::forgetCorpseAt(int cx,int cz) {
    for (auto it=corpseFootprints_.begin();it!=corpseFootprints_.end();) {
        const auto footprint=it->second;
        if (footprint.x!=cx || footprint.z!=cz) { ++it;continue; }
        const auto& type=featTypes_.at(size_t(footprint.type));
        if (auto* corpse=unit(it->first)) {
            corpse->deadFor=kRetiredTicks;
            corpse->corpseUntil=0;
            corpse->corpseBlocks=false;
        }
        it=corpseFootprints_.erase(it);
        if (type.blocking) blockCells(cx,cz,type.fx,type.fz,false);
    }
}

bool World::placeCorpse(Unit& corpse,int type) {
    if (type<0 || corpseFootprints_.contains(corpse.id)) return false;
    const auto& definition=featTypes_.at(size_t(type));
    // 512ee0: origin belongs to the original unit, not the new feature. The
    // corpse offset moves this anchor; a differently sized wreck keeps it.
    const int cx=footprintOrigin(corpse.x,corpse.type->footX)+corpse.type->corpseAdjX;
    const int cz=footprintOrigin(corpse.z,corpse.type->footZ)+corpse.type->corpseAdjZ;
    Feature feature;
    feature.id=cz*terW_+cx;feature.type=type;
    feature.fx=definition.fx;feature.fz=definition.fz;feature.blocks=definition.blocking;
    feature.x=Fixed::fromInt(cx*16+definition.fx*8);
    feature.z=Fixed::fromInt(cz*16+definition.fz*8);
    if (!placeMapFeature(feature)) return false;
    corpseFootprints_[corpse.id]={cx,cz,type};
    corpse.corpseBlocks=definition.blocking;
    if (definition.blocking) blockCells(cx,cz,definition.fx,definition.fz,true);
    corpse.x+=Fixed::fromInt(corpse.type->corpseAdjX*16);
    corpse.z+=Fixed::fromInt(corpse.type->corpseAdjZ*16);
    return true;
}

void World::retireCorpse(Unit& corpse) {
    const auto found=corpseFootprints_.find(corpse.id);
    if (found!=corpseFootprints_.end()) {
        const auto footprint=found->second;
        corpseFootprints_.erase(found);
        const auto& type=featTypes_.at(size_t(footprint.type));
        removeMapFeature(footprint.x,footprint.z);
        if (type.blocking) blockCells(footprint.x,footprint.z,type.fx,type.fz,false);
    }
    corpse.deadFor=kRetiredTicks;
    corpse.corpseUntil=0;
    corpse.corpseBlocks=false;
}

void World::removeMapFeature(int cx,int cz) {
    if (mapPlacementCells_.empty() || cx<0 || cz<0 || cx>=hW_ || cz>=hH_) return;
    const auto index=mapPlacementCells_[size_t(cz)*hW_+cx].feature;
    if (index>=mapPlacementTypes_.size()) return;
    const auto& type=mapPlacementTypes_[index];
    forgetCorpseAt(cx,cz);
    retailMapRemove(cx,cz,type,[&](int x,int z)->RetailMapFeatureCell& {
        return mapPlacementCells_.at(size_t(z)*hW_+x);
    });
    for (auto& plane:searchGrades_) refreshSearchRect(plane,cx,cz,type.footX,type.footZ);
}

bool World::placeMapFeature(const Feature& f) {
    if (mapPlacementCells_.empty()) return true;
    RetailMapFeatureType type;
    type.footX=f.fx;type.footZ=f.fz;type.blocking=f.blocks;type.clearable=true;
    if (f.type>=0) {
        const auto& definition=featTypes_.at(size_t(f.type));
        type.name=definition.name;type.indestructible=definition.indestructible;
        type.projectileHeight=definition.projectileHeight;
        type.clearable=!type.indestructible;
        std::vector<int> pending{f.type};
        std::vector<bool> seen(featTypes_.size(),false);
        while (type.clearable && !pending.empty()) {
            const int i=pending.back();pending.pop_back();
            if (i<0 || seen.at(size_t(i))) continue;
            seen[size_t(i)]=true;
            const auto& stage=featTypes_[size_t(i)];
            if (stage.blocking && stage.indestructible) { type.clearable=false;break; }
            pending.push_back(stage.deadType);pending.push_back(stage.burntType);
        }
    } else type.name="untyped feature";
    auto found=std::find_if(mapPlacementTypes_.begin(),mapPlacementTypes_.end(),[&](const auto& t) {
        return t.name==type.name && t.footX==type.footX && t.footZ==type.footZ &&
            t.blocking==type.blocking && t.indestructible==type.indestructible && t.clearable==type.clearable &&
            t.projectileHeight==type.projectileHeight;
    });
    const size_t index=size_t(found-mapPlacementTypes_.begin());
    if (found==mapPlacementTypes_.end()) {
        if (index>=0xfffa) throw std::runtime_error("too many map feature types");
        mapPlacementTypes_.push_back(std::move(type));
    }
    const int cx=footprintOrigin(f.x,f.fx),cz=footprintOrigin(f.z,f.fz);
    const bool placed=retailMapInstall(hW_,hH_,cx,cz,uint16_t(index),mapPlacementTypes_,[&](int x,int z)->RetailMapFeatureCell& {
            return mapPlacementCells_.at(size_t(z)*hW_+x);
        },[&](int x,int z,uint16_t previous) {
            forgetCorpseAt(x,z);
            const auto& oldType=mapPlacementTypes_[previous];
            if (oldType.blocking) blockCells(x,z,oldType.footX,oldType.footZ,false);
            for (auto& plane:searchGrades_) refreshSearchRect(plane,x,z,oldType.footX,oldType.footZ);
            auto it=featureIdx_.find(z*hW_+x);
            if (it!=featureIdx_.end()) features_[it->second].alive=false;
            bumpFeatGen();
        });
    // Feature grades can change without changing the legacy obstacle bit (for
    // example, clearable -> indestructible). Refresh the actual search cache.
    if (placed) for (auto& plane:searchGrades_) refreshSearchRect(plane,cx,cz,f.fx,f.fz);
    return placed;
}

void World::addFeature(int id, float x, float z, float manaYield, float work,
                       int fx, int fz, bool blocks, int type, bool placeOnMap) {
    Feature f{id, Fixed::fromFloat(x), Fixed::fromFloat(z), fx, fz, manaYield,
              Fixed::fromFloat(std::max(work, 1.0f)),
              Fixed::fromFloat(std::max(work, 1.0f)), blocks, true};
    f.type = type;
    if (placeOnMap && !placeMapFeature(f)) return;
    featureIdx_[id] = features_.size();
    features_.push_back(f);
    if (placeOnMap && blocks) blockCells(footprintOrigin(f.x,fx),footprintOrigin(f.z,fz),fx,fz,true);
    bumpFeatGen();   // a new feature needs a visual instance
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
// neither does ours. Match setup resolves authored burn lifetimes identically
// for headless servers and clients, independent of rendering quality.

// Swap a feature IN PLACE to another stage of its chain (featureburnt on
// burn-out, featuredead on destruction; retail places the replacement neutral
// -- our features carry no owner). newType < 0 = the feature is simply gone.
void World::swapFeature(Feature& f, int newType) {
    bumpFeatGen();   // burnt-stage swap (or removal): art changes
    f.burn = 0;
    f.burnSequence = 0;
    f.dmg = 0;
    const int cx = f.x.floorInt() / 16 - f.fx / 2;
    const int cz = f.z.floorInt() / 16 - f.fz / 2;
    if (f.blocks)   // old stage's footprint frees first
        blockCells(cx, cz, f.fx, f.fz, false);
    removeMapFeature(cx,cz);
    if (newType < 0) { f.alive = false; f.blocks = false; f.type = -1; return; }
    const FeatType& nt = featTypes_[size_t(newType)];
    f.type = newType;
    f.fx = nt.fx; f.fz = nt.fz;
    f.x = Fixed::fromInt(cx * 16 + f.fx * 8);
    f.z = Fixed::fromInt(cz * 16 + f.fz * 8);
    f.blocks = nt.blocking;
    f.alive=placeMapFeature(f);
    if (!f.alive) { f.blocks=false;return; }
    if (f.blocks)
        blockCells(f.x.floorInt() / 16 - f.fx / 2, f.z.floorInt() / 16 - f.fz / 2,
                   f.fx, f.fz, true);
    f.manaYield = nt.energy;
    f.work = f.workFull = Fixed::fromFloat(std::max(nt.energy, 60.0f));
}

void World::igniteFeature(Feature& f) {
    if (f.burn || !f.alive || f.type < 0) return;
    const FeatType& ft = featTypes_[size_t(f.type)];
    if (!ft.flamable || !ft.hasBurnAnim) return;
    static const bool kBurnLog = std::getenv("TAK_BURNLOG") != nullptr;
    if (kBurnLog)
        std::fprintf(stderr, "ignite %s at %.0f,%.0f (spark %d)\n",
                     ft.name.c_str(), f.x.toFloat(), f.z.toFloat(), ft.sparkTicks);
    f.burn = 1;
    f.burnStarted = tickCounter_;
    f.burnSequence = ++burnSequence_;
    bumpFeatGen();   // ignition edge: flame overlay + smoke start
    // Retail spread timer: sparktime30/2 + rand(sparktime30/2), one LCG draw.
    int half = std::max(ft.sparkTicks / 2, 1);
    f.spreadIn = half + burnRand(half);
    f.burnLeft = std::max(1u,ft.burnTicks);
}

void World::tickBurning() {
    if (featTypes_.empty()) return;
    // Index-based: igniteFeature never reallocates (spread only flips state).
    for (size_t i = 0; i < features_.size(); ++i) {
        Feature& f = features_[i];
        if (!f.alive || !f.burn) continue;
        // Native feature clocks retire before evaluating this tick's spark.
        if (f.burnLeft <= 1) {
            f.burnLeft = 0;
            swapFeature(f, featTypes_[size_t(f.type)].burntType);
            continue;
        }
        --f.burnLeft;
        // Spread fires ONCE per burning feature (retail slot+0x44 never reloads):
        // a 7x7 box around the head cell, each flamable neighbour rolls
        // rand(100) < its own spreadchance. (Retail's downwind spark phase moves
        // <1 cell at shipped wind speeds -- omitted.)
        if (f.spreadIn > 0 && --f.spreadIn == 0) {
            int cx = f.x.floorInt() / 16 - f.fx / 2, cz = f.z.floorInt() / 16 - f.fz / 2;
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
        if (!c || c->id == builderId || !c->type || (c->alive() && c->hp > Fixed()))
            return;
        int ct = c->corpseStatue >= 0 ? c->corpseStatue : corpseTypeOf(c->type);
        if (ct < 0 || !featTypes_[size_t(ct)].reclaimable) return;
        tx = c->x.toFloat(); tz = c->z.toFloat();
        static const bool kRcLog = std::getenv("TAK_BURNLOG") != nullptr;
        if (kRcLog)
            std::fprintf(stderr, "corpse-reclaim ORDER: b%d -> %s (%d)\n",
                         builderId, c->type->id.c_str(), -featureId);
    } else {
        const Feature* f = feature(featureId);
        if (!f || !f->alive) return;
        tx = f->x.toFloat(); tz = f->z.toFloat();
    }
    // A reclaim is an ORDER, in the sequence the player gave it -- the same fix the
    // build queue needed. It used to go to a separate reclaimQueue that only ever
    // drained from inside tickReclaim, so unless a reclaim was ALREADY running
    // nothing started it: an area reclaim queued behind a move or a build sat
    // there for the rest of the game.
    if (!queue) {
        cancelPath(*b);
        b->orders.clear();
    }
    Order o;
    o.x = Fixed::fromFloat(tx); o.z = Fixed::fromFloat(tz);
    o.goal = true;
    o.issuedTick = tickCounter_;
    o.reclaimFeat = featureId;
    b->orders.push_back(o);
}

void World::tickReclaim(Unit& b, float dt) {
    auto advance = [&] {
        // The job is over. Retire its order; the next one (which may be another
        // reclaim) simply becomes current.
        b.reclaimId = 0;
        if (!b.orders.empty() && b.orders.front().reclaimFeat) b.orders.erase(b.orders.begin());
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
            if (c->hp > Fixed()) advance();
            return;
        }
        // Body still mid-death-anim: stand by until it settles (statues settle
        // instantly).
        if (c->deadFor < (c->corpseStatue >= 0 ? 0 : kCorpseAnimTicks)) return;
        float dx = (c->x - b.x).toFloat(), dz = (c->z - b.z).toFloat();
        float reach = 24.0f + 8.0f * float(std::max(c->type->footX, c->type->footZ)) +
                      (b.type->buildDist > 0 ? b.type->buildDist : 0.0f);
        if (dx * dx + dz * dz > reach * reach) return;   // still walking there
        b.speed = Fixed();
        int ct = c->corpseStatue >= 0 ? c->corpseStatue : corpseTypeOf(c->type);
        const FeatType* cd = ct >= 0 ? &featTypes_[size_t(ct)] : nullptr;
        // Per TICK. kReclaimRate is work/second and the sim steps at kTick, so this
        // is an exact constant rather than a dt multiply.
        const Fixed step = Fixed::fromFloat(kReclaimRate / kTick);
        // Proportional drip against the INITIAL work (= max(energy,60), set at
        // death). Shipped corpse defs all have energy 0, so this grants nothing.
        if (cd && cd->energy > 0 && c->corpseWork > Fixed())
            players_[size_t(b.player)].creditMana(
                double(cd->energy) * double(fxMin(step, c->corpseWork).toFloat()) /
                double(std::max(cd->energy, 60.0f)) * double(players_[size_t(b.player)].manaMult));
        c->corpseWork -= step;
        if (c->corpseWork <= Fixed()) {
            retireCorpse(*c);
            static const bool kRecLog = std::getenv("TAK_BURNLOG") != nullptr;
            if (kRecLog)
                std::fprintf(stderr, "corpse reclaimed: %s at %.0f,%.0f\n",
                             c->type->id.c_str(), c->x.toFloat(), c->z.toFloat());
            advance();
        }
        return;
    }
    auto it = featureIdx_.find(b.reclaimId);
    if (it == featureIdx_.end()) { advance(); return; }
    Feature& f = features_[it->second];
    if (!f.alive) { advance(); return; }   // someone else got it (RECLAIMFAILED)
    float dx = (f.x - b.x).toFloat(), dz = (f.z - b.z).toFloat();
    float reach = 24.0f + 8.0f * float(std::max(f.fx, f.fz)) +
                  (b.type->buildDist > 0 ? b.type->buildDist : 0.0f);
    if (dx * dx + dz * dz > reach * reach) return;   // still walking there
    // Never the RECLAIM order itself -- that entry IS the job, and it is what
    // holds the queue back until the feature is gone (advance() retires it).
    if (b.orders.empty() || !b.orders.front().reclaimFeat) dropLeg(b);
    b.speed = Fixed();
    const Bam want = fxAtan2(Fixed::fromFloat(dx), Fixed::fromFloat(dz));   // face the feature
    // Stopped (b.speed was zeroed just above), so this is a pivot: turninplacerate.
    const int32_t bTurnMax = b.type->turnInPlaceRate;
    const int32_t diff = bamDiff(want, b.heading);
    const int32_t turn = std::clamp(diff, -bTurnMax, bTurnMax);
    b.heading = b.heading + Bam(turn);
    b.turnReqBam = diff;   // builder pivot request -> TurnDirection anim (icd 0x4d9593)
    // Per TICK, like the corpse drain: kReclaimRate is work/second.
    const Fixed d = fxMin(f.work, Fixed::fromFloat(kReclaimRate / kTick));
    f.work -= d;
    players_[size_t(b.player)].creditMana(
        // DIVIDE IN DOUBLE. `(d / f.workFull)` in fixed point truncates the
        // proportion every tick, and the remainder is never paid: a shipped
        // 525-energy feature returned about 524.66 over a full reclaim.
        double(f.manaYield) * (double(d.toFloat()) / double(f.workFull.toFloat()))
        * double(players_[size_t(b.player)].manaMult));   // drip (income-cheat scaled)
    if (f.work <= Fixed()) {
        f.alive = false;
        removeMapFeature(footprintOrigin(f.x,f.fx),footprintOrigin(f.z,f.fz));
        bumpFeatGen();   // reclaimed away: stop drawing it
        if (f.blocks) {   // free the ground cells it occupied (setupMatch blocked nav_)
            blockCells(f.x.floorInt() / 16 - f.fx / 2, f.z.floorInt() / 16 - f.fz / 2, f.fx, f.fz, false);
        }
        advance();
    }
}

void World::repair(int builderId, int targetId, bool queue) {
    Unit* b = unit(builderId);
    Unit* t = unit(targetId);
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || !b->type->canMove) return;
    if (!t || !t->alive() || !t->type || t->id == b->id || t->underConstruction ||
        !allied(t->player, b->player) || t->hp >= Fixed::fromFloat(t->type->maxHp))
        return;
    // An order, in sequence, like construction and reclaim.
    if (!queue) {
        cancelPath(*b);
        b->orders.clear();
    }
    Order o;
    o.x = t->x; o.z = t->z;
    o.goal = true;
    o.issuedTick = tickCounter_;
    o.repairTarget = targetId;
    b->orders.push_back(o);
}

// A builder repairs a damaged friendly: restores HP at the same rate it would build
// the unit (buildTime / workerTime), draining the owner's mana proportional to the
// HP restored -- pauses if the mana runs out.
void World::tickRepair(Unit& b, float dt) {
    Unit* t = unit(b.repairId);
    auto endRepair = [&] {
        b.repairId = 0;
        if (!b.orders.empty() && b.orders.front().repairTarget) b.orders.erase(b.orders.begin());
    };
    if (!b.type) { endRepair(); return; }
    // `t->hp <= Fixed()` is NOT covered by alive(): that reads deadFor, which is only set
    // by the death sweep in tick(). A unit whose hp has already reached zero this
    // tick still looks alive here, and tickRepair runs BEFORE the sweep -- so
    // without this a builder repairs a unit back off zero and it never dies.
    if (!t || !t->alive() || !t->type || t->hp <= Fixed() || t->underConstruction ||
        t->embarked() || !allied(t->player, b.player) || t->hp >= Fixed::fromFloat(t->type->maxHp)) {
        endRepair();
        return;
    }
    float dx = (t->x - b.x).toFloat(), dz = (t->z - b.z).toFloat();
    float half = 16.0f * float(std::max(t->type->footX, t->type->footZ)) / 2;
    float reach = std::max(half + 40.0f,
                           b.type->buildDist > 0 ? b.type->buildDist + half : 0.0f);
    if (dx * dx + dz * dz > reach * reach) {
        if (b.orders.empty()) order(b.id, t->x.toFloat(), t->z.toFloat(), false);   // (re)walk toward it
        return;
    }
    // ...but never the REPAIR order itself: that entry is the job.
    if (b.orders.empty() || !b.orders.front().repairTarget) dropLeg(b);
    b.speed = Fixed();
    const Bam want = fxAtan2(Fixed::fromFloat(dx), Fixed::fromFloat(dz));
    // Stopped: pivot at turninplacerate, not the moving turn rate.
    const int32_t bPivotMax = b.type->turnInPlaceRate;
    const int32_t diff = bamDiff(want, b.heading);
    b.heading = b.heading + Bam(std::clamp(diff, -bPivotMax, bPivotMax));
    b.turnReqBam = diff;   // builder pivot request -> TurnDirection anim (icd 0x4d9593)
    float total = t->type->buildTime / std::max(b.type->workerTime, 0.01f);
    Player& tm = players_[size_t(b.player)];
    float cost = t->type->buildCost * dt / std::max(total, 0.01f);
    if (tm.mana < cost) return;   // can't afford: pause the repair
    tm.debitMana(cost);
    t->hp = fxMin(Fixed::fromFloat(t->type->maxHp),
                  t->hp + Fixed::fromFloat(t->type->maxHp * dt / std::max(total, 0.01f)));
    if (t->hp >= Fixed::fromFloat(t->type->maxHp)) endRepair();   // mended: on to the next order
}

void World::startDisco(int player) {
    if (player < 0 || player >= int(players_.size())) return;
    auto& p = players_[size_t(player)];
    // One emote at a time: ignore if this player is already dancing OR headbanging
    // (so you can't stack or restart them). Deterministic across peers.
    if (p.discoLeft <= 0 && p.headbangLeft <= 0) p.discoLeft = 10 * int32_t(kTick);
}

bool World::discoActive(int player) const {
    return player >= 0 && player < int(players_.size()) &&
           players_[size_t(player)].discoLeft > 0;
}

void World::startHeadbang(int player) {
    if (player < 0 || player >= int(players_.size())) return;
    auto& p = players_[size_t(player)];
    if (p.discoLeft <= 0 && p.headbangLeft <= 0) p.headbangLeft = 10 * int32_t(kTick);
}

bool World::headbangActive(int player) const {
    return player >= 0 && player < int(players_.size()) &&
           players_[size_t(player)].headbangLeft > 0;
}

// Retire the queued build order a builder is sitting on, whatever ended the job
// (finished, cancelled, or the site turned out to be impossible). Until this runs
// the order blocks the queue, which is exactly what keeps the builder in place.
static void popBuildOrder(Unit& b) {
    if (!b.orders.empty() && b.orders.front().buildType) b.orders.erase(b.orders.begin());
}

static void turnBuilderToSite(Unit& b, const Unit& site) {
    const Bam want=retailHeadingToPort(uint16_t(retailDirection(b.x-site.x,b.z-site.z).v));
    const int32_t diff=bamDiff(want,b.heading);
    b.heading=b.heading+Bam(std::clamp(diff,-b.type->turnInPlaceRate,b.type->turnInPlaceRate));
    b.turnReqBam=diff;
}

void World::emitConstruction(Unit& u,bool rising) {
    ++u.constructionEmissions[rising ? 1 : 0];
    const int x=std::clamp(u.x.floorInt()/16,0,std::max(0,terW_-1));
    const int z=std::clamp(u.z.floorInt()/16,0,std::max(0,terH_-1));
    const int32_t y=u.type->canFly ? u.flightY.v :
        int32_t(heights_.empty() ? 0 : heights_[size_t(z)*terW_+x])*65536;
    if (!u.constructionEmitter) {
        if (!u.cosmeticConstructionEmitter) {
            auto& emitter=u.cosmeticConstructionEmitter.emplace();
            const double halfX=double(u.type->footX)*8*65536;
            const double halfZ=double(u.type->footZ)*8*65536;
            emitter.radius=int32_t(u.type->buildMovementCode==0 ? std::min(halfX,halfZ) :
                std::sqrt(halfX*halfX+halfZ*halfZ)+0.5);
            emitter.height=u.type->modelTop;
            emitter.capacity=uint32_t(std::max(0,emitter.radius>>16))/(u.type->buildMovementCode==1 ? 4u : 1u);
            u.constructionVisualRandom=uint32_t(u.id);
        }
        u.cosmeticConstructionEmitter->emit(1,y,rising,[&] {
            u.constructionVisualRandom=u.constructionVisualRandom*0x343fdu+0x269ec3u;
            return (u.constructionVisualRandom>>16)&0x7fffu;
        });
        return;
    }
    unsigned draw=0;
    u.constructionEmitter->emit(1,y,rising,[&]{return crtRand(draw++ ? 0x4f14a2u : 0x4f1467u);});
}

void World::completeRetailConstruction(Unit& builder,Unit& site) {
    const auto& owner=players_[size_t(builder.player)];
    if (!owner.buildCache) throw std::runtime_error("construction completion catalogue absent");
    const RetailBuildCacheEntry *builderType=nullptr,*siteType=nullptr;
    for (const auto& entry:owner.buildCache->entries) {
        if (entry.type==builder.type) builderType=&entry;
        if (entry.type==site.type) siteType=&entry;
    }
    if (!builderType || !siteType) throw std::runtime_error("construction completion type absent");
    if (site.embarked() || (siteType->inputs.flags&0x1040000u))
        throw std::runtime_error("special construction completion attachment/activation/death host is not ported");
    RetailConstructionCompletion input;
    input.builderPresent=true;input.builderFlags=builder.alive() ? 0x1000000u : 0;
    input.builderTypeFlags=builderType->inputs.flags;input.builderSecondaryFlags=builderType->inputs.secondaryFlags;
    input.siteTypeFlags=siteType->inputs.flags;input.sameUnit=builder.id==site.id;
    input.allied=allied(builder.player,site.player);input.deathType=uint8_t(site.deathType);
    // Retail's owner-role notification publishes completion on its wire.
    // Our lockstep peers already execute this transition from shared commands.
    if (!retailCompleteConstruction(site.retailSite->progress,input).applied)
        throw std::runtime_error("construction completion rejected by original eligibility rules");
    site.underConstruction=false;site.beingBuilt=false;
    site.standbyAllowed=true;site.guardNoMoveAllowed=true;
    // Completion publishes a solid mobile body before later movers/searches.
    // Cached grades must see it even if its stationary mover never refreshes.
    if (!site.type->canFly && !site.type->isStructure()) {
        const int foot=footCells(site.type);
        const int x=footprintOrigin(site.x,foot),z=footprintOrigin(site.z,foot);
        for (int dz=0;dz<foot;++dz) for (int dx=0;dx<foot;++dx)
            if (x+dx>=0 && z+dz>=0 && x+dx<occW_ && z+dz<occH_)
                occ_[size_t(z+dz)*occW_+x+dx]=site.id;
        for (auto& plane:searchGrades_) refreshSearchRect(plane,x,z,foot,foot);
    }
}

void World::tickRetailGetBuilt(Unit& site) {
    struct Host {
        World& w;Unit& u;
        bool enabled() const { return u.alive() && !u.incapacitated(); }
        RetailMissionState* head() { return u.retailSite && u.retailSite->mission ? &*u.retailSite->mission : nullptr; }
        bool hasNext(const RetailMissionState&) const { return false; }
        void idle() {}
        uint32_t random(int n) { return w.pathRand(uint32_t(n)); }
        void remove(RetailMissionState&) { u.retailSite->mission.reset(); }
        void rotate(RetailMissionState&) { throw std::runtime_error("unsupported GetBuilt rotation"); }
        void clear() { u.retailSite->mission.reset(); }
        int handle(RetailMissionState& m,uint32_t events) {
            auto& state=*u.retailSite;
            if (state.progress.remaining==0) {
                auto* builder=w.unit(state.builder);
                if (!u.type->isStructure() && builder && builder->alive() && builder->type->isStructure()) {
                    if (u.type->canFly || !builder->rally.empty())
                        throw std::runtime_error("factory flight/rally completion host is not ported");
                    Order order;order.goal=order.groundMission=true;order.park.emplace();
                    order.park->target=builder->id;
                    order.mission.flags=0x200u; // PARK definition retains its live target.
                    const uint32_t first=random(6),second=random(3);
                    order.park->padding=(first+second+3)*16;
                    u.orders.push_back(order);
                }
                return 5;
            }
            return retailGetBuiltWaiting(m,w.tickCounter_,events,[&] {
                // Fresh construction has no active weapon controller. Existing
                // unfinished imports are already in the waiting stages.
                if (!u.underConstruction) throw std::runtime_error("GetBuilt weapon host requires unfinished unit");
            },[&] {
                auto& owner=w.players_[size_t(u.player)];
                if (!owner.retailResources) throw std::runtime_error("GetBuilt resource state absent");
                state.progress.events=u.missionEvents;
                const auto result=retailConstructionWork(state.progress,state.type,*owner.retailResources,-0.5f);
                owner.mana=owner.retailResources->stored;u.hp=Fixed::fromInt(state.progress.hp);
                u.missionEvents=state.progress.events;
                if (result==RetailConstructionResult::Removed)
                    throw std::runtime_error("restored unconjure removal notifications are not ported");
            });
        }
    } host{*this,site};
    retailDispatchMissions(tickCounter_,site.missionEvents,host);
    site.retailSite->progress.events=site.missionEvents;
}

void World::tickRetailConstruction(Unit& builder) {
    if (!builder.alive() || builder.incapacitated() || builder.underConstruction || builder.embarked()) return;
    auto& job=*builder.retailBuild;
    if (job.turnHeading) {
        if (portHeadingToRetail(builder.heading)!=*job.turnHeading) {
            const Bam want=retailHeadingToPort(*job.turnHeading);
            const int32_t diff=bamDiff(want,builder.heading);
            builder.heading=builder.heading+Bam(std::clamp(diff,-builder.type->turnInPlaceRate,builder.type->turnInPlaceRate));
            builder.turnReqBam=diff;builder.speed=Fixed();
            return;
        }
        job.turnHeading.reset();
    }
    struct Host {
        World& w;Unit& b;
        bool enabled() const { return b.alive() && !b.incapacitated() && !b.underConstruction && !b.embarked(); }
        RetailMissionState* head() { return b.retailBuild ? &b.retailBuild->mission : nullptr; }
        bool hasNext(const RetailMissionState&) const { return false; }
        void idle() {}
        uint32_t random(int n) { return w.pathRand(uint32_t(n)); }
        void remove(RetailMissionState&) { clear(); }
        void rotate(RetailMissionState&) { throw std::runtime_error("unsupported construction rotation"); }
        void clear() {
            if (b.retailBuild && b.retailBuild->working) w.notifyUnitScript(b,"StopBuilding");
            b.retailBuild.reset();b.buildSiteId=0;b.constructionHolding=false;b.standbyAllowed=true;
        }
        int handle(RetailMissionState& m,uint32_t events) {
            auto& job=*b.retailBuild;
            auto* site=w.unit(job.target);
            if (events&2) return 5;
            if ((events&8) || !site || !site->alive()) return 8;
            if (job.flying && m.stage==5) {
                if ((job.flyingOwnerFlags&0xcu) && random(50)!=0) return 1;
                m.stage=4;return 4;
            }
            if (job.flying && m.stage==4) {
                const RetailFlightGoal goal=retailConstructionHoverGoal(
                    {b.x.v,0,b.z.v},{site->x.v,0,site->z.v},job.flyingBuildDistance,
                    [&](int n) {return int(random(n));});
                if (b.orders.empty()) throw std::runtime_error("flying construction order missing");
                auto& order=b.orders.front();order.flightGoal=goal;
                order.x=Fixed::raw(goal.point.x);order.z=Fixed::raw(goal.point.z);
                m.pending&=~0x3700u;
                m.stage=6;return 4;
            }
            if (job.flying && m.stage!=6)
                throw std::runtime_error("unsupported flying construction stage");
            if (m.stage==4) {
                if (job.working) { job.working=false;w.notifyUnitScript(b,"StopBuilding"); }
                return 5; // imported active jobs have exactly one build remaining
            }
            if (m.stage==2) {
                const auto script=w.unitScripts_.find(b.id);
                if (script!=w.unitScripts_.end() && script->second.ready) return 1;
                m.waitMask=0xe;return 2;
            }
            if ((!job.flying && m.stage!=3) || !site->retailSite)
                throw std::runtime_error("unsupported restored construction stage/site");
            auto& owner=w.players_[size_t(b.player)];
            if (!owner.retailResources) throw std::runtime_error("construction resource state absent");
            auto& state=*site->retailSite;
            state.progress.events=site->missionEvents;
            const auto result=retailConstructionWork(state.progress,state.type,*owner.retailResources,
                float(double(job.workerTime)*0.03333333));
            owner.mana=owner.retailResources->stored;
            site->hp=Fixed::fromInt(state.progress.hp);
            site->missionEvents|=state.progress.events;
            if (result==RetailConstructionResult::Completed) w.completeRetailConstruction(b,*site);
            if (result==RetailConstructionResult::Worked || result==RetailConstructionResult::Completed) {
                w.emitConstruction(b,false);w.emitConstruction(*site,true);
            }
            if (!job.flying) job.activityDeadline=std::max(job.activityDeadline,w.tickCounter_+300u);
            if (state.progress.remaining==0) return 1;
            m.sleep(w.tickCounter_,1);m.waitMask|=0xa;
            if (job.flying) { m.stage=5;return 4; }
            return 2;
        }
    } host{*this,builder};
    retailDispatchMissions(tickCounter_,builder.missionEvents,host);
}

void World::tickHoverAttack(Unit& u,const Unit& target,const Weapon* weapon) {
    auto& order=u.orders.front();
    const int distance=weapon && weapon->hoverAttackDistance ? weapon->hoverAttackDistance : u.type->hoverAttackDistance;
    const int altitude=weapon && weapon->hoverAttackAltitude ? weapon->hoverAttackAltitude : u.type->hoverAttackAltitude;
    if (!order.flightGoal || !order.hoverAttackRefresh || tickCounter_>=order.hoverAttackRefresh) {
        order.flightGoal=retailHoverAttackGoal({u.x.v,u.flightY.v,u.z.v},
            {target.x.v,0,target.z.v},distance,[&](int n) {return pathRand(uint32_t(n));});
        order.hoverAttackRefresh=tickCounter_+15+pathRand(15);
    } else {
        // The retail target controller follows its entity between mission wakes.
        order.flightGoal->point.x+=(target.x-order.hoverAttackTargetX).v;
        order.flightGoal->point.z+=(target.z-order.hoverAttackTargetZ).v;
    }
    order.hoverAttackTargetX=target.x;order.hoverAttackTargetZ=target.z;
    auto& goal=*order.flightGoal;
    int ground=seaLevel_;
    if (!heights_.empty()) ground=std::max(ground,retailTerrainHeight(goal.point.x,goal.point.z,
        terW_,terH_,[&](int x,int z) {return heights_[size_t(z)*terW_+x];}));
    goal.point.y=Fixed::fromInt(std::min(511,ground+altitude)).v;
    goal.heading=uint16_t(retailDirection(u.x-target.x,u.z-target.z).v);
    order.x=Fixed::raw(goal.point.x);order.z=Fixed::raw(goal.point.z);
    tickFlightMovement(u,true);
    notifyFlightOccupancy(u);
}

// Flying construction (retail 41ef00, stages 4--6) wanders near builddistance
// while facing the site. Use the same flight controller as ordinary flight;
// this is construction mission behavior, independent of ground navigation.
void World::tickConjureHover(Unit& b, const Unit& site) {
    if (b.conjureHoverTarget!=site.id) {
        b.conjureHoverTarget=site.id;
        b.conjureHoverGoal.reset();
    }
    if (!b.conjureHoverGoal || ((!b.speed.v && !b.turnReqBam) || pathRand(50)==0)) {
        b.conjureHoverGoal=retailConstructionHoverGoal(
            {b.x.v,0,b.z.v},{site.x.v,0,site.z.v},std::max(8,int(b.type->buildDist)),
            [&](int n) {return int(pathRand(uint32_t(n)));});
    }
    // Keep the player's construction/production queue intact. This temporary
    // controller belongs solely to the active conjure job.
    Order hover;
    hover.buildType=site.type;
    hover.flightGoal=b.conjureHoverGoal;
    hover.x=Fixed::raw(hover.flightGoal->point.x);
    hover.z=Fixed::raw(hover.flightGoal->point.z);
    b.orders.insert(b.orders.begin(),hover);
    tickFlightMovement(b);
    b.conjureHoverGoal=b.orders.front().flightGoal;
    b.orders.erase(b.orders.begin());
    notifyFlightOccupancy(b);
}

void World::tickConstruction(Unit& b, float dt) {
    Unit* site = unit(b.buildSiteId);
    if (!site || !site->alive() || !site->underConstruction) {
        b.buildSiteId = 0;
        popBuildOrder(b);
        return;
    }
    site->beingBuilt = true;   // a builder is assigned (walking or working): no decay
    float dx = (site->x - b.x).toFloat(), dz = (site->z - b.z).toFloat();
    float half = 16.0f * float(std::max(site->type->footX, site->type->footZ)) / 2;
    // Reach = the builder's FBI builddistance (to the site edge) when it has one,
    // else the default footprint-derived range.
    float reach = std::max(half + 40.0f,
                           b.type->buildDist > 0 ? b.type->buildDist + half : 0.0f);
    float gd = dx * dx + dz * dz;
    if (gd > reach * reach) {                          // out of range: still walking there
        // Give-up watchdog: if the builder gets no closer (~20px, squared 400) for ~8s,
        // it can't reach the site -- abandon it and pop the next queued build. Uses the
        // same fixed-dt / squared-distance idiom as the deleted movement watchdogs, so
        // it stays deterministic (buildStuck* are non-hashed scratch).
        if (gd < float(b.buildStuckD) - 400.0f) {      // real progress: reset the timer
            b.buildStuckD = int32_t(gd); b.buildStuckT = 0;
        } else if (++b.buildStuckT > 8 * int32_t(kTick)) {
            b.buildStuckT = 0; b.buildStuckD = INT32_MAX;
            // Drop an un-started ghost site (as cancelBuilds does) so its marker and
            // blocked footprint don't linger.
            if (!site->buildBegun) {
                if (site->type && site->type->isStructure()) {
                    blockFoot(*site->type, site->x.toFloat(), site->z.toFloat(), false);
                }
                site->underConstruction = false;
                site->deadFor = kRetiredTicks;
            }
            b.buildSiteId = 0;
            popBuildOrder(b);
            
        }
        return;
    }
    b.buildStuckT = 0; b.buildStuckD = INT32_MAX;      // in range: reset the watchdog
    site->buildBegun = true;   // in range: the site starts materialising now
    // Drop the APPROACH leg, but never the queued build order itself: that entry
    // IS the job, and it is what holds the rest of the queue back until the
    // building is finished (popBuildOrder retires it when the job ends).
    if (b.orders.empty() || !b.orders.front().buildType) dropLeg(b);   // never the build order
    b.constructionHolding = true;
    if (b.type->canFly) tickConjureHover(b,*site);
    else b.speed = Fixed();
    // Face what we're building/conjuring: turn toward the site at the unit's turn
    // rate (a building has turnRate 0, so it simply doesn't rotate).
    if (!b.type->canFly) turnBuilderToSite(b,*site);
    float total = site->type->buildTime / std::max(b.type->workerTime, 0.01f);
    // Record the current build rate so an interrupted conjure decays at this speed.
    // hp per TICK, in the same fixed-point hp is kept in (it is subtracted from hp
    // directly in decayConstruction).
    site->conjureRate = Fixed::fromFloat(site->type->maxHp * 0.95f
                                         / (std::max(total, 0.01f) * kTick));
    Player& tm = players_[size_t(b.player)];
    if (gInstantBuild) {
        site->hp = Fixed::fromFloat(site->type->maxHp);   // finishes this tick, free
    } else {
        float cost = site->type->buildCost * dt / std::max(total, 0.01f);
        if (tm.mana < cost) return;
        tm.debitMana(cost);
        site->hp += Fixed::fromFloat(site->type->maxHp * 0.95f * dt / std::max(total, 0.01f));
    }
    emitConstruction(b,false);
    emitConstruction(*site,true);
    if (site->hp >= Fixed::fromFloat(site->type->maxHp)) {
        site->hp = Fixed::fromFloat(site->type->maxHp);
        site->underConstruction = false;
        b.buildSiteId = 0;
        popBuildOrder(b);
    }
}

void World::decayConstruction(Unit& u, float dt) {
    // No builder worked this conjure this tick: it "un-conjures", losing HP at the
    // rate it was last built, and vanishes at zero (no corpse -- it was never
    // finished). A site that never materialised falls back to its nominal rate.
    const Fixed rate = u.conjureRate > Fixed()
                     ? u.conjureRate
                     : Fixed::fromFloat(u.type->maxHp * 0.95f
                                        / (std::max(u.type->buildTime, 0.01f) * kTick));
    u.hp -= rate;
    if (u.hp > Fixed()) return;
    if (u.type->isStructure()) {
        blockFoot(*u.type, u.x.toFloat(), u.z.toFloat(), false);
    }
    u.underConstruction = false;
    u.deadFor = kRetiredTicks;   // fully gone, no death anim
}

void World::train(int builderId, const UnitType* type, int count) {
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !type) return;
    for (int i = 0, n = std::max(1, count); i < n; ++i) b->buildQueue.push_back(type);
}

void World::tickRetailAiStrike(int owner,unsigned index) {
    auto unsupported=[&](const char* branch) {
        throw std::runtime_error("unsupported retail AI strike "+std::string(branch)+
                                 " at tick "+std::to_string(tickCounter_));
    };
    auto& player=players_[size_t(owner)];auto& ai=*player.retailAi;auto& squad=ai.squads[index];
    if (!player.buildCache) unsupported("type catalogue");
    RetailAiCentroid centroid;
    for (int id:squad.members) if (const Unit* u=unit(id);u && u->alive()) centroid.add(u->x.v,u->z.v);
    const auto center=centroid.center();if (!center) return;
    // With only one occupied base, the strength-filtered search and its
    // unfiltered fallback select the same recipient. Multiple bases need the
    // original weapon-controller strength calculation.
    unsigned bases=0;
    for (unsigned i=1;i<=20;++i) if (ai.schedule.groups[i].present && !ai.squads[i].members.empty()) ++bases;
    if (bases>1) unsupported("multiple retreat bases");
    const int backup=squad.parameters[0];
    if (backup<0 || backup>=100) unsupported("backup reference");
    if (int(squad.members.size()+ai.squads[size_t(backup)].members.size())<=squad.parameters[2])
        unsupported("squad dissolution");
    const Unit* target=unit(int(uint32_t(squad.parameters[4])&65535));
    if (!squad.parameters[5] || !target || !target->alive()) unsupported("target selection");
    if (pathRand(250)==0) unsupported("target refresh");
    int radius=10;
    for (int id:squad.members) {
        const Unit* u=unit(id);
        if (!u || !u->alive() || u->type->isStructure() || u->type->canFly || u->type->weapons.empty() || u->embarked())
            unsupported("member withdrawal");
        const RetailBuildCacheEntry* type=nullptr;
        for (const auto& entry:player.buildCache->entries) if (entry.type==u->type) { type=&entry;break; }
        if (!type || !type->construction) unsupported("member maximum HP");
        const unsigned divisor=pathRand(3)+3;
        if (uint32_t(int32_t(int16_t(u->hp.floorInt())))<type->construction->type.maxHp/divisor)
            unsupported("injured member withdrawal");
        const auto& weapon=u->type->weapons[size_t(std::clamp(u->weaponSlot,0,int(u->type->weapons.size())-1))];
        if (!u->orders.empty() && u->orders[currentLeg(u->orders)].controller && pathRand(20)==0) {
            const auto* nav=&navFor(u->type);
            const auto plane=std::find_if(searchGrades_.begin(),searchGrades_.end(),[&](const auto& p) {
                return p.nav==nav && p.footX==u->type->footX && p.footZ==u->type->footZ;
            });
            if (plane==searchGrades_.end() || plane->cells.empty() || plane->navVersion!=nav->version())
                unsupported("member reachability plane");
            const int range=std::max(1,weapon.range/16);
            if (!retailRectangleReachable(nav->width(),nav->height(),
                    footprintOrigin(u->x,u->type->footX),footprintOrigin(u->z,u->type->footZ),
                    u->type->footX,u->type->footZ,
                    footprintOrigin(target->x,target->type->footX),footprintOrigin(target->z,target->type->footZ),
                    range,range,true,false,[&](int x,int z) { return plane->cells[size_t(z)*nav->width()+x]; }))
                unsupported("unreachable member withdrawal");
        }
        radius=std::max(radius,std::max(u->type->sight,weapon.range));
        if (u->orders.empty() || !u->orders[currentLeg(u->orders)].groundMission || u->orders[currentLeg(u->orders)].park)
            unsupported("non-move member order");
    }
    auto distance=[](int32_t ax,int32_t az,int32_t bx,int32_t bz) {
        const int64_t x=std::bit_cast<int32_t>(uint32_t(ax)-uint32_t(bx));
        const int64_t z=std::bit_cast<int32_t>(uint32_t(az)-uint32_t(bz));
        return std::bit_cast<int32_t>(uint32_t((x*x)>>32)+uint32_t((z*z)>>32));
    };
    const int threatRadius=(radius*3)/2;
    for (const auto& enemy:units_) if (enemy.alive() && !allied(owner,enemy.player) &&
        distance(center->first,center->second,enemy.x.v,enemy.z.v)<=threatRadius*threatRadius)
        unsupported("nearby combat");
    squad.parameters[5]=2;
    bool close=false;
    for (int id:squad.members) {
        const auto& u=*unit(id);const auto& goal=u.orders[currentLeg(u.orders)];
        if ((goal.mission.flags&0x400u) && distance(goal.missionTarget ? goal.missionTarget->first.v : goal.x.v,
                goal.missionTarget ? goal.missionTarget->second.v : goal.z.v,target->x.v,target->z.v)<2500) {
            close=true;break;
        }
    }
    for (int id:squad.members) {
        auto& u=*unit(id);const auto& goal=u.orders[currentLeg(u.orders)];
        const bool force=(goal.mission.flags&0x10000u) && pathRand(50)==0;
        if (!force && close && pathRand(10)!=0) continue;
        // Issue the primary mission now; its stage-zero handler creates the
        // controller on the unit's next update, retaining exact fixed positions.
        Order next;next.x=goal.x;next.z=goal.z;next.goal=next.groundMission=true;
        next.segmentX=goal.segmentX;next.segmentZ=goal.segmentZ;next.hasSegment=goal.hasSegment;
        next.missionTarget=std::pair{target->x,target->z};
        next.mission.flags=0x3001401u;next.issuedTick=tickCounter_;
        next.groundResponse.mode=-1;
        next.groundResponse.goal={int16_t(target->x.floorInt()),int16_t(target->z.floorInt())};
        const size_t end=currentLeg(u.orders);
        cancelPath(u);u.routeStamp=0;u.orders.resize(end+1);u.orders[end]=next;u.standbyActive=false;
    }
    squad.setActive(1);
}

void World::tickRetailAiBase(int owner,unsigned index) {
    auto unsupported=[&](const char* branch) {
        throw std::runtime_error("unsupported retail AI base "+std::string(branch)+
                                 " at tick "+std::to_string(tickCounter_));
    };
    auto& player=players_[size_t(owner)];
    auto& ai=*player.retailAi;auto& squad=ai.squads[index];
    if (!player.buildCache || !player.buildCache->planner || !player.retailResources)
        unsupported("planner metadata");
    auto& cache=*player.buildCache;const auto& planner=*cache.planner;
    auto typeIndex=[&](const Unit& u)->size_t {
        for (size_t i=0;i<cache.entries.size();++i) if (cache.entries[i].type==u.type) return i;
        unsupported("unit catalogue");return 0;
    };
    auto classify=[&](const Unit& u) {
        const size_t i=typeIndex(u);const auto& inputs=cache.entries[i].inputs;
        RetailAiUnitClass result;
        result.flags=u.alive()?0x1000000u:0;
        if (u.type->isStructure()) result.flags|=0x2000000u;
        if (!u.type->weapons.empty()) result.flags|=0x8000000u;
        result.typeFlags=inputs.flags;result.secondaryFlags=inputs.secondaryFlags;
        result.constructor=!planner.types[i].choices.empty();result.mover=!u.type->isStructure();
        result.capacity=inputs.weapon;
        result.mission=u.retailBuild.has_value() || !u.orders.empty() || u.standbyActive;
        result.missionFlags=u.retailBuild?8:(!u.orders.empty()?uint8_t(u.orders.back().mission.flags):0);
        return result;
    };
    auto countCategory=[&](unsigned category,bool ready) {
        int count=0;
        for (int id:squad.members) {
            const Unit* u=unit(id);
            if (!u || !retailAiCategory(classify(*u),category)) continue;
            if (ready) {
                if (u->underConstruction || u->incapacitated()) continue;
                if (u->embarked()) unsupported("attached member count");
                const auto& type=cache.entries[typeIndex(*u)];
                if (!type.construction) unsupported("member maximum HP");
                if (uint32_t(int32_t(int16_t(u->hp.floorInt())))<(type.construction->type.maxHp*3u)/4u) continue;
            }
            ++count;
        }
        return count;
    };
    if (squad.parameters[0] && !ai.anchorsRestored) unsupported("anchor metadata");
    if (squad.parameters[0] && !ai.anchors.contains(squad.parameters[0])) squad.parameters[0]=0;
    std::optional<std::pair<int32_t,int32_t>> center;
    if (auto anchor=ai.anchors.find(squad.parameters[0]);anchor!=ai.anchors.end()) {
        center=std::pair{std::bit_cast<int32_t>(uint32_t(int32_t(anchor->second.first))*1048576u+524288u),
                         std::bit_cast<int32_t>(uint32_t(int32_t(anchor->second.second))*1048576u+524288u)};
    } else {
        RetailAiCentroid centroid;
        for (int id:squad.members) if (const Unit* u=unit(id);u && u->alive()) centroid.add(u->x.v,u->z.v);
        center=centroid.center();
    }
    if (!center) return;
    int32_t footprint=0;
    for (int id:squad.members) if (const Unit* u=unit(id);u && u->alive() && u->type->isStructure())
        footprint+=u->type->footX+u->type->footZ;
    const int radius=retailAiBaseRadius(footprint,0);
    for (const auto& enemy:units_) if (enemy.alive() && !allied(owner,enemy.player)) {
        const int64_t dx=std::bit_cast<int32_t>(uint32_t(center->first)-uint32_t(enemy.x.v));
        const int64_t dz=std::bit_cast<int32_t>(uint32_t(center->second)-uint32_t(enemy.z.v));
        const int32_t distance=std::bit_cast<int32_t>(uint32_t((dx*dx)>>32)+uint32_t((dz*dz)>>32));
        if (distance<=radius*radius) unsupported("nearby threat response");
    }
    unsigned population=0;
    for (const auto& u:units_) if (u.alive() && u.player==owner) ++population;
    for (int id:squad.members) {
        Unit* u=unit(id);
        if (!u || !u->alive() || u->underConstruction || u->incapacitated()) continue;
        if (u->embarked()) unsupported("attached member planning");
        const auto c=classify(*u);const size_t source=typeIndex(*u);
        if (c.constructor && u->type->isStructure()) {
            retailAiPlanFactory(player.retailResources->allocation,!u->buildQueue.empty(),uint16_t(population),[&] {
                std::vector<RetailAiBuildChoice> choices;
                for (uint16_t index:planner.types[source].choices) {
                    const auto& entry=cache.entries[index-1];const auto& meta=planner.types[index-1];
                    choices.push_back({index,retailAiBuildWeight(planner.limited,entry.inputs.desired,
                        entry.inputs.count,meta.weight,int8_t(entry.priority)),meta.special,
                        meta.faction==planner.types[source].faction});
                }
                return retailAiChooseBuild(choices,false,[&](int n){return pathRand(n);});
            },[&](uint16_t index){return cache.entries[index-1].inputs.cost;},
            [&](int n){return pathRand(n);},[&](uint16_t index){train(id,cache.entries[index-1].type);});
        } else if (c.constructor && c.mover) {
            if (!u->retailBuild) unsupported("idle mobile builder");
            if (c.secondaryFlags&0x40000u) {
                const auto& entry=cache.entries[source];
                if (!entry.construction || u->hp.floorInt()<int(entry.construction->type.maxHp/2))
                    unsupported("commander retreat");
                if (countCategory(2,false)>=squad.parameters[1] || tickCounter_<ai.scenarioDeadline)
                    unsupported("commander combat planning");
            }
            // An active construction mission owns the builder's next action.
        } else if (c.mover) {
            if (!c.mission || (!u->orders.empty() && (u->orders.back().mission.flags&0x2000u)))
                unsupported("idle combat member");
            if (pathRand(50)==0) {
                if (!u->type->canFly || u->type->isBuilder || !u->type->weapons.empty())
                    unsupported("combat member replanning");
                const auto& definition=cache.entries[source];
                if (!definition.construction || uint32_t(u->hp.floorInt())<definition.construction->type.maxHp/4)
                    unsupported("injured flight patrol planning");
                const auto points=retailAiPatrolPoints(center->first,center->second,radius,
                    terW_*16,terH_*16,[&](int n){return pathRand(n);},
                    [](int32_t,int32_t){return true;});
                if (!points) unsupported("flight patrol candidate exhaustion");
                if (fxLen(u->x-Fixed::raw(center->first),u->z-Fixed::raw(center->second))>Fixed::fromInt(radius))
                    unsupported("flight patrol return to base");
                cancelPath(*u);u->orders.clear();
                for (size_t i=0;i<points->size();++i) {
                    Order order;order.x=Fixed::raw((*points)[i].first);order.z=Fixed::raw((*points)[i].second);order.goal=true;
                    order.flightMoveMission=i==0;order.patrol=i!=0;
                    order.mission.flags=i==0?0x1000403u:0x1001413u;
                    order.issuedTick=tickCounter_;u->orders.push_back(order);
                }
            }
        }
    }
    if (int(population/(index==1?20:30))>squad.parameters[1]) {
        for (unsigned i=1;i<=4;++i) {
            const uint32_t delta=pathRand(squad.parameters[i]/2)+1u;
            squad.parameters[i]=std::bit_cast<int32_t>(uint32_t(squad.parameters[i])+delta);
        }
    }
    const int freeBuilders=countCategory(3,true)-(index==1?0:1);
    const int builders=countCategory(2,true)-squad.parameters[1];
    const int ground=countCategory(4,true)-(squad.parameters[5]<0?squad.parameters[2]:0);
    const int air=countCategory(5,true)-(squad.parameters[6]<0?squad.parameters[3]:0);
    const int transports=countCategory(6,true)-(squad.parameters[7]<0?squad.parameters[4]:0);
    if (std::max(freeBuilders,builders)>0) unsupported("builder redistribution");
    auto redistribute=[&](const char* branch) {
        // The original recipient search excludes this base and all empty slots.
        // A populated recipient still requires the transfer planner.
        for (unsigned other=1;other<=20;++other)
            if (other!=index && ai.schedule.groups[other].present && !ai.squads[other].members.empty())
                unsupported(branch);
    };
    if (ground>0 && pathRand(10)==0) redistribute("ground redistribution");
    if (air>0 && pathRand(10)==0) redistribute("air redistribution");
    auto magnitude=[](int32_t n) { return n<0?-int64_t(n):int64_t(n); };
    if (ground>=magnitude(squad.parameters[5]) || air>=magnitude(squad.parameters[6]))
        unsupported("strike recruitment");
    if (transports>=magnitude(squad.parameters[7])) unsupported("transport recruitment");
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
        if (idx == 0) { b->buildProgress = 0; b->productionSiteId = 0; }   // canceling the in-progress front
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
        b->productionSiteId = 0;
    } else {
        if (b->type && !b->type->isStructure()) {
            // Entering infinite production also retires an earlier move/attack;
            // otherwise the builder can keep following it beneath the queue.
            cancelBuilds(builderId);
            b->orders.clear();b->landing.reset();b->standbyActive=false;
            cancelPath(*b);b->routeStamp=-1;
        }
        b->repeatType = type;
        if (b->buildQueue.empty()) b->buildQueue.push_back(type);   // kick it off
    }
}

// [AdjustJoy]: a passive repair aura, not morale. Once a second an emitter counts the
// eligible units in its radius, then repairs each DAMAGED one at build power
// `adjustment * falloff / N`, paying its own owner's mana at the target's
// buildcost/buildtime rate (KINGDOMS.icd 0x51d3e0 -> 0x51f7b0 -> 0x429d90). Dividing
// by the full count -- damaged or not -- is what stops it being absurd in a crowd.
void World::tickHealAuras() {
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& s = units_[i];
        if (!s.alive() || s.embarked() || !s.type || s.type->auras.empty() ||
            s.underConstruction || !s.active || s.incapacitated())
            continue;
        for (const Aura& a : s.type->auras) {
            if (a.kind != Aura::Kind::Joy || a.radius <= 0 || a.amount <= 0) continue;
            float r2 = a.radius * a.radius;
            // A target must be alive, fully built, on the right side, and not the
            // emitter itself. Pass 1 counts them all; pass 2 heals the damaged ones.
            auto eligible = [&](const Unit& e, float d2) {
                return e.alive() && !e.embarked() && e.type && e.id != s.id &&
                       !e.underConstruction && d2 <= r2 &&
                       (!allied(e.player, s.player)) == a.affectsEnemy;
            };
            int n = 0;
            forEachNear(s.x.toFloat(), s.z.toFloat(), a.radius, [&](int idx) {
                const Unit& e = units_[size_t(idx)];
                float dx = (e.x - s.x).toFloat(), dz = (e.z - s.z).toFloat();
                if (eligible(e, dx * dx + dz * dz)) ++n;
            });
            if (n == 0) continue;
            Player& tm = players_[size_t(s.player)];
            forEachNear(s.x.toFloat(), s.z.toFloat(), a.radius, [&](int idx) {
                Unit& e = units_[size_t(idx)];
                float dx = (e.x - s.x).toFloat(), dz = (e.z - s.z).toFloat();
                float d2 = dx * dx + dz * dz;
                if (!eligible(e, d2)) return;
                if (e.hp >= Fixed::fromFloat(e.type->maxHp)) return;   // retail heals only the damaged
                // ... and never anything already at zero: alive() reads deadFor, so a
                // unit killed this tick still looks alive until the sweep in tick()
                // runs, and the aura pulse fires AFTER that loop. Healing it here
                // resurrects it (a self-destruct inside a friendly aura never died).
                if (e.hp <= Fixed()) return;
                float power = a.amount * a.falloff(std::sqrt(d2)) / float(n);
                float prog = power / std::max(e.type->buildTime, 0.01f);
                // Deliberate divergence: retail bills the whole pulse even when only a
                // sliver of it lands, so topping off one unit can drain a player's
                // pool. Charge for the repair actually done instead.
                float need = 1.0f - e.hp.toFloat() / std::max(float(e.type->maxHp), 1.0f);
                prog = std::min(prog, need);
                float cost = e.type->buildCost * prog;
                if (cost > tm.mana) {   // short on mana: heal proportionally less
                    prog *= tm.mana / std::max(cost, 1e-6f);
                    cost = tm.mana;
                }
                tm.debitMana(cost);
                e.hp = fxMin(Fixed::fromFloat(e.type->maxHp),
                             e.hp + Fixed::fromFloat(e.type->maxHp * prog));
            });
        }
    }
}

void World::tickAuras(float dt) {
    // The heal aura runs at its own 1 Hz cadence, hoisted above the crowd stride
    // below so a big battle can never swallow its tick.
    if (tickCounter_ % 30 == 0) tickHealAuras();
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
            forEachNear(s.x.toFloat(), s.z.toFloat(), a.radius, [&](int idx) {
                Unit& e = units_[size_t(idx)];
                if (!e.alive() || e.embarked() || !e.type) return;
                bool enemy = !allied(e.player, s.player);
                if (enemy != a.affectsEnemy) return;   // buff friends OR debuff foes
                float dx = (e.x - s.x).toFloat(), dz = (e.z - s.z).toFloat();
                float d2 = dx * dx + dz * dz;
                if (d2 > r2) return;
                // Retail's falloff is edge-at-the-CENTRE, full-at-the-RIM (see the
                // Aura comment); we had it the other way round, so a Shaman's +20%
                // was strongest where it should have been weakest.
                float amt = 1.0f + (a.amount - 1.0f) * a.falloff(std::sqrt(d2));
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
        const int from = c.corpseStatue >= 0 ? 0 : kCorpseAnimTicks;
        return c.type && !c.alive() && c.deadFor >= from && c.deadFor < c.corpseUntil;
    };
    auto corpseDef = [&](const Unit& c) -> const FeatType* {
        int ct = c.corpseStatue >= 0 ? c.corpseStatue : corpseTypeOf(c.type);
        return ct >= 0 ? &featTypes_[size_t(ct)] : nullptr;
    };
    auto retire = [&](Unit& c) { retireCorpse(c); };
    struct Revive { const UnitType* type; float x, z; int player; bool animate; };
    std::vector<Revive> revives;
    const float kR = 56.0f;

    // COLLECT THE CORPSES ONCE, not once per caster.
    //
    // The inner loop below used to walk all of units_ for EVERY idle caster -- and most
    // builders can reclaim, so an idle base makes a lot of casters. Measured at 3,722
    // units: a mean of 546,000 unit visits per tick and a peak of 1,296,823, against
    // 245,000 for the whole of combat acquisition at its own peak. It is O(casters x N),
    // so at 15k units it is roughly 20M visits a tick -- the same shape as the O(n^2)
    // that dropped a stress client at tick 151.
    //
    // The spatial grid cannot answer this: it holds only ALIVE units, deliberately, and
    // corpses are dead. One ordered pass collects them instead, so the inner loop walks
    // corpses rather than the whole army.
    //
    // ASCENDING UNIT ID IS PRESERVED -- units_ is scanned in order, so candidates are
    // considered in exactly the sequence the nested loop used, and "first eligible wins"
    // still picks the same corpse. `retire()` mutates corpses as casters claim them, and
    // the eligibility re-check inside the loop still runs, so a corpse taken by an
    // earlier caster is skipped by a later one exactly as before.
    corpseIdx_.clear();
    for (size_t j = 0; j < units_.size(); ++j)
        if (isCorpse(units_[j])) corpseIdx_.push_back(uint32_t(j));
    // No early return when the list is empty, deliberately. The per-caster loop below
    // does more than search for corpses: it also DROPS a channel whose target has gone
    // (interrupted, out of range, or decomposed). Returning here skipped that, so if the
    // last corpse decomposed under a channeling caster its reviveTarget survived, and
    // the caster then spent the tick the next corpse appeared clearing the stale target
    // instead of acquiring. Only the SEARCH is guaranteed to have no work, and with an
    // empty corpseIdx_ that inner loop is already a no-op -- so the walk stays O(n),
    // which is the whole point of collecting the corpses once. (Found by review.)

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
            float dx = (c->x - u.x).toFloat(), dz = (c->z - u.z).toFloat();
            if (dx * dx + dz * dz > kR * kR * 4) { u.reviveTarget = 0; continue; }
            bool animate = u.reviveMode == 2;
            const UnitType* out = animate ? u.type->animateType : c->type;
            float totalMana = out->buildCost * (animate ? 0.3f : 1.0f);
            float inc = totalMana / float(std::max(u.reviveTotal, 1));
            Player& tm = players_[size_t(u.player)];
            if (tm.mana < inc) continue;   // starved: the channel stalls, not drops
            tm.debitMana(inc);
            --u.reviveLeft;
            if (u.reviveLeft <= 0) {
                revives.push_back({out, c->x.toFloat(), c->z.toFloat(), u.player, animate});
                retire(*c);
                u.reviveTarget = 0;
            }
            continue;
        }

        // Corpses only -- collected once above, in ascending unit id.
        for (uint32_t ci : corpseIdx_) {
            const size_t j = size_t(ci);
            Unit& c = units_[j];
            if (j == i || !isCorpse(c)) continue;   // re-check: an earlier caster may have taken it
            float dx = (c.x - u.x).toFloat(), dz = (c.z - u.z).toFloat();
            if (dx * dx + dz * dz > kR * kR) continue;
            const FeatType* cd = corpseDef(c);
            if (!cd) continue;
            if (u.type->canResurrect && c.player == u.player && cd->resurrectable) {
                // Channel length: buildtime / workertime seconds (retail
                // 0x4201e9), mana drained over it; unit returns at 10% HP.
                u.reviveTarget = c.id;
                u.reviveMode = 1;
                // TICKS. This was seconds assigned straight into an int32 tick
                // count, so a 170s resurrect became 170 ticks -- 5.7 seconds.
                u.reviveTotal = u.reviveLeft = int32_t(
                    std::max(c.type->buildTime / std::max(u.type->workerTime, 0.01f),
                             0.5f) * kTick + 0.5f);
                break;
            } else if (u.type->canAnimate && u.type->animateType &&
                       cd->resurrectable) {
                // Animate (tarpries animatetype=MONGHOUL): ANY resurrectable
                // corpse -- friend or foe -- rises as the caster's creature,
                // at 0.3x the work and FULL HP (retail 0x420257/0x4206ba).
                u.reviveTarget = c.id;
                u.reviveMode = 2;
                u.reviveTotal = u.reviveLeft = int32_t(
                    std::max(u.type->animateType->buildTime /
                                 std::max(u.type->workerTime, 0.01f) * 0.3f,
                             0.5f) * kTick + 0.5f);
                break;
            } else if (u.type->canReclaim && cd->reclaimable) {
                // Retail yield = the corpse def's energy -- 0 for every shipped
                // corpse. You reclaim bodies to DENY resurrection, not for mana.
                players_[size_t(u.player)].creditMana(double(cd->energy)*players_[size_t(u.player)].manaMult);
                retire(c);
            }
        }
    }
    for (const auto& r : revives) {
        int id = spawn(r.type, r.x, r.z, 3.14159f, r.player);
        // Retail HP: resurrect returns the unit at 10% (min 1); an animated
        // creature rises at FULL health (icd 0x420666-0x4206ba).
        if (Unit* nu = unit(id))
            if (!r.animate) nu->hp = Fixed::fromFloat(std::max(nu->type->maxHp * 0.1f, 1.0f));
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

// Collect a finished fog pass and start the next one. Called every tick from the sim;
// the cadence timer decides when a new pass is DUE, this decides when one can actually
// start (never two at once -- visGather's placeholder dedup and the worker's cache
// writes both assume exclusive ownership of visMaskCache_).
void World::visPump() {
    if (visRunning_ && visDone_.load(std::memory_order_acquire)) {
        visWorker_.join();
        visRunning_ = false;
        visDone_.store(false, std::memory_order_relaxed);
        vis_.swap(visBack_);
        ++visGen_;   // renderer: fog content may have changed; re-upload once
        visHavePass_ = true;
    }
}

void World::updateNavigationExploration() {
    if (explorationHeights_.empty()) return;
    const int width=hW_/2,height=hH_/2;
    std::vector<uint16_t> playerViewers(players_.size(),0);
    for (size_t owner=0;owner<players_.size();++owner)
        for (size_t viewer=0;viewer<players_.size() && viewer<16;++viewer)
            if (allied(int(owner),int(viewer))) playerViewers[owner]|=uint16_t(1u<<viewer);
    const unsigned available=serialThreads_ ? 1u : std::max(1u,std::thread::hardware_concurrency());
    const unsigned workers=std::min({4u,available,unsigned(std::max(size_t(1),units_.size()/2048))});
    // Seed private masks before starting workers: each can skip cells already
    // explored without reading another worker's writes.
    for (unsigned k=1;k<workers;++k) explorationScratch_[k-1]=navigationExplored_;
    auto explore=[&](size_t begin,size_t end,std::vector<uint16_t>& revealed) {
        for (size_t index=begin;index<end;++index) {
            auto& u=units_[index];
            if (!u.alive() || !u.type || u.underConstruction || u.embarked()) continue;
            const int16_t distance=int16_t(u.type->sight);
            if (u.sightFootprint.distance!=distance || u.sightFootprint.sightHeight!=u.type->sightHeight) {
                u.sightFootprint={};
                u.sightFootprint.distance=distance;
                u.sightFootprint.sightHeight=u.type->sightHeight;
            }
            const uint16_t viewers=size_t(u.player)<playerViewers.size() ? playerViewers[size_t(u.player)] : 0;
            const Fixed y=u.type->canFly?u.flightY:u.groundY;
            retailUpdateSight(u.sightFootprint,u.x,y,u.z,uint8_t(seaLevel_),true,width,height,uint8_t(u.player),
                [&](int x,int z){return explorationHeights_[size_t(z)*width+x];},
                [&](int x,int z,int delta,uint16_t) {
                    if (delta>0) revealed[size_t(z)*width+x]|=viewers;
                },
                [&](int x,int z,bool active) {
                    // Exploration only adds bits. Already revealed cells need
                    // no terrain calculation, and removal never changes this map.
                    return active && (revealed[size_t(z)*width+x]&viewers)!=viewers;
                });
        }
    };
    // Each worker owns distinct unit footprints and an independent reveal mask.
    // OR is associative and commutative; merge only after all workers join.
    const size_t chunk=(units_.size()+workers-1)/workers;
    std::vector<std::thread> threads;
    struct JoinThreads {
        std::vector<std::thread>& threads;
        ~JoinThreads() { for (auto& thread:threads) if (thread.joinable()) thread.join(); }
    } joinThreads{threads};
    threads.reserve(workers-1);
    for (unsigned k=1;k<workers;++k) {
        const size_t begin=std::min(units_.size(),size_t(k)*chunk);
        const size_t end=std::min(units_.size(),begin+chunk);
        threads.emplace_back([&,k,begin,end]{explore(begin,end,explorationScratch_[k-1]);});
    }
    explore(0,std::min(units_.size(),chunk),navigationExplored_);
    for (auto& thread:threads) thread.join();
    for (unsigned k=1;k<workers;++k)
        for (size_t i=0;i<navigationExplored_.size();++i)
            navigationExplored_[i]|=explorationScratch_[k-1][i];
}

void World::updateVisibility() {
    if (nav_.empty()) return;
    if (visPlayer_ < 0) return;   // headless referee: nothing renders, no fog needed
    if (visRunning_) return;      // previous pass still going; skip this beat
    // The FIRST pass runs inline. Async fog lands a beat late, which is invisible mid-game
    // but would leave the opening frames (and a --shot capture) with an unpopulated fog
    // buffer -- the whole map dark. At world start there are only the handful of starting
    // units, so this one is cheap; it is the crowded steady state that needed the worker.
    //
    // The test is "have we ever PRODUCED a pass", not "is the buffer allocated": setTerrain
    // pre-fills vis_ with zeros so the opening frames render fully fogged rather than fully
    // bare, so vis_.empty() is already false by the time we first get here and the inline
    // path never ran. The safeguard was dead code, and --shot only looked right because it
    // waits seconds before capturing.
    bool first = !visHavePass_;
    if (vis_.empty()) {
        visW_ = nav_.width();
        visH_ = nav_.height();
        vis_.assign(size_t(visW_) * visH_, 0);
    }
    visGather();
    // The back buffer starts from the current fog so EXPLORED memory carries over; the
    // demote and the stamping both happen on it, off-thread, leaving vis_ readable.
    visBack_ = vis_;
    if (first || serialThreads_) {   // first pass, and single-threaded callers: inline
        visCompute();
        vis_.swap(visBack_);
        ++visGen_;
        visHavePass_ = true;
        return;
    }
    visRunning_ = true;
    visDone_.store(false, std::memory_order_relaxed);
    visWorker_ = std::thread([this] {
        visCompute();
        visDone_.store(true, std::memory_order_release);
    });
}

void World::visGather() {
    // PASS 1a (serial, cheap): gather every revealer's stamp parameters, and collect the
    // set of NEW LoS masks that need computing (deduped via an empty cache placeholder --
    // the ray-march that fills them runs in parallel in pass 1b). Bound the cache up front;
    // clearing mid-pass would desync the placeholder dedup. Fog is client-only display (the
    // referee returned above), so NONE of this is hashed.
    if (visMaskCache_.size() >= 8192) visMaskCache_.clear();
    std::vector<Reveal>& reveals = visReveals_;
    std::vector<Miss>&   misses  = visMisses_;
    reveals.clear();
    misses.clear();
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
        int cx = u.x.floorInt() / 16, cz = u.z.floorInt() / 16;
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
}

// Everything below runs on visWorker_: it reads the immutable heightmap and the gathered
// reveal list, and writes only visMaskCache_ (exclusively owned while a pass is running)
// and visBack_ (not the buffer the renderer is reading).
void World::visCompute() {
    std::vector<Reveal>& reveals = visReveals_;
    std::vector<Miss>&   misses  = visMisses_;
    // Eye above the unit's ground cell: sees over small bumps, not over real
    // walls/hills. Paired with the sight-line MARGIN in sightClear so small rock
    // clutter stops casting fog shadows. Live-tunable via TAK_FOG_EYE.
    static float EYE = [] {
        const char* e = std::getenv("TAK_FOG_EYE"); return e ? float(std::atof(e)) : 40.0f;
    }();
    // Demote last pass's visible cells: to EXPLORED (1, dimmed but remembered) normally, or
    // straight to hidden (0) when fog memory is off, so they go dark again once out of sight.
    for (auto& v : visBack_)
        if (v == 2) v = fogExplored_ ? 1 : 0;
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
        unsigned hw = serialThreads_ ? 1u : std::thread::hardware_concurrency();
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
    // PASS 2 (parallel): stamp visBack_[i]=2 for every revealer's cells (cached mask or
    // circle fill). It only READS the now-warm cache and writes the constant 2, and two
    // threads can land on the same cell where reveal circles overlap.
    //
    // This used to claim that was "a benign race, no locks needed" because both threads
    // store the same value. That is not a thing C++ says: concurrent non-atomic writes to
    // the same object are a data race and therefore UB, no matter how equal the values --
    // and it would trip any race detector pointed at this. Do a relaxed atomic store
    // instead: on every target we build for, a relaxed 1-byte store is the same
    // instruction as the plain one, so this is free at runtime and merely makes the
    // guarantee real. Relaxed is enough because nothing ORDERS off these writes; the
    // worker's join in visPump() is what publishes the finished buffer.
    //
    // Not std::atomic_ref, which is the obvious spelling and does not exist in Apple's
    // libc++ -- it broke the macOS release build and nothing local caught it, since
    // libstdc++ has had it since GCC 10. __atomic_store_n is the same store and is
    // available on both compilers we build with.
    auto put = [&](size_t i) {
        __atomic_store_n(&visBack_[i], uint8_t(2), __ATOMIC_RELAXED);
    };
    auto stamp = [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const Reveal& rv = reveals[i];
            if (!rv.los) {
                for (int dz = -rv.r; dz <= rv.r; ++dz)
                    for (int dx = -rv.r; dx <= rv.r; ++dx) {
                        if (dx * dx + dz * dz > rv.r * rv.r) continue;
                        int x = rv.cx + dx, z = rv.cz + dz;
                        if (x < 0 || z < 0 || x >= visW_ || z >= visH_) continue;
                        put(size_t(z) * visW_ + x);
                    }
                continue;
            }
            auto mi = visMaskCache_.find(rv.key);   // guaranteed present after pass 1
            if (mi != visMaskCache_.end())
                for (uint32_t idx : mi->second) put(idx);
        }
    };
    size_t n = reveals.size();
    unsigned hw = serialThreads_ ? 1u : std::thread::hardware_concurrency();
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
}

void World::summonReadyGods() {
    if (!godsEnabled_) return;
    for (size_t t = 0; t < players_.size(); ++t) {
        if (!godReady(int(t))) continue;
        // The god appears among the player's forces: the centroid of everything it
        // has standing. Accumulated in unit order over a container every peer builds
        // identically, so the sum -- and therefore the spawn point -- is bit-identical.
        float cx = 0, cz = 0;
        int n = 0;
        for (const auto& u : units_)
            if (u.alive() && u.player == int(t) && u.type && !u.underConstruction) {
                cx += u.x.toFloat();
                cz += u.z.toFloat();
                ++n;
            }
        // Mark it handled either way: a player with nothing left on the map does not
        // get to bank the summon until it rebuilds.
        players_[t].godSummoned = true;
        if (!n || !players_[t].godType) continue;
        spawn(players_[t].godType, cx / float(n), cz / float(n), 3.14159f, int(t));
    }
}

// Nearest point to (fx,fz) that a `t`-sized body fits in AND nothing is standing on.
// Returns false (leaving out* at the requested point) when the whole neighbourhood is
// taken, so the caller can decide whether to wait or to proceed anyway.
bool World::exitSpot(const UnitType* t, float fx, float fz, float& outX, float& outZ) const {
    outX = fx;
    outZ = fz;
    if (!t) return false;
    const NavGrid& g = navFor(t);
    if (g.empty()) return false;
    const int foot = footCells(t);
    // Clearance to keep from the nearest body: our own half-width plus a little, so
    // the spot is somewhere this unit can actually come to rest.
    const float clr = float(std::max(t->footX, t->footZ)) * 8.0f + 10.0f;
    const int cx = footprintCell(fx, foot), cz = footprintCell(fz, foot);
    for (int r = 0; r <= 12; ++r)
        for (int j = -r; j <= r; ++j)
            for (int i = -r; i <= r; ++i) {
                if (std::max(std::abs(i), std::abs(j)) != r) continue;   // ring only
                const int nx = cx + i, nz = cz + j;
                if (!g.fits(nx, nz, foot)) continue;
                const float wx = footprintWaypoint(nx, foot).toFloat(), wz = footprintWaypoint(nz, foot).toFloat();
                bool taken = false;
                forEachNear(wx, wz, clr, [&](int idx) {
                    const Unit& e = units_[size_t(idx)];
                    if (!e.alive() || !e.type) return;
                    const float dx = e.x.toFloat() - wx, dz = e.z.toFloat() - wz;
                    if (dx * dx + dz * dz < clr * clr) taken = true;
                });
                if (taken) continue;
                outX = wx;
                outZ = wz;
                return true;
            }
    return false;
}

struct World::ScriptHost {
    World& world; Unit& unit; UnitScript& factory;
    uint32_t random(int32_t bound) { return world.gameRand(bound); }
    uint32_t get(int id,const std::array<uint32_t,4>&) {
        switch (id) {
        case 1: return factory.activated;
        case 2: return uint32_t(unit.moveState);
        case 3: return uint32_t(unit.fireState);
        case 4: return uint32_t(int32_t(int16_t(unit.hp.floorInt()))*100/std::max(unit.maximumHp(),1));
        case 5: return factory.ready;
        case 8: return unit.productionSiteId!=0;
        case 17: return unit.retailSite ? retailConstructionPercent(unit.retailSite->progress.remaining)
                                       : uint32_t(unit.underConstruction);
        case 18: return factory.yardOpen;
        case 19: return factory.buggerOff;
        case 27: return portHeadingToRetail(unit.heading);
        case 28: return (unit.groundTerrainFlags&0x1000)!=0;
        case 34: return (unit.groundTerrainFlags&0x800)!=0;
        case 29: case 30: {
            const auto multiplier=unit.groundTerrainFlags&0x800 ? unit.type->roadMult :
                unit.groundTerrainFlags&0x1000 ? unit.type->waterMult : Fixed::fromInt(1);
            const auto maximum=unit.baseSpeed*multiplier;
            const bool refused=!unit.type->canFly && unit.bodyBlockStreak>=2;
            if(id==30) return unit.type->canFly ? uint32_t(retailFlightVerticalPercent(
                unit.flightVelocity.y,maximum.v,false,unit.embarked())) : 0;
            const auto step=retailGroundStep(unit.heading,unit.speed);
            return uint32_t(retailHorizontalAnimationPercent(
                unit.type->canFly ? unit.flightVelocity.x : step.s.v,
                unit.type->canFly ? unit.flightVelocity.z : step.c.v,
                maximum.v,refused,unit.embarked()));
        }
        case 33: {
            const auto multiplier=unit.groundTerrainFlags&0x800 ? unit.type->roadMult :
                unit.groundTerrainFlags&0x1000 ? unit.type->waterMult : Fixed::fromInt(1);
            return uint32_t(retailTurnAnimationPercent(unit.animationTurnBam,
                uint16_t(unit.type->turnRate),uint16_t(unit.type->turnInPlaceRate),
                multiplier.v,unit.embarked()));
        }
        case 32: return uint32_t(unit.veteran);
        case 46: return unit.standingOrder;
        default: return 0;
        }
    }
    void set(int id,int value) {
        switch (id) {
        case 5: factory.ready=(value&1)!=0; unit.missionEvents|=4; break; // 50d49b
        case 18: {
            // 507ae0 refuses a yard transition while another body occupies
            // a cell that must be blocked in the requested state.
            const auto& type=*unit.type;
            unit.missionEvents|=4;
            const int x0=footprintOrigin(unit.x,type.footX),z0=footprintOrigin(unit.z,type.footZ);
            if (x0<1 || z0<1 || x0+type.footX>=world.occW_ || z0+type.footZ>=world.occH_) break;
            bool occupied=false;
            for (int z=0;z<type.footZ && !occupied;++z)
                for (int x=0;x<type.footX;++x) {
                    const char cell=type.yardMap.empty() ? '.' : type.yardMap[size_t(z)*type.footX+x];
                    const bool blocks=cell=='o' || cell=='f' || cell=='w' || cell=='S' ||
                        (value ? cell=='O' : cell=='c' || cell=='C');
                    if (!blocks) continue;
                    const int owner=world.occ_[size_t(z0+z)*world.occW_+x0+x];
                    if (owner && owner!=unit.id) { occupied=true;break; }
                }
            if (!occupied) {
                factory.yardOpen=(value&1)!=0;
                // 507c70 restamps the yard, then 4e1e20 refreshes every
                // movement-class cache, including its clearance border.
                for (auto& plane:world.searchGrades_)
                    world.refreshSearchRect(plane,x0,z0,type.footX,type.footZ);
            }
            break;
        }
        case 19: factory.buggerOff=value!=0; break;
        case 21: case 22: case 23:
            if(value>=0 && size_t(value)<unit.weaponAim.size()) {
                unit.weaponAim[size_t(value)].set(id);
                unit.missionEvents|=4; // 50d450: every weapon SET wakes the owner
            }
            break;
        default: break;
        }
    }
    void effect(uint32_t opcode,int piece,int32_t code) {
        const bool pointEffect=code>=2 && code<=5;
        if(opcode!=0x1000f000 || (!pointEffect && !(uint32_t(code)&0x100u)) ||
           piece<0 || size_t(piece)>=factory.state.pieces.size())return;
        if(pointEffect) {
            const auto& model=unit.type->productionModel;
            int index=-1;
            for(size_t i=0;i<model.size();++i)
                if(model[i].scriptPiece==piece) {index=int(i);break;}
            if(index<0 || model[size_t(index)].emissionVertexCount<2)return;
            World::ScriptEmission event;
            event.tick=world.tickCounter_;event.unitId=unit.id;event.player=unit.player;
            event.piece=piece;event.code=code;
            event.position={unit.x.v,(unit.type->canFly ? unit.flightY : unit.groundY).v,unit.z.v};
            event.heading=portHeadingToRetail(unit.heading);
            event.pitch=unit.groundPitch;event.roll=unit.groundRoll;
            event.vertices=model[size_t(index)].emissionVertices;
            for(;index>=0;index=model[size_t(index)].parent) {
                const auto& node=model[size_t(index)];
                cob::EmissionPose pose;pose.offset=node.offset;
                if(node.scriptPiece>=0 && size_t(node.scriptPiece)<factory.state.pieces.size()) {
                    const auto& state=factory.state.pieces[size_t(node.scriptPiece)];
                    for(size_t axis=0;axis<3;++axis) {
                        pose.move[axis]=state.move[axis];pose.turn[axis]=uint16_t(state.turn[axis]);
                    }
                }
                event.pose.push_back(pose);
            }
            std::reverse(event.pose.begin(),event.pose.end());
            world.scriptEmissions_.push_back(std::move(event));
            return;
        }
        const auto offset=retailPieceOrigin(unit.type->productionModel,factory.state.pieces,piece,
            portHeadingToRetail(unit.heading),unit.groundPitch,unit.groundRoll);
        std::array<int32_t,3> position={unit.x.v,
            (unit.type->canFly ? unit.flightY : unit.groundY).v,unit.z.v};
        for(size_t axis=0;axis<3;++axis)
            position[axis]=std::bit_cast<int32_t>(uint32_t(position[axis])+uint32_t(offset[axis]));
        world.scriptEmissions_.push_back({world.tickCounter_,unit.id,unit.player,piece,code,position});
    }
    uint32_t sound(int,int32_t) { return 0; } // audio remains client-owned
};

void World::tickStraightProjectiles(std::span<const int> airGrid) {
    for(auto& p:projectiles_) {
        if(!p.straight || p.life<=0)continue;
        const auto& w=*p.wsrc;
        if(w.lightning) {
            const auto* owner=unit(p.fromId);
            if(!owner || !owner->alive() || owner->hp<=Fixed()) {p.life=0;continue;}
            p.muzzle=queryUnitScriptPoint(p.fromId,false,p.slot);
        }
        if(tickCounter_<=p.start) {
            const auto* owner=unit(p.fromId);
            if(!owner || !owner->alive() || owner->hp<=Fixed()) {p.life=0;continue;}
            if(tickCounter_==p.start)
                p.angles[1]=uint16_t(p.angles[1]+portHeadingToRetail(owner->heading));
            continue;
        }
        const bool emitting=!p.spent && tickCounter_<p.end;
        bool effectAlive=false;
        if(p.lightningEffect)
            effectAlive=p.lightningEffect->tick(emitting,[&]{return crtRand(0x4f3e3f);});
        p.age=int32_t(tickCounter_-p.start);
        if(!emitting) {if(!effectAlive)p.life=0;continue;}
        const uint32_t flags=(w.unitsOnly?0x800u:0u)|(w.groundBounce?0x1000u:0u)|(w.waterWeapon?0x2000u:0u);
        for(uint32_t step=0;step<p.substeps;++step) {
            for(unsigned axis=0;axis<3;++axis)p.angles[axis]=uint16_t(p.angles[axis]+w.shotSpin[axis]);
            for(unsigned axis=0;axis<3;++axis)p.position[axis]=std::bit_cast<int32_t>(
                uint32_t(p.position[axis])+uint32_t(p.velocity[axis]));
            const auto hit=projectileCollision(p.position,p.velocity[1],flags,p.fromPlayer,airGrid);
            if(hit.code==2) {
                applyHit(w,Fixed::raw(p.position[0]).toFloat(),Fixed::raw(p.position[2]).toFloat(),
                    p.fromPlayer,p.fromId,unit(hit.unitId));
                p.spent=true;break;
            }
        }
        p.x=Fixed::raw(p.position[0]);p.z=Fixed::raw(p.position[2]);
        p.age=int32_t(tickCounter_-p.start);
    }
}

void World::tickFlames(std::span<const int> airGrid) {
    for(auto& flame:flames_) {
        const auto& w=*flame.weapon;
        auto* source=unit(flame.fromId);
        const bool alive=source && source->alive() && source->hp>Fixed();
        auto advanceParticles=[&] {
            std::erase_if(flame.particles,[](auto& particle){return !particle.tick();});
        };
        if(!alive) {
            advanceParticles();flame.expired=flame.particles.empty();continue;
        }
        if(tickCounter_<flame.start)continue;
        const uint32_t flags=(w.unitsOnly?0x800u:0u)|(w.groundBounce?0x1000u:0u)|(w.waterWeapon?0x2000u:0u);
        auto collision=[&](const auto& position) {
            return projectileCollision(position,flame.velocity[1],flags,flame.owner,airGrid);
        };
        if(tickCounter_==flame.start) {
            const auto scan=tak::retailFlameScan(flame.position,flame.velocity,uint32_t(w.range),
                flame.speed,flame.substeps,[&](const auto& point){return collision(point).code;});
            flame.endpoint=scan.endpoint;flame.lifetime=scan.lifetime;flame.impacted=false;
            continue;
        }
        if(tickCounter_>=flame.end) {
            advanceParticles();flame.expired=flame.particles.empty();continue;
        }
        const auto muzzle=queryUnitScriptPoint(flame.fromId,false,flame.slot);
        flame.muzzle=muzzle;
        if(!flame.impacted)for(uint32_t step=0;step<flame.substeps;++step) {
            for(unsigned axis=0;axis<3;++axis)flame.position[axis]=std::bit_cast<int32_t>(
                uint32_t(flame.position[axis])+uint32_t(flame.velocity[axis]));
            const auto hit=collision(flame.position);
            if(hit.code==2) {
                applyHit(w,Fixed::raw(flame.position[0]).toFloat(),Fixed::raw(flame.position[2]).toFloat(),
                    flame.owner,flame.fromId,unit(hit.unitId));
                flame.impacted=true;break;
            }
        }
        const std::array<uint16_t,3> rolls{uint16_t(crtRand(0x52d5d7)),uint16_t(crtRand(0x52d627)),uint16_t(crtRand(0x52d675))};
        const auto velocity=tak::retailFlameVelocity(muzzle,flame.endpoint,int32_t(flame.lifetime),rolls);
        // Native updates the existing emitter before inserting this tick's particle.
        advanceParticles();
        if(flame.particles.size()<500)flame.particles.push_back({muzzle,velocity,int32_t(flame.lifetime),
            int32_t(flame.lifetime),uint32_t(w.flameKind)});
    }
    std::erase_if(flames_,[](const auto& flame){return flame.expired;});
}

std::vector<int> World::projectileAirGrid() {
    std::vector<const Unit*> aircraft;
    for(const auto& u:units_)
        if(u.alive() && u.type && u.type->canFly && !u.embarked())aircraft.push_back(&u);
    if(std::none_of(aircraft.begin(),aircraft.end(),[](const Unit* u){return u->flightGroundMode==2;}))return {};
    std::sort(aircraft.begin(),aircraft.end(),[](const Unit* a,const Unit* b) {
        return a->player!=b->player ? a->player<b->player : a->id<b->id;
    });
    std::vector<RetailAirCollisionBody> bodies;
    bodies.reserve(aircraft.size());
    for(const auto* u:aircraft)bodies.push_back({u->id,footprintOrigin(u->x,u->type->footX),
        footprintOrigin(u->z,u->type->footZ),u->type->footX,u->type->footZ,u->flightGroundMode==2});
    return retailAirCollisionGrid(hW_,hH_,bodies,[&](unsigned n){return gameRand(int32_t(n));});
}

World::ProjectileCollisionResult World::projectileCollision(const std::array<int32_t,3>& point,
        int32_t& verticalSpeed,uint32_t weaponFlags,int owner,std::span<const int> airGrid,
        bool bypassPrimaryGeometry,std::optional<std::array<int32_t,3>> targetShot,
        uint16_t proximityRadius) const {
    const int x=point[0]>>20,z=point[2]>>20;
    if(x<0 || z<0 || x>=hW_ || z>=hH_)return {1,0};
    if(targetShot && retailProjectileProximity(point,*targetShot,proximityRadius))return {2,0};
    const auto primary=searchBodyRect(x,z,1,1);
    const auto* body=primary.cells[0];
    if(body && body->player!=owner && (bypassPrimaryGeometry || projectilePointInUnit(body->id,point)))return {2,body->id};
    const size_t cell=size_t(z)*hW_+x;
    const auto* airborne=cell<airGrid.size() && airGrid[cell]>0 ? unit(airGrid[cell]) : nullptr;
    if(airborne && airborne->alive() && !airborne->embarked() && airborne->type && airborne->player!=owner) {
        // Type +13e is initialized to zero at 4c1419. Secondary occupancy
        // tests altitude only, unlike the primary selection-quad predicate.
        const int32_t base=airborne->flightY.v;
        const int32_t top=std::bit_cast<int32_t>(uint32_t(base)+uint32_t(airborne->type->modelTop));
        if(point[1]>=base && point[1]<=top)return {2,airborne->id};
    }
    return {projectileEnvironment(point,verticalSpeed,weaponFlags),0};
}

int World::projectileEnvironment(const std::array<int32_t,3>& point,int32_t& verticalSpeed,
                                 uint32_t weaponFlags) const {
    // 50e660: signed fixed-point coordinates select 16-unit map cells.
    const int x=point[0]>>20,z=point[2]>>20;
    if(x<0 || z<0 || x>=hW_ || z>=hH_)return 1;
    if(mapPlacementCells_.empty()) {
        const uint8_t minimum=std::min({heights_[size_t(z)*hW_+x],
            heights_[size_t(z)*hW_+std::min(x+1,hW_-1)],
            heights_[size_t(std::min(z+1,hH_-1))*hW_+x],
            heights_[size_t(std::min(z+1,hH_-1))*hW_+std::min(x+1,hW_-1)]});
        return retailProjectileHitsEnvironment(point[1],verticalSpeed,weaponFlags,minimum,
            uint8_t(seaLevel_),{},noSeaLevelTrigger_) ? 2 : 0;
    }
    const auto& cell=mapPlacementCells_[size_t(z)*hW_+x];
    uint16_t feature=cell.feature;
    if(feature==0xfffe) {
        const int ax=x-cell.backX,az=z-cell.backZ;
        feature=ax>=0 && az>=0 ? mapPlacementCells_[size_t(az)*hW_+ax].feature : 0xffff;
    }
    std::optional<uint8_t> height;
    if(feature<0xfffa && feature<mapPlacementTypes_.size())height=mapPlacementTypes_[feature].projectileHeight;
    return retailProjectileHitsEnvironment(point[1],verticalSpeed,weaponFlags,cell.low,
        uint8_t(seaLevel_),height,noSeaLevelTrigger_) ? 2 : 0;
}

bool World::projectilePointInUnit(int unitId,const std::array<int32_t,3>& point) const {
    const auto* target=unit(unitId);
    if(!target || !target->type || !target->type->projectileQuad)return false;
    return retailProjectileInUnit(point,
        {target->x.v,(target->type->canFly ? target->flightY : target->groundY).v,target->z.v},
        target->type->modelTop,portHeadingToRetail(target->heading),*target->type->projectileQuad);
}

std::array<int32_t,3> World::queryUnitScriptPoint(int unitId,bool sweetSpot,int slot) {
    auto* u=unit(unitId);
    if(!u || !u->type)return {};
    std::array<int32_t,3> point{u->x.v,(u->type->canFly ? u->flightY : u->groundY).v,u->z.v};
    const auto it=unitScripts_.find(unitId);
    if(it==unitScripts_.end())return point;
    const auto& file=*u->type->script();
    std::array<uint32_t,4> args{0,uint32_t(slot),0,0};
    ScriptHost host{*this,*u,it->second};
    it->second.state.query(file,file.scriptIndex(sweetSpot ? "SweetSpot" : "QueryWeapon"),args,host);
    const int32_t piece=std::bit_cast<int32_t>(args[0]);
    if(piece<0 || size_t(piece)>=it->second.state.pieces.size())return point;
    std::array<int32_t,3> offset{};
    if(sweetSpot) {
        if(size_t(piece)>=u->type->scriptPieceCenters.size())return point;
        offset=u->type->scriptPieceCenters[size_t(piece)];
    } else offset=retailPieceOrigin(u->type->productionModel,it->second.state.pieces,piece,
        portHeadingToRetail(u->heading),u->groundPitch,u->groundRoll);
    for(size_t axis=0;axis<3;++axis)
        point[axis]=std::bit_cast<int32_t>(uint32_t(point[axis])+uint32_t(offset[axis]));
    return point;
}

std::optional<World::WeaponAimSolution> World::queryWeaponAim(int unitId,int targetId,int slot) {
    auto* u=unit(unitId);auto* target=unit(targetId);
    if(!u || !target || !u->type || !target->type || slot<0 || size_t(slot)>=u->type->weapons.size())return {};
    if(!target->alive() || (target->hp<=Fixed() && !(target->retailSite && target->underConstruction)))return {};
    const auto& weapon=u->type->weapons[size_t(slot)];
    const std::array<int32_t,3> origin{u->x.v,(u->type->canFly ? u->flightY : u->groundY).v,u->z.v};
    WeaponAimSolution result;
    result.target={target->x.v,(target->type->canFly ? target->flightY : target->groundY).v,target->z.v};
    const auto heading=portHeadingToRetail(u->heading);
    result.heading=uint16_t(retailDirection(target->x-u->x,target->z-u->z).v-heading);
    if(weapon.ballistic || weapon.beam || weapon.kind==Weapon::Kind::Guided) {
        const auto source=queryUnitScriptPoint(unitId,false,slot);
        const auto sweetSpot=queryUnitScriptPoint(targetId,true);
        const auto step=retailGroundStep(target->heading,target->speed);
        const std::array<int32_t,3> velocity=target->type->canFly ?
            std::array<int32_t,3>{target->flightVelocity.x,target->flightVelocity.y,target->flightVelocity.z} :
            std::array<int32_t,3>{step.s.v,0,step.c.v};
        const int32_t speed=tak::retailAimSpeed(weapon.projVel);
        result.target=tak::retailAimLead(origin,sweetSpot,velocity,speed,weapon.noLead || target->type->isStructure());
        std::array<int32_t,3> delta;
        for(size_t axis=0;axis<3;++axis)
            delta[axis]=std::bit_cast<int32_t>(uint32_t(result.target[axis])-uint32_t(source[axis]));
        const auto angles=retailDirectAim(delta[0],delta[1],delta[2],heading);
        result.heading=angles[0];result.pitch=angles[1];
        if(weapon.ballistic) {
            result.pitch=tak::retailBallisticPitch(float(delta[0])/65536.f,float(delta[1])/65536.f,
                float(delta[2])/65536.f,float(speed)/65536.f,weapon.gravityAdj,weapon.lobPreferred);
            if(result.pitch==0x8000)result.pitch=0;
        }
    }
    return result;
}

void World::clearScriptWeaponTarget(Unit& u) {
    const auto it=unitScripts_.find(u.id);
    if(it!=unitScripts_.end()) {
        const auto& file=*u.type->script();ScriptHost host{*this,u,it->second};
        for(size_t slot=0;slot<std::min(size_t(3),u.type->weapons.size());++slot) {
            // 51a7f0 retires all targets, but only the selected callback can
            // acknowledge/reset a switcher's weapon state.
            if(u.type->weaponSwitching && int(slot)!=u.weaponSlot)continue;
            u.weaponAnimations.add(tak::RetailWeaponAnimation::Clear,int(slot));
            if(it->second.state.startArguments(file,file.scriptIndex("TargetCleared"),{uint32_t(slot),0,0,0},1))
                it->second.state.tick(file,0,host);
        }
    }
    u.scriptAimTarget=0;
}

bool World::tickScriptWeapon(Unit& u,Unit& target,int slot) {
    const auto it=unitScripts_.find(u.id);
    if(it==unitScripts_.end() || slot<0 || slot>=3)return false;
    const auto& file=*u.type->script();
    const int aimScript=file.scriptIndex("AimWeapon"),fireScript=file.scriptIndex("FireWeapon");
    // Native callback lookup may fail, but it never substitutes an immediate
    // shot: readiness/release still require the script's SET 22 / SET 23.
    const auto solution=queryWeaponAim(u.id,target.id,slot);
    if(!solution)return true;
    u.scriptAimTarget=target.id;
    u.weaponAimPoints[size_t(slot)]=solution->target;
    auto& aim=u.weaponAim[size_t(slot)];const auto& weapon=u.type->weapons[size_t(slot)];
    ScriptHost host{*this,u,it->second};
    const bool dropped=weapon.kind==Weapon::Kind::Dropped;
    if(!dropped && aim.start(solution->heading,solution->pitch)) {
        u.weaponAnimations.add(tak::RetailWeaponAnimation::Aim,slot,solution->heading,solution->pitch);
        if(it->second.state.startArguments(file,aimScript,{solution->heading,solution->pitch,uint32_t(slot),0},3))
            it->second.state.tick(file,0,host);
    }
    const bool available=u.reloads[slot]==0 &&
        (u.type->maxMana<=0 || u.mana>=weapon.manaCost);
    // Native readiness compares body heading with the mover's requested heading,
    // not the weapon's offset SweetSpot or lead point.
    const auto desired=retailDirection(target.x-u.x,target.z-u.z);
    const bool aligned=!(u.type->canFly || u.type->turnInPlaceRate>0) ||
        std::abs(bamDiff(desired,u.heading))<=std::max(512,int(uint16_t(weapon.aimTol)));
    if(dropped ? available : aim.ready(solution->heading,solution->pitch,uint16_t(weapon.aimTol),available,aligned)) {
        const uint16_t nominal=uint16_t(int32_t(weapon.reload*kTick+0.5f));
        u.reloads[slot]=retailWeaponReload(nominal,uint16_t(crtRand(0x530167)));
        const bool all=!u.type->weaponSwitching && u.type->weapons.size()>1;
        u.missionEvents|=retailWeaponFireEvent(0,0x10000,all ? 0xc0000000u : uint32_t(slot)<<30,aim.flags);
        u.fireAnimations|=uint32_t(1)<<slot;
        u.weaponAnimations.add(tak::RetailWeaponAnimation::Fire,slot);
        if(it->second.state.startArguments(file,fireScript,{uint32_t(slot),0,0,0},1)) {
            it->second.state.tick(file,0,host);
        }
    }
    if(aim.flags&16) {
        fire(u,target,slot,true);
        aim.projectileCreated();
    }
    return true;
}

void World::notifyUnitScript(Unit& u,const char* name) {
    if (std::strcmp(name,"BeginFlight")==0) ++u.flightBeginCallbackSerial;
    auto it=unitScripts_.find(u.id);
    if (it==unitScripts_.end()) return;
    auto& state=it->second;
    const auto& file=*u.type->script();
    ScriptHost host{*this,u,state};
    state.state.notify(file,file.scriptIndex(name),host);
}

void World::notifyFlightOccupancy(Unit& u) {
    if (u.retailBuild && u.retailBuild->flying) {
        auto& job=*u.retailBuild;
        uint32_t rate=0;
        if (u.speed.v || u.turnReqBam) {
            const auto& v=u.flightVelocity;
            const int32_t horizontal=int32_t(std::sqrt(double(v.x)*v.x+double(v.z)*v.z));
            rate=horizontal<=job.flyingSlowSpeed ? 1u : 2u+uint32_t(horizontal>job.flyingFastSpeed);
        }
        if (((job.flyingOwnerFlags>>2)&3u)!=rate) {
            const auto it=unitScripts_.find(u.id);
            if (it!=unitScripts_.end()) {
                auto& script=it->second;const auto& file=*u.type->script();
                ScriptHost host{*this,u,script};
                if (script.state.startArguments(file,file.scriptIndex("MoveRate"),{rate,0,0,0},1))
                    script.state.tick(file,0,host);
            }
            job.flyingOwnerFlags=(job.flyingOwnerFlags&~0xcu)|(rate<<2);
        }
    }
    // 4dc600 reports airborne mode after the mover, with an immediate
    // zero-elapsed script pass. Landed/water transitions need their own host.
    if (u.flightGroundMode!=2) return;
    constexpr uint32_t occupancy=5;
    if (occupancy==u.scriptOccupancy) return;
    const auto it=unitScripts_.find(u.id);
    if (it!=unitScripts_.end()) {
        auto& script=it->second;const auto& file=*u.type->script();
        ScriptHost host{*this,u,script};
        if (script.state.startArguments(file,file.scriptIndex("setSFXoccupy"),{occupancy,0,0,0},1))
            script.state.tick(file,0,host);
    }
    u.scriptOccupancy=occupancy;
}

void World::tickUnitScript(Unit& u) {
    auto* script=size_t(u.id)<unitScriptById_.size() ? unitScriptById_[size_t(u.id)] : nullptr;
    if (!script) return;
    if (!u.alive() || (u.hp<=Fixed() && !(u.retailSite && u.underConstruction))) {
        unitScriptById_[size_t(u.id)]=nullptr;
        unitScripts_.erase(u.id);return;
    }
    auto& factory=*script;
    const auto& file=*u.type->script();
    ScriptHost host{*this,u,factory};
    factory.state.tick(file,1,host);
    const bool activated=!u.underConstruction && !u.buildQueue.empty();
    if (u.type->productionScript && activated!=factory.activated) {
        factory.activated=activated;
        factory.state.notify(file,file.scriptIndex(activated ? "Activate" : "Deactivate"),host);
    }
}

void World::initializeRetailSite(Unit& site,int builderId) {
    const auto& owner=players_[size_t(site.player)];
    if (!owner.buildCache) throw std::runtime_error("new construction catalogue absent");
    const auto& entries=owner.buildCache->entries;
    const auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.type==site.type;});
    if (entry==entries.end() || !entry->construction)
        throw std::runtime_error("new construction type absent");
    site.retailSite=RetailConstructionSite{{1,0,0,0x1000000u},entry->construction->type,{},0};
    site.retailSite->mission.emplace();site.retailSite->builder=builderId;
    site.hp=Fixed();site.routeStamp=0;
    // 511ed3 executes Create at zero elapsed time while work remaining is 1.
    if (auto script=unitScripts_.find(site.id);script!=unitScripts_.end()) {
        ScriptHost host{*this,site,script->second};
        script->second.state.tick(*site.type->script(),0,host);
    }
}

void World::tickProduction(Unit& u, float dt) {
    (void)dt;
    if (u.underConstruction || u.incapacitated() || u.hp <= Fixed() || u.buildQueue.empty()) return;
    const UnitType* t = u.buildQueue.front();
    const int producerId=u.id, player=u.player;
    const int32_t total=std::max(1,int32_t(t->buildTime/std::max(u.type->workerTime,0.01f)*kTick+0.5f));
    Unit* site=u.productionSiteId ? unit(u.productionSiteId) : nullptr;
    if (u.productionSiteId && (!site || !site->alive() || (site->hp<=Fixed() && !site->retailSite) || site->player!=player)) {
        u.productionSiteId=0; u.buildProgress=0;
        return;
    }
    if (!site) {
        if (auto it=unitScripts_.find(u.id);u.type->productionScript && it!=unitScripts_.end() && !it->second.ready) return;
        if (atUnitCap(player) || atTypeCap(player,t)) return;
        Fixed spawnX,spawnZ,spawnY;
        if (auto it=unitScripts_.find(u.id);u.type->productionScript && it!=unitScripts_.end()) {
            const auto& file=*u.type->productionScript;
            std::array<uint32_t,4> args{0xffffffffu,0,0,0};
            ScriptHost host{*this,u,it->second};
            if (!it->second.state.query(file,file.scriptIndex("QueryBuildInfo"),args,host)) return;
            const auto offset=retailPieceOrigin(u.type->productionModel,it->second.state.pieces,
                                               std::bit_cast<int32_t>(args[0]),portHeadingToRetail(u.heading));
            spawnX=u.x+Fixed::raw(offset[0]); spawnZ=u.z+Fixed::raw(offset[2]);
            const int cx=std::clamp(u.x.floorInt()/16,0,std::max(0,terW_-1));
            const int cz=std::clamp(u.z.floorInt()/16,0,std::max(0,terH_-1));
            spawnY=Fixed::fromInt(heights_.empty() ? 0 : heights_[size_t(cz)*terW_+cx])+Fixed::raw(offset[1]);
            if (!canPlace(t,spawnX.toFloat(),spawnZ.toFloat())) return;
        } else {
            const float ex=u.x.toFloat(),ez=u.z.toFloat()+float(u.type->footZ)*8+20;
            float sx=ex,sz=ez;
            if (!exitSpot(t,ex,ez,sx,sz)) return;
            spawnX=Fixed::fromFloat(sx); spawnZ=Fixed::fromFloat(sz);
        }
        // Preserve the raw script position across the public float spawn API.
        const int siteId=spawn(t,spawnX.toFloat(),spawnZ.toFloat(),std::nullopt,player);
        site=unit(siteId);
        site->x=site->homeX=spawnX; site->z=site->homeZ=spawnZ; site->flightY=spawnY; updateBodyIndex(*site);
        site->flightSectorX=spawnX.v>>23;site->flightSectorZ=spawnZ.v>>23;
        site->underConstruction=site->buildBegun=true;
        site->hp=Fixed::fromFloat(t->maxHp*0.05f);
        if (players_[size_t(player)].retailResources) initializeRetailSite(*site,producerId);
        unit(producerId)->productionSiteId=siteId;
        if (auto it=unitScripts_.find(producerId);it!=unitScripts_.end()) {
            const auto& file=*unit(producerId)->type->script();
            it->second.state.startArguments(file,file.scriptIndex("StartBuilding"),{},2);
        }
    }
    // spawn can reallocate units_; never use the original reference after it.
    Unit* producer=unit(producerId);
    site->beingBuilt=true;
    // Mobile conjurers use this queue too. Face the actual output position,
    // which exitSpot may have displaced, just as for a placed build site.
    if (!producer->type->isStructure() && producer->orders.empty()) {
        if (producer->type->canFly) tickConjureHover(*producer,*site);
        else turnBuilderToSite(*producer,*site);
    }
    const Fixed work=Fixed::fromFloat(t->maxHp*0.95f/float(total));
    site->conjureRate=work;
    Player& tm=players_[size_t(player)];
    if (site->retailSite && tm.retailResources) {
        float worker=producer->type->workerTime;
        if (tm.buildCache) for (const auto& entry:tm.buildCache->entries)
            if (entry.type==producer->type && entry.construction) worker=entry.construction->workerTime;
        auto& s=*site->retailSite;
        s.progress.events=site->missionEvents;
        const auto result=retailConstructionWork(s.progress,s.type,*tm.retailResources,float(double(worker)*0.03333333));
        tm.mana=tm.retailResources->stored;site->hp=Fixed::fromInt(s.progress.hp);
        site->missionEvents|=s.progress.events;
        if (result==RetailConstructionResult::Completed) completeRetailConstruction(*producer,*site);
        if (result==RetailConstructionResult::Worked || result==RetailConstructionResult::Completed)
            emitConstruction(*site,true);
        if (result==RetailConstructionResult::Completed) {
            notifyUnitScript(*producer,"StopBuilding");
            producer->productionSiteId=0;producer->buildProgress=0;
            producer->buildQueue.erase(producer->buildQueue.begin());
            if (producer->buildQueue.empty()) {
                if (auto it=unitScripts_.find(producerId);it!=unitScripts_.end()) it->second.activated=false;
                notifyUnitScript(*producer,"Deactivate");
            }
        }
        return;
    }
    if (gInstantBuild) {
        producer->buildProgress=total;
        site->hp=Fixed::fromFloat(t->maxHp);
        emitConstruction(*site,true);
    } else {
        const double cost=double(t->buildCost)/double(total);
        if (tm.mana<cost) return;
        tm.debitMana(cost);
        ++producer->buildProgress;
        site->hp=fxMin(Fixed::fromFloat(t->maxHp),site->hp+work);
        emitConstruction(*site,true);
        if (producer->buildProgress<total) return;
        // Correct accumulated fixed-point rounding without undoing combat damage.
        const Fixed rounding=Fixed::fromFloat(t->maxHp)-
            (Fixed::fromFloat(t->maxHp*0.05f)+Fixed::raw(int32_t(int64_t(work.v)*total)));
        site->hp=fxMin(Fixed::fromFloat(t->maxHp),site->hp+rounding);
    }
    const int id=site->id;
    const float sx=site->x.toFloat(),sz=site->z.toFloat();
    site->underConstruction=false;
    producer->productionSiteId=0;
    producer->buildProgress=0;
    producer->buildQueue.erase(producer->buildQueue.begin());
    if (auto it=unitScripts_.find(producerId);it!=unitScripts_.end()) {
        const auto& file=*producer->type->script();
        it->second.state.vm.start(file,file.scriptIndex("StopBuilding"));
    }
    // Then walk it clear of the exit, to a spot that is free RIGHT NOW -- computed
    // after the spawn, so it also avoids the unit we just put down.
    //
    // This used to be `order(id, sx + ((id % 5) - 2) * 22, sz + 60, false)`: a fan of
    // five fixed points keyed on the unit id. With five lanes and no memory of what is
    // already standing there, the sixth unit out of a factory was ordered onto the
    // exact spot the first was still occupying. Bodies are solid and the steering has
    // no local avoidance (that is the router's job, and the router cannot route INTO
    // an occupied cell either), so it pushed at its own countryman for ever. The
    // no-headway watchdog does not save it: replaceLeg resets the tracker and the
    // router re-issues about once a second, so the tracker can never build up (see the
    // KNOWN LIMITATION note on replaceLeg). Reported as "built units always try to go
    // to the same spot, forever", which is exactly what it did.
    //
    // Deterministic: a fixed outward ring scan over the nav grid and the unit grid,
    // both of which every peer builds identically.
    float gx = sx, gz = sz + 60.0f;
    if (exitSpot(t, gx, gz, gx, gz)) order(id, gx, gz, false);
    // ...then hand it the factory's RALLY plan, queued behind that step. A production
    // building accepts move / fight-move / patrol / attack orders (and queues them) --
    // it cannot act on them itself, so they describe what its OUTPUT should do. The
    // step above still runs first so the unit clears the doorway before setting off.
    if (const Unit* pb = unit(producerId))
        for (const Order& ro : pb->rally) {
            Unit* nu = unit(id);
            if (!nu) break;
            // A targeted rally (attack) whose target is already dead is skipped rather
            // than handed over as an order pointing at nothing.
            if (ro.targetId) {
                const Unit* tg = unit(ro.targetId);
                if (!tg || !tg->alive()) continue;
            }
            nu->orders.push_back(ro);
            nu->orders.back().issuedTick = tickCounter_;
        }
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


void World::deliverSearchRoute(int unitId,const std::vector<PathCell>& route,Fixed gxF,Fixed gzF,
                               bool failed,bool crowded,bool traffic,bool detour) {
    const float gx = gxF.toFloat(), gz = gzF.toFloat();
    Unit* u = unit(unitId);
    // 4e4f39: every route delivery, including an empty route, makes
    // the mover recheck nearby obstacles on its next update.
    if (u && u->alive()) u->groundScanTick=0;
    static const bool kPqLog2 = std::getenv("TAK_PATHLOG") != nullptr;
    if (kPqLog2) std::printf("[pq] t=%u id=%d DELIVER wps=%zu failed=%d "
                             "(alive=%d orders=%zu)\n",
                             tickCounter_, unitId, route.size(), int(failed),
                             u && u->alive() ? 1 : 0,
                             u ? u->orders.size() : size_t(0));
    if (route.empty()) {           // failed with NOTHING (start boxed in)
        // Even an empty failure carries the outcome flags, and the ladder
        // governs its retry like any other failed route: traffic evidence
        // earns the 2d8 cadence, none means rest. The fixed 5s backoff
        // that stood here was ours.
        if (u && u->alive()) {
            u->routeCrowded = crowded;
            u->routeTraffic = traffic;
            u->routeFailed = failed;u->routeDetour=detour;
            if (!u->orders.empty()) u->orders[currentLeg(u->orders)].navigationExhausted=true;
            // 4e4ead..4e4ed4: an empty delivery disables navigation and
            // reports failure only when the owning controller is unsatisfied.
            if (auto* goal=navigationMissionOrder(*u)) {
                const bool satisfied=goal->buildRectangle
                    ? goal->buildRectangle->accepts(footprintOrigin(u->x,u->type->footX),footprintOrigin(u->z,u->type->footZ))
                    : groundMissionAccepts(*u,*goal);
                if (!satisfied) (goal->load || goal->transportUnloadApproach
                    ? goal->transportMission : goal->mission).pending|=0x200;
            }
        }
        return;
    }
    if (!u || !u->alive() || u->orders.empty()) return;
    // The mission owns the requested point; the navigator may end at
    // a partial-route boundary. Appending the requested point changes
    // corner pruning before the unit even reaches that boundary.
    auto& mission=u->orders[currentLeg(u->orders)];
    if (mission.groundMission && !mission.missionTarget)
        mission.missionTarget=std::pair{mission.x,mission.z};
    // Only the leg this search was issued for; anything queued behind
    // it stays untouched.
    // Snap the final waypoint to the caller's exact goal ONLY when the
    // route actually reached the goal cell. A route clipped at the
    // 64-waypoint limit stops short, and pointing its last waypoint at
    // the distant goal sends the unit charging straight at whatever lies
    // between -- it gets blocked, re-asks, gets another clipped route,
    // and ping-pongs. Measured: a unit oscillating between two points
    // 2426px of travel later, having never arrived.
    // Whether the RAW trace reached the goal cell. Note this is not the
    // question the order-building loop below needs -- see reachedGoal.
    const bool traceReachedGoal =
        !route.empty() && route.back().x == footprintCell(gxF,u->type->footX) &&
        route.back().z == footprintCell(gzF,u->type->footZ);
    // NO STRING-PULLING. The shortcut pass that stood here (lineOpen +
    // segmentFits, farthest-reachable-waypoint) was ours, and it was also the
    // second scorer: the search accepted a route and this pass re-judged it by
    // different rules, and when the two disagreed the route was thrown away
    // and the unit steered straight at its order -- into the very body the
    // route had detoured around. Retail has exactly one judge: the tracer's
    // reconstruction (0x414450) emits a waypoint at each change of direction,
    // the navigator takes up to 64 of them as-is (0x4e4ea0), and nothing
    // re-validates them. The cost is retail's own gait: a unit walks the
    // cardinal/diagonal staircase of its trace, corner by corner -- the
    // characteristic TA:K zigzag -- instead of a straightened line.
    // RETAIL'S 64-WAYPOINT NAVIGATOR CAP (0x4e4ea0). The reconstruction can
    // hand back more corners than that -- the tracer hugging a long outline
    // produces hundreds -- and the string-pull used to hide it by collapsing
    // them. Installed raw and uncapped, a 94-corner eastward hug of the
    // island in Inner Circle walked a unit 500px the WRONG WAY along its
    // whole length. Retail installs the first 64 and re-anchors: the clip
    // ends the leg early, the appended goal and the re-ask pick up from
    // CLOSER, and each re-trace shortens what remains of the wander.
    std::vector<PathCell> capped;
    const bool clipped = route.size() > 64;
    if (clipped) capped.assign(route.begin(), route.begin() + 64);
    const std::vector<PathCell>& useRoute = clipped ? capped : route;
    const bool reachedGoal = traceReachedGoal && !failed && !clipped;
    std::vector<Order> path;
    path.reserve(useRoute.size());
    // Retail's navigator steers along points [0,1] and tests arrival
    // at point 1 (4e5150). Point 0 is the segment origin, not a
    // destination to walk back toward after a replan.
    const size_t firstTarget=useRoute.size()>1 ? 1 : 0;
    for (size_t i = firstTarget; i < useRoute.size(); ++i) {
        Order o;
        // The waypoint anchor depends on footprint parity, just like
        // the movement and occupancy cell conversion.
        o.x = footprintWaypoint(useRoute[i].x,u->type->footX);
        o.z = footprintWaypoint(useRoute[i].z,u->type->footZ);
        if (i==firstTarget && i>0) {
            o.segmentX=footprintWaypoint(useRoute[0].x,u->type->footX);
            o.segmentZ=footprintWaypoint(useRoute[0].z,u->type->footZ);
            o.hasSegment=true;
        }
        if (i + 1 == useRoute.size() && reachedGoal &&
            !u->orders[currentLeg(u->orders)].missionTarget &&
            !u->orders[currentLeg(u->orders)].buildRectangle) {
            o.x = Fixed::fromFloat(gx); o.z = Fixed::fromFloat(gz);
        }
        path.push_back(o);
    }
    // A route clipped at 64 waypoints stops short of where the player
    // actually sent the unit, and replaceLeg marks the LAST waypoint as
    // the leg's goal -- so installing a clipped route silently threw the
    // real destination away. The unit then walked to the end of each
    // clipped route, asked for another, and shuffled between them for
    // ever: measured 2426px of travel in 60s, never arriving, and the
    // no-headway watchdog never fired because from its point of view the
    // unit kept reaching its goal. Keep the destination on the end.
    if (!reachedGoal && !u->orders[currentLeg(u->orders)].buildRectangle &&
        !u->orders[currentLeg(u->orders)].groundMission &&
        !u->orders[currentLeg(u->orders)].load) {
        // Clipped OR failed, the ordered point stays on the end. For a clip
        // (64-waypoint cap) that is how the destination survives; for a
        // FAILURE it is how the ORDER survives: the best-effort route walks
        // the unit to its closest approach (0x415170), the appended leg has
        // it press there under the refusal caps, and the failed-route
        // deadline (rand(8)+rand(8)+30 ticks x scale, 0x4e535d) re-asks from
        // the crowd's edge -- each re-ask reaching closer as the pack
        // tightens. Truncating the leg at the closest approach instead
        // completed the ORDER there, the re-ask had nothing to work on, and
        // a converging army froze as a loose ring around its first ring of
        // arrivals. Permanent rest is the terrain path's job (pathExists
        // false -> dropLeg), not the crowd path's.
        Order last;
        last.x = Fixed::fromFloat(gx);
        last.z = Fixed::fromFloat(gz);
        path.push_back(last);
    }
    replaceLeg(*u, path);
    // Retain the admission stamp and carry the search's outcome; the per-frame
    // cadence ladder in the mover does the rest (retail rolls fresh dice
    // each frame against the elapsed time -- see the ladder for the table).
    u->routeCrowded = crowded;
    u->routeTraffic = traffic;
    u->routeFailed = failed;u->routeDetour=detour;
}

void World::tickNavigationMovement(Unit& u,Fixed maximum) {
    // Queued mobile production already advanced its hover controller this tick.
    if (u.type->canFly && u.orders.empty() && u.productionSiteId &&
        u.conjureHoverTarget==u.productionSiteId) return;
    if (u.orders.empty()) {
        if (u.type->canFly) { u.speed=fxMax(Fixed(),u.speed-u.type->brake);u.flightVelocity={}; }
        else if (!u.type->isStructure()) brakeGround(u);
    } else if (u.orders.front().wait > 0) {
        // SetMission "w N": hold position while the scripted wait counts down.
        if (u.type->canFly) u.speed=fxMax(Fixed(),u.speed-u.type->brake);
        else if (!u.type->isStructure()) brakeGround(u);
        --u.orders.front().wait;
        if (u.orders.front().wait <= 0.0f) u.orders.erase(u.orders.begin());
    } else if (u.orders.front().waitAttack) {
        // SetMission "wa": ambush -- hold until a non-allied unit is within sight,
        // then release to the next order (usually an attack).
        if (u.type->canFly) u.speed=fxMax(Fixed(),u.speed-u.type->brake);
        else if (!u.type->isStructure()) brakeGround(u);
        float sight = u.type->sight > 0 ? u.type->sight : 200.0f;
        // THROUGH THE SPATIAL GRID. This scanned every unit in the world, for every
        // ambushing unit, every tick -- O(n^2) in the number of ambushers, and a
        // mission that sets "wa" on a large force pays it on the referee as well as
        // on every client. The grid holds ALL alive units, buildings included
        // (rebuildGrid), so this is the same set, merely visited by locality; the
        // exact distance test below is unchanged and the answer is a bool, so
        // iteration order cannot affect it.
        bool threat = false;
        forEachNear(u.x.toFloat(), u.z.toFloat(), sight, [&](int idx) {
            if (threat) return;
            const Unit& e = units_[size_t(idx)];
            if (!e.alive() || e.embarked() || !e.type || allied(e.player, u.player)) return;
            const float dx = (e.x - u.x).toFloat(), dz = (e.z - u.z).toFloat();
            if (dx * dx + dz * dz <= sight * sight) threat = true;
        });
        if (threat) u.orders.erase(u.orders.begin());
    } else {
        // Goal satisfaction belongs to the mission, independently of the
        // navigator's next intermediate point (retail 0x4e5150). A stale
        // waypoint can be occupied even after we have reached the goal area.
        const Order legGoal = u.orders[currentLeg(u.orders)];
        const bool rectangleReached = legGoal.buildRectangle && legGoal.buildRectangle->accepts(
            footprintOrigin(u.x,u.type->footX), footprintOrigin(u.z,u.type->footZ));
        if (u.type->canFly) {
            if (legGoal.buildRectangle && legGoal.flightGoal &&
                legGoal.flightGoal->accepts({u.x.v,u.flightY.v,u.z.v})) {
                u.orders[currentLeg(u.orders)].mission.pending |= 0x100;
                u.speed=fxMax(Fixed(),u.speed-u.type->brake);
                return;
            }
            tickFlightMovement(u); notifyFlightOccupancy(u); return;
        }
        Order* completed = nullptr;
        if (rectangleReached && !(legGoal.mission.pending & 0x100))
            completed = &u.orders[currentLeg(u.orders)];
        else if (auto* goal = navigationMissionOrder(u);goal && !goal->buildRectangle) {
            if (goal->controller && groundMissionAccepts(u,*goal)) completed = goal;
        }
        if (completed) {
            // 4e5168..4e5186 detaches satisfied circle, ring and rectangle
            // controllers. The mover then brakes on its retained final point
            // while the transport handler finishes the transfer.
            cancelPath(u);
            (completed->load || completed->transportUnloadApproach
                ? completed->transportMission : completed->mission).pending |= 0x500;
            completed->controller=0;
            completed->navigationExhausted=true;
            if (uint32_t(u.routeStamp)<=tickCounter_-6u) u.routeStamp=0;
        }
        const Fixed goalRadius = Fixed::fromInt(std::max(16, std::max(u.type->footX, u.type->footZ) * 8));
        if (!groundMissionOrder(u) && legGoal.goal && !legGoal.targetId && !legGoal.buildType &&
            !legGoal.reclaimFeat && !legGoal.repairTarget && !legGoal.load && !legGoal.unload &&
            fxLen(legGoal.x - u.x, legGoal.z - u.z) < goalRadius) {
            cancelPath(u);
            dropLeg(u);
            u.routeStamp = -1;
            if (legGoal.patrol) u.orders.push_back(legGoal);
            return;
        }
        // 0x4e5192..0x4e51c0 advances one navigator point using integer
        // world coordinates and a five-pixel circle, before steering.
        // Continue moving this tick with the next segment.
        if (!u.orders[currentLeg(u.orders)].navigationConsumed) {
            const int64_t px = u.x.floorInt() - u.orders.front().x.floorInt();
            const int64_t pz = u.z.floorInt() - u.orders.front().z.floorInt();
            if (px * px + pz * pz <= 25) {
                if (!u.orders.front().goal && u.orders.size()>1) u.orders.erase(u.orders.begin());
                else {
                    auto& goal=u.orders[currentLeg(u.orders)];
                    goal.navigationExhausted=goal.navigationConsumed=true;
                }
            }
        }
        const bool routeable = !u.orders.empty() &&
            !u.orders.front().unload &&
            ((!u.orders[currentLeg(u.orders)].load && !u.orders[currentLeg(u.orders)].groundMission && !u.orders[currentLeg(u.orders)].buildRectangle) ||
             u.orders[currentLeg(u.orders)].controller);
        if (u.routeStamp < 0 && routeable && !paths_.pending(u.id)) {
            const Order& legEnd = u.orders[currentLeg(u.orders)];
            if (!requestPath(u, legEnd.x.toFloat(), legEnd.z.toFloat()))
                u.routeStamp = int32_t(tickCounter_);   // near-goal: hand the leg
                                                        // to the ladder's timer
        }
        if (u.routeStamp >= 0 && routeable) {
            const int32_t elapsed = int32_t(tickCounter_) - u.routeStamp;
            const uint32_t sc = uint32_t(u.type->halfCellTicks);
            const bool water = u.type->floater && u.type->minWaterDepth > 0;
            const bool constrained = u.groundMovementMode!=0 || u.groundSpeedMode!=0;
            // 0x4e51d4 bypasses the random cadence on repeated
            // refusal. 0x4e4f50 leaves an already-pending request alone.
            bool fire = u.bodyBlockStreak >= 2 || u.orders[currentLeg(u.orders)].navigationConsumed;
            if (fire) {
                fire = !paths_.pending(u.id);
            } else if (u.routeCrowded) {
                const uint32_t dice =
                    water   ? pathRand(10) + pathRand(10) + 10
                  : !constrained ? pathRand(10) + pathRand(10) + 20
                  :            pathRand(8) + pathRand(8) + 10;
                fire = elapsed >= int32_t(dice * sc);
            } else if (u.routeTraffic) {
                const uint32_t dice =
                    water   ? pathRand(8) + pathRand(8) + 30
                  : !constrained ? pathRand(8) + pathRand(8) + 60
                  :            pathRand(8) + pathRand(8) + 30;
                fire = elapsed >= int32_t(dice * sc);
            } else {
                // 4e5454..4e548f: partial routes and long detours
                // suppress the plain random retry, including its draw.
                fire = !u.routeFailed && !u.routeDetour && elapsed >= 120 && pathRand(120) == 0;
            }
            if (fire) {
                const Order& legEnd = u.orders[currentLeg(u.orders)];
                const float tx = legEnd.x.toFloat(), tz = legEnd.z.toFloat();
                if (!requestPath(u, tx, tz)) {
                    // requestPath queued nothing (already in the goal cell
                    // or pathfinding disabled). Do NOT leave
                    // routeStamp at -1 -- that latches the ladder OFF for good
                    // (its guard is routeStamp>=0), which is exactly the "stuck
                    // unit never re-asks, keeps pressing the obstacle" bug.
                    // Retail's re-ask runs on a live timer that never latches
                    // pending; restamp so the ladder keeps rolling.
                    u.routeStamp = int32_t(tickCounter_);
                }
            }
        }
        if (u.orders[currentLeg(u.orders)].navigationExhausted) {
            brakeGround(u);
            return;
        }
        // 0x4dc89a / 0x4dba80: a clear eight-neighbor scan can remove
        // a corner before steering. Each removal repeats the scan and only
        // considers points in this leg, never a later queued command.
        if (tickCounter_ >= u.groundScanTick) {
            const bool boat=u.type->floater && u.type->minWaterDepth>0;
            // All eight ground probes (including repeated corner checks) observe
            // the same positions and yards. Snapshot their union once instead of
            // scanning the whole army for every neighboring footprint. Boats also
            // probe farther ahead, so retain their unrestricted query path.
            const auto scanBodies=boat ? SearchBodyRect{} : searchBodyRect(
                footprintCell(u.x,u.type->footX)-u.type->footX/2-1,
                footprintCell(u.z,u.type->footZ)-u.type->footZ/2-1,
                u.type->footX+2,u.type->footZ+2);
            const int viewer=restoredNavigationViewer_>=0?restoredNavigationViewer_:u.player;
            uint16_t scanTurnRate = uint16_t(u.type->turnRate);
            const Fixed multiplier=groundTerrainMultiplier(u);
            scanTurnRate = uint16_t(int64_t(scanTurnRate)*multiplier.v/65536);
            for (;;) {
                const auto scan=retailGroundScan(u.x,u.groundY,u.z,u.heading,tickCounter_,
                    uint8_t(u.type->halfCellTicks),boat,int16_t(u.type->footX),int16_t(u.type->sight),false,
                    [&](Fixed x,Fixed,Fixed z) {
                        return cellScoreWithBodies(u.type,footprintCell(x,u.type->footX),
                                         footprintCell(z,u.type->footZ),u.id,boat?nullptr:&scanBodies);
                    },[&](Fixed x,Fixed y,Fixed z) {
                        return retailBoatScanVisible(x,y,z,hW_/2,hH_/2,uint8_t(viewer),
                            [&](int cx,int cz){return navigationExplored_[size_t(cz)*(hW_/2)+cx];});
                    });
                u.groundScanTick=scan.nextTick;
                u.groundMovementMode=scan.movementMode;
                u.groundSpeedMode=scan.speedMode;
                if (u.groundMovementMode || u.orders.front().goal || u.orders.size()<2) break;
                const Order& corner = u.orders.front();
                const RetailSteeringPoint start = corner.hasSegment
                    ? RetailSteeringPoint{corner.segmentX,corner.segmentZ}
                    : RetailSteeringPoint{Fixed::fromInt(u.x.floorInt()),Fixed::fromInt(u.z.floorInt())};
                if (!retailPruneGroundCorner(u.x,u.z,start,{corner.x,corner.z},
                        {u.orders[1].x,u.orders[1].z},u.heading,scanTurnRate,boat,u.groundSpeedMode)) break;
                u.orders.erase(u.orders.begin());
            }
        }
        Order& o = u.orders.front();
        if (!u.type->canFly && !o.hasSegment) {
            o.segmentX = Fixed::fromInt(u.x.floorInt());
            o.segmentZ = Fixed::fromInt(u.z.floorInt());
            o.hasSegment = true;
        }
        float dx = (o.x - u.x).toFloat(), dz = (o.z - u.z).toFloat();
        float dist = std::sqrt(dx * dx + dz * dz);
        if (o.guard) {
            const Unit* t = unit(o.targetId);
            if (t && fxLen(t->x - u.x, t->z - u.z) <= Fixed::fromInt(70))
                return;   // in escort position, not merely near a waypoint
        }
        // A FINAL move goal completes a little short, so a crowd sharing one
        // destination settles into a blob (spread by separation) instead of every
        // unit fighting for the exact same point. (Described as "flow-field orders"
        // when the arrival radius came from the field; it is the o.goal flag now.)
        // Retail move goals are AREAS -- NavGoalCircle / NavGoalRect / NavGoalRing
        // in the RTTI, not points (docs/retail-engine.md). A FINAL goal therefore
        // completes on a circle wide enough to hold a body: 16px, or the unit's own
        // footprint if that is bigger, so a 4x4 trebuchet is not asked to stand on
        // the same pixel a swordsman would. Intermediate route waypoints keep the tight
        // ground intermediate points are advanced above.
        // (This was `o.flow ? 16 : 3`, which quietly became 3 for EVERY unit once the
        // retail-nav experiment stopped setting o.flow -- a whole group then fought
        // over one pixel, which is exactly what it looked like.)
        float foot = float(std::max(u.type->footX, u.type->footZ)) * 8.0f;
        float arrive = o.goal ? std::max(16.0f, foot) : 3.0f;
        if (!groundMissionOrder(u) && !legGoal.buildRectangle && (u.type->canFly || o.goal) && dist < arrive) {
            if (o.buildType || o.reclaimFeat || o.repairTarget)
                return;   // the job is claimed above; its order blocks the queue
            if (o.targetId == 0 || !o.goal) {
                Order done = o;
                u.orders.erase(u.orders.begin());
                if (done.patrol) u.orders.push_back(done);
            }
            return;
        }
        // Refusal clamps and route scheduling below share the mover's low
        // refusal bits. Do not add a separate unit-id yielding rule.
        const Fixed target=maximum;
        // Navigator repeats the final point; a queued command is not a
        // look-ahead point in the current route.
        const RetailSteeringPoint next=!o.goal && u.orders.size()>1
            ? RetailSteeringPoint{u.orders[1].x,u.orders[1].z}:RetailSteeringPoint{o.x,o.z};
        const auto displacement=steerGround(u,{o.segmentX,o.segmentZ},{o.x,o.z},next,target);
        const Fixed mx=displacement.s,mz=displacement.c;
        commitGroundStep(u,mx,mz);

    }

}

void World::tick(float dt) {
    bodyIndexEnabled_=true;bodyIndexValid_=false;
    struct BodyIndexScope { bool& enabled; ~BodyIndexScope(){enabled=false;} } bodyIndexScope{bodyIndexEnabled_};
    if (retailAllocation_)
        std::sort(units_.begin(),units_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    if (!tickCounter_) updateNavigationExploration();
    ++tickCounter_;
    // 51b890 runs before unit updates. Active squads publish whether they
    // contain any completed mobile members (50b7a0), dirtying only on change.
    for (auto& player:players_) if (player.retailAi) {
        auto& ai=*player.retailAi;
        for (unsigned i=1;i<100;++i) {
            auto& squad=ai.squads[i];
            if (!ai.schedule.groups[i].present || !squad.active) continue;
            const bool populated=std::any_of(squad.members.begin(),squad.members.end(),[&](int id) {
                const auto* u=unit(id);
                return u && u->alive() && !u->underConstruction && u->type && !u->type->isStructure();
            });
            if (squad.parameters[8]!=int(populated)) { squad.parameters[8]=int(populated);squad.dirty=1; }
        }
    }
    for (auto& _u : units_) {
        _u.turnReqBam = 0;   // requested-turn display field, refreshed below
        _u.tickStartHeadingBam=uint16_t(_u.heading.v);
    }
#ifndef NDEBUG
    hashTrace();   // TAK_HASHTRACE=lo:hi -- per-component dump, EVERY tick on both peers
#endif
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
            // A player/AI may have conjured this limited type since planning.
            // Recheck live counts; dead units release their slot normally.
            if (!atTypeCap(bs.player,bs.type)) spawn(bs.type, bs.x, bs.z, 0, bs.player);
        ++benchCursor_;
    }
    std::chrono::steady_clock::time_point _tk0, _sep0;
    if (g_phase) { _tk0 = std::chrono::steady_clock::now();
                   g_tcomb = g_scriptMs = g_moveMs = 0;
                   g_visMs = g_burnMs = g_gridMs = 0; }
    // Cap repaths per tick: a big group that jams while moving can trip the
    // blocked/stuck watchdogs en masse, and hundreds of path searches in one tick
    // stall the sim. Deferred units retry a later tick. Deterministic (fixed budget,
    // unit-index order), so lockstep peers stay in sync.
    // RETAIL FIDELITY: zero. Retail has no pathfinder at all -- its PathNavigator
    // keeps a two-point segment [position, goal] and searches for nothing
    // (docs/retail-engine.md) -- so neither do we. A* repathing was ours.
    //
    // Know what this costs before changing it back. Measured, 30 units sent the
    // length of a map:
    //                    Angvir's Maze   Athri Cay   Inner Circle
    //     budget 24         15/30          22/30        24/30
    //     budget  0          0/30          21/30         0/30
    // At zero, every unit keeps its order (they no longer give up, see the
    // pathExists fix) but the whole group wedges ~31% along and never moves again:
    // straight-line steering slides along a wall and never commits to moving AWAY
    // from the goal, so concave geometry traps it for good.
    //
    // That is WORSE than retail, not equal to it. Retail units wedged occasionally;
    // ours wedge universally, because we copied retail's absence of a search without
    // yet matching its local avoidance (the mover's clamp-and-slide, and the ~6-tick
    // re-anchor of the segment). The fix is to improve the mover until units stop
    // needing a search -- at which point this zero costs nothing -- NOT to put the
    // budget back. Deliberate call: be faithful now, sharpen the steering later.
    for (auto& u : units_) { u.justFired = false; u.firedWeapons = 0; u.fireAnimations = 0; u.weaponAnimations.clear(); u.justBuilt = 0; }
    if (mission_ || scenario_) justDied_.clear();   // deaths this tick, fed to mission/scenario below
    transportEffects_.clear();
    scriptEmissions_.clear();
    hits_.clear();   // per-tick weapon impacts (drained by the viewer for sounds/fx)
    // Cosmetic disco emote countdown (Shift+D). Deterministic across peers but not
    // hashed -- drives client-side monarch dancing only.
    for (auto& tm : players_) {
        if (tm.discoLeft > 0) --tm.discoLeft;
        if (tm.headbangLeft > 0) --tm.headbangLeft;
    }

    // Economy: recompute income/storage, apply income.
    for (auto& tm : players_) { tm.income = 0; tm.storage = 0; }
    for (auto& u : units_) {
        // A unit under construction contributes no economy until it finishes -- a
        // half-built lodestone must not add its mana income or storage capacity yet.
        if (!u.alive() || !u.type || u.underConstruction) continue;
        auto& tm = players_[size_t(u.player)];
        float income=u.type->income,storage=u.type->storage;
        bool sacred=u.type->sacredIncome;
        if (tm.buildCache) {
            const auto& entries=tm.buildCache->entries;
            auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.type==u.type;});
            if (entry!=entries.end() && entry->hasEconomy) {
                income=entry->income;storage=entry->storage;
                sacred=(entry->inputs.secondaryFlags&0x80000000u)!=0;
            }
        }
        if (sacred) {
            const float multiplier=sacredIncomeMultiplier(
                footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),
                u.type->footX,u.type->footZ);
            tm.income=float(double(tm.income)+double(income)*double(multiplier));
        } else tm.income += income;
        tm.storage += storage;
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
    for (size_t player=0;player<players_.size();++player) {
        auto& tm=players_[player];
        if (!tm.retailResources) { tm.creditMana(tm.income*dt);continue; }
        float demand=0;
        for (const auto& builder:units_) {
            if (builder.player!=int(player) || !builder.alive() || builder.underConstruction) continue;
            int target=0;
            float worker=0;
            if (builder.retailBuild && builder.retailBuild->working) {
                target=builder.retailBuild->target;worker=builder.retailBuild->workerTime;
            } else if (builder.productionSiteId && !builder.buildQueue.empty()) {
                target=builder.productionSiteId;worker=builder.type->workerTime;
                if (tm.buildCache) for (const auto& entry:tm.buildCache->entries)
                    if (entry.type==builder.type && entry.construction) worker=entry.construction->workerTime;
            } else continue;
            const auto* site=unit(target);
            if (site && site->retailSite && site->alive()) {
                const auto& type=site->retailSite->type;
                demand=float(double(demand)+double(worker)*
                    double(type.inverseTime)*double(type.cost));
            }
        }
        tm.retailResources->beginTick(tm.income,tm.storage,demand);
        tm.mana=tm.retailResources->stored;tm.storage=tm.retailResources->capacity;
    }
    const int np = int(players_.size());
    auto cap = [&](int i) { return std::max(players_[size_t(i)].storage, 100.0f); };
    bool teamDone[kMaxPlayers] = {};
    for (int lead = 0; lead < np; ++lead) {
        int team = players_[size_t(lead)].team;
        if (team < 0 || team >= kMaxPlayers || teamDone[team]) continue;
        teamDone[team] = true;
        float pool = 0;   // surplus above caps, gathered from the whole team
        for (int i = 0; i < np; ++i)
            if (!players_[size_t(i)].retailResources && players_[size_t(i)].team == team && players_[size_t(i)].mana > cap(i)) {
                pool += players_[size_t(i)].mana - cap(i);
                players_[size_t(i)].mana = cap(i);
            }
        for (int i = 0; i < np && pool > 0; ++i)
            if (!players_[size_t(i)].retailResources && players_[size_t(i)].team == team) {
                double give = std::min(cap(i) - players_[size_t(i)].mana, double(pool));
                if (give > 0) { players_[size_t(i)].creditMana(give); pool -= float(give); }
            }
        // Any pool left (every member capped) is wasted, as before.
    }
    // ...and once it has filled, the god manifests. This has to happen HERE, in the
    // shared sim, and used to happen in the client instead (GameView::simStep polled
    // godReady() and called its own summonGod()). The referee never did it, so from
    // the first summon the server's world held one fewer unit than every client's and
    // the hashes split for the rest of the match -- a desync arriving tens of minutes
    // in, with nothing in the command stream to explain it.
    summonReadyGods();

    // Both factory and mobile construction keep their sites alive this tick.
    for (auto& u : units_)
        if (u.alive() && u.underConstruction) u.beingBuilt = false;
    // Factory missions run in unit order below. Protect their existing sites
    // from orphan decay while the producer still owns an active build.
    for (const auto& producer:units_)
        if (producer.alive() && producer.hp>Fixed() && !producer.incapacitated() &&
            !producer.underConstruction && !producer.buildQueue.empty() && producer.productionSiteId)
            if (auto* site=unit(producer.productionSiteId);site && site->alive() &&
                (site->hp>Fixed() || site->retailSite) && site->player==producer.player) site->beingBuilt=true;
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        u.constructionHolding=false;
        if (u.conjureHoverTarget && u.conjureHoverTarget!=u.buildSiteId &&
            u.conjureHoverTarget!=u.productionSiteId) {
            u.conjureHoverTarget=0;u.conjureHoverGoal.reset();
        }
        if (u.alive() && u.type && u.retailBuild) {
            u.constructionHolding=!u.retailBuild->flying;
            if (auto* site=unit(u.retailBuild->target);site) site->beingBuilt=true;
        } else if (u.alive() && u.type && u.buildSiteId) tickConstruction(u, dt);
        else if (u.alive() && u.type && u.reclaimId) tickReclaim(u, dt);
        else if (u.alive() && u.type && u.repairId) tickRepair(u, dt);
    }
    for (auto& u : units_)
        if (u.alive() && u.type && u.underConstruction && !u.beingBuilt && !(u.retailSite && u.retailSite->mission))
            decayConstruction(u, dt);

    visPump();   // land a finished async fog pass (cheap; a swap or nothing at all)
    visTimer_ -= dt;
    if (visTimer_ <= 0) {
        // Flat 0.25s. This used to widen to 0.5s with the crowd, to halve the RATE of the
        // periodic fog spike -- and at 3800 units it sat pinned at the 0.5s ceiling, which
        // is exactly the half-second hitch that showed up on hardware. The pass is async
        // now, so there is no spike left to space out and no reason to make a big battle
        // (the case that most wants current fog) reveal at half rate. A pass still in
        // flight simply skips the beat. Fog is client-only display, never hashed.
        visTimer_ = 0.25f;
        {
        auto _v = std::chrono::steady_clock::now();
        updateVisibility();
        if (g_phase) g_visMs += std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - _v).count();
    }
    }

    // Native airborne occupancy is rebuilt before projectile updates.
    const auto projectileAircraft=projectileAirGrid();
    tickFlames(projectileAircraft);
    tickStraightProjectiles(projectileAircraft);
    // Projectiles.
    for (auto& p : projectiles_) {
        if(p.straight)continue;
        if(p.guided3d) {
            if(p.life<=0)continue;
            if(tickCounter_<=p.start) {
                const Unit* owner=unit(p.fromId);
                if(!owner || !owner->alive() || owner->hp<=Fixed())p.life=0;
                continue;
            }
            if(tickCounter_>=p.end) {p.life=0;continue;}
            const Unit* target=unit(p.targetId);
            const auto targetPoint=target && target->alive() && !target->embarked() ?
                queryUnitScriptPoint(p.targetId,true) : p.position;
            RetailGuidedShot state{p.position,p.velocity,p.angles};
            const uint32_t flags=(p.wsrc->unitsOnly?0x800u:0u)|
                (p.wsrc->groundBounce?0x1000u:0u)|(p.wsrc->waterWeapon?0x2000u:0u);
            const int32_t rawSpeed=int32_t(double(p.wsrc->projVel)*2184.5333333333333);
            const int32_t speedPerSubstep=rawSpeed/int32_t(p.substeps);
            int hitUnit=0;
            const bool hit=retailGuidedTick(state,targetPoint,p.wsrc->turnRate/kTick,
                speedPerSubstep,
                p.substeps,p.wsrc->shotSpin,!p.wsrc->shotModel.empty(),
                [&](const auto& point,int32_t& verticalSpeed) {
                    const auto result=projectileCollision(point,verticalSpeed,flags,p.fromPlayer,
                        projectileAircraft);
                    if(result.code==2)hitUnit=result.unitId;
                    return result.code;
                });
            p.position=state.position;p.velocity=state.velocity;p.angles=state.angles;
            p.x=Fixed::raw(p.position[0]);p.z=Fixed::raw(p.position[2]);
            p.age=int32_t(tickCounter_-p.start);
            if(hit) {
                applyHit(*p.wsrc,Fixed::raw(p.position[0]).toFloat(),
                    Fixed::raw(p.position[2]).toFloat(),p.fromPlayer,p.fromId,unit(hitUnit));
                p.spent=true;p.life=0;
            } else if(p.life>0)--p.life;
            continue;
        }
        const bool ballistic3d=p.ballistic3d && p.wsrc;
        const float ox = p.x.toFloat(), oz = p.z.toFloat();   // segment start (before this step)
        p.x += p.vx;   // px per tick already
        p.z += p.vz;
        --p.life;
        ++p.age;
        if(ballistic3d) {
            // BallisticWeapon::update (0x52bf90) resolves every 3D substep
            // through 0x52a4d0. The old port advanced this XYZ state for drawing
            // but then used only the separate 2D target sweep for collision, so
            // arrows passed through trees, terrain and water in World.
            RetailBallisticShot state{p.position,p.velocity,p.angles};
            const int32_t gravityStep=retailBallisticGravityStep(ballisticGravityRaw_,
                p.wsrc->gravityAdj,p.substeps);
            const uint32_t flags=(p.wsrc->unitsOnly?0x800u:0u)|
                (p.wsrc->groundBounce?0x1000u:0u)|(p.wsrc->waterWeapon?0x2000u:0u);
            bool stopped=false;
            for(uint32_t step=0;step<p.substeps;++step) {
                retailBallisticStep(state,gravityStep,p.wsrc->shotSpin);
                const auto hit=projectileCollision(state.position,state.velocity[1],flags,
                    p.fromPlayer,projectileAircraft);
                if(hit.code==0)continue;
                p.position=state.position;p.velocity=state.velocity;p.angles=state.angles;
                p.x=Fixed::raw(p.position[0]);p.z=Fixed::raw(p.position[2]);
                if(hit.code==2)
                    applyHit(*p.wsrc,p.x.toFloat(),p.z.toFloat(),p.fromPlayer,p.fromId,unit(hit.unitId));
                // Native retires an out-of-map shell without damage and dispatches
                // exactly one impact when collision returns 2.
                p.spent=true;p.life=-1;stopped=true;break;
            }
            if(!stopped) {
                p.position=state.position;p.velocity=state.velocity;p.angles=state.angles;
                p.x=Fixed::raw(p.position[0]);p.z=Fixed::raw(p.position[2]);
            }
            // Ballistic XYZ motion owns collision. Keep the old range-derived
            // endpoint for a shell that completes its simulated flight without a
            // 3D impact; do not also run the unrelated 2D target-radius sweep.
            continue;
        }
        Unit* t = unit(p.targetId);
        // A falling bomb is ABOVE everything until it lands, so it takes no
        // in-flight collision -- it detonates once, on the ground, below.
        bool bomb = p.wsrc && p.wsrc->kind == Weapon::Kind::Dropped;
        if (!bomb && t && t->alive() && !t->embarked()) {
            // Distance from the target to the segment travelled this tick, so a
            // fast projectile (e.g. the totem's lightning, ~50px/tick) can't
            // skip past the small hit radius between ticks.
            float sx = p.x.toFloat() - ox, sz = p.z.toFloat() - oz;
            float seg = sx * sx + sz * sz;
            float u = seg > 0 ? ((t->x.toFloat() - ox) * sx + (t->z.toFloat() - oz) * sz) / seg : 0.0f;
            u = std::clamp(u, 0.0f, 1.0f);
            float cx = ox + u * sx, cz = oz + u * sz;
            float dx = t->x.toFloat() - cx, dz = t->z.toFloat() - cz;
            float r = t->type ? 8.0f + 8.0f * float(std::max(t->type->footX,
                                                             t->type->footZ)) : 8.0f;
            if (dx * dx + dz * dz < r * r) {
                // Apply the impact: direct hit + area splash (per the weapon's
                // FBI areaofeffect), using the grid from the previous rebuild.
                // A shot ALWAYS has its weapon: all three launch sites set wsrc, and
                // damage is read from the weapon at impact (applyHit -> damageVs), which
                // is also how retail does it -- it loads the def's damage field at the
                // point of use in 76 places and snapshots it onto nothing. The
                // Projectile::damage this branch used was a dead copy of w.damage.
                applyHit(*p.wsrc, t->x.toFloat(), t->z.toFloat(), p.fromPlayer, p.fromId, t);
                p.life = -1;
                p.spent = true;   // connected: don't also crater at expiry
            }
        }
    }
    // A released bomb detonates the moment it reaches the ground, wherever it has
    // drifted to -- unlike a fired shot, which is a dud if it runs out of life.
    // Whatever it lands ON takes the direct hit: several bombs carry no
    // areaofeffect at all (tarbeak's Egg Bomb), and a splash-only detonation would
    // make those deal nothing whatsoever.
    // ...and so does an ordinary BALLISTIC shell that missed. A retail ballistic
    // projectile has no lifetime at all: it flies its arc under gravity and always
    // comes down, and the terrain impact runs the ordinary area-damage path with no
    // hit unit (KINGDOMS.icd 0x52a6f3 -> 0x529c10). Ours simply vanished, so every
    // missed siege shell -- 32 ballistic weapon blocks with a real blast radius,
    // the catapults, trebuchets and mortars -- dealt nothing whatsoever.
    // A guided or hitscan shot that runs out is still a silent dud, which is retail
    // too: those DO carry a range-derived expiry that kills them without damage.
    for (size_t bi = 0; bi < projectiles_.size(); ++bi) {
        const Projectile bp = projectiles_[bi];
        if (bp.spent || bp.life > 0 || !bp.wsrc) continue;
        bool dropped = bp.wsrc->kind == Weapon::Kind::Dropped;
        // Ballistic = the plain arcing shot: not guided, not a hitscan beam, not melee.
        bool ballistic = bp.wsrc->kind == Weapon::Kind::Normal &&
                         !bp.wsrc->beam && !bp.wsrc->melee;
        // Splash weapons only. Retail also strikes whatever feature sits in the
        // impact cell even for a shot with no areaofeffect, but our applyHit damages
        // that centre cell unconditionally -- so porting it literally would have
        // every stray arrow chopping and igniting forests. Deliberate divergence.
        if (!dropped && !(ballistic && bp.wsrc->aoe > 0.0f)) continue;
        // A bomb is let go over its target, so whatever it lands on takes the DIRECT
        // hit -- several bombs carry no areaofeffect at all and a splash-only
        // detonation would deal nothing. A missed shell has no such victim: retail's
        // terrain impact carries no hit unit, and a unit genuinely standing there
        // would have been caught by the in-flight test instead.
        Unit* under = nullptr;
        if (dropped) {
            float bestD = 1e30f;
            forEachNear(bp.x.toFloat(), bp.z.toFloat(), 40.0f, [&](int idx) {
                Unit& e = units_[size_t(idx)];
                if (!e.alive() || e.embarked() || !e.type || allied(e.player, bp.fromPlayer)) return;
                float dx = (e.x - bp.x).toFloat(), dz = (e.z - bp.z).toFloat();
                float d = dx * dx + dz * dz;
                if (d < bestD) { bestD = d; under = &e; }
            });
        }
        applyHit(*bp.wsrc, bp.x.toFloat(), bp.z.toFloat(), bp.fromPlayer, bp.fromId, under);
    }
    std::erase_if(projectiles_, [](const Projectile& p) { return p.life <= 0; });

    rebuildGrid();   // spatial hash for this tick (combat acquire + separation)
    rebuildOccupancy();   // who is parked where: units are solid (see sim.h)
    tickAutomaticGates();

    // The periodic re-anchor sweep that lived here (every unit with a route,
    // every 120 ticks, staggered by id) is gone: it predates the cadence ladder
    // and duplicated it -- and it kept re-asking FAILED routes, which the
    // ladder's failed-quiet branch exists to forbid (cadence_test measured it
    // probing a sealed wall of parked bodies every four seconds). Retail's
    // re-anchoring IS the ladder: the plain branch chains a long haul route by
    // route, the traffic branches pace crowd re-asks, and a failed route with
    // no traffic evidence rests.
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
        // (Goal quantization used to coarsen with the crowd here as well -- 2x2 cell
        // blocks up to 8x8 -- so that fewer distinct goal blocks meant fewer full-map
        // flow-field builds per tick, which was the dominant cost at 10k+ units. Both
        // the quantization and the flow fields are gone; only acqStride_ above
        // survives. Deterministic: it keys off the live count.)
    }


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
                f->sx += u.x.toFloat(); f->sz += u.z.toFloat(); ++f->n;
                f->slowest = std::min(f->slowest, u.baseSpeed.toFloat());
            }
    for (auto& u : units_) {
        if (!u.alive() || !u.type || u.squad >= 0 || !u.orders.empty()) continue;
        if (u.type->isStructure() || u.underConstruction) continue;   // buildings don't rejoin
        FormAgg* f = formOf(u);
        if (!f || f->n <= 1) continue;
        float cx = float(f->sx / f->n), cz = float(f->sz / f->n);
        if (detmath::len(u.x.toFloat() - cx, u.z.toFloat() - cz) > kFormRejoin) order(u.id, cx, cz, false);
    }

    for (size_t unitIndex=0;unitIndex<units_.size();++unitIndex) {
#if defined(__GNUC__) || defined(__clang__)
        // Fetch upcoming thread flags while the current unit runs. This is
        // only a cache hint; scripts still execute in their original order.
        if (unitIndex+8<units_.size()) {
            const size_t id=size_t(units_[unitIndex+8].id);
            if (id<unitScriptById_.size()) if (const auto* next=unitScriptById_[id]) {
                for (const auto& thread:next->state.vm.threads)
                    __builtin_prefetch(thread.words.data(),0,1);
                __builtin_prefetch(&next->state.vm.active,0,1);
            }
        }
#endif
        if (!units_[unitIndex].type) continue;
        if (units_[unitIndex].alive() && units_[unitIndex].constructionEmitter)
            units_[unitIndex].constructionEmitter->advance();
        if (units_[unitIndex].alive() && units_[unitIndex].cosmeticConstructionEmitter)
            units_[unitIndex].cosmeticConstructionEmitter->advance();
        if (g_phase) {
            const auto start=std::chrono::steady_clock::now();
            tickUnitScript(units_[unitIndex]);
            g_scriptMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        } else tickUnitScript(units_[unitIndex]);
        if (units_[unitIndex].retailSite && units_[unitIndex].retailSite->mission)
            tickRetailGetBuilt(units_[unitIndex]);
        if (units_[unitIndex].retailBuild) tickRetailConstruction(units_[unitIndex]);
        const auto generationBefore=spawnGeneration_;
        const int currentId=units_[unitIndex].id;
        if (units_[unitIndex].alive()) tickProduction(units_[unitIndex],dt);
        if (spawnGeneration_!=generationBefore) {
            if (retailAllocation_) {
                std::sort(units_.begin(),units_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
                bodyIndexValid_=false;
                unitIndex=size_t(std::find_if(units_.begin(),units_.end(),[&](const auto& u){return u.id==currentId;})-units_.begin());
            }
            rebuildGrid();rebuildOccupancy();
        }
        // Production may reallocate the vector; acquire the reference afterward.
        auto& u=units_[unitIndex];
        // 51e220 updates surface Y after each mover, including early returns.
        // The next mover tick consumes that stored height for its water flag.
        struct SurfaceUpdate {
            World& world; int id; Fixed x,z; Bam heading;
            ~SurfaceUpdate() {
                // A mission can create a construction site and reallocate units.
                // Resolve the owner again at this final per-unit phase.
                auto* subject=world.unit(id);
                if (!subject || !subject->type) return;
                auto& u=*subject;
                if (u.alive() && !u.embarked() && !u.type->canFly && !u.type->isStructure() &&
                    u.flightGroundMode==1 && (u.type->canHover || u.x!=x || u.z!=z || u.heading!=heading))
                    u.groundY=world.surfaceHeight(u,world.tickCount(),&u.groundPitch,&u.groundRoll);
            }
        } surfaceUpdate{*this,u.id,u.x,u.z,u.heading};
        if (!u.alive()) {
            ++u.deadFor;
            if (u.corpseStatue<0 && u.deadFor==kCorpseAnimTicks && u.deadFor<u.corpseUntil) {
                if (!placeCorpse(u,corpseTypeOf(u.type))) retireCorpse(u);
            }
            if (u.deadFor>=u.corpseUntil &&
                (u.deadFor<kRetiredTicks-30 || corpseFootprints_.contains(u.id)))
                retireCorpse(u);
            continue;
        }
        if (u.hp <= Fixed() && !(u.retailSite && u.underConstruction)) {
            cancelPath(u);
            refreshMovingSearchBody(u);
            if (u.player >= 0 && u.player < int(players_.size()))
                players_[size_t(u.player)].losses++;   // end-of-game "Losses" column
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
                deathBlasts_.push_back({&u.type->explodeAs, u.x.toFloat(), u.z.toFloat(), u.player, u.id});
            // Corpse window: the body lies reclaimable (and, if its corpse def
            // says so, resurrectable) until decomposetime runs out. Gibbed
            // (overkill >= maxHp -- placeholder severity rule pending the icd
            // Killed RE) or corpse-less units vanish with the death anim.
            {
                // Retail severity (icd 0x512610): ((overkill% + HP% one second
                // before death) / 2), clamped 1..100. Passed to the COB Killed.
                float okPct = u.overkill.toFloat() * 100.0f / std::max(float(u.maximumHp()), 1.0f);
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
                    std::fprintf(stderr, "statue edge: %s statue=%d stoned=%d\n",
                                 u.type->id.c_str(), u.corpseStatue, u.stonedFor);
                int ct = u.corpseStatue >= 0 ? u.corpseStatue : corpseTypeOf(u.type);
                // Retail gib rule (icd 0x512610): deathType = the killing blow's
                // FBI damagetype; 3 (explosion) makes Killed refuse the corpse
                // and EXPLODE every piece. An unfinished conjure never leaves a
                // corpse (corpseType forced 0 at 0x5127f5). Statues place
                // unconditionally.
                bool gib = (u.deathType == 3 || u.underConstruction) &&
                           u.corpseStatue < 0;
                // A self-destructed unit leaves nothing: it is not gibbed (that
                // is the explosion type) and it does not lie there as a wreck
                // either -- it fades. Statues still place, as they do for every
                // other death.
                if (u.deathType == Unit::kDeathSelfDestruct && u.corpseStatue < 0)
                    ct = -1;
                if (ct >= 0 && !gib) {
                    if (u.corpseStatue < 0 && isWater(u.x.toFloat(), u.z.toFloat())) {
                        // Retail water graves sink and fade in seconds, never
                        // decompose, never get reclaimed (icd 0x512fbe).
                        u.corpseUntil = kCorpseAnimTicks + int32_t(2.5f * kTick);
                    } else {
                        int d30 = featTypes_[size_t(ct)].decomposeTicks;
                        // decomposetime 0 = never rots (building wrecks linger
                        // until reclaimed, like retail).
                        // decomposeTicks is ALREADY a tick count, so this is now a
                        // plain add rather than a round trip through seconds.
                        const int starts=u.corpseStatue>=0 ? 0 : kCorpseAnimTicks;
                        u.corpseUntil = d30 > 0 ? starts + d30 : INT32_MAX;
                    }
                } else {
                    u.corpseUntil = kCorpseAnimTicks;
                }
                u.corpseWork = Fixed::fromFloat(
                    std::max(ct >= 0 ? featTypes_[size_t(ct)].energy : 0.0f,
                             60.0f));   // ~0.5s minimum consume time
                if (u.type->isStructure())
                    blockFoot(*u.type,u.x.toFloat(),u.z.toFloat(),false);
            }
            // Drop this unit's per-unit pathfinding state. Ids are never reused, so a
            // dead unit's entries can never match anything again -- they would simply
            // accumulate for the rest of the match.
            u.deadFor = 0; u.orders.clear(); u.speed = Fixed();
            // Refresh the old body before a corpse offset moves the record.
            for (auto& plane:searchGrades_)
                refreshSearchRect(plane,footprintOrigin(u.x,u.type->footX),
                    footprintOrigin(u.z,u.type->footZ),u.type->footX,u.type->footZ);
            // Frozen/stone deaths bypass the ordinary death animation.
            if (u.corpseStatue>=0 && !placeCorpse(u,u.corpseStatue)) retireCorpse(u);
            continue;
        }

        // Retail 1-second HP-percent samples (unit+0x110/+0x111): the Killed
        // severity reads the PREVIOUS sample, i.e. your health ~1s before death.
        if (tickCounter_ % 30 == 0) {
            u.hpPct1s = u.hpPctCur;
            u.hpPctCur = uint8_t(std::clamp(u.hp.toFloat() / std::max(float(u.maximumHp()), 1.0f)
                                            * 100.0f, 0.0f, 100.0f));
        }
        // Status timers count down; HP regenerates (healtime); mana recharges.
        if (u.frozenFor > 0) --u.frozenFor;
        if (u.stonedFor > 0) --u.stonedFor;
        if (u.paralyzedFor > 0) --u.paralyzedFor;
        if (u.selfDestructT >= 0) {   // armed self-destruct: tick down, then blow up
            --u.selfDestructT;
            if (u.selfDestructT <= 0) {
                u.selfDestructT = -1;
                // NOT an explosion. Retail's self-destruct mission (icd
                // 0x4017e0, the handler behind SelfDestruct /
                // UNITMISSIONCODE_SELFDESTRUCT) ends by applying 30000 damage of
                // DAMAGE TYPE 5 to the unit itself. Type 5 is not the explosion
                // type -- 3 is, and 3 is the one that gibs -- and it takes its
                // own branch in the death handler (0x5126a9), distinct from
                // every other type, setting a flag none of them set.
                //
                // What the player sees, reported from retail: the unit does not
                // blow up. It quietly leaves your command and fades, with no
                // wreck left behind.
                u.hp = Fixed(); u.lastHitBy = 0; u.deathType = Unit::kDeathSelfDestruct;
            }
        }
        // Deadly water (.ota waterdoesdamage): a ground unit standing in it is
        // burned down. Flyers are over it, not in it; ships and amphibians belong
        // there, so anything that can cross water is exempt.
        if (waterDamage_ > 0 && !u.type->canFly && u.type->maxWaterDepth <= 0 &&
            isWater(u.x.toFloat(), u.z.toFloat())) {
            u.hp -= Fixed::fromFloat(waterDamage_ * dt);
            if (u.hp <= Fixed()) { u.overkill = fxMax(u.overkill, -u.hp); u.deathType = 1; }
        }
        // `u.hp > Fixed()` matters because of WHERE hp gets zeroed, not by how much.
        // Every other death path puts hp <= 0 outside this loop body, so the sweep
        // at the top (3912) catches it and `continue`s -- never reaching this line.
        // The self-destruct expiry a few lines up is the one that fires INSIDE the
        // body, after this unit's own sweep has already gone by, so its kill waits
        // for the next tick and regen gets to run first. Without the guard a
        // regenerating unit healed straight back off zero and never died at all,
        // which is nearly every unit: of the 202 types this registry loads from a
        // retail install, 200 have healtime > 0 (only aranull and npcwagon do not).
        // That is why Ctrl+Shift+D self-destruct appeared to do nothing.
        if (u.type->healTime > 0 && u.hp > Fixed() && u.hp < Fixed::fromFloat(u.maximumHp()))
            u.hp = fxMin(Fixed::fromFloat(u.maximumHp()),
                         u.hp + Fixed::fromFloat(dt / u.type->healTime));
        if (u.type->maxMana > 0 && u.mana < u.type->maxMana)
            u.mana = std::min(double(u.type->maxMana), u.mana + double(u.type->manaRegen) * double(dt));
        // A WANDERER keeps its spawn point as home -- that anchor is what stops its
        // stroll turning into a migration.
        if (u.orders.empty() && !u.type->wanders) { u.homeX = u.x; u.homeZ = u.z; }
        // Retail dispatches each unit's missions immediately before its mover,
        // not all ground missions before every other unit's activity.
        if (u.constructionHolding && u.buildSiteId) continue;
        if (g_phase) {
            const auto start=std::chrono::steady_clock::now();
            tickGroundMission(u);
            g_moveMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        } else tickGroundMission(u);
        if (plainFlightPatrol(u)) tickFlightPatrol(u);
        tickGuardNoMove(u);
        // A captured MobileBuild waits for navigator arrival before creating
        // the site. Keep that rectangle attached to the owning goal across
        // every intermediate route, and hand construction over on the next tick.
        if (!u.orders.empty()) {
            auto& goal=u.orders[currentLeg(u.orders)];
            if (prepareBuildApproach(u)) {
                const Order arrived=goal;
                dropLeg(u);
                u.orders.insert(u.orders.begin(),arrived);
                u.orders.front().buildRectangle.reset();
                u.orders.front().x=arrived.buildX;
                u.orders.front().z=arrived.buildZ+Fixed::fromInt(arrived.buildType->footZ*8+24);
                cancelPath(u);
                // Allocate during this unit's dispatch: later units may draw
                // construction particles from the same CRT stream.
                const int builderId=u.id;
                const int siteId=startBuild(builderId,arrived.buildType,arrived.buildX.toFloat(),
                                             arrived.buildZ.toFloat(),Approach::None);
                if (siteId) turnBuilderToSite(*unit(builderId),*unit(siteId));
                else popBuildOrder(*unit(builderId));
                // spawn invalidates references; re-establish slot order before
                // continuing so a later newborn receives its first update today.
                if (retailAllocation_) {
                    std::sort(units_.begin(),units_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
                }
                bodyIndexValid_=false;
                unitIndex=size_t(std::find_if(units_.begin(),units_.end(),
                    [&](const auto& candidate){return candidate.id==builderId;})-units_.begin());
                rebuildGrid();rebuildOccupancy();
                continue;
            }
        }

        // Wildlife and villagers roam. Retail gives them Standby_wander and they
        // amble; ours stood like statues on every campaign and scenario map. An idle
        // wanderer strolls every ~8 seconds, staggered by id so a herd doesn't move
        // as one, rolled on the sim's deterministic RNG -- and always to a point
        // near HOME, so they mill about their patch instead of drifting off it.
        if (u.type->wanders && u.orders.empty() && !u.underConstruction &&
            u.type->maxVel > Fixed() && (uint32_t(u.id) + tickCounter_) % 240 == 0) {
            float ang = float(burnRand(628)) / 100.0f;
            float r = float(burnRand(96));
            order(u.id, u.homeX.toFloat() + detmath::sin(ang) * r,
                  u.homeZ.toFloat() + detmath::cos(ang) * r, false);
        }

        // Cloaking: drains player mana; an enemy within mincloakdistance forces a
        // decloak, and so does running dry of mana.
        if (u.type->canCloak && u.cloakOn) {
            bool enemyNear = false;
            float md = std::max(float(u.type->minCloakDist), 1.0f);
            forEachNear(u.x.toFloat(), u.z.toFloat(), md, [&](int idx) {
                const Unit& e = units_[size_t(idx)];
                if (e.alive() && !e.embarked() && !allied(e.player, u.player) && e.type) {
                    float dx = (e.x - u.x).toFloat(), dz = (e.z - u.z).toFloat();
                    if (dx * dx + dz * dz <= md * md) enemyNear = true;
                }
            });
            float cost = (u.speed.toFloat() * kTick > 3.0f ? u.type->cloakCostMove : u.type->cloakCost) * dt;
            Player& tm = players_[size_t(u.player)];
            if (!enemyNear && tm.mana >= cost) { tm.debitMana(cost); u.cloaked = true; }
            else u.cloaked = false;
        } else if (u.cloaked) {
            u.cloaked = false;   // toggled off (or no longer a cloaker): decloak now
        }

        if (u.underConstruction) continue;   // silent until finished
        if (u.embarked()) {                  // riding a transport
            Unit* t = unit(u.inTransport);
            if (t && t->alive()) { u.x = t->x; u.z = t->z; updateBodyIndex(u); }
            else { if (mission_ || scenario_) justDied_.push_back(u.id); u.deadFor = 0; }  // transport lost with all hands
            continue;
        }
        // Frozen / petrified / paralyzed: the unit is inert this tick.
        if (u.incapacitated()) { u.speed = Fixed(); if (u.type->canFly) u.flightVelocity={}; continue; }
        if (!u.type->canFly && !u.type->isStructure()) {
            updateGroundTerrainFlags(u);
        }
        // Jam tracker DECAYS every tick and is topped up only while the mover is
        // genuinely stuck (see the fully-blocked branch). Decaying here rather than
        // clearing it inside the movement code makes it self-correcting: the mover has
        // several early exits -- escorting, arriving, handing an order to a builder --
        // and a value only ever cleared on the movement path would survive all of them
        // and leave a unit reading as jammed long after it stopped trying to move.
        // A build order that has reached the front claims its ground NOW, before
        // the builder has walked anywhere. Waiting until arrival would leave the
        // spot unreserved for the whole walk, so two builders sent to the same
        // place would both pass canPlace and one would arrive to find it taken.
        // The actual startBuild happens after this loop: it spawns the site, which
        // can reallocate units_ and leave every Unit& here dangling -- an earlier
        // version called it inline and read the order queue out of freed memory.
        if (!u.orders.empty() && u.orders.front().buildType && !u.orders.front().buildRectangle && u.buildSiteId == 0)
            buildDue_.push_back(u.id);
        // Reclaim needs no deferral: consuming a feature spawns nothing, so
        // nothing can reallocate units_ underneath us.
        if (!u.orders.empty() && u.orders.front().reclaimFeat && u.reclaimId == 0)
            u.reclaimId = u.orders.front().reclaimFeat;
        if (!u.orders.empty() && u.orders.front().repairTarget && u.repairId == 0)
            u.repairId = u.orders.front().repairTarget;

        bool groundMovementHandled=false;
        const size_t currentOrder=u.orders.empty()?0:currentLeg(u.orders);
        const bool routedSurfaceUnload=!u.orders.empty() && currentOrder<u.orders.size() &&
            u.orders[currentOrder].unload && u.orders[currentOrder].transportUnloadApproach;
        if (!u.orders.empty() && (u.orders.front().load || u.orders.front().unload || routedSurfaceUnload))
            groundMovementHandled=tickTransport(u, dt);
        else if (g_phase) { auto _c0=std::chrono::steady_clock::now(); tickCombat(u, dt, groundMovementHandled); g_tcomb += std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-_c0).count(); }
        else tickCombat(u, dt, groundMovementHandled);

        if (groundMovementHandled) continue;
        if (u.type->canFly && u.retainedFlightGoal) {
            if (u.orders.empty()) {
                tickRetainedFlightMovement(u);
                continue;
            }
            u.retainedFlightGoal.reset();
            u.retainedFlightControllerActive=false;
        }
        bool combatHold =
            (u.type->canFly || u.type->isStructure()) && !u.orders.empty() && u.orders.front().targetId != 0 &&
            !u.orders.front().load && !u.orders.front().guard && [&] {
                Unit* t = unit(u.orders.front().targetId);
                if (!t) return false;
                float dx = (t->x - u.x).toFloat(), dz = (t->z - u.z).toFloat();
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
                float pad = t->type->maxVel <= Fixed()
                    ? 8.0f * float(std::max(t->type->footX, t->type->footZ)) + 24.0f : 0.0f;
                // Weapon switchers approach using the selected weapon, just as
                // tickCombat does. A longer secondary spell must not stop a
                // dragon outside its selected breath weapon's range.
                const bool allWeapons=!u.type->weaponSwitching && u.type->weapons.size()>1;
                const float range=allWeapons ? u.type->maxRange() : (sel ? float(sel->range) : u.type->maxRange());
                if (std::sqrt(dx * dx + dz * dz) > (range + pad) * 0.95f) return false;
                // Don't sit still with no shot: a ranged unit whose line to the
                // target is blocked keeps moving to get around the wall.
                bool needLoS = range > 64.0f && !u.type->canFly &&
                               t->type && !t->type->canFly && u.type->canMove &&
                               !u.type->lobs();
                return !needLoS ||
                       combatLineOfSight(u,*t);
            }();
        if (combatHold) continue;

        Fixed target = u.baseSpeed;
        // Formation pacing: a pure-move member keeps to the group's slowest speed,
        // UNLESS it's behind the centre relative to the goal (a straggler), in which
        // case it sprints at its own speed to catch up (see the FormAgg pass above).
        if (u.squad < 0 && !u.orders.empty() && u.orders.front().targetId == 0) {
            if (FormAgg* f = formOf(u); f && f->n > 1) {
                float cx = float(f->sx / f->n), cz = float(f->sz / f->n);
                // "Behind the centre" is measured against the leg the group is
                // walking NOW -- against the last QUEUED leg a straggler check
                // would compare everyone to a point nobody is heading for yet.
                const Order& legEnd = u.orders[currentLeg(u.orders)];
                float gx = legEnd.x.toFloat(), gz = legEnd.z.toFloat();
                float uToGoal = detmath::len(u.x.toFloat() - gx, u.z.toFloat() - gz);
                float cToGoal = detmath::len(cx - gx, cz - gz);
                if (uToGoal <= cToGoal + kFormBehind) target = fxMin(target, Fixed::fromFloat(f->slowest));
            }
        }
        if (g_phase) {
            const auto start=std::chrono::steady_clock::now();
            tickNavigationMovement(u,target);
            g_moveMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        } else tickNavigationMovement(u,target);

    }

    // Retail updates missions/movers in 51d3e0 before 4f6c70 runs 416430
    // (5263aa, then 526411). Install completed routes only after movement,
    // and admit new searches from the position and occupancy after that step.
    if (pathService_) paths_.tick(
        [&](int unitId, int cx, int cz) {
            const Unit* u = unit(unitId);
            if (!u || !u->type) return int(kCellImpassable);
            // Plain cellScore, which now carries retail's grades -- bodies included.
            // The exemption wrapper that stood here (parked cells near the unit or its
            // goal graded passable, kParkedFreeCells) was ours: it existed so the
            // search could route out of and into crowds while bodies were otherwise
            // invisible to it. Retail has no such carve-out -- its map grades a parked
            // neighbour 0 wherever it stands, a fully ringed start simply fails the
            // search, and the mover presses on its segment until the cadence retries.
            // Cell-exclusive parking makes a ringed start rare rather than normal.
            return cellScore(u->type, cx, cz, unitId);
        },
        [&](int unitId, const std::vector<PathCell>& route, Fixed gxF, Fixed gzF,
            bool failed, bool crowded, bool traffic, bool detour) {
            deliverSearchRoute(unitId,route,gxF,gzF,failed,crowded,traffic,detour);
        },[&](int id,PathCell& start,int& heading,RetailCostSearch::Costs& costs) {
            refreshSearchRequest(id,start,heading,costs);
        },tickCounter_,[&](int id,uint32_t now) {
            return admitSearchRequest(id,now);
        });

    for (const auto& event : paths_.takeNotifications()) {
        if (auto* u = unit(event.unitId))
            if (auto* goal = navigationMissionOrder(*u); goal && goal->controller == event.controller)
                (goal->load || goal->transportUnloadApproach
                    ? goal->transportMission : goal->mission).pending |= uint32_t(event.events);
    }

    // Queued builds that came due this tick, started now that nothing holds a
    // reference into units_. Re-fetch by id each pass for the same reason.
    for (int bid : buildDue_) {
        Unit* b = unit(bid);
        if (!b || !b->alive() || b->buildSiteId != 0 || b->orders.empty()) continue;
        const Order o = b->orders.front();
        if (!o.buildType) continue;
        const int siteId=startBuild(bid, o.buildType, o.x.toFloat(), o.z.toFloat() - float(o.buildType->footZ) * 8 - 24,
                                   Approach::None);
        // Restored perimeter arrival creates its site and begins the pivot on
        // this dispatch. Do not run a second movement or construction step.
        if (siteId && (o.mission.pending & o.mission.waitMask & 0x100))
            turnBuilderToSite(*unit(bid),*unit(siteId));
        // startBuild may have failed (the spot is taken now, or mana ran out). Drop
        // the order rather than parking the builder on it forever.
        if (Unit* b2 = unit(bid); b2 && b2->buildSiteId == 0 && !b2->orders.empty() &&
                                  b2->orders.front().buildType)
            b2->orders.erase(b2->orders.begin());
    }
    buildDue_.clear();

    // Retail runs player bookkeeping after unit updates and before wind.
    // Restored priority caches use the current World economy and unit lifecycle.
    // Other availability queries continue to read live World state.
    for (auto& player:players_) {
        if (player.retailResources) {
            auto& r=*player.retailResources;
            r.finishTick();player.mana=r.stored;player.storage=r.capacity;
            player.displayResources.income=r.produced;
            player.displayResources.usage=r.requested;
            if (player.buildCache) {
                auto& history=player.buildCache->resources;
                history.income=r.produced;history.usage=r.requested;history.advance();
            }
            r.produced=r.requested=0;
        }
        player.displayResources.advance();
        if (player.retailAi) {
            auto& ai=*player.retailAi;
            const int owner=int(&player-players_.data());
            auto unsupported=[&](const char* branch) {
                throw std::runtime_error("unsupported retail AI "+std::string(branch)+
                                         " at tick "+std::to_string(tickCounter_));
            };
            retailTickAiSchedule(ai.schedule,tickCounter_,[&](int n){return pathRand(n);},
                [&] {
                    for (auto& squad:ai.squads)
                        std::erase_if(squad.members,[&](int id) {
                            const Unit* member=unit(id);
                            return !member || !member->alive() || member->player!=owner;
                        });
                },
                [&] {
                    if (!ai.initialized && tickCounter_<=30) unsupported("initial assignment");
                    ai.initialized=true;
                    if (ai.scenarioDeadline && tickCounter_>=ai.scenarioDeadline)
                        unsupported("scenario assignment");
                    for (auto& member:units_) {
                        if (member.player!=owner || !member.alive() || member.underConstruction ||
                            !member.type || member.type->isStructure()) continue;
                        bool assigned=false;
                        for (const auto& squad:ai.squads)
                            if (std::find(squad.members.begin(),squad.members.end(),member.id)!=squad.members.end()) {
                                assigned=true;break;
                            }
                        if (assigned || member.incapacitated()) continue;
                        if (member.embarked()) unsupported("attached unit assignment");
                        std::array<RetailAiBaseCandidate,20> bases;
                        for (unsigned i=1;i<=20;++i) {
                            const auto& base=ai.squads[i];
                            if (!ai.schedule.groups[i].present || base.members.empty()) continue;
                            if (base.kind!=RetailAiSquadKind::Base) unsupported("non-base selection slot");
                            if (base.parameters[0] && !ai.anchorsRestored) unsupported("missing base anchors");
                            auto& candidate=bases[i-1];candidate.occupied=true;
                            if (auto anchor=ai.anchors.find(base.parameters[0]);anchor!=ai.anchors.end()) {
                                candidate.center=std::pair{std::bit_cast<int32_t>(uint32_t(int32_t(anchor->second.first))*1048576u+524288u),
                                                          std::bit_cast<int32_t>(uint32_t(int32_t(anchor->second.second))*1048576u+524288u)};
                            } else {
                                RetailAiCentroid centroid;
                                for (int id:base.members) {
                                    const Unit* u=unit(id);
                                    if (!u || !u->alive()) continue;
                                    centroid.add(u->x.v,u->z.v);
                                }
                                candidate.center=centroid.center();
                            }
                        }
                        unsigned selected=retailNearestAiBase(bases,member.x.v,member.z.v);
                        if (!selected) selected=2;
                        if (!ai.schedule.groups[selected].present) unsupported("missing assignment group");
                        member.moveState=1;member.fireState=2;member.stance=0;member.standingOrder=2;
                        ai.squads[selected].members.push_back(member.id);
                    }
                },
                [&](unsigned index,auto& clock) {
                    auto& squad=ai.squads[index];
                    retailTickAiSquad(squad,clock,tickCounter_,[&](int n){return pathRand(n);},
                        [&](auto& strike) {
                            const int backup=strike.parameters[0];
                            if (backup<0 || backup>=100) unsupported("backup index");
                            if (backup!=int(index) && ai.schedule.groups[size_t(backup)].present &&
                                !ai.squads[size_t(backup)].members.empty()) unsupported("strike recruitment");
                        },
                        [&](auto&) {
                            if (squad.kind==RetailAiSquadKind::Base && player.buildCache && player.buildCache->planner) {
                                tickRetailAiBase(owner,index);return;
                            }
                            if (squad.kind==RetailAiSquadKind::Strike && player.buildCache && player.buildCache->planner) {
                                tickRetailAiStrike(owner,index);return;
                            }
                            const auto detail="occupied squad planner slot "+std::to_string(index);
                            unsupported(detail.c_str());
                        });
                });
        }
        if (player.buildCache && !player.retailResources) player.buildCache->resources.advance();
        if (retailTickPlayerCache(player.cacheClock,tickCounter_,[&](int n){return pathRand(n);}) && player.buildCache) {
            auto& cache=*player.buildCache;
            const int owner=int(&player-players_.data());
            unsigned population=0;
            for (auto& entry:cache.entries) entry.inputs.count=entry.inputs.completed=0;
            for (const auto& member:units_) {
                if (member.player!=owner || !member.alive()) continue;
                ++population;
                const auto entry=std::find_if(cache.entries.begin(),cache.entries.end(),
                    [&](const auto& candidate){return candidate.type==member.type;});
                if (entry==cache.entries.end()) throw std::runtime_error("unit missing from restored build catalogue");
                ++entry->inputs.count;
                if (!member.underConstruction) ++entry->inputs.completed;
            }
            const float stored=float(player.mana);
            const float capacity=std::max(player.storage,100.0f); // current World economy host
            const float ratio=double(capacity)>0.01 ? stored/capacity : 0.0f;
            const bool shortfall=cache.resources.shortfall(stored);
            for (auto& entry:cache.entries)
                entry.priority=retailBuildPriority(entry.inputs,uint16_t(population),ratio,shortfall,
                    [&](int n){return pathRand(n);});
        }
    }
    if (windEnabled_)
        retailTickWind(wind_, tickCounter_, [&](int n) { return pathRand(n); },
                       [&] { return crtRand(0x525248); });

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
        --e.at;
        --e.endAt;
        bool pulse = e.at <= 0;
        if (pulse) {
            if (e.period > 0) e.at += e.period;
            else e.at = INT32_MAX;     // single-pulse: never again
        }
        bool done = e.endAt <= 0;
        const PendingEffect cur = e;   // applyHit walks/kills units_; copy first
        if (done) pendingEffects_.erase(pendingEffects_.begin() + std::ptrdiff_t(i));
        else ++i;
        if (pulse && cur.w) applyHit(*cur.w, cur.x.toFloat(), cur.z.toFloat(), cur.player, cur.fromId, nullptr);
    }
    // Wandering storms: drift along the launch heading, weave, and grind whatever
    // they touch. The FBI `damage` is a PER-TICK rate, not a per-hit figure -- the
    // Tornado's 50 is 1500/sec inside its small radius (it wears a unit down over
    // its 9 seconds), while a god vortex's 12500 is simply death to stand in, which
    // its `monarch = 0.01` row scales back so a Monarch has a few seconds to escape.
    for (size_t i = 0; i < storms_.size();) {
        Storm& s = storms_[i];
        if (!s.w) { storms_.erase(storms_.begin() + std::ptrdiff_t(i)); continue; }
        auto vary = [&] {
            for(size_t index=0;index<2;++index) {
                const size_t axis=index*2;
                const float amplitude=s.variation[index];
                const int32_t offset=retailStormInteger(
                    (retailWanderRandom(s.wanderSeed,amplitude*2)-double(amplitude))*65536.0);
                s.velocity[axis]=std::bit_cast<int32_t>(uint32_t(s.baseVelocity[axis])+uint32_t(offset));
            }
            s.velocity[1]=s.baseVelocity[1];
        };
        auto activate = [&] {
            s.phase=Storm::Phase::Active;s.animation.start(s.w->wanderLoopTicks);
            s.end=tickCounter_+uint32_t(s.w->durationTicks);
            s.nextVary=tickCounter_+uint32_t(s.w->variationTicks);
            vary();
        };
        if(s.phase==Storm::Phase::Waiting) {
            if(tickCounter_<s.start) {
                const auto* caster=unit(s.fromId);
                if(!caster || !caster->alive() || caster->hp<=Fixed()) {
                    storms_.erase(storms_.begin()+std::ptrdiff_t(i));continue;
                }
            } else if(!s.w->wanderStartTicks.empty()) {
                s.phase=Storm::Phase::Starting;s.animation.start(s.w->wanderStartTicks);
            } else activate();
            ++i;continue;
        }
        if(s.phase==Storm::Phase::Starting) {
            s.animation.tick(s.w->wanderStartTicks,false);
            if(!s.animation.active)activate();
            ++i;continue;
        }
        const bool ending=s.phase==Storm::Phase::Ending;
        s.animation.tick(ending ? s.w->wanderEndTicks : s.w->wanderLoopTicks,!ending);
        if(ending && !s.animation.active) {
            storms_.erase(storms_.begin()+std::ptrdiff_t(i));continue;
        }
        // Native integrates each substep without clamping at the map edge.
        s.x.v=std::bit_cast<int32_t>(uint32_t(s.x.v)+uint32_t(s.velocity[0])*s.substeps);
        s.y.v=std::bit_cast<int32_t>(uint32_t(s.y.v)+uint32_t(s.velocity[1])*s.substeps);
        s.z.v=std::bit_cast<int32_t>(uint32_t(s.z.v)+uint32_t(s.velocity[2])*s.substeps);
        if(!ending) {
            if(s.nextVary<=tickCounter_) { vary();s.nextVary+=uint32_t(s.w->variationTicks); }
            const Storm hit=s;
            applyHit(*hit.w,hit.x.toFloat(),hit.z.toFloat(),hit.player,hit.fromId,nullptr);
            if(s.end<=tickCounter_) {
                if(s.w->wanderEndTicks.empty()) {
                    storms_.erase(storms_.begin()+std::ptrdiff_t(i));continue;
                }
                s.phase=Storm::Phase::Ending;s.animation.start(s.w->wanderEndTicks);
            }
        }
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
    // Footprint ownership prevents movement from creating overlaps, so no
    // after-the-fact separation force is needed.
    // There was an "unstick" pass here, deleted 2026-09-12. It nudged any ground
    // unit standing on a blocked cell toward the nearest walkable one, to rescue
    // bodies "spawned by a building, shoved by a crowd, or clipped a corner".
    //
    // Retail has no such thing, and the three failure modes that comment lists
    // were ours: the crowd-shoving one was the separation pass, itself deleted
    // earlier today. What the nudge did do was move a unit every tick, which
    // reads as progress, which reset the wedged timer, which meant a unit that
    // could not reach its goal never gave up -- measured, one wandered 2426px in
    // 60 seconds and was still going. Take the nudge away and the same unit
    // settles after 3.9s having moved 30px.
    //
    // The risk taken knowingly: a unit that does end up on illegal ground now
    // stays there rather than being walked off it. The mover's own solidity is
    // what should keep it from happening in the first place.
    // Win/defeat is derived from unit state on every sim (clients + referee),
    // so all peers agree on the tick a team is eliminated / the game is won.
    updateNavigationExploration();
    updateOutcome();
    if (g_phase) {
        auto _end = std::chrono::steady_clock::now();
        double tsep = std::chrono::duration<double,std::milli>(_end-_sep0).count();
        double ttot = std::chrono::duration<double,std::milli>(_end-_tk0).count();
        static double thr = getenv("TAK_PHASE_MS") ? atof(getenv("TAK_PHASE_MS")) : 15.0;
        if (ttot > thr) {   // only report a stall (threshold tunable via TAK_PHASE_MS)
            int alive = 0; for (auto& u : units_) if (u.alive()) ++alive;
            // flow= and path= used to sit here; the flow fields and the inline
            // A* are both gone, so the counters were always zero.
            std::fprintf(stderr, "SIMPHASE tick=%.1fms combat=%.1f sep=%.1f vis=%.1f burn=%.1f grid=%.1f scripts=%.1f movement=%.1f other=%.1f units=%d\n",
                         ttot, g_tcomb, tsep, g_visMs, g_burnMs, g_gridMs, g_scriptMs, g_moveMs,
                         ttot - g_tcomb - tsep - g_visMs - g_burnMs - g_gridMs - g_scriptMs - g_moveMs, alive);
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
    // Unit COBs ran before the movers above, so GET 33 on the next tick reads
    // this tick's actual wrapped heading delta, not the mover's unclamped request.
    for (auto& u:units_)
        u.animationTurnBam=std::bit_cast<int16_t>(
            uint16_t(uint16_t(u.heading.v)-u.tickStartHeadingBam));
}

#ifndef NDEBUG
// See sim.h. Recomputes the same quantities stateHash() folds, but grouped, so a
// mismatch can be attributed to a component instead of a 64-bit number.
void World::hashTrace() const {
    static bool init = false, on = false;
    static uint32_t lo = 0, hi = 0;
    if (!init) {
        init = true;
        if (const char* e = std::getenv("TAK_HASHTRACE")) {
            unsigned a = 0, b = 0;
            if (std::sscanf(e, "%u:%u", &a, &b) == 2) { lo = a; hi = b; on = true; }
        }
    }
    if (!on || tickCounter_ < lo || tickCounter_ > hi) return;
    if (std::getenv("TAK_HASHDETAIL")) (void)stateHash();

    auto fnv = [](uint64_t h, uint64_t v) {
        for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 1099511628211ULL; }
        return h;
    };
    // TEMPLATED SO A Fixed CANNOT SLIP THROUGH. These helpers take a float, and Fixed
    // converts to one implicitly (the port scaffold in fixed.h), so folding a position
    // silently hashed a float APPROXIMATION of it -- lossy past 256px, which means two
    // units genuinely a fraction of a pixel apart could produce the same checksum and a
    // real divergence would be invisible to the very check meant to catch it. That
    // happened three times in this file before it was noticed. Now it will not compile.
    // mana is a double (retail's own width -- see Player::mana), so it needs all eight
    // bytes folded. Narrowing it to a float to reuse bits() would throw away exactly the
    // low-order state the checksum exists to compare.
    auto bits64 = [](double d) { uint64_t b; std::memcpy(&b, &d, 8); return b; };
    auto bits = [](auto f) {
        static_assert(!std::is_same_v<std::decay_t<decltype(f)>, Fixed>,
                      "fold u.x.v -- the raw fixed-point -- not a float of it");
        static_assert(!std::is_same_v<std::decay_t<decltype(f)>, Bam>,
                      "fold the angle's raw int, not a float of it");
        static_assert(std::is_floating_point_v<decltype(f)>, "bits() is for floats");
        uint32_t b; std::memcpy(&b, &f, 4); return uint64_t(b);
    };
    const uint64_t seed = 1469598103934665603ULL;

    uint64_t hUnitPos = seed, hUnitHp = seed, hUnitOrd = seed, hUnitMisc = seed;
    uint64_t aliveN = 0;
    for (const auto& u : units_) {
        if (u.alive()) ++aliveN;
        // THE RAW FIXED-POINT, not a float of it. u.x/u.z are Fixed now, and letting
        // them convert through the scaffold operator hashed a float APPROXIMATION of
        // the stored value -- lossy past 256px at 1/65536, so two units genuinely a
        // fraction of a pixel apart could fold identically and a divergence would be
        // invisible to the lockstep check that exists to catch it.
        hUnitPos  = fnv(fnv(fnv(hUnitPos, u.id), uint64_t(uint32_t(u.x.v))),
                        uint64_t(uint32_t(u.z.v)));
        hUnitPos  = fnv(hUnitPos, uint64_t(uint32_t(u.heading.v)));
        hUnitHp   = fnv(fnv(hUnitHp, u.id), uint64_t(uint32_t(u.hp.v)));
        hUnitOrd  = fnv(fnv(hUnitOrd, u.id), u.orders.size());
        if (!u.orders.empty()) {
            const Order& o = u.orders.front();
            hUnitOrd = fnv(fnv(fnv(hUnitOrd, uint32_t(o.targetId)),
                               uint64_t(uint32_t(o.x.v))), uint64_t(uint32_t(o.z.v)));
        }
        hUnitMisc = fnv(fnv(hUnitMisc, u.id), uint64_t(u.alive() ? 1 : 0));
        hUnitMisc = fnv(hUnitMisc, uint64_t(u.veteran));
        hUnitMisc = fnv(hUnitMisc, uint32_t(u.baseSpeed.v));
        if (u.animationTurnBam) hUnitMisc = fnv(hUnitMisc,uint16_t(u.animationTurnBam));
        hUnitMisc=fnv(hUnitMisc,uint32_t(u.scriptAimTarget));
        for (int32_t rl : u.reloads) hUnitMisc = fnv(hUnitMisc, uint64_t(uint32_t(rl)));
        for(const auto& aim:u.weaponAim) {
            hUnitMisc=fnv(hUnitMisc,aim.heading);hUnitMisc=fnv(hUnitMisc,aim.pitch);hUnitMisc=fnv(hUnitMisc,aim.flags);
        }
        hUnitMisc = fnv(hUnitMisc, uint64_t(uint32_t(u.stance)));
        hUnitMisc = fnv(hUnitMisc, uint64_t(u.moveState) * 3 + uint64_t(u.fireState));
        hUnitMisc = fnv(hUnitMisc, u.standingOrder);
        hUnitMisc = fnv(hUnitMisc, uint64_t((u.cloakOn ? 1u : 0u) | (u.active ? 2u : 0u)));
        hUnitMisc = fnv(hUnitMisc, uint64_t(uint32_t(u.repairId)));
        hUnitMisc = fnv(hUnitMisc, uint64_t(uint32_t(int32_t(u.squad))));
    }
    uint64_t hProj = fnv(seed, projectiles_.size());
    for (const auto& p : projectiles_) {
        hProj = fnv(fnv(fnv(hProj, uint32_t(p.fromPlayer)), uint64_t(uint32_t(p.x.v))), uint64_t(uint32_t(p.z.v)));
        if(p.guided3d) {
            hProj=fnv(hProj,0x47554944u);
            hProj=fnv(fnv(hProj,uint32_t(p.fromId)),uint32_t(p.slot));
            hProj=fnv(fnv(hProj,uint32_t(p.targetId)),p.start);hProj=fnv(hProj,p.end);
            hProj=fnv(hProj,p.substeps);hProj=fnv(hProj,uint32_t(p.life));hProj=fnv(hProj,uint32_t(p.age));
            hProj=fnv(hProj,p.spent);
            for(const auto* values:{&p.position,&p.velocity})
                for(int32_t value:*values)hProj=fnv(hProj,uint32_t(value));
            for(auto angle:p.angles)hProj=fnv(hProj,angle);
        }
    }
    uint64_t hEff = fnv(seed, pendingEffects_.size());
    for (const auto& e : pendingEffects_)
        hEff = fnv(fnv(fnv(fnv(hEff, uint32_t(e.player)),
                           uint64_t(uint32_t(e.x.v))), uint64_t(uint32_t(e.z.v))),
                   uint64_t(uint32_t(e.at)));
    uint64_t hStorm = fnv(fnv(seed, uint32_t(stormSeq_)), storms_.size());
    for (const auto& s : storms_)
        hStorm = fnv(fnv(fnv(fnv(hStorm, uint32_t(s.player)),
                             uint64_t(uint32_t(s.x.v))), uint64_t(uint32_t(s.z.v))),
                     uint64_t(s.end));
    uint64_t hPlayers = seed;
    for (const auto& t : players_)
        hPlayers = fnv(fnv(hPlayers, bits64(t.mana)), uint32_t(t.team));
    uint64_t fAlive = 0, fWork = 0;
    for (const auto& f : features_)
        if (f.alive) { ++fAlive; fWork ^= (uint64_t(uint32_t(f.work.v)) << 1) ^ uint64_t(uint32_t(f.id)); }
    uint64_t hFeat = fnv(fnv(seed, fAlive), fWork);
    // The nav overlay + grid cells drive losBetween, which gates target acquisition --
    // but NEITHER is folded into stateHash, so a divergence here is invisible to the
    // referee until it changes a decision seconds later.
    uint64_t hObst = seed;
    for (uint8_t o : obst_) hObst = fnv(hObst, o);
    // Keep the terrain inputs distinct from the dynamic obstacle overlay.
    uint64_t hHeights = seed;
    for (uint8_t v : heights_) hHeights = fnv(hHeights, v);
    const uint64_t hNav = nav_.debugCellsHash();

    std::fprintf(stderr,
        "HASHTRACE t=%u units=%zu alive=%llu pos=%016llx hp=%016llx ord=%016llx misc=%016llx "
        "proj=%016llx(%zu) eff=%016llx storm=%016llx play=%016llx feat=%016llx rng=%u obst=%016llx nav=%016llx hgt=%016llx sea=%d hw=%d hh=%d\n",
        tickCounter_, units_.size(), (unsigned long long)aliveN,
        (unsigned long long)hUnitPos, (unsigned long long)hUnitHp,
        (unsigned long long)hUnitOrd, (unsigned long long)hUnitMisc,
        (unsigned long long)hProj, projectiles_.size(),
        (unsigned long long)hEff, (unsigned long long)hStorm,
        (unsigned long long)hPlayers, (unsigned long long)hFeat,
        gameRng_,
        (unsigned long long)hObst, (unsigned long long)hNav,
        (unsigned long long)hHeights, seaLevel_, hW_, hH_);
}
#endif

uint64_t World::stateHash() const {
    // FNV-1a over the quantities that must agree between lockstep peers.
    uint64_t h = 1469598103934665603ULL;
    // Optional checkpoints include the newer script/navigation state omitted
    // from the older motion-oriented hashTrace diagnostic.
    auto checkpoint = [&](const char* section) {
#ifndef NDEBUG
        static const char* limit = std::getenv("TAK_HASHDETAIL");
        if (limit && tickCounter_ <= std::strtoul(limit,nullptr,10)) {
            std::fprintf(stderr,"HASHDETAIL t=%u section=%s hash=%016llx\n",
                         tickCounter_,section,static_cast<unsigned long long>(h));
            if (std::strcmp(section,"grades")==0) {
                int index=0;
                for (const auto& plane:searchGrades_) {
                    std::fprintf(stderr,"HASHGRADE t=%u index=%d active=%d foot=%d,%d version=%llu checksum=%016llx recent=%u stale=%u requester=%d\n",
                        tickCounter_,index++,activeSearchGrade_,plane.footX,plane.footZ,
                        static_cast<unsigned long long>(plane.navVersion),static_cast<unsigned long long>(plane.checksum),
                        plane.preparation.recent,plane.preparation.stale,plane.preparation.requestSlot);
                }
            }
        }
#else
        (void)section;
#endif
    };
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFF;
            h *= 1099511628211ULL;
        }
    };
    // Same guard as bits() above, and for the same reason -- see the note there.
    auto mixf = [&](auto f) {
        static_assert(!std::is_same_v<std::decay_t<decltype(f)>, Fixed>,
                      "fold the raw fixed-point (x.v), not a float of it");
        static_assert(!std::is_same_v<std::decay_t<decltype(f)>, Bam>,
                      "fold the angle's raw int, not a float of it");
        static_assert(std::is_floating_point_v<decltype(f)>, "mixf() is for floats");
        uint32_t b;
        static_assert(sizeof b == sizeof f);
        std::memcpy(&b, &f, 4);
        mix(b);
    };
    mix(nextMovementController_);
    mix(navigationExplored_.size());
    mix(uint32_t(restoredNavigationViewer_));
    for (auto mask:navigationExplored_) mix(mask);
    checkpoint("exploration");
    for (const auto& [id,factory]:unitScripts_) {
        mix(0x434f42564dull); mix(uint32_t(id));
        mix(factory.activated); mix(factory.ready); mix(factory.yardOpen); mix(factory.buggerOff);
        const auto& state=factory.state;
        mix(state.vm.active); mix(uint32_t(state.vm.ticksPerSecond));
        for (auto value:state.vm.statics) mix(value);
        // Every word is serialized as eight bytes, including unused stack
        // words. Eight zero-byte FNV steps equal one multiplication modulo 2^64.
        constexpr uint64_t zeroWordFactor=[] {
            uint64_t factor=1;
            for (int i=0;i<8;++i) factor*=1099511628211ULL;
            return factor;
        }();
        for (const auto& thread:state.vm.threads) for (auto value:thread.words) {
            if (value==0) h*=zeroWordFactor;
            else mix(value);
        }
        for (const auto& piece:state.pieces) {
            mix(piece.active); mix(piece.visible); mix(piece.cached); mix(piece.shaded); mix(piece.rendered);
            for (const auto* values:{&piece.moveTarget,&piece.moveSpeed,&piece.turnTarget,&piece.turnSpeed,
                                    &piece.spinTarget,&piece.spinAcceleration,&piece.move,&piece.turn})
                for (auto value:*values) mix(uint32_t(value));
        }
    }
    checkpoint("scripts");
    mix(searchGrades_.size()); mix(uint32_t(activeSearchGrade_));
    for (const auto& plane:searchGrades_) {
        mix(plane.footX); mix(plane.footZ); mix(plane.navVersion); mix(plane.checksum);
        mix(plane.preparation.recent); mix(plane.preparation.stale); mix(plane.preparation.requestSlot);
    }
    checkpoint("grades");
    for (const auto& u : units_) {
        mix(uint64_t(u.id));
        mix(uint64_t(u.player));
        // Raw fixed-point -- see the note in hashTrace. A float conversion here would
        // silently drop the low bits of the very state this checksum guards.
        mix(uint64_t(uint32_t(u.x.v)));
        mix(uint32_t(u.groundY.v)); mix(u.groundMoveTick);
        mix(u.groundPitch); mix(u.groundRoll); mix(u.groundSpeedMode);
        mix(uint64_t(uint32_t(u.z.v)));
        mix(uint64_t(uint32_t(u.hp.v)));       // hp is fixed-point now
        mix(uint64_t(uint32_t(u.heading.v)));
        if (u.animationTurnBam) mix(uint16_t(u.animationTurnBam));
        mix(u.variationPhase);
        mix(uint64_t(uint32_t(u.baseSpeed.v)));
        if (u.type && u.type->canFly) {
            if (u.scriptOccupancy) { mix(0x5346584fu);mix(u.scriptOccupancy); }
            mix(u.flightGroundMode); mix(u.landing.has_value());
            if (u.landing) {
                const auto& l=*u.landing; const auto& m=l.mission;
                mix(m.stage); mix(m.waitMask); mix(m.deadline); mix(m.pending); mix(m.flags);
                mix(uint32_t(l.anchor.x)); mix(uint32_t(l.anchor.y)); mix(uint32_t(l.anchor.z));
                mix(l.angle); mix(l.parity); mix(l.canceled);
            }
            mix(uint32_t(u.flightY.v));
            mix(u.retainedFlightGoal.has_value());
            mix(u.retainedFlightControllerActive);
            if (u.retainedFlightGoal) {
                const auto& g=*u.retainedFlightGoal;
                mix(uint32_t(g.point.x));mix(uint32_t(g.point.y));mix(uint32_t(g.point.z));
                mix(g.flags);mix(g.heading);mix(uint16_t(g.radius));
            }
            mix(uint32_t(u.flightSectorX));mix(uint32_t(u.flightSectorZ));
            for (const auto& v:{u.flightVelocity,u.flightNavigation.destination,u.flightNavigation.velocity}) {
                mix(uint32_t(v.x)); mix(uint32_t(v.y)); mix(uint32_t(v.z));
            }
            mix(u.flightNavigation.heading);
            for(int32_t value:u.flightAcceleration)mix(uint32_t(value));
        }
        if (u.type && !u.type->canFly && !u.type->isStructure()) {
            mix(uint32_t(u.routeStamp));mix(u.routeCrowded);mix(u.routeTraffic);
            mix(u.routeFailed);mix(u.routeDetour);
        }
        mix(uint64_t(u.orders.size()));
        mix(u.missionEvents);
        mix(u.standbyAllowed); mix(u.standbyActive);
        mix(u.guardNoMoveAllowed); mix(u.guardNoMoveActive);
        if (u.guardNoMoveActive) {
            const auto& m=u.guardNoMoveState;
            mix(m.stage); mix(m.waitMask); mix(m.deadline); mix(m.pending); mix(m.flags);
        }
        if (u.standbyActive) {
            mix(u.standbyState.stage); mix(u.standbyState.waitMask);
            mix(u.standbyState.deadline); mix(u.standbyState.pending); mix(u.standbyState.flags);
        }
        for (const auto& order : u.orders) {
            if (order.park) {
                mix(0x5041524bu);const auto& p=*order.park;
                mix(p.target);mix(p.padding);mix(p.attempts);mix(uint32_t(p.permanent));mix(p.ring.has_value());
                if (p.ring) {mix(p.ring->x);mix(p.ring->z);mix(p.ring->innerRadius);
                    mix(p.ring->outerTolerance);mix(p.ring->outerSquared);}
            }
            mix(order.transportUnloadApproach);
            if(order.load || order.unload || order.transportUnloadApproach) {
            mix(order.transportPickup);mix(order.transportUnloadReleasePending);
            mix(order.transportUnloadTransferDeferred);
            mix(order.transportTicks);mix(order.transportPassenger);
                mix(order.transportMission.stage);mix(order.transportMission.waitMask);
                mix(order.transportMission.deadline);mix(order.transportMission.pending);
                mix(order.transportMission.flags);mix(order.transportApproachAttempts);
                mix(uint32_t(order.transportX.v));mix(uint32_t(order.transportZ.v));
                if(order.unload)mix(uint32_t(order.transportY.v));
            }
            mix(order.groundMission);mix(order.navigationExhausted);mix(order.navigationConsumed);
            mix(order.buildRectangle.has_value());
            if (u.type && u.type->canFly) {
                mix(order.landing);
                mix(order.flightMoveMission);
                mix(order.hoverAttackRefresh);
                mix(uint32_t(order.hoverAttackTargetX.v));mix(uint32_t(order.hoverAttackTargetZ.v));
                mix(order.flightGoal.has_value());
                if (order.flightGoal) {
                    const auto& g=*order.flightGoal;
                    mix(uint32_t(g.point.x)); mix(uint32_t(g.point.y)); mix(uint32_t(g.point.z));
                    mix(g.flags); mix(g.heading); mix(uint16_t(g.radius));
                }
            }
            if (order.buildRectangle) {
                mix(order.buildRectangle->minX); mix(order.buildRectangle->maxX);
                mix(order.buildRectangle->minZ); mix(order.buildRectangle->maxZ);
                mix(uint32_t(order.buildX.v)); mix(uint32_t(order.buildZ.v));
                mix(order.mission.waitMask); mix(order.mission.pending);
                if (order.buildType) {
                    mix(order.buildType->id.size());
                    for (unsigned char c : order.buildType->id) mix(c);
                } else mix(0);
            }
            if (!order.groundMission && !(u.type && u.type->canFly && (order.patrol || order.flightMoveMission))) continue;
            mix(order.controller);
            mix(order.missionRadius);
            if (order.missionTarget) {
                mix(0x4d544754u);mix(uint32_t(order.missionTarget->first.v));mix(uint32_t(order.missionTarget->second.v));
            }
            mix(uint32_t(order.groundResponse.mode));
            if (order.groundResponse.mode) {
                mix(uint16_t(order.groundResponse.origin.x)); mix(uint16_t(order.groundResponse.origin.z));
                mix(uint16_t(order.groundResponse.goal.x)); mix(uint16_t(order.groundResponse.goal.z));
                mix(uint32_t(u.lastHitBy));
            }
            mix(order.mission.stage);
            mix(order.mission.waitMask);
            mix(order.mission.deadline);
            mix(order.mission.pending);
            mix(order.mission.flags);
        }
        // Order target + attack-move flag: two sims can hold the same order
        // COUNT while chasing different targets -- fold the head order in so a
        // divergence shows up before damage lands (the referee must attribute
        // desyncs fast at 8 players).
        if (!u.orders.empty()) {
            const Order& o = u.orders.front();
            mix(uint64_t(uint32_t(o.targetId)));
            mix(uint64_t(o.attackMove ? 1 : 0));
            // Raw fixed-point: a goal is in the same domain as a position, and folding
            // a float of it would lose the low bits the mover actually steers to.
            mix(uint64_t(uint32_t(o.x.v)));
            mix(uint64_t(uint32_t(o.z.v)));
            mix(uint64_t(o.hasSegment));
            if (o.hasSegment) {
                mix(uint64_t(uint32_t(o.segmentX.v)));
                mix(uint64_t(uint32_t(o.segmentZ.v)));
            }
        }
        mix(uint64_t(u.alive() ? 1 : 0));
        mix(uint64_t(u.groundMovementMode));
        mix(uint64_t(u.groundTerrainFlags));
        mix(uint64_t(u.groundScanTick));
        mix(uint16_t(u.sightFootprint.x));mix(uint16_t(u.sightFootprint.z));
        mix(uint32_t(u.sightFootprint.eyeHeight));mix(uint16_t(u.sightFootprint.distance));
        mix(u.sightFootprint.sightHeight);mix(u.sightFootprint.active);
        mix(u.groundGradeTick);
        mix(uint64_t(u.veteran));
        // All three reload timers, not just the primary: they now decide WHICH
        // weapon the auto-selector fires, so a drift in any of them would change
        // behaviour.
        for (int32_t rl : u.reloads) mix(uint64_t(uint32_t(rl)));   // ticks
        for(const auto& aim:u.weaponAim) {mix(aim.heading);mix(aim.pitch);mix(aim.flags);}
        mix(uint32_t(u.scriptAimTarget));
        mix(uint64_t(uint32_t(u.selfDestructT)));   // ticks; drives a deterministic death
        // Stance / cloak-intent / active gate auto-acquire, cloaking and firing, so a
        // divergence in them must fault directly rather than diffusing into positions.
        // Both standing orders drive behaviour, so both belong in the checksum --
        // the displayed stance alone would hide a peer whose move/fire fields had
        // drifted (they are set together, but only one of them is shown).
        mix(uint64_t(uint32_t(u.stance)));
        mix(uint64_t(u.moveState) * 3 + uint64_t(u.fireState));
        mix(u.standingOrder);
        mix(uint64_t((u.cloakOn ? 1u : 0u) | (u.active ? 2u : 0u)));
        if (u.conjureHoverTarget) {
            mix(0x484f5645u);mix(uint32_t(u.conjureHoverTarget));
            if (u.conjureHoverGoal) {
                const auto& g=*u.conjureHoverGoal;
                mix(uint32_t(g.point.x));mix(uint32_t(g.point.y));mix(uint32_t(g.point.z));
                mix(g.flags);mix(g.heading);mix(uint16_t(g.radius));
            }
        }
        mix(uint32_t(u.productionSiteId)); mix(uint32_t(u.buildProgress));
        mix(u.underConstruction); mix(u.buildBegun); mix(uint32_t(u.conjureRate.v));
        if (u.retailSite) {
            mix(0x53495445u);const auto& s=*u.retailSite;
            mixf(s.progress.remaining);mix(s.progress.hp);mix(s.progress.events);mix(s.progress.flags);
            mixf(s.type.inverseTime);mixf(s.type.cost);mix(s.type.maxHp);
            mix(s.mission.has_value());mix(uint32_t(s.builder));
            if (s.mission) {
                const auto& m=*s.mission;
                mix(m.stage);mix(m.waitMask);mix(m.deadline);mix(m.pending);mix(m.flags);
            }
        }
        if (u.retailBuild) {
            mix(0x4255494cu);const auto& b=*u.retailBuild;
            mix(b.target);mixf(b.workerTime);mix(b.working);mix(b.activityDeadline);
            if (b.flying) {
                mix(0x464c5942u);mix(b.flyingOwnerFlags);mix(uint32_t(b.flyingSlowSpeed));
                mix(uint32_t(b.flyingFastSpeed));mix(b.flyingBuildDistance);
            }
            mix(b.mission.stage);mix(b.mission.waitMask);mix(b.mission.deadline);
            mix(b.mission.pending);mix(b.mission.flags);
            if (b.turnHeading) { mix(0x5455524eu);mix(*b.turnHeading); }
        }
        if (u.constructionEmitter) {
            mix(0x454d4954u);const auto& e=*u.constructionEmitter;
            mix(e.capacity);mix(uint32_t(e.radius));mix(uint32_t(e.height));mix(e.particles.size());
            for (const auto& p:e.particles) {
                mix(uint32_t(p.x));mix(uint32_t(p.y));mix(uint32_t(p.z));
                mix(uint32_t(p.speed));mix(uint32_t(p.ceiling));
            }
        }
        mix(u.buildQueue.size());
        if (u.repeatType) {
            mix(0x52455054u);
            for (unsigned char ch : u.repeatType->id) mix(ch);
            mix(0);
        }
        for (const auto* queued : u.buildQueue) {
            mix(queued->id.size());
            for (unsigned char c : queued->id) mix(c);
        }
        mix(uint64_t(uint32_t(u.repairId)));   // build-power target -> HP/mana divergence
        mix(uint64_t(uint32_t(int32_t(u.squad))));   // control squad: formation<0 drives movement
    }
    checkpoint("units");
    // Projectiles: count alone hides same-count divergence, so fold owner and
    // position of each in flight (mixf hashes the exact bits -- deterministic
    // across peers and NaN-safe, unlike an int cast).
    mix(uint64_t(projectiles_.size()));
    for (const auto& p : projectiles_) {
        mix(uint64_t(uint32_t(p.fromPlayer)));
        mix(uint64_t(uint32_t(p.x.v)));   // projectile position is fixed-point
        mix(uint64_t(uint32_t(p.z.v)));
        if(p.guided3d) {
            mix(0x47554944u);mix(p.fromId);mix(p.slot);mix(p.targetId);mix(p.start);mix(p.end);mix(p.substeps);
            mix(p.spent);mix(uint32_t(p.life));mix(uint32_t(p.age));
            for(const auto* values:{&p.position,&p.velocity})
                for(int32_t value:*values)mix(uint32_t(value));
            for(auto angle:p.angles)mix(angle);
        }
        if(p.straight) {
            mix(0x53545254u);mix(p.fromId);mix(p.slot);mix(p.start);mix(p.end);mix(p.substeps);mix(p.spent);
            mix(uint32_t(p.life));mix(uint32_t(p.age));
            for(const auto* values:{&p.position,&p.velocity})for(int32_t value:*values)mix(uint32_t(value));
            for(auto angle:p.angles)mix(angle);
            if(p.lightningEffect) {
                mix(0x4c494748u);
                const auto& effect=*p.lightningEffect;
                mix(effect.nextSource);mix(effect.particles.size());
                for(const auto& source:effect.sources)mix(uint32_t(source.countdown));
                for(const auto& particle:effect.particles)
                    for(const auto* values:{&particle.position,&particle.velocity})
                        for(int32_t value:*values)mix(uint32_t(value));
                for(uint8_t pixel:effect.pixels)mix(pixel);
            }
        }
    }
    if(!flames_.empty()) {
        mix(0x464c414du);mix(flames_.size());
        for(const auto& flame:flames_) {
            mix(flame.owner);mix(flame.fromId);mix(flame.slot);mix(flame.start);mix(flame.end);
            mix(flame.lifetime);mix(flame.speed);mix(flame.substeps);mix(flame.impacted);
            for(const auto* values:{&flame.position,&flame.velocity,&flame.endpoint})
                for(int32_t value:*values)mix(uint32_t(value));
            mix(flame.particles.size());
            for(const auto& particle:flame.particles) {
                mix(uint32_t(particle.remaining));mix(uint32_t(particle.lifetime));mix(particle.kind);
                for(const auto* values:{&particle.position,&particle.velocity})
                    for(int32_t value:*values)mix(uint32_t(value));
            }
        }
    }
    checkpoint("flames");
    // Remote Effect spells mid-channel and wandering storms mid-roam are live sim
    // state that outlives a tick, so a divergence in either must show up here.
    mix(uint64_t(pendingEffects_.size()));
    for (const auto& e : pendingEffects_) {
        mix(uint64_t(uint32_t(e.player)));
        mix(uint64_t(uint32_t(e.x.v))); mix(uint64_t(uint32_t(e.z.v)));
        mix(uint64_t(uint32_t(e.at)));   // ticks
    }
    mix(uint64_t(uint32_t(stormSeq_)));
    mix(uint64_t(storms_.size()));
    for (const auto& s : storms_) {
        mix(uint64_t(uint32_t(s.player)));
        mix(uint64_t(uint32_t(s.x.v))); mix(uint64_t(uint32_t(s.z.v)));
        mix(s.start);mix(s.end);mix(uint32_t(s.phase));
        mix(s.animation.frame);mix(s.animation.remaining);mix(s.animation.active);
        mix(uint32_t(s.fromId));mix(uint32_t(s.y.v));mix(s.substeps);
        for(auto value:s.baseVelocity)mix(uint32_t(value));
        for(auto value:s.velocity)mix(uint32_t(value));
        for(auto value:s.variation)mix(std::bit_cast<uint32_t>(value));
        // The private sampler state, offset and timer decide the storm's path.
        mix(s.wanderSeed);
        mix(uint64_t(uint32_t(s.nextVary)));
    }
    for (const auto& t : players_) {
        mix(uint32_t(t.automaticGates) | (uint32_t(t.defensiveAi)<<1));
        { uint64_t b; std::memcpy(&b, &t.mana, 8); mix(b); }   // double: fold all 8 bytes
        mix(t.cacheClock.enabled); mix(t.cacheClock.lastRefresh);
        if (t.retailResources) {
            mix(0x5245534fu);const auto& r=*t.retailResources;
            mixf(r.stored);mixf(r.allocation);mixf(r.requested);mixf(r.produced);
            mixf(r.capacity);mixf(r.capacityOverride);
            mix(std::bit_cast<uint64_t>(r.totalProduced));mix(std::bit_cast<uint64_t>(r.excess));
        }
        if (t.buildCache) {
            mix(0x4255494c44434143ull);
            const auto& cache=*t.buildCache;
            if (cache.planner) {
                mix(0x504c414eu);mix(cache.planner->limited);mix(cache.planner->types.size());
                for (const auto& type:cache.planner->types) {
                    mix(type.weight);mix(type.special);mix(type.faction.size());
                    for (unsigned char c:type.faction) mix(c);
                    mix(type.choices.size());for (auto choice:type.choices) mix(choice);
                }
            }
            mixf(cache.resources.income); mixf(cache.resources.usage);
            for (const auto& sample:cache.resources.samples) for (float value:sample) mixf(value);
            mix(cache.entries.size());
            for (const auto& entry:cache.entries) {
                mix(entry.type->id.size()); for (unsigned char c:entry.type->id) mix(c);
                const auto& p=entry.inputs;
                mix(p.flags);mix(p.secondaryFlags);mix(uint16_t(p.count));mix(uint16_t(p.completed));
                mix(uint16_t(p.weapon));mix(uint32_t(p.desired));mixf(p.cost);mix(entry.priority);
                mix(entry.hasEconomy);
                if (entry.hasEconomy) { mixf(entry.income);mixf(entry.storage); }
                if (entry.construction) {
                    mix(0x434f4e53u);const auto& c=*entry.construction;
                    mixf(c.type.inverseTime);mixf(c.type.cost);mix(c.type.maxHp);mixf(c.workerTime);
                    mix(c.emitter.capacity);mix(uint32_t(c.emitter.radius));mix(uint32_t(c.emitter.height));
                }
            }
        }
        if (t.retailAi) {
            mix(0x524149u);
            const auto& ai=*t.retailAi;
            mix(uint32_t(ai.schedule.assignmentCountdown));mix(ai.initialized);mix(ai.scenarioDeadline);
            if (ai.anchorsRestored) {
                mix(0x414e4348u);mix(ai.anchors.size());
                for (const auto& [reference,cell]:ai.anchors) {
                    mix(uint32_t(reference));mix(uint16_t(cell.first));mix(uint16_t(cell.second));
                }
            }
            for (unsigned i=0;i<100;++i) {
                const auto& clock=ai.schedule.groups[i];const auto& squad=ai.squads[i];
                mix(clock.present);mix(clock.deadline);mix(unsigned(squad.kind));
                mix(squad.active);mix(squad.dirty);
                for (int value:squad.parameters) mix(uint32_t(value));
                mix(squad.members.size());for (int id:squad.members) mix(uint32_t(id));
            }
        }
        // Team assignment drives sim behaviour (splash/acquire/auras) but is set
        // from setup -- fold it in so a lobby/config mismatch faults immediately
        // as a desync instead of diverging mysteriously.
        mix(uint64_t(uint32_t(t.team)));
    }
    checkpoint("players");
    // Reclaimable features: fold a cheap signature so a reclaim divergence faults as
    // a desync directly (mana/nav already reflect it indirectly). Order-independent.
    uint64_t fAlive = 0, fWork = 0;
    for (const auto& f : features_)
        if (f.alive) {
            ++fAlive;
            // Raw fixed-point / raw int -- not a float of either. work was a float
            // whose BITS were folded; it is 16.16 now and dmg is an integer.
            const uint32_t w = uint32_t(f.work.v);
            const uint32_t dm = uint32_t(f.dmg);
            fWork ^= (uint64_t(w) << 1) ^ uint64_t(uint32_t(f.id)) ^
                     // burning/damage state: type swaps and timers are sim state
                     (uint64_t(uint32_t(f.type + 1)) << 17) ^
                     (uint64_t(f.burn) << 33) ^ (uint64_t(uint32_t(f.burnLeft)) << 40) ^
                     (uint64_t(dm) << 9);
        }
    mix(fAlive);
    mix(fWork);
    mix(corpseFootprints_.size());
    for (const auto& [id,footprint]:corpseFootprints_) {
        mix(uint32_t(id));mix(uint32_t(footprint.x));mix(uint32_t(footprint.z));mix(uint32_t(footprint.type));
    }
    for (const auto& corpse:units_) if (!corpse.alive() &&
        (corpse.deadFor<kRetiredTicks || corpseFootprints_.contains(corpse.id))) {
        mix(uint32_t(corpse.id));mix(uint32_t(corpse.deadFor));mix(uint32_t(corpse.corpseUntil));
        mix(uint32_t(corpse.corpseStatue));mix(uint32_t(corpse.corpseWork.v));mix(corpse.corpseBlocks);
    }
    for (const auto& cell:mapPlacementCells_) {
        mix(cell.feature);
        if (cell.feature==0xfffe) { mix(cell.backX);mix(cell.backZ); }
    }
    mix(uint64_t(gameRng_));   // shared RNG stream position must agree
    mix(windEnabled_);
    if (retailAllocation_) {
        mix(0x534c4f5453ull);
        for (const auto& [first,count]:*retailEntityPools_) { mix(first);mix(count); }
        mix(windRng_);
    }
    if (!windEnabled_ && std::any_of(units_.begin(),units_.end(),[](const auto& u){return bool(u.constructionEmitter);}))
        mix(windRng_);
    if (windEnabled_) {
        mix(windRng_); mix(wind_.minimum); mix(wind_.maximum); mix(wind_.nextTick);
        mix(uint32_t(wind_.x)); mix(uint32_t(wind_.z)); mix(uint32_t(wind_.speed));
        mix(wind_.heading); mix(wind_.flags); mix(wind_.generation);
    }
    if (mission_) mission_->foldHash(h);   // mission triggers/vars/outcome are lockstep state
    if (scenario_) scenario_->foldHash(h); // scenario flags/timers/outcome are lockstep state
    for (uint8_t d : forcedDefeat_) { h ^= (d + 1u); h *= 1099511628211ULL; }
    return h;
}

// Mission runner ownership -- ctor/dtor defined here where MissionScript is a complete
// type, so no other TU instantiates the unique_ptr<MissionScript> deleter.
World::World() {
    paths_.setGradeHost({
        [this](int id,bool last) { prepareSearchGrade(id,last); },
        [this](int id,int x,int z,int retry,PathCell start) { return searchGrade(id,x,z,retry,start); },
        [this](int id) { finishSearchGrade(id); }
    });
}
World::~World() {
    if (visRunning_) visWorker_.join();   // the fog worker outlives nothing
}
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
        bool wasDefeated = players_[size_t(p)].defeated;
        players_[size_t(p)].defeated = (aliveByPlayer[size_t(p)] == 0) || monarchDead || forced;
        // Stamp the moment of elimination once, for the end-of-game "Time" column.
        if (!wasDefeated && players_[size_t(p)].defeated) players_[size_t(p)].defeatedAt = int32_t(tickCounter_);
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
