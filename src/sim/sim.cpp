#include "sim/sim.h"
#include "sim/retailtransport.h"
#include "sim/retailanimationqueries.h"
#include "sim/retailaim.h"
#include "sim/retailhweffectdata.h"
#include "sim/retailstorm.h"
#include "sim/retailguided.h"
#include "sim/retailinstantcorpse.h"
#include "sim/retailsplash.h"
#include "sim/retailairattack.h"

#include <type_traits>

#include "hpi/hpi.h"
#include "gaf/nimbus.h"
#include "gaf/animationtiming.h"
#include "sim/detmath.h"
#include "sim/simprobe.h"
#include "sim/statefold.h"
#include "util/devenv.h"
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
#include <optional>
#include <tuple>
#include <array>
#include <atomic>
#include <limits>
#include <thread>

namespace tak::sim {


bool gInstantBuild = false;

// TAK_PHASE sim profiler globals (zero cost when unset). The accumulators are
// thread_local: the server may tick several games in parallel, one World::tick per
// thread, so each thread accumulates (and prints) its own room's timing -- both
// race-free and correctly attributed.
static const bool g_phase = tak::devEnv("TAK_PHASE") != nullptr;
// finer "other" split. g_visMs timed the display fog pass, which the sim no longer runs
// (the client reads viewCounts_); it stays 0 so SIMPHASE keeps its columns for the tools.
static thread_local double g_visMs = 0, g_burnMs = 0, g_gridMs = 0;
static thread_local double g_tcomb = 0, g_scriptMs = 0, g_moveMs = 0, g_navMs = 0;
static thread_local double g_exploreMs = 0;   // navigation exploration; part of sep
// Part of "other": the tick prologue (everything between the navigator's upkeep and the
// per-unit loop, less burn and vis), and the once-a-second retired-unit compaction, which
// runs after the tick total is taken and is reported beside it.
static thread_local double g_prologueMs = 0, g_compactMs = 0;
#ifndef NDEBUG
// TAK_VERIFY_LISTS (debug): every tick, each id list and reused count that
// stands in for a full walk over units_ (WE E2.5) is checked against that walk.
static bool verifyLists() {
    static const bool on = std::getenv("TAK_VERIFY_LISTS") != nullptr;
    return on;
}
// TAK_VERIFY_POOL (debug): every pass that WE E4.3 put on the SimPool is
// redone serially and its result compared (aborts on a difference).
static bool verifyPool() {
    static const bool on = std::getenv("TAK_VERIFY_POOL") != nullptr;
    return on;
}
[[noreturn]] static void poolMismatch(const char* pass, uint32_t tick) {
    std::fprintf(stderr, "TAK_VERIFY_POOL tick %u: %s differs from the serial pass\n", tick, pass);
    std::abort();
}
#endif

namespace {
// One TAK_PHASE bucket: its wall time, and in a debug build with TAK_PMU also the PMU
// deltas over the same span (src/sim/simprobe.h).
struct PhaseSpan {
    double& acc;
    std::chrono::steady_clock::time_point t0;
#ifndef NDEBUG
    int phase;
    probe::PmuSample p0;
#endif
    PhaseSpan(double& a, [[maybe_unused]] int ph) : acc(a), t0(std::chrono::steady_clock::now()) {
#ifndef NDEBUG
        phase = ph;
        if (probe::kPmu) p0 = probe::pmuRead();
#endif
    }
    ~PhaseSpan() {
        acc += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
#ifndef NDEBUG
        if (probe::kPmu) probe::pmuAdd(phase, p0);
#endif
    }
};
} // namespace

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTick = 30.0f;               // FBI per-tick values -> per-second
constexpr float kCobAngle = 2 * kPi / 65536.0f;

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
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
            m.retailMaxWaterDepth=int16_t(int(c.numberOr("MaxWaterDepth",10000)));
            m.retailMinWaterDepth=int16_t(int(c.numberOr("MinWaterDepth",-10000)));
            m.retailMaxSlope=uint8_t(int(c.numberOr("MaxSlope",255)));
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
            // A file that loses its objectname is still parsed, into
            // classicVariants_ (the Classic AI's catalogue only).
            bool variant = false;
            if (types_.count(t.id)) {
                if (!canonical || canonicalTypes_.count(t.id)) variant = true;   // keep existing
                else {
                    auto& displaced = types_[t.id];
                    if (displaced.classicFbi.unitName != t.id)
                        classicVariants_.try_emplace(displaced.classicFbi.unitName, displaced);
                }
            }
            if (canonical) canonicalTypes_.insert(t.id);
            t.classicFbi.unitName = lower(info->valueOr("unitname", stem));
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
            t.buildMovementCode=uint8_t(int32_t(info->numberOr("bmcode",0)));   // retail's default, as below
            t.classicFbi.bmcode=int32_t(info->numberOr("bmcode",0));
            t.classicFbi.isFeature=(int32_t(info->numberOr("isfeature",0))&1)!=0;
            t.classicFbi.category=info->valueOr("category","");
            t.classicFbi.yardMap=info->valueOr("yardmap","");
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
            // Native HP is a signed 16-bit word (shipped data tops out at 29999);
            // cap a modded maxdamage there so every Fixed::fromInt(maxHp) on the
            // HP, heal and repair paths stays inside 16.16's whole-number range.
            t.maxHp = truncSat32(double(float(std::min(info->numberOr("maxdamage", 100), 32767.0))));
            t.experiencePoints = int32_t(info->numberOr("experiencepoints", 666));
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
            // Native 4bfc62 reads only this spelling, defaulting to 16.16 1.0.
            // The Crusades deer definitions' "watermultipliser" is ignored.
            t.waterMult = Fixed::fromRetailNumber(info->numberOr("watermultiplier", 1));
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
            t.builderLimited = info->numberOr("builderlimited", 0) != 0;
            t.canGuard = info->numberOr("canguard", 0) != 0;
            t.canPatrol = info->numberOr("canpatrol", 0) != 0;
            t.canReclaim = info->numberOr("canreclaim", 0) != 0;
            t.canResurrect = info->numberOr("canresurrect", 0) != 0;
            t.canCapture = info->numberOr("cancapture", 0) != 0;
            t.canCloak = info->numberOr("cancloak", 0) != 0;
            t.cloakCost = float(info->numberOr("cloakcost", 0));
            // Retail (0x4c01a1) reads cloakcost, scales it by 1/30 to a per-tick
            // cost, and passes that SCALED value as cloakcostmoving's default before
            // scaling the result by 1/30 again: a unit without the key (zonspide)
            // pays cloakcost/30 per second while moving.
            t.cloakCostMove = float(info->numberOr("cloakcostmoving", t.cloakCost / 30.0f));
            t.minCloakDist = float(info->numberOr("mincloakdistance", 0));
            // Boolean in retail too: the parser does `and eax,1`.
            t.fireAtWillRandom = (int(info->numberOr("fireatwillrandom", 0)) & 1) != 0;
            t.attractsGods = info->numberOr("attractsgods", 0) != 0;
            t.weaponSwitching = info->numberOr("weaponswitching", 0) != 0;
            t.kamikaze = info->numberOr("kamikaze", 0) != 0;
            t.shootMe = info->numberOr("shootme", 0) != 0;
            {
                auto tokens=[](const std::string& text) {
                    std::vector<std::string> out;std::string tok;
                    for (char c : text + " ") {
                        if (c == ' ' || c == '\t') { if (!tok.empty()) { out.push_back(tok); tok.clear(); } }
                        else tok.push_back(char(std::tolower(static_cast<unsigned char>(c))));
                    }
                    return out;
                };
                t.noTargetCats = tokens(info->valueOr("notargetcategory", ""));
                const char* bad[3] = {"wpri_badtargetcategory","wsec_badtargetcategory","wspe_badtargetcategory"};
                for (size_t i = 0; i < 3; ++i) t.badTargetCats[i] = tokens(info->valueOr(bad[i], ""));
            }
            t.onOffable = info->numberOr("onoffable", 0) != 0;
            t.activateWhenBuilt = info->numberOr("activatewhenbuilt", 1) != 0;
            t.cantBeStoned = info->numberOr("cantbestoned", 0) != 0;
            t.cantBeFrozen = info->numberOr("cantbefrozen", 0) != 0;
            // Retail forces both on Monarchs (icd: def bit18 -> cantbestoned +
            // cantbefrozen) -- else petrify would be an instant Monarch kill.
            if (t.commander) t.cantBeStoned = t.cantBeFrozen = true;
            t.cantBeCaptured = info->numberOr("cantbecaptured", 0) != 0;
            t.immuneToParalyzer = info->numberOr("immunetoparalyzer", 0) != 0;
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
            // Native 4bfe91-4bfed5 sets UnitDef+260 bit31 from either key;
            // 4d453c tests that bit before delivering the WindChange callback.
            t.receivesWind = info->numberOr("wind", 0) != 0 ||
                             info->numberOr("windgenerator", 0) != 0;
            t.transportLandEligible=info->numberOr("minwaterdepth",-10000)<0;
            t.landMaxWaterDepth=int16_t(int(info->numberOr("maxwaterdepth",10000)));
            t.landMinWaterDepth=int16_t(int(info->numberOr("minwaterdepth",-10000)));
            t.landMaxSlope=uint8_t(int(info->numberOr("maxslope",255)));
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
                t.landMaxWaterDepth=mci->second.retailMaxWaterDepth;
                t.landMinWaterDepth=mci->second.retailMinWaterDepth;
                t.landMaxSlope=mci->second.retailMaxSlope;
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
            {
                // 4c0501..4c054d: the two hover words, the distance unsigned and
                // the altitude signed, through x87 sqrt and _ftol.
                const uint16_t hover=uint16_t(t.hoverAttackDistance);
                const double altitude=double(int16_t(t.hoverAttackAltitude));
                const double reach=hover ? std::sqrt(double(hover)*hover+altitude*altitude) : 100.0;
                t.airToAirDistance=uint16_t(int32_t(info->numberOr("airtoairdistance",double(int32_t(reach)))));
            }
            // 4c082e..4c0837: read as an integer, stored as a word.
            t.attackRunLength = uint16_t(int32_t(info->numberOr("attackrunlength", 0)));
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
                wp.airToAirDistance = uint16_t(int32_t(w->numberOr("airtoairdistance", 0)));
                wp.reload = float(w->numberOr("reloadtime", 1));
                wp.switchReloadTicks = uint16_t(int32_t(w->numberOr(
                    "switchreloadtime", w->numberOr("reloadtime", 0))*30.0));
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
                wp.toAir = w->numberOr("toairweapon", 0) != 0;
                wp.commandFire = w->numberOr("commandfire", 0) != 0;
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
                    wp.damage = uint16_t(std::atoi(dmg->valueOr("default", "0").c_str()));
                    // Per-target-category damage (every DAMAGE key but `default`).
                    for (const auto& [k, v] : dmg->values)
                        if (k != "default") {
                            // Retail stores integer damage after a binary64 product
                            // (CRT precision53), before attack/armor modifiers.
                            volatile double amount = double(wp.damage) * std::atof(v.c_str());
                            wp.dmgVs[k] = int32_t(amount);
                        }
                }
                return wp;
            };
            t.hasPrimaryWeaponBlock = root.child("WEAPON1") != nullptr;
            for (int slot = 1; slot <= 3; ++slot) {
                const auto* w = root.child("WEAPON" + std::to_string(slot));
                if (!w) continue;
                t.classicFbi.weaponBlocks |= uint8_t(1u << (slot - 1));
                Weapon wp = parseWeapon(w);
                // 5307e9 truncates reloadtime*30 into a 16-bit tick count;
                // 52aae0 takes the maximum over all three slots, even when a
                // slot has no damage, then converts ticks to milliseconds.
                const auto reloadTicks=uint16_t(int32_t(w->numberOr("reloadtime",0)*30.0));
                t.maxWeaponReloadMs=std::max(t.maxWeaponReloadMs,int32_t(reloadTicks)*1000/30);
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
            if (const auto* sd = root.child("SELFDESTRUCTAS")) {
                t.selfDestructAs = parseWeapon(sd);
                t.hasSelfDestructAs = true;
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
            if (variant) {
                if (t.classicFbi.unitName != t.id) classicVariants_.try_emplace(t.classicFbi.unitName, std::move(t));
            } else types_[t.id] = std::move(t);
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
                t.instantCorpse=t.isStructure() && !t.corpse.empty() && retailInstantCorpse(*script);
                t.explosionCorpse=!t.corpse.empty() && retailExplosionCorpse(*script);
                const auto modelPath="objects3d/"+t.id+".3do";
                if(vfs.has(modelPath)) {
                    auto model=tdo::load(vfs.read(modelPath));
                    auto& currentType=t;
                    t.scriptPieceVertices.resize(script->pieces.size());
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
                        // A script piece names its first model object, whose
                        // chain retailPieceDrawnCenter poses (as retailPieceOrigin).
                        bool first=true;
                        for(int i=0;i<index;++i)first=first && productionModel[size_t(i)].scriptPiece!=piece;
                        if(piece>=0 && first) {
                            // Duplicates cannot move a bound, so keep each vertex once.
                            auto& vertices=currentType.scriptPieceVertices[size_t(piece)];
                            for(auto vertex:object.verticesRaw) {
                                for(int axis:{0,2})vertex[size_t(axis)]=std::bit_cast<int32_t>(0u-uint32_t(vertex[size_t(axis)]));
                                vertices.push_back(vertex);
                            }
                            std::sort(vertices.begin(),vertices.end());
                            vertices.erase(std::unique(vertices.begin(),vertices.end()),vertices.end());
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
    // Intern here rather than asking callers to remember: this rebuilds from scratch
    // on every loadDir call, so it is idempotent and cannot be left stale by a
    // second load.
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
    // Retail 0x4c1950: for every roster type with the builder flag, list
    // CanBuild\<builder>\*.tdf, keep each entry that names a roster type (0x515ac0
    // returns 0 for anything else and the entry is skipped), read [Menu] Priority
    // (default 9999999999 when absent) and sort ascending. The roster must
    // therefore be loaded (loadDir) before this. The VFS enumerates
    // <prefix>/<builder>/<buildable>.tdf recursively; group by the builder
    // subdirectory (the path segment right after `prefix`).
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
        const auto bt = types_.find(builder);
        if (bt == types_.end() || !bt->second.isBuilder) continue;
        std::string id = lower(fs::path(path).stem().string());
        if (!types_.count(id)) continue;
        double prio = 9999999999.0;
        try {
            auto bytes = vfs.read(path);
            auto root = tdf::parseText(std::string(bytes.begin(), bytes.end()), path);
            if (const auto* m = root.child("Menu")) prio = m->numberOr("priority", prio);
        } catch (const std::exception&) {}
        if (!byBuilder.count(builder)) order.push_back(builder);
        byBuilder[builder].push_back({prio, std::move(id)});
    }
    for (const std::string& builder : order) {
        auto entries = byBuilder[builder];
        // Retail bubble-sorts with a strict > swap: ascending priority, ties kept in
        // enumeration order (here the VFS's sorted listing).
        std::stable_sort(entries.begin(), entries.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        auto& list = buildTree_[builder];
        for (auto& [p, id] : entries)
            if (std::find(list.begin(), list.end(), id) == list.end())
                list.push_back(id);
        if (auto t = types_.find(builder); t != types_.end()) t->second.buildMenu = list;
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

// 5121d8: a new record takes a uniformly ranked FREE slot of its owner's pool,
// one CRT draw (none when the pool is full). Retirement 512ae0 frees the
// record (512e23 clears +0x130 bit 24, 512e52 drops the allocated count)
// right after it places the corpse feature (512ee0): a dying unit keeps its
// slot through the death animation, not through the corpse window, which is
// a map feature in retail. Here that is deadFor reaching corpseAnimationTicks,
// where the corpse is placed. Freed slots are reclaimed when the pool is next
// searched, which no other state observes in between.
void World::allocateEntitySlot(int unitId,int player) {
    auto& table=entitySlots_;
    auto& pool=table.unitAt[size_t(player)];
    if (pool.empty()) pool.assign(size_t(entityPoolCapacity(unitCap_)),-1);
    int allocated=0;
    for (auto& id:pool) {
        if (id<0) continue;
        const Unit* member=unit(id);
        if (member && member->deadFor<std::min(member->corpseAnimationTicks(),kRetiredTicks)) {
            ++allocated;continue;
        }
        if (size_t(id)<table.slotOf.size()) table.slotOf[size_t(id)]=-1;
        id=-1;
    }
    int slot=retailAllocateSlot(int(pool.size()),allocated,[&](int index){return pool[size_t(index)]>=0;},
        [&]{return crtRand(0x5121dd);});
    // Retail refuses the creation instead. Engine caps count live units only,
    // may exceed retail's 500 and an uncapped world has none, so a full pool
    // grows by the one slot this unit needs (no draw): a pool is only ever as
    // large as its player's most records held at once demands, so normal
    // games scan exactly retail's pool.
    if (slot<0) { slot=int(pool.size());pool.push_back(-1); }
    pool[size_t(slot)]=unitId;
    if (table.slotOf.size()<=size_t(unitId)) table.slotOf.resize(size_t(unitId)+1,-1);
    table.slotOf[size_t(unitId)]=slot;
}

void World::releaseEntitySlot(int unitId) {
    const int slot=entitySlots_.slot(unitId);
    if (slot<0) return;
    for (auto& pool:entitySlots_.unitAt)
        if (size_t(slot)<pool.size() && pool[size_t(slot)]==unitId) pool[size_t(slot)]=-1;
    entitySlots_.slotOf[size_t(unitId)]=-1;
}

void World::transferEntitySlot(Unit& u,int newPlayer) {
    if (retailAllocation_ || u.player==newPlayer || !validUnitOwner(newPlayer) || entitySlot(u.id)<0) return;
    // The old record's request goes with it (cancel resolves the old slot).
    paths_.cancel(u.id,NavigationTelemetry::Cancel::Replaced);
    releaseEntitySlot(u.id);
    allocateEntitySlot(u.id,newPlayer);
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
    if (!validUnitOwner(player)) {
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
    } else {
        u.id=nextId_++;
        allocateEntitySlot(u.id,player);
    }
    players_[size_t(player)].unitCount++;   // keep the cap count exact within a tick
    players_[size_t(player)].built++;        // end-of-game "Units" column
    u.player = player;
    if (type && type->canSetStance && players_[size_t(player)].defensiveAi) {
        u.moveState=0;u.fireState=2;u.stance=1;u.standingOrder=1;
    }
    u.type = type;
    if (type) if (auto it = scenarioTypeStats_.find(type->id); it != scenarioTypeStats_.end()) {
        u.scenarioArmor = float(it->second[0]) * 0.01f;
        u.scenarioWeapon = float(it->second[1]) * 0.01f;
        u.veteran = type->noVeteran ? 0 : std::clamp(it->second[2], 0, 10);
        u.xp = u.veteran;
    }
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
    if(unitSlotById_.size()<=size_t(u.id))unitSlotById_.resize(size_t(u.id)+1,-1);
    unitSlotById_[size_t(u.id)]=int32_t(units_.size()-1);
    ++spawnGeneration_;
    bodyIndexValid_=false;
    if (type && type->script()) {
        auto [it,inserted]=unitScripts_.try_emplace(u.id,*type->script());
        (void)inserted;
        if (unitScriptById_.size()<=size_t(u.id)) unitScriptById_.resize(size_t(u.id)+1,nullptr);
        unitScriptById_[size_t(u.id)]=&it->second;
        if (scriptYardById_.size()<=size_t(u.id)) scriptYardById_.resize(size_t(u.id)+1,0);
        scriptYardById_[size_t(u.id)]=it->second.yardOpen;
        it->second.state().vm.start(*type->script(),type->script()->scriptIndex("Create"));
        it->second.state().startArguments(*type->script(),type->script()->scriptIndex("SetMaxReloadTime"),
                                       {uint32_t(type->maxWeaponReloadMs),0,0,0},1);
    }
    groundStamp(*unit(u.id));   // 5122d9: creation places the body
    return u.id;
}

Unit* World::unit(int id) {
    if(id<=0)return nullptr;
    if(size_t(id)<unitSlotById_.size()) {
        const int32_t slot=unitSlotById_[size_t(id)];
        if(slot>=0 && size_t(slot)<units_.size() && units_[size_t(slot)].id==id)
            return &units_[size_t(slot)];
        if(slot<0 && retiredOwners_.contains(id))return nullptr;
    }
    // Restored retail fixtures can reorder or author IDs directly. Repair only
    // that lookup; normal spawn/compaction paths maintain the table eagerly.
    TAK_PASS();
    for(size_t i=0;i<units_.size();++i)if(units_[i].id==id) {
        if(unitSlotById_.size()<=size_t(id))unitSlotById_.resize(size_t(id)+1,-1);
        unitSlotById_[size_t(id)]=int32_t(i);return &units_[i];
    }
    return nullptr;
}

Unit* World::unitNoRepair(int id) {
    if(id<=0)return nullptr;
    if(size_t(id)<unitSlotById_.size()) {
        const int32_t slot=unitSlotById_[size_t(id)];
        if(slot>=0 && size_t(slot)<units_.size() && units_[size_t(slot)].id==id)
            return &units_[size_t(slot)];
        if(slot<0 && retiredOwners_.contains(id))return nullptr;
    }
    for(size_t i=0;i<units_.size();++i)if(units_[i].id==id)return &units_[i];
    return nullptr;
}

int World::creditedOwner(int id) const {
    if(const auto* u=unit(id);u && u->type)return u->player;
    const auto it=retiredOwners_.find(id);
    return it==retiredOwners_.end() ? -1 : it->second;
}

void World::compactRetiredUnits() {
    // Keep native restored-pool allocation fixtures on their existing lifecycle.
    // Ordinary matches compact once a second, after scripts consume death events.
    if(retailAllocation_ || tickCounter_%30)return;
    // In-flight attacks still consume the original source's veteran/aura stats
    // and location. Keep those few records until every delayed effect finishes.
    std::unordered_set<int> referenced;
    for(const auto& p:projectiles_) {referenced.insert(p.fromId);referenced.insert(p.targetId);}
    for(const auto& f:flames_)referenced.insert(f.fromId);
    for(const auto& e:pendingEffects_)referenced.insert(e.fromId);
    for(const auto& storm:storms_)referenced.insert(storm.fromId);
    for(const auto& blast:deathBlasts_)referenced.insert(blast.fromId);
    size_t write=0;
    TAK_PASS();
    for(size_t read=0;read<units_.size();++read) {
        auto& u=units_[read];
        if(!u.alive() && u.deadFor>=kRetiredTicks+30 && u.corpseUntil==0 && !u.viewSight.active &&
           !corpseFootprints_.contains(u.id) && !unitScripts_.contains(u.id) && !referenced.contains(u.id)) {
            const int owner=u.type ? u.player : -1;
            retiredOwners_.emplace(u.id,owner);
            // Immutable attribution history is folded once, not rescanned every hash.
            if(!retiredHash_)retiredHash_=1469598103934665603ULL;
            retiredHash_^=uint32_t(u.id);retiredHash_*=1099511628211ULL;
            retiredHash_^=uint32_t(owner);retiredHash_*=1099511628211ULL;
            if(size_t(u.id)<unitSlotById_.size())unitSlotById_[size_t(u.id)]=-1;
            continue;
        }
        if(write!=read) {TAK_PROBE(++probe::tl.compactMoved);units_[write]=std::move(u);}
        const int id=units_[write].id;
        if(unitSlotById_.size()<=size_t(id))unitSlotById_.resize(size_t(id)+1,-1);
        unitSlotById_[size_t(id)]=int32_t(write++);
    }
    if(write==units_.size())return;
    units_.resize(write);
    corpseIdx_.clear();bodyIndexValid_=false;
    rebuildGridAndOccupancy();
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
    u.speed=fxMin(retailGroupMaximum_>Fixed() ? retailGroundPitchCap(retailGroupMaximum_,u.groundPitch)
                  : retailGroundSpeedCap(u.baseSpeed*multiplier,u.groundPitch,u.groundSpeedMode),
        fxMax(Fixed(),u.speed-u.type->brake*multiplier));
    if (facing && u.speed==Fixed()) {
        const int32_t diff=retailTurnRequest(*facing,u.heading);
        // 4d91b0 scales the unsigned 16-bit pivot rate, truncates toward zero,
        // then retains its low word before clamping the signed request.
        const int32_t rate=uint16_t(int64_t(uint16_t(u.type->turnInPlaceRate))*multiplier.v/65536);
        u.heading=u.heading+Bam(std::clamp(diff,-rate,rate));
        u.turnReqBam=diff;
        // 4d9ad0 asked 4d91b0 for no turn before braking: TurnDirection hears both.
        if (auto* script=unitScript(u.id)) script->turnInPlace=true;
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
    Fixed effectiveMaximum=maximum*multiplier;
    const Fixed delta=retailGroundAcceleration(u.x,u.z,start,end,next,u.heading,u.speed,
        effectiveMaximum,u.type->accel*multiplier,u.type->brake*multiplier,uint16_t(rate),
        u.groundMovementMode==0 ? &aim : nullptr,u.groundMovementMode==0 ? direction : -1);
    // 4d95f0: a paced group member's capped maximum replaces the speed-mode
    // scaled one (retail skips the mode factor there); pitch still applies.
    u.speed=fxMin(retailGroupMaximum_>Fixed() ? retailGroundPitchCap(retailGroupMaximum_,u.groundPitch)
                  : retailGroundSpeedCap(effectiveMaximum,u.groundPitch,u.groundSpeedMode),
                  fxMax(Fixed(),u.speed+delta));
    return retailGroundStep(u.heading,u.speed);
}

void World::commitGroundStep(Unit& u,Fixed dx,Fixed dz,bool strictDiagonal) {
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
    if(clear&&strictDiagonal&&nx!=ox&&nz!=oz) {
        // A corner's destination alone is insufficient for a rectangular body.
        // Locally planned detours prove both side footprints. Native routes
        // retain native placement rules: applying this extra constraint to
        // them would repeatedly reject corners their own solver permits.
        if(!mapPlacementCells_.empty())clear=mobilePlacement(u,nx,oz,false)&&mobilePlacement(u,ox,nz,false);
        else {
            const auto& grid=navFor(u.type);const int foot=std::clamp(std::max(fx,fz),1,15);
            clear=grid.fits(footprintCell(px,foot),footprintCell(u.z,foot),foot)&&
                  grid.fits(footprintCell(u.x,foot),footprintCell(pz,foot),foot)&&
                  cellFree(px,u.z,u.id,foot)&&cellFree(u.x,pz,u.id,foot);
        }
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
                if (x>=0 && z>=0 && x<occW_ && z<occH_) setOccupant(size_t(z)*occW_+x,u.id);
        }
        // 4dafd2..4db001: unstamp, commit the new cell, stamp.
        groundUnstamp(u);
        u.x=px;u.z=pz;
        groundStamp(u);
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

bool World::mobilePlacement(const UnitType& type,uint16_t self,int x,int z,bool allowMoving) const {
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
        type.maxSlope,type.maxWaterSlope,self,allowMoving,1,cellAt,
        [](uint16_t){return 0x20u;},[&](uint16_t id) {
            const auto* u=unit(id);
            return RetailPlacementEntity{u!=nullptr,u && u->alive(),u && !u->type->isStructure(),id};
        });
}

void World::setMapPlacementFeatures(const std::vector<uint16_t>& raw,
                                    std::vector<RetailMapFeatureType> types) {
    mapPlacementTypes_=std::move(types);
    ++placementEpoch_;placementDirtyAll_=true;placementDirty_.clear();
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
    // 495360/496380: a feature a later overlap removed (or that was refused)
    // has no state anywhere once the map has loaded. Snapshot the survivors
    // before the boundary pass (which marks only empty cells and tails), so
    // matchsetup and a late-loading viewer register exactly these.
    installedMapFeatures_=retailSurvivingMapFeatures(hW_,hH_,raw,cell);
    if (hW_>=2 && hH_>=16)
        retailMapBoundary(hW_,hH_,hH_*16,seaLevel_,false,cell);
}

void World::setTerrain(const std::vector<uint8_t>& heights, int w, int h, int seaLevel,
                       const std::vector<uint16_t>* features) {
    mapPlacementCells_.clear();mapPlacementTypes_.clear();corpseFootprints_.clear();corpseAnchors_.clear();
    installedMapFeatures_.clear();
    ++placementEpoch_;
    paths_.clear();legion_.reset();
    searchGrades_.clear(); activeSearchGrade_=-1;
    seaLevel_ = seaLevel;
    noSeaLevelTrigger_ = false;
    heights_ = heights;
    hW_ = w; hH_ = h;
    airOccupancy_.reset(w,h);
    groundRebuild();
    for (auto& u:units_) u.airOccupant={};
    explorationHeights_=retailExplorationHeights(w,h,uint8_t(seaLevel),
        [&](int x,int z){return heights_[size_t(z)*w+x];});
    navigationExplored_.assign(explorationHeights_.size(),0);
    restoredNavigationViewer_=-1;
    for (auto& u:units_) { u.sightFootprint={};u.viewSight={};u.viewPlayer=-1; }
    viewCounts_.clear();deathSights_.clear();
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
            // must admit it -- the same diagonal rule the movers enforce, so a
            // shortcut cannot squeeze through a corner the unit would be stopped by.
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
        o.chaseLeashed = tmpl.chaseLeashed; o.chaseLeash = tmpl.chaseLeash; o.chaseOrigin = tmpl.chaseOrigin;
        o.issuedTick = tmpl.issuedTick;
        o.setIdentity(tmpl.identity());
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
            o.productionExit = tmpl.productionExit;
            o.groundResponse = tmpl.groundResponse;
            o.formationLevel = tmpl.formationLevel;
            o.reclaimAreaApproach = tmpl.reclaimAreaApproach;
            o.controller = tmpl.controller;
            o.park = tmpl.park;
            o.buildType = tmpl.buildType;
            // Work orders remain jobs when a route replaces their approach.
            // Otherwise the new final waypoint becomes an ordinary move and
            // reclaim completion can consume the next queued obstacle instead.
            o.reclaimFeat = tmpl.reclaimFeat;
            o.reclaimCountdown = tmpl.reclaimCountdown; o.reclaimHeading = tmpl.reclaimHeading;
            o.repairTarget = tmpl.repairTarget;
            o.repairLooks = tmpl.repairLooks;
            o.helpSite = tmpl.helpSite;
            o.patrolRepair = tmpl.patrolRepair;
            o.guardRepair = tmpl.guardRepair;
            o.patrolOpen = tmpl.patrolOpen;
            o.buildRectangle = tmpl.buildRectangle;
            o.buildX = tmpl.buildX; o.buildZ = tmpl.buildZ;
        }
        next.push_back(o);
    }
    next.insert(next.end(), u.orders.begin() + long(end) + 1, u.orders.end());
    u.orders.swap(next);

}

void World::noteOrders(int unitId) {
    if (legion_) legion_->ordersChanged(unitId);
}

namespace {
// Reports a unit's orders to Legion when an order helper returns (every
// return path), after the helper's own changes (World::noteOrders).
struct OrdersNote {
    World& w; int id;
    ~OrdersNote() { w.noteOrders(id); }
};
} // namespace

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
    noteOrders(u.id);
}

// Production buildings and infinitely producing mobile builders hold rally orders:
// the building cannot go anywhere itself, but the units it makes can. Returns true
// when the order was consumed as a rally, so the caller stops there.
//
// The orders are stored raw and never enter the mover -- see Unit::rally. `queue`
// appends, so a player can lay out "move here, then fight-move there, then patrol"
// and every unit off the line follows the whole plan.
bool World::setRally(Unit& u, const Order& o, bool queue) {
    if (!u.type || (!u.type->producesUnits() && !u.repeatType)) return false;
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

// One convoy per click (sim/convoy.h). Legion only; the point is the order's
// own, offset or not, and the test that applies to it follows the shared
// predicate the client used to build it.
uint32_t World::joinConvoy(const Unit& u, ConvoyClass cls, Fixed x, Fixed z) {
    return joinArea(u.player, cls, x, z, legionSharedClick(u.type->canFly,u.type->footX,u.type->footZ), ConvoyTable::kNone);
}

uint32_t World::joinArea(int player, ConvoyClass cls, Fixed x, Fixed z, bool shared, uint32_t key) {
#ifndef NDEBUG
    // TAK_LEGION_VERIFY: the indexed lookup picks the convoy a scan of the
    // whole table picks.
    const ConvoyTable::Convoy* brute=nullptr;
    const bool verify=LegionNavigator::verifying();
    if(verify) {convoys_.prune(tickCounter_);brute=convoys_.bruteMatch(player,cls,x.v,z.v,shared,tickCounter_);}
    const uint32_t bruteId=brute?brute->id:0;
    const size_t open=convoys_.size();
#endif
    const auto& convoy=convoys_.join(player,cls,x.v,z.v,shared,tickCounter_,key);
#ifndef NDEBUG
    if(verify&&bruteId&&convoy.id!=bruteId) {
        std::fprintf(stderr,"TAK_LEGION_VERIFY tick %u: convoy index picked %u, a scan picks %u\n",
                     tickCounter_,convoy.id,bruteId);
        std::abort();
    }
    if(verify&&!bruteId&&convoys_.size()==open) {
        std::fprintf(stderr,"TAK_LEGION_VERIFY tick %u: convoy index joined %u, a scan opens one\n",tickCounter_,convoy.id);
        std::abort();
    }
#endif
    return convoy.key;
}

void World::beginOrder(uint32_t serial, int player, bool point, Fixed x, Fixed z) {
    activeOrder_ = ActiveOrder{};
    activeOrder_->serial = serial;
    activeOrder_->player = player;
    activeOrder_->point = point;
    activeOrder_->x = x;
    activeOrder_->z = z;
}

// An explicit order's members share its identity: the order joins the area
// table once per class, as one shared order at its own click, on the first leg
// of that class any member gets. Only the member being applied (orderMember)
// gets it, and only for an order with a click point; anything else -- a leg the
// command gives another unit, an Assist's approach move toward its site -- joins
// per unit, as before 267. (A changed owner is refused before this by
// applyCommand's ownership check.)
OrderIdentity World::stampIdentity(const Unit& u, ConvoyClass cls, Fixed x, Fixed z) {
    if (!activeOrder_ || !activeOrder_->point || activeOrder_->member != u.id || activeOrder_->player != u.player)
        return {joinConvoy(u, cls, x, z), 0};
    uint32_t& area = activeOrder_->area[size_t(cls)];
    if (area == ConvoyTable::kNone)
        area = joinArea(u.player, cls, activeOrder_->x, activeOrder_->z, true, kExplicitArea | activeOrder_->serial);
    return {area, activeOrder_->serial};
}

void World::order(int unitId, float x, float z, bool queue, ConvoyClass cls, std::optional<OrderIdentity> identityCopy) {
    OrdersNote note{*this,unitId};
    Unit* u = unit(unitId);
    // isStructure(), NOT canMove: the Keep and both Taros/Veruna walls declare
    // canmove=1 with no velocity (the CLAUDE.md gotcha). Gating on canMove let a
    // BUILDING accept a move order -- it could not go anywhere, but the mover still
    // turned its heading toward the goal, so you could spin a keep by right-clicking.
    if (!u || !u->alive() || !u->type) return;
    if (u->type->isStructure() || u->repeatType) { setRally(*u, Order{Fixed::fromFloat(x), Fixed::fromFloat(z), 0}, queue); return; }
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
        if (isLegionPathfinding(pathfindingMode_)) {
            Order& o = u->orders.back();
            o.setIdentity(identityCopy ? *identityCopy : stampIdentity(*u, cls, o.x, o.z));
        }
    };
    if (u->type->canFly) {
        u->orders.push_back({Fixed::fromFloat(x), Fixed::fromFloat(z), 0});
        u->orders.back().flightMoveMission = true;
        u->orders.back().mission.flags = 0x3000400u;
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
    static const bool kPqLog = tak::devEnv("TAK_PATHLOG") != nullptr;
    if (kPqLog) std::printf("[pq] t=%u id=%d CANCEL\n", tickCounter_, u.id);
    paths_.cancel(u.id);
    if(legion_)legion_->cancel(u.id);
    // A new destination earns the cheap tracer again. This deliberately does NOT live in
    // requestPath: that is also the once-a-second re-anchor, so clearing there wiped the
    // detour history on every re-ask and the fallback could never accumulate -- the
    // trigger fired and was forgotten within the same second, and the benchmark showed
    // not a single number moving.
}

Order* World::groundMissionOrder(Unit& u, bool includeCombatMoves) {
    if (!u.type || u.type->canFly || u.orders.empty()) return nullptr;
    const auto plain = [includeCombatMoves](const Order& o) {
        return !o.targetId && (includeCombatMoves || (!o.attackMove && !o.patrol)) && !o.guard && !o.load &&
               (!o.unload || o.transportUnloadApproach) &&
               !o.buildType && !o.reclaimFeat && !o.reclaimArea && !o.manaBuildArea &&
               !o.repairTarget && !o.wait && !o.waitAttack;
    };
    if (!plain(u.orders.front())) return nullptr;
    Order& goal = u.orders[currentLeg(u.orders)];
    return goal.groundMission && plain(goal) ? &goal : nullptr;
}

Order* World::navigationMissionOrder(Unit& u) {
    if (auto* goal=groundMissionOrder(u,true)) return goal;
    if (!u.type || u.type->canFly || u.orders.empty()) return nullptr;
    auto& goal=u.orders[currentLeg(u.orders)];
    return goal.buildRectangle || goal.load ? &goal : nullptr;
}

bool World::groundMissionAccepts(const Unit& u,const Order& goal) {
    if(goal.productionExit && u.bodyBlockStreak>=2) {
        const int64_t dx=int64_t(u.x.floorInt())-goal.productionExit->first.floorInt();
        const int64_t dz=int64_t(u.z.floorInt())-goal.productionExit->second.floorInt();
        // The planned point includes extra tolerance for the Retail navigator.
        // A blocked automatic Legion exit can finish once its entire birth
        // footprint is clear, whether or not a rally follows. Its parking spot
        // was generated, not a precise player destination.
        const int xClear=u.type->footX*16;
        const int zClear=u.type->footZ*16;
        if(std::abs(dx)>=xClear || std::abs(dz)>=zClear)return true;
    }
    const int radius=std::bit_cast<int32_t>(goal.missionRadius+4u);
    const RetailCircleGoal circle{footprintOrigin(goal.missionTarget ? goal.missionTarget->first : goal.x,u.type->footX),
        footprintOrigin(goal.missionTarget ? goal.missionTarget->second : goal.z,u.type->footZ),radius,retailCircleRadiusSquared(radius)};
    // A Legion spread PARK (legionParkSpot) arrives at its own spot only, in
    // the band or beyond it: arriving anywhere in the band would stop a body
    // in the door lane or on a spot another output has claimed.
    if (goal.park && goal.park->spot) {
        const int dx=footprintOrigin(u.x,u.type->footX)-goal.park->spot->first;
        const int dz=footprintOrigin(u.z,u.type->footZ)-goal.park->spot->second;
        return std::abs(dx)<=1 && std::abs(dz)<=1;
    }
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
    if (!u.type || !u.type->canFly || u.orders.empty()) return false;
    const auto& order=u.orders.front();
    if (order.targetId || order.buildType || order.buildRectangle || order.reclaimFeat ||
        order.repairTarget || u.retailBuild) return false;
    // Ordinary VTOL_Move is shared by armed aircraft, flying builders and
    // transports. The patrol handler below only covers plain patrols.
    return order.flightMoveMission ||
        (order.patrol && !u.type->isBuilder && u.type->weapons.empty());
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
            const RetailGroupRecord* group=w.retailGroupOf(u);
            if (u.orders.front().flightMoveMission && u.orders.front().formationLevel) {
                // 417750 VTOL_Move_Formation: fly to the group centre until
                // back inside the level's radius.
                auto& order=u.orders.front();
                const int level=order.formationLevel;
                const bool all=(m.flags&0x4000000u)!=0;
                const auto member=w.retailGroupMember(u);
                return retailFlightFormation(m,w.tickCounter_,level,u.type->canFly,
                    [&] { return group && (level>0 ? retailGroupOutOfSlot(*group,member,false,level,all)
                                                   : retailGroupOutOfSlot(*group,member,true,-level,all)); },
                    [&] {
                        const auto c=retailGroupCentre(*group,member,all,level<=0);
                        order.x=Fixed::fromInt(int16_t(c.x));order.z=Fixed::fromInt(int16_t(c.z));
                    },
                    [&] {
                        // 416c50, as VTOL_Move's stage 0.
                        if (auto script=w.unitScripts_.find(u.id);script!=w.unitScripts_.end() && !script->second.activated) {
                            script->second.activated=true;w.notifyUnitScript(u,"Activate");
                        }
                        w.notifyUnitScript(u,"BeginFlight");
                    },
                    [&] {
                        order.x=footprintWaypoint(footprintCell(order.x,u.type->footX),u.type->footX);
                        order.z=footprintWaypoint(footprintCell(order.z,u.type->footZ),u.type->footZ);
                        order.flightGoal=RetailFlightGoal{{order.x.v,u.flightY.v,order.z.v},0x30,0,16};
                    },
                    [&](int n) { return random(n); });
            }
            if (u.orders.front().flightMoveMission && group) {
                // 417e02..418052: VTOL_Move runs Move_Ground's two group
                // checks (402b00) before its stages. As on the ground, the
                // y terms are left at zero (docs/pathfinding-port.md).
                auto& order=u.orders.front();
                const int check=retailGroundGroupCheck(*group,w.retailGroupMember(u),m.flags,{u.x.v,0,u.z.v},
                    {order.x.v,0,order.z.v},[&](int n){return int(random(n));});
                if (check==1) {
                    // Ahead again: 50c480, then 4d6a50(unit,0) drops every mission.
                    w.leaveRetailGroupCentre(u);
                    u.orders.clear();
                    return 2;
                }
                if (check) {
                    Order formation;
                    formation.x=u.x;formation.z=u.z;formation.goal=formation.flightMoveMission=true;
                    formation.formationLevel=int8_t(check);formation.mission.flags=0x1000000u;
                    formation.issuedTick=w.tickCounter_;
                    // The parent restarts at stage 0 when the formation retires;
                    // inserting invalidates m, so set it first.
                    m.stage=0;
                    u.orders.insert(u.orders.begin(),formation);
                    return 4;
                }
            }
            if ((m.flags&0x8000000u) && random(10)==0) m.flags&=~0x8000000u;
            if (u.orders.front().flightMoveMission) {
                auto& order=u.orders.front();
                if (m.stage==0) {
                    if (order.park && !order.park->attempts) w.resolveFlyerPark(u,order);
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
                if (m.stage==3) {
                    if (u.orders.size()>1) return 5;
                    // 41865c: the final move refines its coarse arrival with
                    // an ordinary precise point controller before retiring.
                    order.flightGoal=RetailFlightGoal{{order.x.v,u.flightY.v,order.z.v}};
                    m.pending&=~0x700u;
                    m.waitMask=0x700;
                    return 1;
                }
                if (m.stage==4) {
                    u.retainedFlightGoal=order.flightGoal;
                    u.retainedFlightControllerActive=false;
                    return 5;
                }
                return 7;
            }
            if (m.stage==0) {
                const bool anchored=std::any_of(u.orders.begin(),u.orders.end(),[](const Order& o) {return (o.mission.flags&0x4000u)!=0;});
                if (!anchored) {
                    Order anchor;anchor.x=u.x;anchor.z=u.z;anchor.goal=anchor.patrol=true;
                    anchor.mission.flags=0x1000412u;anchor.issuedTick=w.tickCounter_;
                    // A return leg: it belongs to the outbound leg's convoy (and order).
                    anchor.setIdentity(u.orders.front().identity());
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
    noteOrders(u.id);
}

void World::openFlightPatrol(Unit& u) {
    const size_t current=currentLeg(u.orders);
    if (current>=u.orders.size() || !u.orders[current].patrol || !u.orders[current].patrolOpen) return;
    // As the ground host's initializer: a patrol leg that starts with no
    // closed patrol leg behind it adds the return point where the flyer stands.
    u.orders[current].patrolOpen=false;
    bool loop=false;
    for (size_t i=current+1;i<u.orders.size();++i)
        loop|=u.orders[i].goal && u.orders[i].patrol && !u.orders[i].patrolOpen;
    if (!loop) patrolTo(u.id,u.x.toFloat(),u.z.toFloat(),true,u.orders[current].identity());
}

// World holds a flyer in place by skipping its flight body, so the flyer does
// not move -- and a retail mover's velocity is what moves it (tickFlightBody
// keeps speed as its length). Every hold stops both: shooters lead a target by
// its velocity (51aa50), the COB speed queries and MoveRate read it, and the
// flight step that ends the hold starts from it.
static void holdFlight(Unit& u) { u.flightVelocity={}; u.speed=Fixed(); }

void World::tickFlightMovement(Unit& u, bool persistent) {
    auto& order=u.orders.front();
    // A guard VTOL_Follow runs (tickFlyingGuard) flies its orbit controller,
    // or before stage 0 the one back onto the map (waiting on its events).
    // A guard it has not started (a builder at work looks for none) holds
    // near the guarded unit.
    const bool orbit=order.guard && (order.mission.stage==2 || order.mission.waitMask);
    if (order.guard && !orbit) {
        const auto* target=unit(order.targetId);
        if (target && fxLen(target->x-u.x,target->z-u.z)<=Fixed::fromInt(70)) { holdFlight(u); return; }
    }
    if (u.flightGroundMode!=2) { u.flightGroundMode=2;groundMove(u); }   // 51b3b0: taking off unstamps
    if (orbit && !order.flightGoal) {
        // Its controller went with a job (4d4d40(0)): the mover flies on to
        // the navigator's last destination until the next look installs one.
        tickFlightBody(u);
        return;
    }
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
    if (!order.patrol && !order.unload && !pickupDeparture && !orbit) {
        goal.point.x=order.x.v; goal.point.z=order.z.v;
    }
    // Legion: travel with a mixed formation's ground members.
    if (const auto* station=orbit ? nullptr : legionFlightStation(u)) {
        if (station->steer) { goal.point.x=station->x.v; goal.point.z=station->z.v; }
        if (station->hold) persistent=true;
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
    if (orbit) {
        // 524c10 posts arrival to the guard and the navigator lets the
        // controller go (0x400); the body flies on until the look (woken by
        // them) installs the next point.
        if (goal.accepts(position)) order.mission.pending|=0x500;
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
        // A builder at its build, reclaim or repair point holds there.
        if (!flyingBuild && (order.buildType || order.reclaimFeat || order.repairTarget)) { holdFlight(u); return; }
        // Point controllers used by both move and patrol are nonpersistent:
        // arrival posts 0x100 and releasing the controller posts 0x400.
        if (flyingBuild) u.retailBuild->mission.pending|=0x500;
        else if (plainFlightPatrol(u)) order.mission.pending|=0x500;
        else {
            Order done=order;
            // The point controller retires on arrival before the native mover
            // commits this tick. Keep its accepted destination for the final
            // body step and the ensuing coast-down, as the independent mover
            // continues after controller detach.
            if (!done.patrol) {
                u.retainedFlightGoal=goal;
                u.retainedFlightControllerActive=false;
            }
            u.orders.erase(u.orders.begin());
            if (done.patrol) {
                u.retainedFlightGoal.reset();
                done.flightGoal.reset(); u.orders.push_back(done);
            }
            tickFlightBody(u);
            return;
        }
    }
    tickFlightBody(u);
}

void World::tickRetainedFlightMovement(Unit& u) {
    if (!u.retainedFlightGoal) return;
    auto& goal=*u.retainedFlightGoal;
    if (u.flightGroundMode!=2) { u.flightGroundMode=2;groundMove(u); }   // 51b3b0: taking off unstamps
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
    Fixed maximum=u.baseSpeed;
    // Legion formation flyer: the ground's pace (see planLegionFlightStations).
    const auto* station=legionFlightStation(u);
    if (station && station->capped) maximum=fxMin(maximum,station->cap);
    const uint16_t heading=portHeadingToRetail(u.heading);
    const auto previousVelocity=u.flightVelocity;
    u.flightVelocity=retailFlightVelocity(u.flightVelocity,position,
        u.flightNavigation.destination,u.flightNavigation.velocity,u.speed.v,std::max(1,maximum.v),
        u.type->accel.v,u.type->brake.v,heading);
    // W6 (Legion, deliberate): an escort with no way on over the ground --
    // climbing straight up from the ground at the start of a leg, or held
    // still by the ground's pace -- does not yaw on the spot toward its lead
    // point; it turns once it moves (mixed2 leg 2: 16 flyers yawing through
    // their vertical take-off read as ~110 spins).
    const bool parked=station && u.flightVelocity.x==0 && u.flightVelocity.z==0;
    const int diff=parked ? 0 : int16_t(uint16_t(u.flightNavigation.heading-heading));
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
    u.speed=Fixed::raw(truncSat32(std::sqrt((double(v.z)*v.z+double(v.y)*v.y)+double(v.x)*v.x)));
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

// 509400, ported exactly. Every footprint cell is tested only when the
// coarse exploration word at the footprint lacks the lander's player bit:
// retail accepts an unexplored site with no terrain or occupancy test at all.
// Word +0 is World's ground body plane (searchBodyRect), word +2 the persistent
// secondary grid airOccupancy_, +0xd bit 0x10 any non-'.' structure yard cell.
bool World::flightLandingFree(const Unit& self,Fixed x,Fixed z) const {
    const auto& type=*self.type;
    const RetailLandingLimits limits{int16_t(type.footX),int16_t(type.footZ),type.landMaxWaterDepth,
        type.landMinWaterDepth,type.landMaxSlope,type.canFly,type.floater,uint8_t(self.player)};
    const int fx=type.footX,fz=type.footZ;
    const int x0=int16_t(std::bit_cast<int32_t>(uint32_t(x.v)-(uint32_t(fx)<<19)+0x80000u)>>20);
    const int z0=int16_t(std::bit_cast<int32_t>(uint32_t(z.v)-(uint32_t(fz)<<19)+0x80000u)>>20);
    struct Host {
        const World& w; const Unit& self; int x0,z0,fx,fz;
        std::optional<SearchBodyRect> bodies;
        std::vector<uint8_t> yards;
        int width() const { return w.hW_; }
        int height() const { return w.hH_; }
        uint16_t exploration(size_t coarse) const {
            if (w.navigationExplored_.empty()) return 0xffff;
            return coarse<w.navigationExplored_.size() ? w.navigationExplored_[coarse] : 0;
        }
        bool mapCells() const { return w.mapPlacementCells_.size()==size_t(w.hW_)*w.hH_ && w.hW_>0; }
        uint16_t feature(size_t cell) const { return mapCells() && cell<w.mapPlacementCells_.size() ? w.mapPlacementCells_[cell].feature : 0xffff; }
        int featureBack(size_t cell) const {
            const auto& c=w.mapPlacementCells_[cell];
            return int(c.backZ)*w.hW_+c.backX;
        }
        int featureCount() const { return int(w.mapPlacementTypes_.size()); }
        bool featureBlocking(int index) const {
            return size_t(index)>=w.mapPlacementTypes_.size() || w.mapPlacementTypes_[size_t(index)].blocking;
        }
        size_t local(size_t cell) const {
            return size_t(int(cell/size_t(w.hW_))-z0)*fx+size_t(int(cell%size_t(w.hW_))-x0);
        }
        bool yard(size_t cell) {
            if (yards.empty()) {
                // 5066f0 stamps +0xd bit 0x10 for every yard byte with bit 0:
                // the decoded characters C c O o S w f y Y (4c0f59 table).
                // An unparsed (empty) yardmap is a repeated blocking character.
                yards.assign(size_t(fx)*fz,2);
                auto stamp=[&](const Unit& u) {
                    if (!u.alive() || u.embarked() || !u.type || !u.type->isStructure()) return;
                    const auto& t=*u.type;
                    const int ux=footprintOrigin(u.x,t.footX),uz=footprintOrigin(u.z,t.footZ);
                    for (int cz=std::max(z0,uz);cz<std::min(z0+fz,uz+t.footZ);++cz)
                        for (int cx=std::max(x0,ux);cx<std::min(x0+fx,ux+t.footX);++cx) {
                            const char c=t.yardMap.empty() ? 'o' : t.yardMap[size_t(cz-uz)*t.footX+cx-ux];
                            if (std::strchr("CcOoSwfyY",c) && c) yards[size_t(cz-z0)*fx+cx-x0]=1;
                        }
                };
                if (w.bodyIndexEnabled_) {
                    if (!w.bodyIndexValid_) w.rebuildBodyIndex();
                    for (int tz=z0/8;tz<(z0+fz+7)/8;++tz)
                        for (int tx=x0/8;tx<(x0+fx+7)/8;++tx)
                            for (int index:w.bodyTiles_[size_t(tz)*w.bodyTilesW_+tx]) stamp(w.units_[size_t(index)]);
                } else for (const auto& u:w.units_) stamp(u);
            }
            return yards[local(cell)]==1;
        }
        bool groundOther(size_t cell) {
            if (!bodies) bodies=w.searchBodyRect(x0,z0,fx,fz);
            const Unit* body=bodies->cells[local(cell)];
            return body && body!=&self;
        }
        bool airOther(size_t cell) const {
            if (w.airOccupancy_.empty() || cell>=w.airOccupancy_.cells().size()) return false;
            const int id=w.airOccupancy_.cells()[cell];
            if (id<=0 || id==self.id) return false;
            const Unit* other=w.unit(id);
            return other && other->alive();
        }
        int low(size_t cell) const { return w.cellHeightRange(cell).first; }
        int high(size_t cell) const { return w.cellHeightRange(cell).second; }
        int sea() const { return uint8_t(w.seaLevel_); }
    } host{*this,self,x0,z0,fx,fz,{},{}};
    if (!retailLandingSiteFree(x.v,z.v,limits,host)) return false;
    // Legion difference (deliberate, not retail): never touch down on another
    // flyer. Retail lets a stack of flyers land on one spot: a descending
    // flyer stays airborne in word +2, where a shared cell's owner is redrawn
    // every tick and eight or more overlapping flyers leave only 0xffff,
    // both of which 509400 treats as free; an unexplored site skips every
    // test. Legion refuses any footprint that overlaps a landed flyer or one
    // that has already begun its descent (stage 3), whatever the exploration.
    // Both states are the units' own hashed state and the test runs in unit
    // order, so simultaneous descents serialize: the later lander sees the
    // earlier one's descent in the same tick. It refuses a mobile ground
    // unit's footprint too: 509400 sees those, moving or not, only at an
    // explored site (tools/re/check_landing_moving_ground.py).
    if (isLegionPathfinding(pathfindingMode_) && flyerLandingOccupied(self,x0,z0,fx,fz)) return false;
    return true;
}

bool World::flyerLandingOccupied(const Unit& self,int x0,int z0,int fx,int fz) const {
    auto blocks=[&](const Unit& o,int ox,int oz) {
        if (&o==&self || !o.alive() || o.embarked() || !o.type) return false;
        if (ox>=x0+fx || oz>=z0+fz || ox+o.type->footX<=x0 || oz+o.type->footZ<=z0) return false;
        if (!o.type->canFly) return !o.type->isStructure();
        return flyerGrounded(o);
    };
    if (bodyIndexEnabled_ && hW_>0 && hH_>0 && x0>=0 && z0>=0 && x0+fx<=hW_ && z0+fz<=hH_) {
        if (!bodyIndexValid_) rebuildBodyIndex();
        for (int tz=z0/8;tz<(z0+fz+7)/8;++tz)
            for (int tx=x0/8;tx<(x0+fx+7)/8;++tx)
                for (int index:bodyTiles_[size_t(tz)*bodyTilesW_+tx]) {
                    const auto& o=units_[size_t(index)];
                    if (o.type && blocks(o,footprintOrigin(o.x,o.type->footX),footprintOrigin(o.z,o.type->footZ))) return true;
                }
        return false;
    }
    for (const auto& o:units_)
        if (o.type && blocks(o,footprintOrigin(o.x,o.type->footX),footprintOrigin(o.z,o.type->footZ))) return true;
    return false;
}

// Legion (deliberate, not retail): idle landed flyers make way for allied
// ground groups. Retail stamps a landed flyer into the ground word (5066f0)
// and every ground mover plans and steers round it as round any body; a
// parked air wing on a group's way makes the group detour or file past.
// In Legion a ground member whose planned cells ahead cover such a flyer
// asks it to lift (LegionNavigator::liftFlyers). It takes off as VTOL_Move
// does (416c50: Activate, BeginFlight), holds over the spot it left at
// cruise altitude through the retail flight kernel, and the ground walks
// under it. Every further request extends the hold, and so does any member
// still on its way near the flyer (the area must stay clear for a while);
// kLegionLiftQuiet ticks after the last one it hands over to the retail
// landing mission, which lands on the spot it left if that is free, else on
// the nearest free site. It then stays down for kLegionLiftRest ticks (no
// bobbing up and down in a crowd that is still settling). Only an idle flyer (no order, job, cargo or controller) of an
// allied player lifts; an enemy's stays an obstacle.
bool World::legionLiftable(const Unit& f,int player) const {
    return f.alive() && !f.embarked() && f.type && f.type->canFly && !f.type->isStructure() &&
        f.baseSpeed>Fixed() && !f.underConstruction && allied(f.player,player) &&
        f.orders.empty() && !f.buildSiteId && !f.repairId && !f.reclaimId && f.buildQueue.empty() &&
        !f.repeatType && f.cargo.empty() && !f.retainedFlightGoal && !f.landing &&
        (f.legionLift || (f.flightGroundMode==1 && tickCounter_>=f.legionLiftRest));
}

// W6 FL-01 (PLAN 3.6): an episode lasts at most kLegionLiftCap ticks unless
// a member is blocked by the flyer itself (it covers the member's next planned
// cells); a capped episode then rests kLegionLiftCapRest ticks once landed.
// While lifted the flyer polls for a target on the VTOL-standby call
// (acquireTarget, as the landed standby does) every kLegionLiftPoll ticks,
// staggered by id: lifted flyers fight instead of hovering through a battle.
bool World::requestLegionLift(Unit& f, bool blocked) {
    if (!f.legionLift) {
        f.legionLift=true; f.legionLiftX=f.x; f.legionLiftZ=f.z; f.legionLiftSince=tickCounter_;
    } else if (!blocked && tickCounter_-f.legionLiftSince>=kLegionLiftCap) return false;
    // An unblocked request never carries the episode past the cap, quiet
    // period included.
    f.legionLiftUntil=blocked ? tickCounter_+kLegionLiftQuiet
                              : std::min(tickCounter_+kLegionLiftQuiet,f.legionLiftSince+kLegionLiftCap);
    return true;
}

bool World::tickLegionLift(Unit& u) {
    if (!u.legionLift) return false;
    if (!isLegionPathfinding(pathfindingMode_) || !legionLiftable(u,u.player)) {
        // An order (or anything else that makes it busy) takes over from here.
        u.legionLift=false;
        return false;
    }
    if (tickCounter_>=u.legionLiftUntil) {
        // Nobody asked for kLegionLiftQuiet ticks: land again, searching from
        // the spot it holds over (the retail landing mission's own search).
        const bool capped=tickCounter_-u.legionLiftSince>=kLegionLiftCap-1;
        u.legionLift=false;
        u.legionLiftRest=tickCounter_+(capped ? kLegionLiftCapRest : kLegionLiftRest);
        u.legionLiftEnded=tickCounter_;
        if (capped && legion_) legion_->noteFlyerEvent(LegionNavigator::FlyerEvent::CapHit);
        if (u.flightGroundMode==2) { u.standbyActive=false; u.landing.emplace(); }
        return false;
    }
    if (u.flightGroundMode==2 && (uint32_t(u.id)+tickCounter_)%kLegionLiftPoll==0) {
        if (legion_) legion_->noteFlyerEvent(LegionNavigator::FlyerEvent::TargetPoll);
        if (acquireTarget(u,true)) {
            // A target: the attack order ends the lift (the flyer is no longer
            // liftable) and this tick's combat update takes it on.
            u.legionLift=false;
            u.legionLiftRest=tickCounter_+kLegionLiftRest;
            u.legionLiftEnded=tickCounter_;
            return false;
        }
    }
    if (u.flightGroundMode!=2) {
        // 416c50, as VTOL_Move's stage 0.
        if (auto script=unitScripts_.find(u.id);script!=unitScripts_.end() && !script->second.activated) {
            script->second.activated=true;notifyUnitScript(u,"Activate");
        }
        notifyUnitScript(u,"BeginFlight");
        u.standbyActive=false;
        if (u.flightGroundMode!=2) { u.flightGroundMode=2;groundMove(u); }   // 51b3b0: taking off unstamps
    }
    const int cruise=flightGround(u)+u.type->cruiseAlt;
    const RetailFlightGoal goal{{u.legionLiftX.v,Fixed::fromInt(std::min(cruise,511)).v,u.legionLiftZ.v}};
    const RetailFlightVector position{u.x.v,u.flightY.v,u.z.v};
    u.flightNavigation=retailFlightNavigation(position,u.flightNavigation.destination,goal.point,
        u.flightNavigation.heading,Fixed::fromInt(cruise).v,false,(goal.flags&0x40)!=0,goal.heading);
    tickFlightBody(u);
    notifyFlightOccupancy(u);
    return true;
}

// Cell quad extrema (+6 low, +5 high) from the corner lattice (50ed60).
std::pair<int,int> World::cellHeightRange(size_t cell) const {
    if (heights_.empty() || hW_<=0) return {0,0};
    const int x=int(cell%size_t(hW_)),z=int(cell/size_t(hW_));
    auto at=[&](int cx,int cz){return int(heights_[size_t(std::min(cz,hH_-1))*hW_+std::min(cx,hW_-1)]);};
    const int a=at(x,z),b=at(x+1,z),c=at(x,z+1),d=at(x+1,z+1);
    return {std::min(std::min(a,b),std::min(c,d)),std::max(std::max(a,b),std::max(c,d))};
}

// Retail group pacing (Retail mode only). A Ctrl-group (+N) is native group
// N; our separate formation squads (-N) become groups 10+N, since retail has
// a single kind of group (522170 assigns Ctrl+N from the selection).
int World::retailGroupIndex(const Unit& u) {
    return u.squad>0 ? u.squad : u.squad<0 ? 10-u.squad : 0;
}

RetailGroupMember World::retailGroupMember(const Unit& u) const {
    RetailGroupMember m;
    m.kind=u.type->canFly ? RetailGroupClass::Flyer
         : u.type->floater && u.type->minWaterDepth>0 ? RetailGroupClass::Boat : RetailGroupClass::Ground;
    m.position={u.x.floorInt(),0,u.z.floorInt()};
    m.area=u.type->footX*u.type->footZ;
    m.mover=!u.type->isStructure();
    return m;
}

const RetailGroupRecord* World::retailGroupOf(const Unit& u) const {
    const int group=retailGroupIndex(u);
    if (pathfindingMode_!=PathfindingMode::Retail || group<1 || group>=100 ||
        u.player<0 || u.player>=kMaxPlayers) return nullptr;
    return &retailGroups_[size_t(u.player)][size_t(group)];
}

// Flags of the unit's current mission (+0x5a), for the missions this host
// models: landing, ground missions and standby. Other orders report none.
uint32_t World::retailMissionFlags(Unit& u) {
    if (u.landing) return u.landing->mission.flags;
    // VTOL_Move, VTOL_Move_Formation and VTOL_Patrol all carry 0x1000000.
    if (plainFlightPatrol(u)) return u.orders.front().mission.flags;
    if (auto* o=groundMissionOrder(u,true))
        return o->transportUnloadApproach ? o->transportMission.flags : o->mission.flags;
    if (u.orders.empty() && u.standbyActive) return u.standbyState.flags;
    return 0;
}

// 51b890: aggregate every group, set each member's navigator formation bit,
// then apply the straggler slowdown. The changed-flag notification at
// 51c612 (navigator vt+0x38) has no movement consumer and is not modelled.
void World::tickRetailGroups() {
    retailGroupPaced_.assign(units_.size(),0);
    std::array<std::array<bool,100>,kMaxPlayers> members{};
    std::array<std::vector<RetailGroupUnit>,kMaxPlayers> roster;
    std::array<std::vector<size_t>,kMaxPlayers> index;
    TAK_PASS();
    for (size_t i=0;i<units_.size();++i) {
        Unit& u=units_[i];
        if (!u.alive() || !u.type || u.player<0 || u.player>=kMaxPlayers) continue;
        const int g=retailGroupIndex(u);
        if (g<1 || g>=100) continue;
        members[size_t(u.player)][size_t(g)]=true;
        RetailGroupUnit entry;
        entry.group=g;
        entry.eligible=!u.underConstruction && !u.embarked() && !u.type->isStructure();
        if (entry.eligible) {
            entry.member=retailGroupMember(u);
            entry.missionFlags=retailMissionFlags(u);
            entry.typeMaximum=(u.type->maxVel*groundTerrainMultiplier(u)).v;
        }
        roster[size_t(u.player)].push_back(entry);index[size_t(u.player)].push_back(i);
    }
    std::vector<uint8_t> paced;
    for (int player=0;player<kMaxPlayers;++player) {
        retailGroupTick(roster[size_t(player)],members[size_t(player)],retailGroups_[size_t(player)],paced);
        for (size_t k=0;k<paced.size();++k) retailGroupPaced_[index[size_t(player)][k]]=paced[k];
    }
}

// 50c480: take a member out of its group's moving aggregate for the rest of
// the tick. With fewer than two moving members only a flyer subset clears.
void World::leaveRetailGroupCentre(const Unit& u) {
    if (!retailGroupOf(u) || u.type->isStructure()) return;
    retailGroupLeave(retailGroups_[size_t(u.player)][size_t(retailGroupIndex(u))],retailGroupMember(u));
}

// 4d95f0's group cap for the unit about to move, after terrain scaling and
// the speed mode; zero leaves the unit at its own maximum.
Fixed World::retailGroupLimit(const Unit& u) const {
    const size_t index=size_t(&u-units_.data());
    if (index>=retailGroupPaced_.size() || !retailGroupPaced_[index] || u.type->canFly) return Fixed();
    const auto* record=retailGroupOf(u);
    if (!record || !record->active) return Fixed();
    // A computer player (owner +0xea == 3) takes navigator vt+0x3c instead,
    // which is zero for ground navigators (4e60d0): no group cap. Our
    // computer players are the slots with the AI's automatic gates.
    if (players_[size_t(u.player)].automaticGates) return Fixed();
    const Fixed multiplier=groundTerrainMultiplier(u);
    const int32_t type=(u.type->maxVel*multiplier).v;
    Fixed moded=Fixed::raw(type);
    if (u.groundSpeedMode==1) moded=moded*Fixed::raw(0xaac0);
    else if (u.groundSpeedMode==2) moded=moded*Fixed::raw(0x553f);
    const bool boat=retailGroupMember(u).kind==RetailGroupClass::Boat;
    return Fixed::raw(retailGroupSpeedCap(boat ? record->boatSpeed : record->groundSpeed,
        (u.baseSpeed*multiplier).v,type,moded.v));
}

// A flyer's head order that Legion keeps with its formation: a plain move,
// fight-move or patrol leg, nothing that carries its own target or job.
static bool legionStationOrder(const Unit& u) {
    if (u.orders.empty() || u.retailBuild) return false;
    const Order& o=u.orders.front();
    if (o.targetId || o.load || o.unload || o.guard || o.wait>0 || o.waitAttack || o.buildType ||
        o.reclaimFeat || o.reclaimArea || o.manaBuildArea || o.repairTarget || o.patrolRepair ||
        o.landing || o.transportPickup || o.buildRectangle || o.formationLevel) return false;
    return o.flightMoveMission || o.attackMove || o.patrol;
}

// Square spiral around the origin: slot 0 is the centre, ring r holds 8r slots.
static void legionStationSlot(int k,int& ox,int& oz) {
    ox=oz=0;
    if (k<=0) return;
    int r=1,n=k-1;
    while (n>=8*r) { n-=8*r; ++r; }
    const int side=n/(2*r),pos=n%(2*r);
    switch (side) {
    case 0: ox=r;oz=-r+1+pos; break;
    case 1: ox=r-1-pos;oz=r; break;
    case 2: ox=-r;oz=r-1-pos; break;
    default: ox=-r+1+pos;oz=-r; break;
    }
}

// Legion formation flyers ride over the front of their ground: the side the
// ground travels toward, (dx,dz) its 16.16 unit direction. The stations fill
// rows of C lanes, `spacing` px apart across the ground's lateral extent (a
// short last row spread over all of it), front row first. A lane holds over
// the frontmost ground body within half a spacing of it -- a body farther out
// counts that much farther back, so the front is continuous in the bodies'
// positions and a body sliding across a lane edge moves no station at once --
// and each further row sits a spacing behind; or, with `ahead` > 0 (the
// landing points), `ahead` px beyond the front and each further row a spacing
// farther on, clear of the ground's slots. The flyers (`flyers`: where each
// stands, or the point it is bound for) take the rows in order of their
// distance behind that front (beyond it, for landing points) and each row's
// lanes in order across, so flyers in formation keep their stations, and a
// turn re-ranks them where they are rather than by who they are. C is the
// extent's lane count, but never so few that the rows outnumber twice the
// lanes: over ground narrower than that (one body, a column) the rows spread
// that wide about its centre line, so the flyers stand at most twice as deep
// as wide rather than in a file trailing behind it, and a lane past the
// ground's edge takes the front at that edge. Within a quarter spacing of a
// lane boundary the flyers keep the layout of the lane count across it if
// they stand nearer it (by a quarter spacing squared per flyer), so a
// formation whose width hovers at a boundary does not reshuffle them each tick.
// `at` and `flyers` hold raw positions; out[i] is flyer i's station.
static void legionFrontSlots(const std::vector<std::array<int64_t,2>>& at,int64_t dx,int64_t dz,int spacing,int ahead,
                             const std::vector<std::array<int64_t,2>>& flyers,std::vector<std::array<Fixed,2>>& out) {
    const int count=int(flyers.size());
    out.assign(size_t(count),{});
    if (at.empty() || count==0) return;
    int64_t cx=0,cz=0;
    for (const auto& p:at) { cx+=p[0]; cz+=p[1]; }
    cx/=int64_t(at.size()); cz/=int64_t(at.size());
    // Along (toward the front) and across offsets from the centroid.
    const auto frame=[&](const std::array<int64_t,2>& p)->std::array<int64_t,2> {
        const int64_t rx=p[0]-cx,rz=p[1]-cz;
        return {(rx*dx+rz*dz)>>16,(rz*dx-rx*dz)>>16};
    };
    std::vector<std::array<int64_t,2>> rel; rel.reserve(at.size());
    int64_t lo=INT64_MAX,hi=INT64_MIN;
    for (const auto& p:at) { rel.push_back(frame(p)); lo=std::min(lo,rel.back()[1]); hi=std::max(hi,rel.back()[1]); }
    const int64_t gap=int64_t(spacing)<<16;
    // The least lane count, rows no more than twice as many as lanes; over ground narrower than its rows they
    // spread past its edges, each edge's front held flat.
    int least=std::max(int(isqrt64(uint64_t(count/2))),1);
    while (2*least*least<count) ++least;
    const bool narrow=hi-lo<int64_t(least-1)*gap;
    const auto front=[&](int64_t s) {
        if (narrow) s=std::clamp(s,lo,hi);
        int64_t a=INT64_MIN;
        for (const auto& q:rel) a=std::max(a,q[0]-std::max<int64_t>((q[1]>s ? q[1]-s : s-q[1])-gap/2,0));
        return ahead>0 ? a+(int64_t(ahead)<<16) : a;
    };
    // Each flyer's rank key: its distance behind the front (beyond it).
    std::vector<std::array<int64_t,3>> rank(static_cast<size_t>(count));
    for (int j=0;j<count;++j) {
        const auto f=frame(flyers[size_t(j)]);
        rank[size_t(j)]={ahead>0 ? f[0]-front(f[1]) : front(f[1])-f[0],f[1],j};
    }
    std::sort(rank.begin(),rank.end());
    const int64_t lanesFit=(hi-lo)/gap+1,past=(hi-lo)%gap;
    const int fit=int(std::min<int64_t>(lanesFit,count)),nominal=std::max(fit,least);
    std::vector<std::array<int64_t,3>> row;
    std::vector<std::array<int64_t,2>> slots(static_cast<size_t>(count)),best;
    int64_t bestCost=INT64_MAX;
    for (const int lanes:{nominal,nominal-1,nominal+1}) {
        // A width set by the flyers' count has no boundary to hover at.
        if (lanes<least || lanes>count || (lanes!=nominal && nominal!=fit)) continue;
        // One lane fewer only just past a lane boundary, one more only just short of the next.
        if ((lanes<nominal && (nominal<lanesFit || 4*past>=gap)) || (lanes>nominal && 4*past<=3*gap)) continue;
        int64_t cost=lanes==nominal ? 0 : int64_t(count)*spacing*spacing/4;
        // The rows' lateral extent: the ground's, or over narrow ground lanes-1 spacings about its centre line.
        const int64_t wide=narrow ? int64_t(lanes-1)*gap : hi-lo,left=narrow ? lo+(hi-lo)/2-wide/2 : lo;
        for (int first=0,r=0;first<count;first+=lanes,++r) {
            const int n=std::min(lanes,count-first);
            row.assign(rank.begin()+first,rank.begin()+first+n);
            std::sort(row.begin(),row.end(),[](const auto& p,const auto& q){return p[1]!=q[1] ? p[1]<q[1] : p[2]<q[2];});
            for (int lane=0;lane<n;++lane) {
                const int64_t s=n<=1 ? lo+(hi-lo)/2 : left+wide*lane/(n-1);
                const int64_t a=front(s)+(ahead>0 ? r*gap : -r*gap);
                const int j=int(row[size_t(lane)][2]);
                slots[size_t(j)]={cx+((dx*a-dz*s)>>16),cz+((dz*a+dx*s)>>16)};
                const int64_t ex=(slots[size_t(j)][0]-flyers[size_t(j)][0])>>16,ez=(slots[size_t(j)][1]-flyers[size_t(j)][1])>>16;
                cost+=ex*ex+ez*ez;
            }
        }
        if (cost<bestCost) { bestCost=cost; best=slots; }
    }
    for (int j=0;j<count;++j) out[size_t(j)]={Fixed::raw(int32_t(best[size_t(j)][0])),Fixed::raw(int32_t(best[size_t(j)][1]))};
}

// Legion (a deliberate difference from retail, docs/legion-pathfinding.md):
// a flyer in a squad -- an Alt+N formation (squad < 0) or a Ctrl+N group
// (squad > 0) -- that also has ground members travels with the ground of its
// own click. Retail flies it at its own speed toward the shared point and,
// once it is far out of slot, pulls it back to the group centre (417e02,
// ported for Retail mode); with neither, a Legion flyer raced ahead, landed
// at the goal and waited there.
//
// Pairing (W6, PLAN 3.5 / C4): one click is one convoy (Order::convoyTick,
// sim/convoy.h), whatever the client's uplink split and whatever the flyer's
// +-60 px click offset. Per squad (retailGroupIndex: Ctrl 1..10, Alt 11..20)
// the busy ground members' last orders fall into up to kBuckets buckets keyed
// by (order class, convoyTick); a flyer whose last order carries the same key
// keeps station over that bucket's ground. A further distinct click gets no
// bucket (stationOverflow) and its flyers fly their own orders, as does a
// flyer of a click with no ground (a separate air click or air patrol).
//
// While the bucket's ground is under way the flyer keeps a station over the
// front of the ground (legionFrontSlots: lanes across the leading bodies, one
// row per lane width, then the rows behind), flyers taking them where they
// stand; with no direction of travel the stations fall back to a square spiral over
// the ground's centroid. It does not steer AT the station:
// a point destination puts retail's flight model in its arrival regime,
// where the navigator keeps its old heading inside 16 px and the body only
// creeps after a target that moves a fraction of a pixel per tick (the flyer
// looked parked). It steers at a point 160 px ahead of the station along the
// ground's direction of travel, so it cruises facing its way and banks
// through turns as any flyer does, and its maximum follows the ground's mean
// speed, raised while it is behind its station and lowered while it is
// ahead. Once the ground's centroid is within the formation radius of the
// flyer's destination its order's point moves, once (stationFree 3), to its
// lane just beyond the front of the ground's goals (their formation slots), and
// it is released to that order, as in retail; it arrives, completes the order
// and lands at once, in front of the ground rather than on its slots. A squad with one bucket
// keeps every ground member (idle ones too) in its centroid, radius and
// spacing, as before W6; with several, each bucket counts its own busy
// members only. Everything here is derived from hashed state each tick, in
// unit order, with integer arithmetic; nothing persists.
void World::planLegionFlightStations() {
    legionFlightStationsLive_=false;
    if (!isLegionPathfinding(pathfindingMode_)) return;
    constexpr int kSquads=21;         // retailGroupIndex: 1..10 Ctrl, 11..20 Alt
    constexpr int kBuckets=4;         // clicks per squad with a station (PLAN 3.5)
    constexpr int32_t kLead=160;      // px ahead of the station the flyer steers at
    constexpr int64_t kCatchUp=192;   // px behind the station at which it flies at full speed
    constexpr int64_t kBrake=128;     // px ahead of the station at which it stops
    const auto near=[](Fixed ax,Fixed az,Fixed bx,Fixed bz,int64_t pixels) {
        const int64_t dx=int64_t(ax.v)-bx.v,dz=int64_t(az.v)-bz.v,r=pixels<<16;
        return dx*dx+dz*dz<=r*r;
    };
    const auto classOf=[](const Order& o) {
        return o.patrol ? ConvoyClass::Patrol : o.attackMove ? ConvoyClass::Fight : ConvoyClass::Move;
    };
    struct Bucket {
        ConvoyClass cls=ConvoyClass::Move;
        uint32_t convoy=ConvoyTable::kNone;
        int64_t sx=0,sz=0,dx=0,dz=0,speed=0;
        int busy=0,area=0,slots=0,landings=0;
        // The flyers on station (stationFree 0 and 3, where each stands) and
        // with a landing point (0, and 3 at the point it is bound for), in
        // unit order; their stations over the front of the live ground and
        // landing points beyond the front of its goals (legionFrontSlots),
        // planned once per tick on the first use. arriving: the ground's
        // centroid has come within the formation radius of a station flyer's
        // point (every flyer of the bucket takes its landing point at once).
        std::vector<std::array<int64_t,2>> onStation,toLand;
        std::vector<std::array<Fixed,2>> front,landing;
        bool planned=false,landed=false,arriving=false;
        // FL-04: a busy member still making headway (LegionNavigator::
        // advancing over kStationStall; a ground mover Legion does not route
        // counts while it moves), and one that fired lately (a reload running).
        bool advancing=false,engaged=false;
    };
    // A ground member: its bucket (-1: idle or none), live position and goal.
    struct Body {int bucket=-1;int64_t x=0,z=0,gx=0,gz=0;};
    struct Ground {
        int64_t sx=0,sz=0;
        int n=0,area=0,foot=1,groundFoot=1,buckets=0;
        bool flyers=false,overflow=false;
        std::array<Bucket,kBuckets> bucket{};
        std::vector<Body> bodies;
    };
    std::array<std::array<Ground,kSquads>,kMaxPlayers> groups{};
    const auto member=[](const Unit& u) {
        return u.alive() && u.type && u.squad!=0 && u.squad>=-10 && u.squad<=10 && u.player>=0 && u.player<kMaxPlayers &&
               !u.type->isStructure() && !u.underConstruction && !u.embarked();
    };
    bool flyers=false;
    TAK_PASS();
    for (const auto& u:units_) {
        if (!member(u) || !u.type->canFly) continue;
        auto& g=groups[size_t(u.player)][size_t(retailGroupIndex(u))];
        g.flyers=flyers=true;
        g.foot=std::max({g.foot,int(u.type->footX),int(u.type->footZ)});
    }
    if (!flyers) return;
    TAK_PASS();
    for (const auto& u:units_) {
        if (!member(u) || u.type->canFly) continue;
        auto& g=groups[size_t(u.player)][size_t(retailGroupIndex(u))];
        if (!g.flyers) continue;
        g.sx+=u.x.v;g.sz+=u.z.v;
        ++g.n;g.area+=u.type->footX*u.type->footZ;
        g.groundFoot=std::max({g.groundFoot,int(u.type->footX),int(u.type->footZ)});
        g.bodies.push_back({-1,u.x.v,u.z.v,u.x.v,u.z.v});
        if (u.orders.empty()) continue;
        const Order& last=u.orders.back();
        if (last.convoyTick==ConvoyTable::kNone) continue;
        const ConvoyClass cls=classOf(last);
        Bucket* b=nullptr;
        for (int k=0;k<g.buckets && !b;++k)
            if (g.bucket[size_t(k)].cls==cls && g.bucket[size_t(k)].convoy==last.convoyTick) b=&g.bucket[size_t(k)];
        if (!b) {
            if (g.buckets==kBuckets) { g.overflow=true; continue; }
            b=&g.bucket[size_t(g.buckets++)];
            b->cls=cls;b->convoy=last.convoyTick;
        }
        // Its goal: the Legion slot, else its order's point.
        auto& body=g.bodies.back();
        body.bucket=int(b-g.bucket.data());
        int32_t gx,gz;
        if (legion_ && legion_->goalPoint(u.id,gx,gz)) { body.gx=gx; body.gz=gz; }
        else { body.gx=last.x.v; body.gz=last.z.v; }
        b->sx+=u.x.v;b->sz+=u.z.v;
        ++b->busy;b->area+=u.type->footX*u.type->footZ;
        b->speed+=u.speed.v;
        const bool engaged=u.reloads[0]>0||u.reloads[1]>0||u.reloads[2]>0;
        b->engaged|=engaged;
        if (!b->advancing) b->advancing=engaged ||
            (legion_ && legion_->unitState(u.id) ? legion_->advancing(u.id,LegionNavigator::kStationStall) : u.speed>Fixed());
        // Direction of travel: the sum of the busy members' unit vectors
        // (16.16), the way a moving member's route heads it (its course: its
        // body faces its own steps, sideways round a body too; so a column
        // turning round a wall turns its front with it) and toward its
        // current leg for one standing still.
        if (u.speed>Fixed()) {
            const auto step=retailGroundStep(legion_ ? legion_->course(u) : u.heading,Fixed::fromInt(1));
            b->dx+=step.s.v;b->dz+=step.c.v;   // (x, z)
            continue;
        }
        const Order& leg=u.orders[currentLeg(u.orders)];
        const Fixed lx=leg.x-u.x,lz=leg.z-u.z,len=fxLen(lx,lz);
        if (len>Fixed::fromInt(1)) {
            b->dx+=(int64_t(lx.v)<<16)/len.v;b->dz+=(int64_t(lz.v)<<16)/len.v;
        }
    }
    const auto bucketOf=[&](Ground& g,const Order& last)->Bucket* {
        const ConvoyClass cls=classOf(last);
        for (int k=0;k<g.buckets;++k)
            if (g.bucket[size_t(k)].cls==cls && g.bucket[size_t(k)].convoy==last.convoyTick) return &g.bucket[size_t(k)];
        return nullptr;
    };
    // A bucket's ground centroid and formation radius (px): a squad with one
    // bucket counts all its ground, idle members too.
    const auto centre=[&](const Ground& g,const Bucket& b,Fixed& cx,Fixed& cz) {
        const bool whole=g.buckets==1;
        const int64_t n=whole?g.n:b.busy;
        cx=Fixed::raw(int32_t((whole?g.sx:b.sx)/n));cz=Fixed::raw(int32_t((whole?g.sz:b.sz)/n));
        return std::max<int64_t>(256,32*int64_t(isqrt64(uint64_t(whole?g.area:b.area))));
    };
    TAK_PASS();
    for (const auto& u:units_) {
        if (!member(u) || !u.type->canFly || !legionStationOrder(u)) continue;
        auto& g=groups[size_t(u.player)][size_t(retailGroupIndex(u))];
        const Order& last=u.orders.back();
        if (g.n==0 || last.convoyTick==ConvoyTable::kNone || last.stationFree==1 || last.stationFree==2) continue;
        Bucket* b=bucketOf(g,last);
        if (!b) continue;
        b->onStation.push_back({u.x.v,u.z.v});
        if (last.stationFree==3) { b->toLand.push_back({last.x.v,last.z.v}); continue; }
        b->toLand.push_back({u.x.v,u.z.v});
        Fixed cx,cz;
        const int64_t radius=centre(g,*b,cx,cz);
        b->arriving|=b->cls!=ConvoyClass::Patrol && near(cx,cz,last.x,last.z,radius);
    }
    TAK_PASS();
    for (size_t i=0;i<units_.size();++i) {
        Unit& u=units_[i];
        if (!member(u) || !u.type->canFly || !legionStationOrder(u)) continue;
        auto& g=groups[size_t(u.player)][size_t(retailGroupIndex(u))];
        if (g.overflow && legion_) {
            // Observation only: once per squad and tick.
            legion_->noteFlyerEvent(LegionNavigator::FlyerEvent::StationOverflow);
            g.overflow=false;
        }
        Order& last=u.orders.back();
        if (g.n==0 || last.convoyTick==ConvoyTable::kNone || last.stationFree==1 || last.stationFree==2) continue;
        Bucket* b=bucketOf(g,last);
        if (!b) continue;
        const bool bound=last.stationFree==3;
        const int slot=b->slots++,landing=b->landings++;
        Fixed cx,cz;
        centre(g,*b,cx,cz);
        const int spacing=16*g.foot+16;
        // Unit direction of travel (16.16), zero when the ground has none.
        int64_t dx=0,dz=0;
        if (const int64_t l=int64_t(isqrt64(uint64_t(b->dx*b->dx+b->dz*b->dz)));l>0) {
            dx=(b->dx<<16)/l;dz=(b->dz<<16)/l;
        }
        // The front: the bucket's busy bodies as they stand (one that has
        // arrived waits at its slot, and the flyers stay with those still
        // under way), or at their goals with, for a squad of one bucket, its
        // idle ground where it stands (the first arrivals among them).
        const auto bodies=[&](bool goals) {
            std::vector<std::array<int64_t,2>> at;
            for (const auto& body:g.bodies)
                if (body.bucket==int(b-g.bucket.data()) || (goals && g.buckets==1)) at.push_back({goals?body.gx:body.x,goals?body.gz:body.z});
            return at;
        };
        const bool arriving=bound || b->arriving;
        if (arriving && !bound) {
            // The order's point moves once to the flyer's landing point beyond
            // the front of the ground's goals, the way the ground travelled.
            if (!b->landed) {
                b->landed=true;
                int64_t ax=dx,az=dz;
                if (!ax && !az) {
                    const int64_t ex=int64_t(last.x.v)-cx.v,ez=int64_t(last.z.v)-cz.v;
                    if (const int64_t l=int64_t(isqrt64(uint64_t(ex*ex+ez*ez)));l>0) { ax=(ex<<16)/l; az=(ez<<16)/l; }
                }
                if (ax || az) legionFrontSlots(bodies(true),ax,az,spacing,8*(g.groundFoot+g.foot)+16,b->toLand,b->landing);
            }
            if (size_t(landing)<b->landing.size()) {
                const int32_t edge=8*g.foot;
                last.x=Fixed::fromInt(std::clamp(b->landing[size_t(landing)][0].floorInt(),edge,std::max(edge,terW_*16-edge)));
                last.z=Fixed::fromInt(std::clamp(b->landing[size_t(landing)][1].floorInt(),edge,std::max(edge,terH_*16-edge)));
            }
            last.stationFree=3;
        }
        if (!arriving) {
            // FL-04 (PLAN 3.6): the ground has stopped. Release A: no busy
            // member has made headway for kStationStall ticks (a dead-end
            // queue, a sealed wall, an approach hold). Release B: its centroid
            // has stood within 16 px for kStationStatic ticks with nobody
            // firing (a crowd one member still creeps through). Either frees
            // the flyer for this order: it flies its own order and lands, as
            // retail's VTOL_Move ends.
            constexpr int64_t kStationStill=16;
            constexpr uint32_t kStationStatic=900;
            if (!b->advancing) {
                last.stationFree=1;
                if (legion_) legion_->noteFlyerEvent(LegionNavigator::FlyerEvent::ReleaseA);
                continue;
            }
            if (!last.stationRefTick || b->engaged ||
                !near(cx,cz,Fixed::raw(last.stationRefX),Fixed::raw(last.stationRefZ),kStationStill)) {
                last.stationRefX=cx.v;last.stationRefZ=cz.v;last.stationRefTick=tickCounter_;
            } else if (tickCounter_-last.stationRefTick>=kStationStatic) {
                last.stationFree=2;
                if (legion_) legion_->noteFlyerEvent(LegionNavigator::FlyerEvent::ReleaseB);
                continue;
            }
        }
        // Its station: over the front (bound for its landing point, the pace
        // still holds it there), or with no direction of travel a square
        // spiral over the ground's centroid.
        Fixed sx=cx,sz=cz;
        if (dx || dz) {
            if (!b->planned) { b->planned=true; legionFrontSlots(bodies(false),dx,dz,spacing,0,b->onStation,b->front); }
            sx=b->front[size_t(slot)][0]; sz=b->front[size_t(slot)][1];
        } else {
            int ox,oz;
            legionStationSlot(slot,ox,oz);
            sx=cx+Fixed::fromInt(ox*spacing); sz=cz+Fixed::fromInt(oz*spacing);
        }
        // Along-track error: positive while the flyer is behind its station.
        const int64_t behind=((int64_t(sx.v-u.x.v)*dx+int64_t(sz.v-u.z.v)*dz)>>16);
        const int64_t pace=b->speed/b->busy,base=u.baseSpeed.v;
        int64_t cap;
        // Far off its station (joining, or left behind on a turn) it flies at
        // its own speed to rejoin.
        if (!near(u.x,u.z,sx,sz,kCatchUp)) cap=base;
        else if (behind>=0) cap=pace+(std::max<int64_t>(base-pace,0)*std::min(behind,kCatchUp<<16))/(kCatchUp<<16);
        else cap=pace*std::max<int64_t>((kBrake<<16)+behind,0)/(kBrake<<16);
        if (!legionFlightStationsLive_) {
            legionFlightStations_.assign(units_.size(),LegionFlightStation{});
            legionFlightStationsLive_=true;
        }
        auto& station=legionFlightStations_[i];
        station.active=true;
        station.cap=Fixed::raw(int32_t(std::clamp<int64_t>(cap,0,base)));
        if (arriving) {
            // Arriving: the flyer flies its own order and lands on arrival.
            // The pace holds until it is near its point, so it does not dash
            // ahead of the ground; the final approach is retail's. It keeps a
            // quarter of its own speed at least: its point is in front of the
            // ground's goals, so a front station the settling ground no longer
            // carries forward must not hold it short of it, hovering.
            station.steer=station.hold=false;
            station.capped=!near(u.x,u.z,last.x,last.z,kLead);
            station.cap=Fixed::raw(int32_t(std::clamp<int64_t>(std::max(cap,base/4),0,base)));
            continue;
        }
        station.steer=station.hold=station.capped=true;
        station.x=sx+Fixed::raw(int32_t((dx*kLead)));
        station.z=sz+Fixed::raw(int32_t((dz*kLead)));
    }
}

const World::LegionFlightStation* World::legionFlightStation(const Unit& u) const {
    if (!legionFlightStationsLive_ || units_.empty()) return nullptr;
    const Unit* base=units_.data();
    if (&u<base || &u>=base+units_.size()) return nullptr;
    const size_t index=size_t(&u-base);
    if (index>=legionFlightStations_.size() || !legionFlightStations_[index].active) return nullptr;
    return &legionFlightStations_[index];
}

// 4d856a: when the dispatcher finds the list empty -- including right after the
// last mission retired in the same call -- it builds the FBI default mission for
// owner types 1/2. The mover's controller is not a mission and does not defer it:
// VTOL_UNLOAD retires with its step-out controller, aimed 2*(range+400)px ahead,
// still attached, and VTOL_Standby lands the carrier two ticks later
// (tools/re/probe_transport_air_unload_default_mission.py). Waiting for that
// controller to finish flew a Roc 1100-1400px past its drop point.
// Known deviation kept: after a point controller has ARRIVED (flyer moves), World
// still lets the detached coast settle before standing by. Retail does not wait
// there either, but landing during the coast (VTOL_LANDIFCAN's 32px brake point)
// scatters a packed flyer formation's touchdowns (legion_flyerfront 56 -> 171px).
bool World::standbyEligible(const Unit& u) const {
    return u.standbyAllowed && u.type && (!u.type->canFly || u.type->vtolStandby) && !u.type->isStructure() &&
           !u.type->wanders && !u.buildSiteId && !u.repairId && !u.reclaimId &&
           u.buildQueue.empty() && !u.repeatType && u.orders.empty() &&
           (!u.retainedFlightGoal || u.retainedFlightControllerActive);
}

void World::beginStandby(Unit& u) {
    if (!standbyEligible(u)) return;
    u.standbyState = {};
    if (!u.type->canFly) u.standbyState.flags=0x5012000u;
    u.standbyActive = true;
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
        std::optional<std::pair<Fixed,Fixed>> patrolReturn; // appended after dispatch
        OrderIdentity patrolReturnIdentity;                  // ... with the outbound leg's convoy and order
        bool enabled() const { return u.alive() && !u.underConstruction && !u.embarked(); }
        bool canStandby() const { return w.standbyEligible(u); }
        RetailMissionState* head() {
            if (u.landing) return &u.landing->mission;
            if (auto* o = groundMissionOrder(u,true)) {
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
        void idle() { w.beginStandby(u); }
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
            Order goal = u.orders[currentLeg(u.orders)];
            w.dropLeg(u);
            // A patrol owns a point, not the last corner of its previous route.
            // Re-enter with a fresh controller and segment after every lap.
            if (goal.missionTarget) { goal.x=goal.missionTarget->first;goal.z=goal.missionTarget->second; }
            goal.controller=0;goal.navigationExhausted=goal.navigationConsumed=false;
            goal.hasSegment=false;goal.mission.pending=0;
            u.orders.push_back(goal);
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
                        // Legion: a free spot of the band (or beyond it) instead
                        // of the bearing's midpoint, which every output of one
                        // door shares (legionParkSpot). Re-chosen on each
                        // failure, so a blocked spot hands over to the next.
                        park.spot.reset();
                        if (park.spread && useTarget && w.legion_ && isLegionPathfinding(w.pathfindingMode_) &&
                            w.legion_->supports(u))
                            park.spot=w.legionParkSpot(u,park,*target);
                        if (park.spot) {
                            goal.x=Fixed::fromInt(park.spot->first*16+u.type->footX*8);
                            goal.z=Fixed::fromInt(park.spot->second*16+u.type->footZ*8);
                        } else {
                            const auto point=park.ring->navigationPoint(u.x,u.z,u.type->footX,u.type->footZ);
                            goal.x=point.x;goal.z=point.z;
                        }
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
            const auto resetGoal=[&](uint32_t) {
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
            };
            // 4067ae: RECLAIMAREA's approach installs 4d4da0(point,4) with wait
            // mask 0x700, and its next stage runs on arrival (0x500), failure
            // (0x200) or detachment alike. No radius growth, no group pacing.
            if (goal.reclaimAreaApproach) {
                if (m.stage==0) { resetGoal(4); m.waitMask=0x700; return 1; }
                return 5;
            }
            // Retail group pacing: Move_Ground_Formation (402880) and the
            // two group checks at the head of Move_Ground (402b00..402cd8).
            const RetailGroupRecord* group=w.retailGroupOf(u);
            if (goal.formationLevel) {
                const int level=goal.formationLevel;
                const bool all=(m.flags&0x4000000u)!=0;
                return retailGroundFormation(m,w.tickCounter_,level,u.embarked(),response.mode,
                    [&] { return group && (level>0
                        ? retailGroupOutOfSlot(*group,w.retailGroupMember(u),false,level,all)
                        : retailGroupOutOfSlot(*group,w.retailGroupMember(u),true,-level,all)); },
                    [&] {
                        // 4d4d40(mission,0): drop the controller; the mover brakes.
                        w.cancelPath(u);goal.controller=0;goal.navigationExhausted=true;
                        m.pending&=~0x3700u;
                    },
                    [&] {
                        const auto c=retailGroupCentre(*group,w.retailGroupMember(u),all,level<=0);
                        // 402918..40292f writes the centre to mission+0x22: the
                        // mission point itself, which 4d4da0 then hands to the
                        // controller. Retarget missionTarget too, or the reset
                        // below would steer to the first cycle's stale centre.
                        goal.x=Fixed::fromInt(int16_t(c.x));goal.z=Fixed::fromInt(int16_t(c.z));
                        goal.missionTarget=std::pair{goal.x,goal.z};
                    },
                    [&] { resetGoal(4); },[] { return false; },[&](int n) { return random(n); });
            }
            if (group && w.pathfindingMode_==PathfindingMode::Retail && !goal.park) {
                const auto member=w.retailGroupMember(u);
                // The formation goal's y terms are left at zero (see
                // docs/pathfinding-port.md); x/z use the 16.16 positions.
                const int check=retailGroundGroupCheck(*group,member,m.flags,{u.x.v,0,u.z.v},
                    {goal.x.v,0,goal.z.v},[&](int n){return int(random(n));});
                if (check==1) {
                    // Ahead again: drop out of the moving centre and the orders.
                    w.leaveRetailGroupCentre(u);
                    w.cancelPath(u);u.orders.clear();u.routeStamp=-1;u.standbyActive=false;
                    return 2;
                }
                const int8_t level=int8_t(check);
                if (level) {
                    Order formation;
                    formation.x=u.x;formation.z=u.z;formation.goal=formation.groundMission=true;
                    formation.formationLevel=level;formation.mission.flags=0x11000000u;
                    formation.groundResponse.mode=response.mode;
                    formation.issuedTick=w.tickCounter_;
                    // The parent restarts at stage 0 when the formation mission
                    // retires; inserting invalidates m, so set it first.
                    m.stage=0;
                    u.orders.insert(u.orders.begin(),formation);
                    return 4;
                }
            }
            // Fight-move and patrol used to bypass the ground mission host.
            // Partial routes then completed at their last corner and failed
            // controllers never received the native radius/rotation handling.
            // Combat acquisition still runs through tickCombat below.
            // A Legion-routed patrol never relaxes its goal circle either
            // (see the Move mask below); Legion owns its arrival.
            if (goal.patrol && w.legion_ && isLegionPathfinding(w.pathfindingMode_) && w.legion_->supports(u))
                events&=~0x2600u;
            if (goal.patrol)
                return retailGroundPatrol(m,goal.missionRadius,w.tickCounter_,events,
                    int16_t(u.type->footX),true,[&](int n){return random(n);},
                    [&] {
                        // Initialize: a Shift-queued patrol that starts with no
                        // loop behind it closes one here. The queue may not grow
                        // until the dispatcher is done with m and goal.
                        if (!goal.patrolOpen) return;
                        goal.patrolOpen=false;
                        const size_t current=currentLeg(u.orders);
                        bool loop=false;
                        for (size_t i=current+1;i<u.orders.size();++i)
                            loop|=u.orders[i].goal && u.orders[i].patrol && !u.orders[i].patrolOpen;
                        if (!loop) {patrolReturn=std::pair{u.x,u.z};patrolReturnIdentity=goal.identity();}
                    },resetGoal,[]{return 0;});
            // Retail relaxes the goal radius after failed searches. Legion
            // waits/retries the same plain Move instead: a blocked army must
            // never "arrive" because repeated failures enlarged its circle.
            if(w.legion_&&isLegionPathfinding(w.pathfindingMode_)&&w.legion_->supports(u))
                events&=~0x2600u;
            const int result=retailGroundMove(m,goal.missionRadius,w.tickCounter_,events,
                int16_t(u.type->footX),u.embarked(),response.mode,uint8_t(u.moveState),[&](int n) { return random(n); },
                resetGoal,[&] {
                    // Target scoring and retaliation eligibility still use the
                    // World's combat host; the mission owns when to poll it.
                    int target=w.findTarget(u,true,true,response.mode<0);
                    if (!target && u.fireState!=0 && u.lastHitBy) {
                        const Unit* enemy=w.unit(u.lastHitBy);
                        if (enemy && enemy->alive() && !enemy->embarked() && enemy->type &&
                            !w.allied(u.player,enemy->player) && !enemy->cloaked &&
                            fxLen(enemy->x-u.x,enemy->z-u.z)<=Fixed::fromFloat(u.type->maxRange()+90)) {
                            if (w.canAttackTarget(u,*enemy)) target=enemy->id;
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
            const int32_t speed=truncSat32(std::sqrt(double(v.x)*v.x+double(v.z)*v.z));
            return std::clamp(int(double(speed)*100/u.baseSpeed.v+0.5),0,100);
        }
        bool landable(RetailFlightVector point) {
            return w.flightLandingFree(u,Fixed::raw(point.x),Fixed::raw(point.z));
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
            if(u.flightGroundMode!=1) {
                u.flightGroundMode=1;
                w.groundMove(u);   // 51b3b0's mode change stamps the landed body
            }
            u.flightVelocity={};u.speed=Fixed();
        }
        void install(RetailFlightGoal goal) {
            std::erase_if(u.orders,[](const Order& o){return o.landing;});
            Order order; order.x=Fixed::raw(goal.point.x); order.z=Fixed::raw(goal.point.z);
            order.flightGoal=goal; order.landing=true; u.orders.insert(u.orders.begin(),order);
        }
    } host{*this,u};
    retailDispatchMissions(tickCounter_,u.missionEvents,host);
    if(host.patrolReturn) patrolTo(u.id,host.patrolReturn->first.toFloat(),host.patrolReturn->second.toFloat(),true,
                                   host.patrolReturnIdentity);
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
    noteOrders(u.id);
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
    static const bool kPqLog = tak::devEnv("TAK_PATHLOG") != nullptr;
    if (!pathService_) return false;
    if (!u.type || u.type->canFly || u.type->isStructure()) return false;
    if(isLegionPathfinding(pathfindingMode_)) {
        // Legion plans group fields itself; supported legs never queue a
        // native search. Everything else continues to Retail below.
        if(!legion_)legion_=std::make_unique<LegionNavigator>(*this);
        if(legion_->supports(u)) {legion_->registerMove(u);return false;}
    }
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
    // Retail's setDestination 4e54e0 submits whenever a controller exists
    // (4e5540 push 1; call 4e4f50), even inside the requester's own cell:
    // 415170 then accepts the start at once (500 work, event 0x1000) and
    // 4e4ea0 delivers no points, deactivating the navigator and raising
    // 0x200 only for an unsatisfied controller. Retail mode keeps that
    // submission; Legion keeps its in-cell cancellation.
    if (from.x == to.x && from.z == to.z && pathfindingMode_!=PathfindingMode::Retail) {
        if (kPqLog) std::printf("[pq] t=%u id=%d NEAR-CANCEL to=(%.0f,%.0f)\n",
                                tickCounter_, u.id, x, z);
        paths_.cancel(u.id); return false;
    }
    if (kPqLog) std::printf("[pq] t=%u id=%d QUEUED to=(%.0f,%.0f)\n", tickCounter_, u.id, x, z);
    // The 5x budget class is the PLAYER's, not the request's: retail flags player
    // slots (+0x24e7) and the scheduler weighs whole players. Only Retail mode
    // carries retail's classes; the other modes keep their unweighted split.
    const int32_t tolerance = mission ? std::bit_cast<int32_t>(mission->missionRadius + 4u) : 4;
    // A tool that edits an owner directly bypasses transferEntitySlot; give
    // the unit its record in the owner's pool before it is scheduled there.
    if (!retailAllocation_ && u.player>=0 && u.player<int(entitySlots_.unitAt.size())) {
        const auto& pool=entitySlots_.unitAt[size_t(u.player)];
        const int slot=entitySlot(u.id);
        if (slot<0 || size_t(slot)>=pool.size() || pool[size_t(slot)]!=u.id) {
            paths_.cancel(u.id,NavigationTelemetry::Cancel::Replaced);
            releaseEntitySlot(u.id);allocateEntitySlot(u.id,u.player);
        }
    }
    const bool newlyPending=!paths_.pending(u.id);
    paths_.request(u.id, from, to, g.width(), g.height(),
                   target.first, target.second,
                   u.player, /*priority=*/pathfindingMode_==PathfindingMode::Retail &&
                       ((pathBudgetClassMask_ >> (unsigned(u.player) & 31)) & 1),
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
// exist but leave the window, or the corridor really was severed), resolveSplit() settles
// it over the whole grid at the cost of the smaller side; only if that runs over budget
// does the next query rebuild from scratch -- conservative, never wrong.
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
    // reroute may exist outside the window where this search cannot see it. The window
    // cannot tell which, so resolveSplit() below settles it.
    // Group by ROOT, not by raw label. After an unblock merge two cells can share a
    // component while still carrying the different ids they were first given; grouping
    // by raw label would see two unrelated ids, find each trivially self-consistent,
    // and conclude nothing was severed -- leaving the cache asserting a connection the
    // edit just cut. Reachability would then depend on whether a passage had ever been
    // open, which is history, not geometry. (Found by review; reproduced by opening and
    // re-closing a gate in a full-width wall in ONE blockCells call each way. Closing it
    // in three calls hides the bug, because then cells sharing a raw label straddle the
    // split and the check trips for the wrong reason.)
    // pre-edit component root -> the local components its boundary cells landed in, in
    // first-seen (boundary) order, each with those boundary cells as flood seeds.
    std::map<int32_t, std::vector<std::pair<int32_t, std::vector<int>>>> pieces;
    bool possibleSplit = false;
    for (int idx : boundary) {
        const int bx = idx % w, bz = idx / w;
        const int32_t pre = cg.root(labelAt(bx, bz));
        if (pre < 0) continue;
        const int32_t lc = loc[size_t(bz - wz0) * size_t(ww) + size_t(bx - wx0)];
        auto& ps = pieces[pre];
        auto it = std::find_if(ps.begin(), ps.end(), [&](const auto& p) { return p.first == lc; });
        if (it == ps.end()) { ps.push_back({lc, {idx}}); possibleSplit |= ps.size() > 1; }
        else it->second.push_back(idx);
    }
    if (!possibleSplit) {
        for (int idx : removed) cg.raw_[size_t(idx)] = -1;
        ++compStats_.blockCarried;
        return true;
    }

    // Some component's boundary cells landed in different local components: the edit
    // may have split it, or they rejoin outside the window. Settle each such component
    // over the whole grid. That used to be a whole-map relabel, ~90ms on a 2048x2048
    // grid and 16-24 times a minute in a late 8-AI game -- and in a user's 23-minute
    // replay 164 of its 200 were no split at all, while the 36 real ones cut off 195
    // cells between them.
    std::vector<std::vector<int>> cut;
    for (const auto& [pre, ps] : pieces) {
        if (ps.size() < 2) continue;
        std::vector<std::vector<int>> seeds;
        for (const auto& p : ps) seeds.push_back(p.second);
        if (!resolveSplit(g, foot, seeds, wx0, wz0, wx1, wz1, okw, cut)) {
            ++compStats_.splitFallback;
            return false;
        }
    }
    // Each cut-off piece takes a fresh id of its own; the piece left out of `cut` keeps
    // the old root. Fresh ids are roots, so no alias can join a piece back to it.
    for (const auto& cells : cut) {
        const int32_t id = int32_t(cg.alias.size());
        cg.alias.push_back(id);
        for (int idx : cells) cg.raw_[size_t(idx)] = id;
    }
    for (int idx : removed) cg.raw_[size_t(idx)] = -1;
    ++(cut.empty() ? compStats_.splitJoined : compStats_.splitCut);
    return true;
}

// Every still-fitting cell of the component that was cut reaches some boundary cell of
// it without crossing the edit (walk its old route to the edit: the last step that is
// still legal starts on a boundary cell), so the pieces the seeds grow into are ALL of
// what is left of it. Flood each piece on the full connectivity rule, a batch each in
// turn. Seeds are marked before any flood moves, so two pieces of one component always
// meet: whichever reaches the other's ground first sees its mark, and they are united.
// A piece whose flood runs dry without meeting another has been flooded to its last
// cell and is a component of its own. Once at most one piece is still growing, every
// other is settled -- and the growing one is the remainder, which keeps the old root
// untouched (if all of them closed, the largest keeps it). So sealing off a pocket
// costs about pieces x the pocket, not the map.
//
// Which cell a flood takes next cannot change what it ends up holding, only how soon
// it meets another, so each takes the cell nearest the other pieces first. Two sides
// that rejoin around a wall then follow the wall toward each other instead of filling
// the open ground behind them, which is what made a plain breadth-first flood run out
// of budget on the very case it exists for.
bool World::resolveSplit(const NavGrid& g, int foot, const std::vector<std::vector<int>>& seeds,
                         int wx0, int wz0, int wx1, int wz1, const std::vector<uint8_t>& okw,
                         std::vector<std::vector<int>>& cut) const {
    const int w = g.width(), h = g.height();
    const size_t n = seeds.size(), cells = size_t(w) * size_t(h);
    const int ww = wx1 - wx0 + 1;
    auto fitsAt = [&](int x, int z) -> bool {
        if (x < 0 || z < 0 || x >= w || z >= h) return false;
        if (x >= wx0 && z >= wz0 && x <= wx1 && z <= wz1)
            return okw[size_t(z - wz0) * size_t(ww) + size_t(x - wx0)] != 0;
        return g.fits(x, z, foot);
    };
    // Marks are epochs: this call owns [base, base+n), anything else reads as unvisited,
    // so the grid-sized array is never cleared between calls (only on wrap).
    if (compMark_.size() != cells) { compMark_.assign(cells, 0); compMarkTop_ = 0; }
    if (compMarkTop_ > UINT32_MAX - uint32_t(n) - 1) {
        std::fill(compMark_.begin(), compMark_.end(), 0u);
        compMarkTop_ = 0;
    }
    const uint32_t base = compMarkTop_ + 1;
    compMarkTop_ += uint32_t(n);

    std::vector<size_t> parent(n);
    for (size_t i = 0; i < n; ++i) parent[i] = i;
    auto find = [&](size_t i) {
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
        return i;
    };
    // Where each piece is aimed: one seed of every piece still growing and not yet
    // joined to it (a closed piece can never be met).
    std::vector<std::pair<int, int>> rep(n);
    for (size_t i = 0; i < n; ++i) rep[i] = {seeds[i].front() % w, seeds[i].front() / w};
    std::vector<uint8_t> growing(n, 1);       // by group representative
    std::vector<std::vector<std::pair<int, int>>> aims(n);   // by group representative
    auto aim = [&] {
        for (size_t i = 0; i < n; ++i) {
            aims[i].clear();
            if (find(i) != i) continue;
            for (size_t j = 0; j < n; ++j)
                if (find(j) != i && growing[find(j)]) aims[i].push_back(rep[j]);
        }
    };
    aim();
    auto nearness = [&](size_t i, int x, int z) {
        uint32_t best = UINT32_MAX;
        for (const auto& [ax, az] : aims[find(i)]) {
            const uint32_t dx = uint32_t(std::abs(x - ax)), dz = uint32_t(std::abs(z - az));
            best = std::min(best, std::max(dx, dz) * 2 + std::min(dx, dz));   // ~octile
        }
        return best;
    };
    struct Next { uint32_t d, seq; int idx; };
    auto later = [](const Next& a, const Next& b) { return a.d != b.d ? a.d > b.d : a.seq > b.seq; };
    std::vector<std::vector<Next>> open(n);   // per flood, a heap nearest-first
    std::vector<std::vector<int>> held(n);    // every cell each flood has marked
    uint32_t seq = 0;
    auto take = [&](size_t i, int idx) {
        compMark_[size_t(idx)] = base + uint32_t(i);
        held[i].push_back(idx);
        open[i].push_back({nearness(i, idx % w, idx / w), seq++, idx});
        std::push_heap(open[i].begin(), open[i].end(), later);
    };
    for (size_t i = 0; i < n; ++i)
        for (int idx : seeds[i]) {
            const uint32_t m = compMark_[size_t(idx)];
            if (m >= base && m < base + uint32_t(n)) continue;   // seeded twice
            take(i, idx);
        }

    static const int dcx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dcz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    // A whole-map relabel is the fallback, so give up well before costing as much.
    const size_t budget = cells / 8;
    constexpr size_t kBatch = 64;
    size_t work = 0, lastActive = n;
    for (;;) {
        std::fill(growing.begin(), growing.end(), uint8_t(0));
        size_t active = 0;
        for (size_t i = 0; i < n; ++i)
            if (!open[i].empty() && !growing[find(i)]) { growing[find(i)] = 1; ++active; }
        if (active <= 1) break;
        if (active != lastActive) {           // pieces met or closed: re-aim the rest
            aim();
            for (size_t i = 0; i < n; ++i) {
                for (Next& e : open[i]) e.d = nearness(i, e.idx % w, e.idx / w);
                std::make_heap(open[i].begin(), open[i].end(), later);
            }
            lastActive = active;
        }
        for (size_t i = 0; i < n; ++i)
            for (size_t b = 0; b < kBatch && !open[i].empty(); ++b) {
                std::pop_heap(open[i].begin(), open[i].end(), later);
                const int cur = open[i].back().idx;
                open[i].pop_back();
                const int cx = cur % w, cz = cur / w;
                bool orth[4];
                for (int k = 0; k < 4; ++k) orth[k] = fitsAt(cx + dcx[k], cz + dcz[k]);
                for (int k = 0; k < 8; ++k) {
                    if (k < 4 ? !orth[k]
                              : (!orth[dcx[k] > 0 ? 0 : 1] || !orth[dcz[k] > 0 ? 2 : 3] ||
                                 !fitsAt(cx + dcx[k], cz + dcz[k])))
                        continue;                        // same rule as components()
                    const int ni = (cz + dcz[k]) * w + (cx + dcx[k]);
                    const uint32_t m = compMark_[size_t(ni)];
                    if (m >= base && m < base + uint32_t(n)) {
                        const size_t a = find(i), c = find(m - base);
                        if (a != c) parent[std::max(a, c)] = std::min(a, c);   // they meet
                        continue;
                    }
                    take(i, ni);
                }
                if (++work > budget) return false;
            }
    }

    // Settled pieces, by representative flood. The still-growing one (if any) keeps the
    // old root; otherwise the largest does, so the fewest cells are rewritten.
    std::vector<size_t> size(n, 0);
    int keep = -1;
    for (size_t i = 0; i < n; ++i) {
        size[find(i)] += held[i].size();
        if (!open[i].empty()) keep = int(find(i));
    }
    if (keep < 0)
        for (size_t i = 0; i < n; ++i)
            if (find(i) == i && (keep < 0 || size[i] > size[size_t(keep)])) keep = int(i);
    for (size_t r = 0; r < n; ++r) {
        if (find(r) != r || int(r) == keep) continue;
        std::vector<int> piece;
        piece.reserve(size[r]);
        for (size_t i = 0; i < n; ++i)
            if (find(i) == r) piece.insert(piece.end(), held[i].begin(), held[i].end());
        cut.push_back(std::move(piece));
    }
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
    ++compStats_.unblockCarried;
    return true;
}

const World::CompGrid* World::components(const NavGrid& g, int foot) const {
    if (g.empty()) return nullptr;
    auto& cg = compCache_[{&g, foot}];
    if (cg.ver == g.version() && cg.w == g.width() && cg.h == g.height()) {
        // A current entry may have been carried across edits rather than built: check
        // it once per version against a from-scratch labelling (debug only, slow).
        static const bool kCheck = tak::devFlag("TAK_COMPCHECK");
        if (kCheck && cg.checkedVer != cg.ver) {
            verifyComponents(g, foot, cg);
            cg.checkedVer = cg.ver;
        }
        return &cg;
    }
    ++compStats_.relabels;
    labelComponents(g, foot, cg);
    cg.checkedVer = cg.ver;   // built from scratch: nothing to check
    return &cg;
}

void World::verifyComponents(const NavGrid& g, int foot, const CompGrid& cg) const {
    CompGrid fresh;
    labelComponents(g, foot, fresh);
    // Same partition iff the fresh id <-> carried root correspondence is a bijection
    // and both agree on where nothing fits.
    std::vector<int32_t> toRoot(fresh.alias.size(), -1), toFresh(cg.alias.size(), -1);
    for (int z = 0; z < cg.h; ++z)
        for (int x = 0; x < cg.w; ++x) {
            const int32_t a = fresh.componentAt(x, z), b = cg.componentAt(x, z);
            bool ok = (a < 0) == (b < 0);
            if (ok && a >= 0) {
                if (toRoot[size_t(a)] < 0 && toFresh[size_t(b)] < 0) {
                    toRoot[size_t(a)] = b; toFresh[size_t(b)] = a;
                }
                ok = toRoot[size_t(a)] == b && toFresh[size_t(b)] == a;
            }
            if (!ok) {
                std::fprintf(stderr, "[compcheck] MISMATCH tick=%u foot=%d %dx%d cell=(%d,%d) "
                             "fresh=%d carried=%d stats relabels=%llu block=%llu joined=%llu "
                             "cut=%llu fallback=%llu unblock=%llu\n",
                             tickCounter_, foot, cg.w, cg.h, x, z, a, b,
                             (unsigned long long)compStats_.relabels,
                             (unsigned long long)compStats_.blockCarried,
                             (unsigned long long)compStats_.splitJoined,
                             (unsigned long long)compStats_.splitCut,
                             (unsigned long long)compStats_.splitFallback,
                             (unsigned long long)compStats_.unblockCarried);
                std::abort();
            }
        }
}

void World::labelComponents(const NavGrid& g, int foot, CompGrid& cg) {
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

bool World::buildSiteReachable(int builderId, const UnitType* site, float x, float z) const {
    const Unit* b = unit(builderId);
    if (!site || !b || !b->alive() || !b->type || b->embarked() || b->type->canFly ||
        b->type->isStructure()) return true;
    const UnitType& bt = *b->type;
    const NavGrid& grid = navFor(&bt);
    if (grid.empty()) return true;
    const int foot = std::clamp(std::max(bt.footX, bt.footZ), 1, 15);
    const CompGrid* cg = components(grid, foot);
    if (!cg || cg->empty()) return true;
    // Where the builder can be: its own cell's component, or (shoved off a cell its
    // body fits, as pathExists allows) the nearest one.
    int32_t from = cg->componentAt(footprintCell(b->x, foot), footprintCell(b->z, foot));
    if (from < 0) {
        const int cx = std::clamp(footprintCell(b->x, foot), 0, cg->w - 1);
        const int cz = std::clamp(footprintCell(b->z, foot), 0, cg->h - 1);
        for (int r = 1; r < 24 && from < 0; ++r)
            for (int j = -r; j <= r && from < 0; ++j)
                for (int i = -r; i <= r; ++i) {
                    if (std::max(std::abs(i), std::abs(j)) != r) continue;
                    if ((from = cg->componentAt(cx + i, cz + j)) >= 0) break;
                }
    }
    if (from < 0) return true;   // wedged nowhere a body fits: leave it to the order
    // The site as makeBuildOrder snaps it (Order::buildX/buildZ).
    const Fixed gx = footprintWaypoint(footprintCell(Fixed::fromFloat(x), site->footX), site->footX);
    const Fixed gz = footprintWaypoint(footprintCell(Fixed::fromFloat(z), site->footZ), site->footZ);
    const int reach = std::max(0, int(bt.buildDist));
    // Every cell that can pass the test lies within reach plus both footprint
    // half-diagonals (plus a cell of rounding) of the site.
    const int span = (reach + 12 * (bt.footX + bt.footZ + site->footX + site->footZ)) / 16 + 2;
    const int gcx = footprintCell(gx, foot), gcz = footprintCell(gz, foot);
    // The common case, any site on the builder's own ground: it can stand on the site
    // itself, which is within reach of anything. Answered without the scan below.
    if (cg->componentAt(gcx, gcz) == from) return true;
    for (int cz = std::max(0, gcz - span); cz <= std::min(cg->h - 1, gcz + span); ++cz)
        for (int cx = std::max(0, gcx - span); cx <= std::min(cg->w - 1, gcx + span); ++cx) {
            if (cg->componentAt(cx, cz) != from) continue;
            if (retailBuildWithinReach(footprintWaypoint(cx, foot), footprintWaypoint(cz, foot), gx, gz,
                    int16_t(bt.footX), int16_t(bt.footZ), int16_t(site->footX), int16_t(site->footZ),
                    uint16_t(reach))) return true;
        }
    return false;
}

bool World::scriptYardOpen(int unitId) const {
    return size_t(unitId)<scriptYardById_.size() && scriptYardById_[size_t(unitId)];
}

bool World::canLoadInto(int unitId,int transportId) const {
    const Unit* u=unit(unitId);const Unit* t=unit(transportId);
    if (!u || !t || u==t || !u->alive() || !t->alive() || u->embarked() || t->embarked() ||
        !u->type || !t->type || u->underConstruction || t->underConstruction) return false;
    // Paralyze sets unit+114 bit 0x10 (402202 -> 51e4d0), which
    // native 519f50 rejects on either member of the boarding pair.
    if(u->paralyzedFor>0 || t->paralyzedFor>0)return false;
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

void World::loadInto(int unitId, int transportId, bool queue) {
    OrdersNote note{*this,unitId};
    if (!canLoadInto(unitId,transportId)) return;
    Unit* u=unit(unitId);
    if (!queue) {
        // 4d78a0 replaces ordinary construction missions when a fresh
        // Move_Seek_Pickup is issued. Production lives outside orders here,
        // so retire that work too instead of resuming it after unloading.
        if(u->productionSiteId)notifyUnitScript(*u,"StopBuilding");
        cancelBuilds(unitId);
        stop(unitId);
    }
    Order o;
    o.targetId = transportId;
    o.load = true;
    o.goal = true;
    o.transportMission={};
    o.transportProductionAhead=queue ? uint32_t(u->buildQueue.size()) : 0;
    u->orders.push_back(o);
    Unit* carrier=unit(transportId);
    OrdersNote carrierNote{*this,transportId};
    {
        // GROUND_PICKUP and VTOL_PICKUP both own a reciprocal carrier mission.
        // Only its active passenger may enter the transfer stage.
        if(!queue && (carrier->orders.empty() || !carrier->orders.front().transportPickup)) {
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

void World::unloadAt(int transportId, float x, float z, Fixed destinationY, bool queue) {
    OrdersNote note{*this,transportId};
    Unit* t = unit(transportId);
    if (!t || !t->alive() || !t->type || !t->type->canTransport ||
        (!queue && t->cargo.empty())) return;
    // A queued pickup can supply cargo before this mission becomes active.
    // Inspect cargo in the unload handler, not while appending the command.
    if (!queue) {
        t->orders.clear();
        // A search for the orders just discarded is still queued, and cases 1 and 3 below
        // re-request nothing -- so it would land on the bare unload and rewrite it.
        cancelPath(*t);
    }
    // Retail routes surface carriers to a circle centered on the exact drop point,
    // with radius transportdistance-34 (408e84/408d50). VTOL_UNLOAD installs the
    // same-radius flight controller before it steps out for transfer. Keep that
    // mission on the active flying order so the carrier does not overshoot to the
    // exact drop point before its unload handler wakes.
    const bool inRange=retailTransportInRange((Fixed::fromFloat(x)-t->x).v,
        (Fixed::fromFloat(z)-t->z).v,uint16_t(t->type->transportDist));
    // Earlier queued orders can move the carrier out of today's range. Keep
    // the approach mission so its activation checks the then-current position
    // and installs the native circle, rather than moving to the drop point.
    const bool needsApproach=!inRange || (queue && !t->orders.empty());
    if (needsApproach && t->type->canFly) {
        Order approach;
        approach.x=Fixed::fromFloat(x);approach.z=Fixed::fromFloat(z);
        approach.goal=true;approach.unload=true;approach.transportUnloadApproach=true;
        approach.transportMission=RetailMissionState{1};
        approach.transportY=destinationY;
        approach.missionTarget=std::pair{approach.x,approach.z};
        t->orders.push_back(std::move(approach));
        return;
    }
    if (needsApproach) {
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
        // Retail retires (VTOL_)UNLOAD inside its dispatcher, which builds the
        // default mission in that same call when nothing else is queued (4d856a).
        if(u.orders.empty())beginStandby(u);
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
        if(passenger && (u.type->canFly || !o.controller)) {o.x=passenger->x;o.z=passenger->z;}
        auto& mission=o.transportMission;
        // Native 519a60 invalidates target references at the death edge and
        // posts event 8 to their owning missions (519a30).
        if(!passenger || !passenger->alive())mission.pending|=8;
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
            if(!passenger)return advancePickup();
            o.flightGoal=retailPickupPursuitGoal({passenger->x.v,passenger->groundY.v,passenger->z.v},
                uint16_t(u.type->transportDist));
            tickFlightMovement(u,true);notifyFlightOccupancy(u);
            return true;
        };
        if(tickCounter_>=mission.deadline) {
            mission.deadline=0xffffffffu;mission.pending|=1;
        }
        const uint32_t events=(u.missionEvents|mission.pending)&mission.waitMask;
        if(mission.waitMask && !events)
            return mission.stage==1 && (mission.waitMask&0x700u) ? advanceApproach() : advancePickup();
        u.missionEvents&=~events;mission.pending&=~events;mission.waitMask=0;
        // 4d8450 only enters the carrier handler after an admitted wake.
        // A passenger order change must not bypass this sleep, and event 8
        // explicitly aborts both native pickup handlers before stage dispatch.
        if((events&8u) || !passenger || !canLoadInto(passenger->id,u.id) || passenger->orders.empty() ||
            !passenger->orders.front().load || passenger->orders.front().transportProductionAhead ||
            passenger->orders.front().targetId!=u.id) {
            removePickup();return true;
        }
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
            // Native BeCarried (4024e2) retires weapon targets on boarding.
            // Pickup bypasses tickCombat, so its usual target-clear path has
            // not run. Publish the same callback to the display as well.
            if(passenger->scriptAimTarget)clearScriptWeaponTarget(*passenger);
            groundRemove(*passenger);   // World keeps cargo off the ground word
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
            if(!request.load || request.transportProductionAhead || request.targetId!=u.id)continue;
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
        // Mobile production is stored separately from movement orders. An
        // appended pickup is not the current retail mission until that work
        // finishes or is canceled; do not dispatch it ahead of production.
        if(o.transportProductionAhead) {brakeGround(u);return true;}
        Unit* t=unit(o.targetId);
        auto remove=[&] {
            cancelPath(u);
            u.orders.erase(u.orders.begin(),u.orders.begin()+long(pickupLeg)+1);
        };
        auto& mission=o.transportMission;
        if(!t || !t->alive())mission.pending|=8;
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
        // GROUND_UNLOAD's blocked-placement retry briefly returns its
        // RetailMissionState to stage 1 before it starts transfer stage 2
        // again. The native mover still consumes any remaining surface
        // momentum during that sleep. Treat every live mission stage as an
        // active surface route so this tick's refusal/retry path does not
        // zero speed in the middle of the coast-down.
        (o.controller || ((o.transportMission.stage>0 || o.transportUnloadReleasePending) &&
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
    groundStamp(*c);
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
        noteOrders(c->id);
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
    if (u->type->isStructure() || u->repeatType) {
        Order o{Fixed::fromFloat(x), Fixed::fromFloat(z), 0};
        o.attackMove = true;
        setRally(*u, o, queue);
        return;
    }
    size_t before = queue ? u->orders.size() : 0;
    order(unitId, x, z, queue, ConvoyClass::Fight);
    for (size_t i = before; i < u->orders.size(); ++i) {
        u->orders[i].attackMove = true;
        u->orders[i].flightMoveMission = false;
    }
}

void World::patrol(int unitId, float x, float z) {
    OrdersNote note{*this,unitId};
    Unit* u = unit(unitId);
    // isStructure(), NOT canMove: the Keep and both Taros/Veruna walls declare
    // canmove=1 with no velocity (the CLAUDE.md gotcha). Gating on canMove let a
    // BUILDING accept a move order -- it could not go anywhere, but the mover still
    // turned its heading toward the goal, so you could spin a keep by right-clicking.
    if (!u || !u->alive() || !u->type) return;
    if (u->type->isStructure() || u->repeatType) {
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
    if (u->type->canFly) {
        // Flying patrols keep their existing handler and definition flags.
        u->orders.clear();cancelPath(*u);
        Order a{u->x,u->z,0},b{Fixed::fromFloat(x),Fixed::fromFloat(z),0};
        a.goal=a.patrol=a.attackMove=true;
        b.goal=b.patrol=b.attackMove=true;
        // The outbound leg joins the click's Patrol convoy; the return leg (the
        // flyer's own position) copies it rather than open a convoy of its own.
        if(isLegionPathfinding(pathfindingMode_)) {const auto id=stampIdentity(*u,ConvoyClass::Patrol,b.x,b.z);a.setIdentity(id);b.setIdentity(id);}
        u->orders.push_back(b);u->orders.push_back(a);
        return;
    }
    const float rx=u->x.toFloat(),rz=u->z.toFloat();
    patrolTo(unitId,x,z,false);
    const OrderIdentity outbound=u->orders.empty()?OrderIdentity{}:u->orders.back().identity();
    patrolTo(unitId,rx,rz,true,outbound);
}

void World::patrolTo(int unitId, float x, float z, bool queue, std::optional<OrderIdentity> identityCopy) {
    Unit* u = unit(unitId);
    // isStructure(), NOT canMove: the Keep and both Taros/Veruna walls declare
    // canmove=1 with no velocity (the CLAUDE.md gotcha). Gating on canMove let a
    // BUILDING accept a move order -- it could not go anywhere, but the mover still
    // turned its heading toward the goal, so you could spin a keep by right-clicking.
    if (!u || !u->alive() || !u->type) return;
    if (u->type->isStructure() || u->repeatType) {
        Order o{Fixed::fromFloat(x), Fixed::fromFloat(z), 0};
        o.patrol = true;
        o.attackMove = true;
        setRally(*u, o, queue);
        return;
    }
    size_t before = queue ? u->orders.size() : 0;
    order(unitId, x, z, queue, ConvoyClass::Patrol, identityCopy);
    // Mark every waypoint of this move as a looping, engage-en-route patrol leg; a
    // chain of patrolTo calls then cycles the unit through all of them.
    for (size_t i = before; i < u->orders.size(); ++i) {
        u->orders[i].patrol = true;
        u->orders[i].attackMove = true;
        u->orders[i].flightMoveMission = false;
    }
}

void World::queuePatrol(int unitId, float x, float z) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type) return;
    const size_t before = u->orders.size();
    patrolTo(unitId, x, z, true);
    // Producers keep rally lists and unarmed flyers the VTOL patrol handler
    // (plainFlightPatrol), which appends its return point when the patrol
    // starts. Armed and builder flyers fly patrols on the ordinary VTOL_Move
    // path and close the loop in openFlightPatrol; without it a lone queued
    // point was reached once and then hovered over for good.
    if (u->type->isStructure() || u->repeatType) {
        // A queued patrol rally step stays OPEN, as a unit's own queued patrol
        // does: each output closes the loop from where it starts that leg, so
        // "move A, Shift-patrol B" makes every output patrol A <-> B.
        if ((u->type->producesUnits() || u->repeatType) && !u->rally.empty())
            u->rally.back().patrolOpen = true;
        return;
    }
    if (u->type->canFly && !u->type->isBuilder && u->type->weapons.empty()) return;
    for (size_t i = before; i < u->orders.size(); ++i) u->orders[i].patrolOpen = true;
}

void World::orderWait(int unitId, float seconds, bool queue) {
    OrdersNote note{*this,unitId};
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
    OrdersNote note{*this,unitId};
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
    OrdersNote note{*this,unitId};
    Unit* u = unit(unitId);
    Unit* t = unit(targetId);
    if (!u || !t || !u->alive() || !t->alive() || u->id == t->id) return;
    if (!u->type || !u->type->canMove || u->player != t->player) return;
    if (!queue) {
        u->orders.clear();
        cancelPath(*u);
        u->routeStamp = -1;
        u->guardHelpTick = 0;   // a fresh guard looks for work at once
        u->guardHeard = t->attackedTick;   // nor hears an alarm older than itself
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
    u->rally.clear();        // a producer's Stop drops the plan it hands its output too
    u->landing.reset(); u->standbyActive=false;
    cancelPath(*u);          // stop means stop: do not finish routing where it was going
    // Stop also halts a conjurer: cancel the infinite loop and drain the queue.
    u->repeatType = nullptr;
    killProductionOutput(*u,true);   // a factory's cancelled output dies at once
    u->buildQueue.clear();
    u->buildProgress = 0; u->buildFraction = 0;
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
    // 51a8b0 accepts only a different, present weapon on a switching unit.
    // Re-selecting the current slot must not restart its reload countdown.
    if (!u->type->weaponSwitching || slot < 0 || slot >= std::min(n,3) ||
        slot == u->weaponSlot) return;
    u->weaponAuto = false;
    switchWeaponSlot(*u,slot,true);
}

// 51a8b0: drop the old slot's target (TargetCleared only when one was set),
// select the new slot, give it its switchreloadtime and start COB
// SwitchWeapon(slot). The +0xd0 0x40 event bit it also sets has no known
// gameplay reader and is not modelled.
bool World::switchWeaponSlot(Unit& u,int slot,bool fromCommand) {
    const int n=int(u.type->weapons.size());
    if (!u.type->weaponSwitching || slot<0 || slot>=std::min(n,3) || slot==u.weaponSlot) return false;
    if (u.scriptAimTarget) clearScriptWeaponTarget(u,fromCommand);
    dropSlotTarget(u,u.weaponSlot);
    u.weaponSlot=slot;
    u.reloads[size_t(slot)]=u.type->weapons[size_t(slot)].switchReloadTicks;
    if (const auto script=unitScripts_.find(u.id);script!=unitScripts_.end()) {
        const auto& file=*u.type->script();
        script->second.state().startArguments(file,file.scriptIndex("SwitchWeapon"),{uint32_t(slot),0,0,0},1);
        if (fromCommand) u.pendingWeaponAnimations.push_back({tak::RetailWeaponAnimation::Switch,uint8_t(slot),0,0});
        else u.weaponAnimations.add(tak::RetailWeaponAnimation::Switch,slot);
    }
    return true;
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
    if(u->cloakOn==on)return;
    u->cloakOn = on;
    // Native Cloak/Decloak missions (402560/4025b0) notify the script when
    // the requested mode changes, independently of visibility or mana payment.
    if(auto it=unitScripts_.find(u->id);it!=unitScripts_.end()) {
        const auto& file=*u->type->script();
        it->second.state().startArguments(file,file.scriptIndex(on ? "StartCloaking" : "StopCloaking"),{},0);
    }
}

void World::setActive(int unitId, bool on) {
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type || !u->type->onOffable) return;
    if (u->active==on) return;
    u->active = on;
    // 51e4d0 notifies every script on an active-bit edge, including
    // non-gate on/off units such as the Sacred Fire's emitter controller.
    if (auto script=unitScripts_.find(u->id);script!=unitScripts_.end())
        script->second.activated=on;
    notifyUnitScript(*u,on ? "Activate" : "Deactivate");
}

void World::setSquad(int unitId, int squad) {
    Unit* u = unit(unitId);
    if (!u || !u->alive()) return;
    u->squad = int8_t(std::clamp(squad, -10, 10));   // 0 none, +N group N, -N formation N
}

void World::attack(int unitId, int targetId, bool queue) {
    OrdersNote note{*this,unitId};
    Unit* u = unit(unitId);
    if (!u || !u->alive() || !u->type) return;
    // A production building sends its OUTPUT at the target rather than shooting it
    // itself -- and it may well have no weapon, so this is checked before the
    // weapon gate below. A mobile builder on infinite production does the same:
    // while it repeats, every order it accepts is a rally for its output (the
    // rule Move and Patrol already follow), so an armed one does not walk off.
    if ((u->type->isStructure() && u->type->producesUnits()) || u->repeatType) {
        Order o{Fixed(), Fixed(), targetId};
        setRally(*u, o, queue);
        return;
    }
    if (u->type->weapon.damage <= 0) return;
    const Unit* target=unit(targetId);
    const bool converts=u->type->canCapture ||
        std::any_of(u->type->weapons.begin(),u->type->weapons.end(),
                    [](const Weapon& weapon){return weapon.mindControl;});
    if (converts && (!target || !canAttackTarget(*u,*target))) return;
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
// same id. Damage uses the single DamageCategory token, not CATEGORY/TEDClass.
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
        t.damageCategoryId = t.damageCategory.empty() ? -1 : idOf(t.damageCategory);
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
    // KINGDOMS.icd 531e50 reads only UnitDef+9e (DamageCategory); 531de0
    // returns the precomputed integer table entry, or the unsigned default.
    if (!t) return damage;
    if (t->damageCategoryId >= 0 && !dmgVsIds.empty()) {
        for (const auto& [key, amount] : dmgVsIds)
            if (key == t->damageCategoryId) return float(amount);
        return damage;
    }
    const auto it = dmgVs.find(t->damageCategory);
    return it == dmgVs.end() ? damage : float(it->second);
}

void World::applyHit(const Weapon& w, float hx, float hz, int fromPlayer, int fromId,
                     Unit* primary, std::optional<std::array<int32_t,3>> position,
                     std::optional<std::array<int32_t,3>> impact) {

    const size_t hitIndex = hits_.size();
    // Record the impact for the viewer (hit sound / effect).
    {
        HitFx hf{hx, hz, &w, primary ? primary->type : nullptr};
        if (primary) hf.victimId = primary->id;
        const Unit* from = fromId ? unit(fromId) : nullptr;
        hf.fromX = from ? from->x.toFloat() : hx;
        hf.fromZ = from ? from->z.toFloat() : hz;
        hf.fromPlayer = fromPlayer;
        // Capture display geometry before damage/death callbacks and before the
        // next movement tick. Never infer height from another nearby aircraft.
        if (position) hf.position = position;
        else if (primary) hf.position = std::array<int32_t,3>{Fixed::fromFloat(hx).v,
            (primary->type && primary->type->canFly ? primary->flightY : primary->groundY).v,
            Fixed::fromFloat(hz).v};
        if (from) hf.fromPosition = std::array<int32_t,3>{from->x.v,
            (from->type && from->type->canFly ? from->flightY : from->groundY).v,from->z.v};
        hits_.push_back(hf);
    }
    // Attacker's veteran attack multiplier boosts damage dealt (retail scales the
    // attacker's attack stat by vetMul); the victim's boosts armour (below).
    const Unit* attacker = fromId ? unit(fromId) : nullptr;
    // Attack multiplier = veterancy × live aura buff (AdjustAttack).
    float atkMul = attacker ? attacker->vetMul() * attacker->atkBuff * attacker->scenarioWeapon : 1.0f;
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
            // A previously launched charm may arrive after another conversion.
            if (allied(fromPlayer,e.player)) return;
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
        // Base × attacker attack (↑) ÷ victim armour (↓) × splash falloff. Retail
        // 0x52a330 then consumes one CRT draw per recipient and applies its
        // asymmetric ±15% integer spread before the HP update. Keep this stream
        // separate from gameRng_: pathfinding shares the latter's exact sequence.
        float armour = std::max(e.vetMul() * e.armBuff * e.scenarioArmor, 0.01f);
        float scaledDamage = w.damageVs(e.type) * atkMul / armour * scale;
        damageCrtUsed_ = true;
        const int dealt = retailDamageWithSpread(scaledDamage, crtRand(0x52a3ba), uint8_t(damageMode_));
        // Retail 51a140/51a240: a unit whose HP has reached zero is flagged
        // dying (+0x130 |= 0x1000, 51a639) on the spot, and every later hit
        // that tick returns before touching it -- no HP, no kill credit, no
        // death type, no HitByWeapon, no status. The CRT draw above (52a3ba)
        // is still consumed. Our hp<=0 sweep runs later in the tick, so the
        // dying test is the sweep's own. A retail construction site at zero
        // is not dying and keeps taking damage.
        if (e.hp <= Fixed() && !(e.retailSite && e.underConstruction)) return;
        // Native HP is a 16-bit word that wraps; ours is 16.16 and saturates,
        // and `dealt` may exceed the whole-number range of fromInt (armour
        // and veterancy reach 33619). Subtract in 64 bits, clamped so that
        // -hp (the overkill below) is representable too.
        e.hp = Fixed::raw(int32_t(std::clamp<int64_t>(int64_t(e.hp.v) - int64_t(dealt) * Fixed::kOne,
                                                      -int64_t(INT32_MAX), INT32_MAX)));
        // Retail 51a3c2..51a408: every non-heal hit on an unfinished site (progress
        // remaining at +0x108 nonzero) rewrites remaining = 1 - hp/maxdamage from
        // the new SIGNED hp word (movsx 51a3e0), whatever the damage source. Without
        // it the next work tick restored hp from the stale progress and erased the
        // damage. A lethal hit leaves remaining above 1; read unsigned, the word made
        // it hugely negative and the next work tick completed the site at full HP.
        if (e.retailSite && e.underConstruction && e.retailSite->progress.remaining!=0) {
            auto& progress=e.retailSite->progress;
            progress.hp=uint16_t(e.hp.floorInt());
            progress.remaining=float(1.0-double(int16_t(progress.hp))/std::max(1,e.maximumHp()));
        }
        // Retail 409f7a..409fb2 routes the local monarch's hit to AlarmMon.
        // Keep ownership at impact, including splash and fatal hits; no visibility
        // gate and no simulation/RNG changes for this presentation event.
        if (dealt > 0 && e.type && e.type->commander && e.player >= 0 && e.player < 8 &&
            (fromPlayer != e.player || w.dmgType == 1))
            hits_[hitIndex].attackedMonarchPlayers |= uint8_t(1u << e.player);
        // 52a40a forms the hit bearing from the impact position, relative to
        // the victim's current native heading. 51a140 retains its high byte;
        // 51a4d2 notifies each normal/fire/explosion recipient, including splash.
        if(w.status==Weapon::Status::None && w.dmgType>=1 && w.dmgType<=3) {
            if(auto script=unitScripts_.find(e.id);script!=unitScripts_.end()) {
                const auto& file=*e.type->script();
                const int callback=file.scriptIndex("HitByWeapon");
                if(callback>=0) {
                    const uint16_t bearing=uint16_t(retailDirection(Fixed::fromFloat(hx)-e.x,
                        Fixed::fromFloat(hz)-e.z).v-portHeadingToRetail(e.heading))&0xff00u;
                    const uint16_t damage=uint16_t(dealt);
                    e.weaponAnimations.add(tak::RetailWeaponAnimation::Hit,w.dmgType,bearing,0,damage);
                    script->second.state().startArguments(file,callback,
                        {uint32_t(w.dmgType),uint32_t(retailScaledCosine(bearing,400)),
                         uint32_t(retailScaledSine(bearing,400)),damage},4);
                }
            }
        }
        if (e.hp <= Fixed()) {
            e.overkill = fxMax(e.overkill, -e.hp);          // retail severity input
            e.deathType = uint8_t(w.dmgType);                  // 3 = explosion -> gib
        }
        if (fromId) e.lastHitBy = fromId;
        e.lastHitPlayer = fromPlayer;
        retailHitResponse(e,attacker,w.dmgType);
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
                    static const bool kStatLog = tak::devEnv("TAK_BURNLOG") != nullptr;
                    if (kStatLog)
                        std::fprintf(stderr, "statue kill: %s %s at %.0f,%.0f\n",
                                     e.type->id.c_str(), freeze ? "frozen" : "stoned",
                                     e.x.toFloat(), e.z.toFloat());
                }
            }
        }
    };
    // Retail 529c10: a shot that struck a unit with a weapon of areaofeffect
    // <= 16 is the direct hit 52a460 alone, full damage to that unit and no
    // splash or scenery. Every other impact is the area pass 529dc0 below,
    // where the struck unit takes the same falloff as any other.
    const uint16_t aoe = uint16_t(w.aoe);
    if (primary && retailDirectHitOnly(aoe, true)) { hurt(*primary, 1.0f); return; }
    // Features under fire (retail icd 0x529dc0 -> DamageFeature 0x4961a0):
    // every feature head cell within aoe/2 of the impact -- the impact cell
    // (resolved to its head for footprint tails) exempt from the radius check,
    // so a no-splash weapon (Fire Breath, aoe 0)
    // still strikes the cell it hits. A firestarter hit on a flamable feature
    // IGNITES it instead of damaging it that hit; everything else accumulates
    // weapon damage, and at the def's `damage` value the feature is destroyed
    // -- swapped to its `featuredead` stage (npcwreck chains) or removed.
    // Deterministic in every peer's sim (state + RNG hashed).
    // unitsonly=1: the effect touches UNITS only, never the scenery. Retail sets it
    // on the big area spells (Earthen/Wind Wave, Death Aura, Energy Blast, both
    // Mind Controls) precisely so a 500px nuke doesn't also level every forest and
    // mana-bearing prop inside it.
    if (!featTypes_.empty() && terW_ > 0 && !w.unitsOnly) {
        const float r = std::max(w.aoe * 0.5f, 8.0f);
        const int icx = int(hx) / 16, icz = int(hz) / 16;
        auto damageFeature = [&](Feature& f) {
            // A reclaimed feature fading out (494fd0) is already removed with
            // no featuredead; ours keeps it inert rather than let a hit or a
            // fire swap it into a stage that would outlive the fade.
            if (!f.alive || f.type < 0 || f.fadeLeft) return;
            const FeatType& ft = featTypes_[size_t(f.type)];
            if (ft.indestructible) return;
            if (w.fireStarter && ft.flamable && !f.burn && ft.hasBurnAnim) {
                igniteFeature(f);
                return; // ignition replaces the damage this hit
            }
            if (ft.hp <= 0) return;
            f.dmg += w.damage;
            if (f.dmg >= ft.hp) swapFeature(f, ft.deadType);
        };
        // Dead-unit visuals/reclaim targets live separately from scenery, but
        // their installed footprints receive the same feature weapon damage.
        // Look up only the impacted anchors, never scan every corpse per shot.
        const auto damageCorpse = [&](int anchor, bool direct) {
            const auto owner=corpseAnchors_.find(anchor);
            if (owner==corpseAnchors_.end()) return false;
            auto& footprint=corpseFootprints_.at(owner->second);
            if (const Unit* fading=unit(owner->second);fading && fading->corpseFading) return true;
            const auto& ft=featTypes_[size_t(footprint.type)];
            const float x=float(footprint.x*16+ft.fx*8),z=float(footprint.z*16+ft.fz*8);
            if (!direct && (x-hx)*(x-hx)+(z-hz)*(z-hz)>r*r) return true;
            if (ft.indestructible || ft.hp<=0) return true;
            footprint.damage += w.damage;
            if (footprint.damage<ft.hp) return true;
            Unit* corpse=unit(owner->second);
            const int cx=footprint.x,cz=footprint.z,next=ft.deadType;
            retireCorpse(*corpse); // remove the body and resurrection/reclaim target
            if (next>=0) {
                const auto& rubble=featTypes_[size_t(next)];
                addFeature(anchor,float(cx*16+rubble.fx*8),float(cz*16+rubble.fz*8),
                    rubble.energy,rubble.fx,rubble.fz,
                    rubble.blocking,next);
            }
            return true; // never damage the newly installed rubble with this hit
        };
        // Native 529c10 resolves a hit on a footprint tail to its feature head
        // before dispatching damage (4961a0). Save the ID before destruction
        // changes the map cells, and exclude it from the splash pass below.
        int directId = -1;
        if (icx >= 0 && icz >= 0 && icx < terW_ && icz < terH_) {
            int ax = icx, az = icz;
            if (!mapPlacementCells_.empty()) {
                const auto& cell = mapPlacementCells_[size_t(icz) * terW_ + icx];
                if (cell.feature == 0xfffe) { ax -= cell.backX; az -= cell.backZ; }
            }
            if (ax >= 0 && az >= 0) {
                directId = az * terW_ + ax;
                if (!damageCorpse(directId,true)) {
                    const auto it = featureIdx_.find(directId);
                    if (it != featureIdx_.end()) damageFeature(features_[it->second]);
                }
            }
        }
        const int rc = int(r) / 16 + 1;
        for (int dz = -rc; dz <= rc; ++dz)
            for (int dx = -rc; dx <= rc; ++dx) {
                const int cx = icx + dx, cz = icz + dz;
                if (cx < 0 || cz < 0 || cx >= terW_ || cz >= terH_) continue;
                const int id = cz * terW_ + cx;
                if (id == directId || damageCorpse(id,false)) continue;
                const auto it = featureIdx_.find(id);
                if (it == featureIdx_.end()) continue;
                Feature& f = features_[it->second];
                const float fdx = f.x.toFloat() - hx, fdz = f.z.toFloat() - hz;
                if (fdx * fdx + fdz * fdz <= r * r) damageFeature(f);
            }
    }

    // The unit side of 529dc0 (retailSplashUnits). Each cell of the window
    // gives its ground occupant (the body plane the projectile collision
    // reads), then its airborne one (the persistent air grid). The shot's
    // owner is never hit; its own side is. Damage falls from full to `edge`
    // with the square of the distance from the 3D impact to each unit's
    // bounding box (+-footX*8, +-footZ*8, 0..modelTop around its position)
    // over aoe/2. World measured flat distance to the unit's centre, so a
    // shell bursting on the ground hurt a flyer cruising overhead.
    std::array<int32_t,3> point{Fixed::fromFloat(hx).v, 0, Fixed::fromFloat(hz).v};
    if (impact) point = *impact;
    else if (position) point = *position;
    else if (primary && primary->type)
        point[1] = (primary->type->canFly ? primary->flightY : primary->groundY).v;
    else if (!heights_.empty())
        point[1] = Fixed::fromInt(retailTerrainHeight(point[0], point[2], terW_, terH_,
            [&](int x, int z) { return heights_[size_t(z) * size_t(terW_) + size_t(x)]; })).v;
    const auto window = retailSplashWindow(point[0], point[2], aoe, hW_, hH_);
    if (window.x0 >= window.x1 || window.z0 >= window.z1) return;
    const SearchBodyRect bodies = searchBodyRect(window.x0, window.z0,
        window.x1 - window.x0, window.z1 - window.z0);
    const auto air = airOccupancy_.cells();
    struct Host {
        const World& world; const SearchBodyRect& bodies; std::span<const int> air;
        int groundAt(int x, int z) const {
            const Unit* u = bodies.cells[size_t(z - bodies.z) * size_t(bodies.width) + size_t(x - bodies.x)];
            return u ? u->id : 0;
        }
        int airAt(int x, int z) const {
            const size_t cell = size_t(z) * size_t(world.hW_) + size_t(x);
            return cell < air.size() ? air[cell] : 0;
        }
        bool live(int id) const {
            const Unit* u = world.unit(id);
            return u && u->type && u->alive() && !u->embarked();
        }
        RetailSplashBody body(int id) const {
            const Unit& u = *world.unit(id);
            const int32_t x = int32_t(uint32_t(u.type->footX) << 19), z = int32_t(uint32_t(u.type->footZ) << 19);
            return {{u.x.v, (u.type->canFly ? u.flightY : u.groundY).v, u.z.v},
                    {-x, 0, -z}, {x, u.type->modelTop, z}};
        }
    } host{*this, bodies, air};
    // Collect first: damage never moves a unit or changes the planes, and the
    // body plane holds pointers that must not outlive a units_ reallocation.
    std::vector<std::pair<int, float>> recipients;
    retailSplashUnits(point, aoe, w.edge, fromId, hW_, hH_, host,
        [&](int id, float fraction) { recipients.emplace_back(id, fraction); });
    for (const auto& [id, fraction] : recipients)
        if (Unit* e = unit(id)) hurt(*e, fraction);
    static const bool kLog = tak::devEnv("TAK_SPLASHLOG") != nullptr;
    if (!recipients.empty() && kLog)
        std::fprintf(stderr, "splash %s aoe=%d hit %zu\n",
                     w.name.c_str(), int(aoe), recipients.size());
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
    if ((w.mindControl || u.type->canCapture) && !canCaptureTarget(u.player,target)) return;
    // manapershot: a caster spends personal mana to fire; if it can't pay, the
    // shot doesn't happen (reload not consumed, so it fires the moment it can).
    if (w.manaCost > 0 && u.type->maxMana > 0) {
        if (!scriptTriggered && u.mana < w.manaCost) return;
        u.mana -= w.manaCost;
    }
    // Every shot keeps the shooter visible for 600 ticks (icd 0x52af41, set just
    // before the release 0x530220).
    blockCloak(u, 600);
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
        e.y = target.type && target.type->canFly ? target.flightY : target.groundY;
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
        flame.targetId=target.id;
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
        // The aim pass already queried these attachment points. Reuse them for
        // status-shot presentation without another script call at release.
        const bool captured=w.beam && scriptTriggered && slot >= 0 && slot < 3;
        const auto muzzle=captured ? u.weaponMuzzlePoints[size_t(slot)] : std::array<int32_t,3>{};
        const auto aimPoint=captured ? u.weaponAimPoints[size_t(slot)] : std::array<int32_t,3>{};
        const size_t event=hits_.size();
        // A melee shot sits on the target's origin (52bac0 copies +68..+70).
        applyHit(w, target.x.toFloat(), target.z.toFloat(), u.player, u.id, &target, {},
                 std::array<int32_t,3>{target.x.v,
                     (target.type && target.type->canFly ? target.flightY : target.groundY).v, target.z.v});
        if (captured) {
            hits_[event].fromPosition=muzzle;
            hits_[event].position=aimPoint;
        }
        return;
    }
    // Dropped: the bomb is RELEASED, not fired. Native
    // DroppedBallistic::initShot uses the QueryWeapon muzzle, solves horizontal
    // travel to the selected aim point over the gravity-derived fall time, then
    // the common ballistic updater advances and collides the full XYZ shot.
    if (w.kind == Weapon::Kind::Dropped) {
        const auto aim=queryWeaponAim(u.id,target.id,slot);
        const auto targetPoint=aim ? aim->target :
            std::array<int32_t,3>{target.x.v,(target.type->canFly ? target.flightY : target.groundY).v,target.z.v};
        const auto muzzle=queryUnitScriptPoint(u.id,false,slot);
        uint32_t fallTicks=1;
        const auto launch=retailDroppedBallisticLaunch(muzzle,targetPoint,ballisticGravityRaw_,&fallTicks);
        Projectile b;b.ballistic3d=true;b.wsrc=&w;
        b.position=launch.position;b.velocity=launch.velocity;b.angles=launch.angles;
        b.x=Fixed::raw(b.position[0]);b.z=Fixed::raw(b.position[2]);
        b.targetId=target.id;b.slot=slot;b.fromPlayer=u.player;b.fromId=u.id;b.fx=w.fx;
        b.projectileUsesVeteranModel=w.usesVeteranShotModel(u.veteran);
        b.substeps=uint32_t(std::max(1,w.subSteps));
        b.start=tickCounter_+1;b.flight=int32_t(fallTicks);
        // BallisticWeapon has no range-derived expiry. Collision with terrain,
        // a feature or a unit retires the shot; retain a large failsafe countdown
        // for malformed terrain that prevents all impacts.
        b.life=std::numeric_limits<int32_t>::max();
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
    // Retail's BallisticWeapon class uses XYZ motion and collision whether its
    // authored projectile is a mesh or a sprite. Preserve that native trajectory
    // for every ordinary ballistic shot; the renderer draws a mesh when present
    // and otherwise places the weapon sprite at the same 3D position.
    if (w.ballistic && w.kind == Weapon::Kind::Normal && !w.melee &&
        !w.beam) {
        const double rawSpeed = double(w.projVel) * 2184.5333333333333;
        if (rawSpeed > 0.0 && rawSpeed < double(INT32_MAX)) {
            const int32_t raw = int32_t(rawSpeed);
            const uint32_t steps = std::max(1u, (uint32_t(raw) + 0xfffffu) >> 20);
            const auto aim = scriptTriggered ? std::optional<WeaponAimSolution>{} :
                                               queryWeaponAim(u.id, target.id, slot);
            if (aim && aim->unreachable) return;   // no arc: no flat shell (queryWeaponAim)
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
    // BallisticWeapon has no range-derived expiry. Its gravity arc and collision
    // with the map retire it; dist/projVel is only the old flat-projectile lifetime
    // and can expire a pitched shot in midair.
    if (p.ballistic3d) p.life = std::numeric_limits<int32_t>::max();
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
    TAK_PROBE(++probe::tl.losCalls);
    const bool naval=from.type->domain==UnitType::Domain::Water ||
                     to.type->domain==UnitType::Domain::Water;
    return (naval?navalSight_:nav_).losBetween(from.x.toFloat(),from.z.toFloat(),to.x.toFloat(),to.z.toFloat(),
        std::max(from.type->footX,from.type->footZ)/2,
        std::max(to.type->footX,to.type->footZ)/2);
}

bool World::canCaptureTarget(int player,const Unit& target) const {
    // Match the charm impact gates before choosing or pursuing a target. The
    // weapon damage table is an additional targeting filter, not an immunity
    // override for commanders, protected units, passengers or petrified units.
    return target.type && target.alive() && target.hp>Fixed() &&
        !target.embarked() && !target.type->commander && !target.type->cantBeCaptured &&
        target.stonedFor<=0 && !allied(player,target.player) && !atUnitCap(player);
}

bool World::canAttackTarget(const Unit& from,const Unit& target) const {
    if (!from.type || !target.type) return false;
    // Contact capture is independent of damage, but obeys the same immunity and
    // capacity gates as mind-control impacts.
    if (from.type->canCapture) return canCaptureTarget(from.player,target);
    const auto usable=[&](const Weapon& weapon) {
        return !(target.type->canFly && weapon.noAir) && weapon.damageVs(target.type)>0 &&
            (!weapon.mindControl || canCaptureTarget(from.player,target));
    };
    if (from.type->weaponSwitching && !from.type->weapons.empty()) {
        const int slot=std::clamp(from.weaponSlot,0,int(from.type->weapons.size())-1);
        const auto& selected=from.type->weapons[size_t(slot)];
        if (selected.mindControl) return usable(selected);
    }
    return std::any_of(from.type->weapons.begin(),from.type->weapons.end(),usable);
}

int World::findTarget(Unit& u, bool missionPoll, bool groundResponse, bool chases) {
    // VTOL landing also clears active. Only switchable units interpret it as
    // power-off; ordinary landed flyers must still acquire and fire.
    if (u.type->onOffable && !u.active) return false;
    // Auto-acquire: idle armed units engage the nearest enemy in reach;
    // attack-movers and patrollers interrupt their route to fight. A PASSIVE unit
    // (stance 2, hold fire) never auto-acquires -- it only fights when ordered.
    // A builder mid-job -- constructing a site, repairing, reclaiming, holding
    // queued builds, or producing from a queue -- stays on task: it does NOT
    // auto-acquire a nearby enemy and wander off to fight while it should build.
    // Every gate below is a pure test and the scan runs only if all of them pass,
    // so their order is free: the cheap ones go first and the order-queue walk
    // (hasQueuedWork) last, because most armed units fail the stride gate on any
    // given tick and would otherwise walk their whole queue for nothing.
    // Auto-acquisition belongs to the FIRE order: only Fire At Will goes looking.
    // Hold Fire and Return Fire both wait to be handed a target (an explicit attack
    // order still works -- retail's hold-fire gate has a "forced" bypass for
    // exactly that).
    // Auto-acquisition is staggered across ticks by unit id: an idle armed unit
    // rescans for a target every kAcqStride ticks (~0.13s at 30Hz), not every
    // tick. That turns the dense-crowd O(n^2) neighbour scan into O(n^2/stride)
    // -- the dominant sim cost when thousands of idle armed units pile together.
    // Deterministic (id+tick, identical on every lockstep peer), so no desync. The
    // stride widens with the crowd (acqStride_, set at tick start) so massive battles
    // rescan less often -- the acquisition cost scales sub-linearly instead of O(n).
    const bool acqTurn = missionPoll || (uint32_t(u.id) + tickCounter_) % acqStride_ == 0;
    const bool acquireGate = u.fireState == 2 && acqTurn && !isNeutralPlayer(u.player) &&
        u.type->weapon.damage > 0 &&
        (groundResponse || u.orders.empty() ||
         (u.orders.front().targetId == 0 &&
          (u.orders.front().attackMove || u.orders.front().patrol)) ||
         u.orders.front().guard) &&
        u.buildSiteId == 0 && u.repairId == 0 && u.reclaimId == 0 &&
        (u.type->isStructure() || (u.buildQueue.empty() && !u.repeatType));
    TAK_PROBE(if (acquireGate) ++probe::tl.acqGateWalks;
              if (u.buildSiteId == 0 && u.repairId == 0 && u.reclaimId == 0) ++probe::tl.acqGateWalksBefore);
    const bool acquiring = acquireGate && !hasQueuedWork(u);
#ifndef NDEBUG
    // TAK_VERIFY_ACQ_GATE: the gates in their original order (busyBuilding first)
    // must decide exactly as the reordered ones.
    static const bool verifyGate = probe::envOn("TAK_VERIFY_ACQ_GATE");
    if (verifyGate) {
        const bool busyBuilding = u.buildSiteId != 0 || u.repairId != 0 || u.reclaimId != 0 ||
                                  hasQueuedWork(u) ||
                                  (!u.type->isStructure() && (!u.buildQueue.empty() || u.repeatType));
        const bool acquiringBefore = !busyBuilding && u.fireState == 2 &&
                                     (groundResponse || u.orders.empty() ||
                                      (u.orders.front().targetId == 0 &&
                                       (u.orders.front().attackMove || u.orders.front().patrol)) ||
                                      u.orders.front().guard);
        if ((acquiringBefore && !isNeutralPlayer(u.player) && u.type->weapon.damage > 0 && acqTurn) != acquiring)
            throw std::runtime_error("findTarget: reordered acquisition gates disagree with the original order");
    }
#endif
    auto canTarget = [&](const Unit& e) {
        if (e.cloaked) return false;   // cloaked units are invisible to auto-acquire
        return canAttackTarget(u,e);
    };
    if (acquiring) {
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
        // Whether that needs the 2D line is decided by the weapon the unit
        // fights with (approachNeedsClearLine), as tickCombat decides it: a
        // Ballistic shell flies its arc over the wall, as in retail.
        uint64_t enemies=~uint64_t(0);
        for (int player=0;player<numPlayers() && player<64;++player)
            if (allied(player,u.player)) enemies&=~(uint64_t(1)<<player);
        TAK_PROBE(++probe::tl.acqScans; probeNear(u.x.toFloat(), u.z.toFloat(), ar, enemies, true));
        forEachNear(u.x.toFloat(), u.z.toFloat(), ar, [&](int idx) {
            const Unit& e = units_[size_t(idx)];
            if (!e.alive() || e.embarked() || isNeutralPlayer(e.player) || allied(e.player, u.player) || !e.type) return;
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
            if (shotBlockedHere(u,e)) return;   // written off from this spot (noteShotImpact)
            // A unit that may not leave its post (tickCombat's !mayChase) drops
            // an auto target out of its reach at once; never pick one up, or it
            // is acquired and dropped again on every acquisition turn, and each
            // acquisition (setSlotsManual) wipes the fire-at-will sweep's slot
            // targets. The reach is tickCombat's own (combatReach): the arc for
            // a ballistic weapon, else the selected weapon's range (the longest
            // when all fire), centre to centre for a structure too. Melee keeps
            // the old reach.
            // A ground mission's attack response (chases: not an auto target)
            // may chase, as tickCombat's mayChase says, so it is not held to it.
            if (!(u.type->canMove && (u.moveState != 0 || chases))) {
                const CombatReach cr=combatReach(u,e,std::sqrt(d));
                if (cr.weapon && !cr.weapon->melee && !cr.in) return;
            }
            if (approachNeedsClearLine(u,e) && !combatLineOfSight(u,e))
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
    // The attack it starts owns the weapons (Attack_* calls 519b70(3)).
    setSlotsManual(u,kSlotsAttack);
    u.orders.insert(u.orders.begin(),{Fixed(),Fixed(),target});
    u.orders.front().autoTarget=true; u.orders.front().goal=true;
    noteOrders(u.id);
    return true;
}

// ---------------------------------------------------------------------------
// Retail's weapon-slot layer: auto/manual slots, the fire-at-will sweep
// (icd 40f330 + 40f2e0 + 412620), the auto-slot weapon update (52ae90 ->
// 52fe30/52fff0) and the per-slot hit response (409a50 part b). It sits under
// the order-driven combat above: an attack order makes every slot manual, so
// the sweep never touches what an order owns, and an auto slot never moves or
// turns a moving unit -- which is how a retail builder on its way to a site,
// or any unit on a plain move, shoots what it passes.

// The units whose aim start (52fe30) posts a facing request and whose
// readiness (52fff0) then waits for the body to face the slot's point.
static bool slotNeedsFacing(const UnitType& t) {
    return retailPostsFacing(!t.isStructure(),t.canFly,t.hoverAttack,uint16_t(t.turnInPlaceRate));
}

namespace {
// Retail's squared centre distance (411ae0/412620): each axis squared as a
// 64-bit product of the 16.16 deltas, keeping the high dword (whole px^2).
int64_t retailCentreDist2(const Unit& a,const Unit& b) {
    const int64_t dx=int32_t(uint32_t(b.x.v)-uint32_t(a.x.v));
    const int64_t dz=int32_t(uint32_t(b.z.v)-uint32_t(a.z.v));
    return ((dx*dx)>>32)+((dz*dz)>>32);
}
bool categoryMatch(const std::vector<std::string>& cats,const UnitType& t) {
    for (const auto& c : cats)
        if (std::find(t.categories.begin(),t.categories.end(),c)!=t.categories.end()) return true;
    return false;
}
} // namespace

void World::dropSlotTarget(Unit& u,int slot) {
    if (slot<0 || slot>=3 || !u.slotTarget[size_t(slot)]) return;
    u.slotTarget[size_t(slot)]=0;
    // 51a7f0: TargetCleared(slot) only when a target was set, and for a
    // switcher only on the selected slot.
    if (u.type->weaponSwitching && slot!=u.weaponSlot) return;
    if (const auto it=unitScript(u.id)) {
        const auto& file=*u.type->script();
        u.weaponAnimations.add(tak::RetailWeaponAnimation::Clear,slot);
        it->state().startArguments(file,file.scriptIndex("TargetCleared"),{uint32_t(slot),0,0,0},1);
    }
}

void World::setSlotTarget(Unit& u,int slot,int id) {
    // 51a700: record +0x10 = target, +0x12 = 0x8000, and the slot's fire and
    // aim events are cleared from the unit's mission event word.
    u.slotTarget[size_t(slot)]=id;
    u.missionEvents&=0xfe08ffffu;
}

// 519b10/519b70 on one slot: a slot that changes mode drops its target
// (51a7f0); one already in the mode keeps it.
void World::setSlotMode(Unit& u,int slot,bool automatic) {
    const uint8_t bit=uint8_t(1u<<slot);
    if (((u.slotAuto&bit)!=0)==automatic) return;
    u.slotAuto=automatic ? uint8_t(u.slotAuto|bit) : uint8_t(u.slotAuto&~bit);
    dropSlotTarget(u,slot);
}

void World::setSlotsAuto(Unit& u) {
    u.slotManual=kSlotsAuto;
    for (int s=0;s<3;++s) setSlotMode(u,s,true);
}

void World::setSlotsManual(Unit& u,uint8_t cause) {
    u.slotManual=cause;
    for (int s=0;s<3;++s) setSlotMode(u,s,false);
}

// Our orders are not retail missions, so the mission destructor's setAuto
// (4d6da0) is reproduced by retiring the manual mode once the reason that set
// it is gone: the attack order left the front, the work job ended, the status
// wore off. The work stage itself sets manual where retail does (placement,
// the in-reach work call); an attack order and a status are recognised here.
void World::syncSlotMode(Unit& u) {
    const bool status=u.incapacitated() || u.embarked() || u.underConstruction;
    const bool attack=!u.orders.empty() && u.orders.front().targetId!=0 &&
        !u.orders.front().guard && !u.orders.front().load;
    const bool work=u.buildSiteId || u.repairId || u.reclaimId || u.reviveTarget || u.workScriptWorking;
    // A resurrect channel starts only in reach (and from the threaded ability
    // pass, which must not start scripts), so it is recognised here too.
    const uint8_t want=status ? kSlotsStatus : attack ? kSlotsAttack :
        (u.reviveTarget || (u.slotManual==kSlotsWork && work)) ? kSlotsWork : kSlotsAuto;
    // AirToGround's and AirToAir's mixed slots last only while their order
    // is at the front; any other attack's own stages make every slot manual.
    const bool mixed=attack && want==kSlotsAttack && u.orders.front().mixedSlots;
    if (want==u.slotManual && (mixed || want!=kSlotsAttack || !u.slotAuto)) return;
    if (want==kSlotsAuto) setSlotsAuto(u);
    else setSlotsManual(u,want);
}

bool World::slotInsideMinRange(const Unit& u,const Unit& t,int slot) const {
    const int64_t m=u.type->weapons[size_t(slot)].minRange;
    return m>0 && retailCentreDist2(u,t)<m*m;
}

// 52bc60 (BallisticWeapon::inRange): the launch arc from the shooter's
// origin to the target's (flight y for a flyer, a submerged body's own y),
// at the weapon's aim speed under the map's gravity; then the strict centre
// test, with no footprint pad. A shell that cannot reach is never in range,
// so the weapon is not aimed or fired and an ordered attacker closes in.
bool World::ballisticInRange(Fixed x,Fixed y,Fixed z,const Unit& t,const Weapon& w) const {
    const Fixed ty=t.type->canFly ? t.flightY : t.groundY;
    auto delta=[](Fixed to,Fixed from) { return std::bit_cast<int32_t>(uint32_t(to.v)-uint32_t(from.v)); };
    return tak::retailBallisticInRange(delta(t.x,x),delta(ty,y),delta(t.z,z),tak::retailAimSpeed(w.projVel),
        w.gravityAdj,w.lobPreferred,ballisticGravityRaw_,w.range);
}

bool World::ballisticInRange(const Unit& u,const Unit& t,const Weapon& w) const {
    return ballisticInRange(u.x,u.type->canFly ? u.flightY : u.groundY,u.z,t,w);
}

// The weapon an attack order approaches on: the selected one for a switcher,
// the longest when they all fire (tickCombat's reach). -1: none.
int World::approachSlot(const Unit& u) const {
    if (u.type->weapons.empty()) return -1;
    int s=std::clamp(u.weaponSlot,0,int(u.type->weapons.size())-1);
    if (!u.type->weaponSwitching && u.type->weapons.size()>1)
        for (int sl=0;sl<int(u.type->weapons.size());++sl)
            if (u.type->weapons[size_t(sl)].range>u.type->weapons[size_t(s)].range) s=sl;
    return s;
}

World::CombatReach World::combatReach(const Unit& u,const Unit& t,float dist) const {
    CombatReach r{};
    r.slot=approachSlot(u);
    r.weapon=r.slot>=0 ? &u.type->weapons[size_t(r.slot)] : nullptr;
    // Approach on the reach this unit actually fights at: the selected weapon for a
    // switcher, the longest of them when they all fire.
    const bool allWeapons=!u.type->weaponSwitching && u.type->weapons.size()>1;
    r.best=allWeapons ? u.type->maxRange() : (r.weapon ? float(r.weapon->range) : u.type->maxRange());
    // Range is to the target's footprint EDGE, not its centre. A building's centre is
    // deep inside a blocked footprint, so a centre-distance check leaves a short-range
    // attacker grinding the edge (never "in range") or a flyer buried inside it. For a
    // STRUCTURE target, pad the effective range by its footprint half-extent plus a
    // small margin for where nav actually halts at the edge (mirrors the builder reach
    // in tickConstruction). Mobile targets keep centre-distance, so unit-vs-unit combat
    // is unchanged. Only a melee approach weapon is padded: every other retail
    // weapon class measures a building centre to centre (Weapon::centreReach),
    // and so approaches to its range itself, as retail's Attack_Chase (403bb0)
    // heads for a ring of the weapon's range round the target's centre.
    r.pad=t.type->maxVel <= Fixed() && !(r.weapon && r.weapon->centreReach())
              ? 8.0f*float(std::max(t.type->footX,t.type->footZ))+24.0f
              : 0.0f;
    // A ballistic weapon reaches only where its arc does (52bc60, see
    // ballisticInRange): no pad, and a target its shell cannot reach is out
    // of reach however near it stands.
    r.arc=r.weapon && r.weapon->ballisticClass();
    r.in=r.arc ? ballisticInRange(u,t,*r.weapon) : dist <= r.best+r.pad;
    return r;
}

bool World::approachNeedsClearLine(const Unit& u,const Unit& t) const {
    if (u.type->canFly || !t.type || t.type->canFly) return false;
    const int slot=approachSlot(u);
    return slot>=0 && u.type->weapons[size_t(slot)].needsClearLine();
}

bool World::slotInRange(const Unit& u,const Unit& t,int slot) const {
    const Weapon& w=u.type->weapons[size_t(slot)];
    if (w.melee) return meleeInRange(u.type,t.type,(t.x-u.x).toFloat(),(t.z-u.z).toFloat());
    if (w.ballisticClass()) return ballisticInRange(u,t,w);
    // The base test (530580) is centre to centre, inclusive, for a structure
    // too (Weapon::centreReach): no footprint pad.
    const int64_t r=w.range;
    return retailCentreDist2(u,t)<=r*r;
}

// Engine deviation shared with findTarget and the order-driven fire test: a
// ranged ground weapon whose shot flies the line (Weapon::needsClearLine: not
// a plain Ballistic shell) needs a clear 2D line to its target (retail has no
// such test; without it a unit would empty its auto slots into a wall). Part
// of "in range" for the sweep, the weapon update and the hit response alike,
// so a blocked target is never selected and then held. Decided by THIS slot's
// weapon: a Ballistic slot of a unit whose other slot needs the line is free
// of it, and the reverse.
bool World::slotClearShot(const Unit& u,const Unit& t,int slot) const {
    const Weapon& w=u.type->weapons[size_t(slot)];
    if (shotBlockedHere(u,t)) return false;   // its shots from here stop short (noteShotImpact)
    if (!w.needsClearLine() || u.type->canFly || t.type->canFly) return true;
    return combatLineOfSight(u,t);
}

// Blocked shots (engine deviation; retail has no such test). Retail's weapon
// update (52ae90/52fe30) admits a shot on range alone, and its projectile
// collision (52a4d0) stops the shot on the first terrain or feature cell it
// meets (tools/re/probe_ballistic_blocked_target.py), so a retail archer
// behind a rock empties its quiver into the rock for ever. The 2D sight test
// above (slotClearShot) catches walls on the nav grid; it cannot see a hill
// crest a low arc clips, a tall feature that does not block movement, water
// or a shot from below a ledge. So the sim watches where the shots land: an
// impact on terrain, water or scenery this weapon cannot destroy, short of
// the target and clear of it, counts against the target; a hit on it or a
// miss beside it clears the count, and so does walking away from the spot.
static constexpr uint8_t kBlockedShots=3;    // in a row before the spot is written off
static constexpr int kBlockedSpot=16;        // px the shooter may drift and keep its count

// Is (dx,dz), in whole px from a body's centre, within `slack` of the body?
// Retail's collision (52a4d0) reaches a unit through the map cells it owns,
// its footprint, whatever its extent the other way
// (tools/re/probe_long_footprint_blocked.py: a ridge one cell before a long
// body's near face still stops the shot on terrain). A square footprint keeps
// the circle of its half-extent plus slack; a longer one (a castle, a gate)
// is also held to its footprint rectangle plus slack, so a crest in front of
// its long face is short of it rather than inside a circle of its length.
static bool nearBody(const UnitType& t,int64_t dx,int64_t dz,int64_t slack) {
    const int64_t hx=8*int64_t(t.footX),hz=8*int64_t(t.footZ),r=std::max(hx,hz)+slack;
    if (dx*dx+dz*dz>r*r) return false;
    return t.footX==t.footZ || (std::abs(dx)<=hx+slack && std::abs(dz)<=hz+slack);
}

bool World::impactIndestructible(const Weapon& w,int cx,int cz) const {
    if (cx<0 || cz<0 || cx>=terW_ || cz>=terH_) return true;
    if (w.unitsOnly) return true;            // never touches scenery
    int ax=cx,az=cz;
    if (!mapPlacementCells_.empty()) {
        const auto& cell=mapPlacementCells_[size_t(cz)*size_t(terW_)+size_t(cx)];
        if (cell.feature==0xfffe) { ax-=cell.backX; az-=cell.backZ; }
    }
    if (ax<0 || az<0) return true;
    const int id=az*terW_+ax;
    auto destructible=[&](const FeatType& ft,bool burning) {
        if (ft.indestructible) return false;
        return ft.hp>0 || (w.fireStarter && ft.flamable && ft.hasBurnAnim && !burning);
    };
    if (const auto owner=corpseAnchors_.find(id);owner!=corpseAnchors_.end()) {
        const auto& footprint=corpseFootprints_.at(owner->second);
        return footprint.type<0 || size_t(footprint.type)>=featTypes_.size() ||
            !destructible(featTypes_[size_t(footprint.type)],false);
    }
    if (const auto it=featureIdx_.find(id);it!=featureIdx_.end()) {
        const Feature& f=features_[it->second];
        if (f.alive && f.type>=0 && size_t(f.type)<featTypes_.size())
            return !destructible(featTypes_[size_t(f.type)],f.burn!=0);
    }
    return true;   // bare terrain, water, or a map blocker with no feature record
}

void World::noteShotImpact(int fromId,int targetId,const Weapon& w,const std::array<int32_t,3>& impact,int hitUnit) {
    Unit* u=unit(fromId);
    const Unit* t=unit(targetId);
    if (!u || !u->alive() || !u->type || !t || !t->alive() || !t->type || targetId==fromId) return;
    if (hitUnit) {
        // The target itself: the line is open. Any other body neither opens
        // nor blocks it (it can be killed, or it moves).
        // A hit also renews the walk budget: the line it opened is spent once
        // the target or the shooter moves on.
        if (hitUnit==targetId && u->blockedTarget==targetId) { u->blockedShots=0;u->repositions=0;u->searchFailed=0; }
        return;
    }
    // Short of the target and clear of it, in whole px: in front of the
    // target's projection on the line of fire and outside its body (its
    // footprint, nearBody), a body of slack and the weapon's splash. A miss
    // beside or beyond the target reached it.
    const int64_t sx=u->x.floorInt(),sz=u->z.floorInt(),tx=t->x.floorInt(),tz=t->z.floorInt();
    const int64_t ix=impact[0]>>16,iz=impact[2]>>16;
    const int64_t along=(ix-sx)*(tx-sx)+(iz-sz)*(tz-sz),line=(tx-sx)*(tx-sx)+(tz-sz)*(tz-sz);
    const bool shortOf=along<line && !nearBody(*t->type,ix-tx,iz-tz,8+24+int64_t(uint16_t(w.aoe))/2);
    if (!shortOf || !impactIndestructible(w,int(impact[0]>>20),int(impact[2]>>20))) {
        if (u->blockedTarget==targetId) { u->blockedShots=0;u->searchFailed=0; }
        return;
    }
    if (u->blockedTarget!=targetId) { u->blockedTarget=targetId;u->blockedShots=0;u->repositions=0;u->searchFailed=0; }
    if (u->blockedShots && (std::abs(sx-u->blockedX)>kBlockedSpot || std::abs(sz-u->blockedZ)>kBlockedSpot)) {
        u->blockedShots=0;u->searchFailed=0;
    }
    if (u->blockedShots<255) ++u->blockedShots;
    u->blockedX=int16_t(sx);u->blockedZ=int16_t(sz);
}

// A ballistic slot aimed and ready whose muzzle-to-SweetSpot solve has no
// arc (queryWeaponAim's `unreachable`; the origin solve, 52bc60, passed)
// launches nothing (tickScriptWeapon). Retail lobs it flat; we count it as a
// shot that stops short, once per reload, so a unit that cannot close in
// writes the target off from this spot like a blocked one instead of holding
// it for ever without a shot.
void World::noteNoArc(Unit& u,const Unit& t) {
    const int64_t sx=u.x.floorInt(),sz=u.z.floorInt();
    if (u.blockedTarget!=t.id) { u.blockedTarget=t.id;u.blockedShots=0;u.repositions=0;u.searchFailed=0; }
    if (u.blockedShots && (std::abs(sx-u.blockedX)>kBlockedSpot || std::abs(sz-u.blockedZ)>kBlockedSpot)) {
        u.blockedShots=0;u.searchFailed=0;
    }
    if (u.blockedShots<255) ++u.blockedShots;
    u.blockedX=int16_t(sx);u.blockedZ=int16_t(sz);
}

bool World::shotBlockedHere(const Unit& u,const Unit& t) const {
    return u.blockedTarget==t.id && u.blockedShots>=kBlockedShots &&
        std::abs(u.x.floorInt()-u.blockedX)<=kBlockedSpot && std::abs(u.z.floorInt()-u.blockedZ)<=kBlockedSpot;
}

// Would a shot fired by `u` standing at (fromX,fromZ) reach `t`'s SweetSpot
// clear of terrain, water and scenery? The straight chord from the muzzle to
// the aim point, sampled every 4 px through the projectile collision's own
// environment test (52a4d0's map half). A ballistic arc lies above its chord
// and a straight shot flies along it, so a clear chord is a clear shot; the
// shooter's and the target's own footprints are not sampled.
bool World::shotChordClear(const Unit& u,Fixed fromX,Fixed fromZ,const Unit& t,int slot) {
    if (heights_.empty() || slot<0 || size_t(slot)>=u.type->weapons.size()) return true;
    if (u.type->weapons[size_t(slot)].unitsOnly) return true;
    auto ground=[&](Fixed x,Fixed z) {
        return int64_t(retailTerrainHeight(x.v,z.v,terW_,terH_,
            [&](int cx,int cz) { return heights_[size_t(cz)*size_t(terW_)+size_t(cx)]; }))<<16;
    };
    // The muzzle's height over the body's origin, as it stands now.
    const auto muzzle=queryUnitScriptPoint(u.id,false,slot);
    const int64_t lift=std::max<int64_t>(0,int64_t(muzzle[1])-int64_t(u.groundY.v));
    const std::array<int64_t,3> a{fromX.v,ground(fromX,fromZ)+lift,fromZ.v};
    const auto aim=queryUnitScriptPoint(t.id,true);
    return chordClear(a,{aim[0],aim[1],aim[2]},u.type->weapons[size_t(slot)],
                      8*std::max(u.type->footX,u.type->footZ),8*std::max(t.type->footX,t.type->footZ)+8,t);
}

// The chord from a to b, sampled every 4 px through 52a4d0's map half,
// skipping the first skipFrom and the last skipTo px (the two bodies). Near
// b a sample is skipped only near the body `to` too (nearBody, 8 px of
// slack), so a longer body skips no crest in front of its long face; a
// square one skips the circle alone.
bool World::chordClear(const std::array<int64_t,3>& a,const std::array<int64_t,3>& b,const Weapon& w,
                       int64_t skipFrom,int64_t skipTo,const Unit& to) const {
    const uint32_t flags=(w.unitsOnly?0x800u:0u)|(w.groundBounce?0x1000u:0u)|(w.waterWeapon?0x2000u:0u);
    if (flags&0x800u) return true;
    const int64_t dx=(b[0]-a[0])>>16,dz=(b[2]-a[2])>>16;
    const int64_t steps=std::max(std::abs(dx),std::abs(dz))/4+1;
    for (int64_t i=1;i<steps;++i) {
        const std::array<int32_t,3> point{int32_t(a[0]+(b[0]-a[0])*i/steps),int32_t(a[1]+(b[1]-a[1])*i/steps),
                                          int32_t(a[2]+(b[2]-a[2])*i/steps)};
        const int64_t fx=(int64_t(point[0])-a[0])>>16,fz=(int64_t(point[2])-a[2])>>16;
        const int64_t tx=(int64_t(point[0])-b[0])>>16,tz=(int64_t(point[2])-b[2])>>16;
        if (fx*fx+fz*fz<=skipFrom*skipFrom) continue;
        if (tx*tx+tz*tz<=skipTo*skipTo && (to.type->footX==to.type->footZ ||
                nearBody(*to.type,(int64_t(point[0])>>16)-to.x.floorInt(),(int64_t(point[2])>>16)-to.z.floorInt(),8)))
            continue;
        int32_t vertical=-1;
        if (projectileEnvironment(point,vertical,flags)) return false;
    }
    return true;
}

// The response to a written-off spot: walk to the nearest spot within
// kRepositionReach of the unit, in the weapon's range band of the target,
// that the unit can stand on and reach, away from the spot it was blocked
// at, with a clear chord (and the 2D sight test the attack fires on). A plain
// move leg is put in front of the attack, so each pathfinding mode walks it
// its own way and the attack resumes on arrival. Each target gets
// kMaxRepositions walks between hits on it; with the walks used up the unit
// fires on as retail does. A search that finds no spot writes off the
// moment, not the target: the unit fires on, and searches again after
// another kBlockedShots blocked shots, since the target may have moved
// (a hit, a miss that reached the target or the shooter moving restart the
// count as usual).
static constexpr uint8_t kMaxRepositions=3;
static constexpr int kRepositionReach=12;    // cells from the unit searched for a spot
static constexpr int kRepositionChords=256;  // chord tests per search, nearest spots first

// The order behind the front one is a movement leg: a move, an attack-move
// or a patrol, with no target or job of its own.
static bool movementQueued(const Unit& u) {
    if (u.orders.size()<2) return false;
    const Order& o=u.orders[1];
    return !o.targetId && !o.guard && !o.load && !o.unload && !o.buildType && !o.reclaimFeat &&
        !o.reclaimArea && !o.manaBuildArea && !o.repairTarget && !o.wait && !o.waitAttack;
}

bool World::repositionForShot(Unit& u,const Unit& t,int slot) {
    if (u.repositions>=kMaxRepositions) return false;
    if (u.searchFailed && u.blockedShots<2*kBlockedShots) return false;
    const Weapon& w=u.type->weapons[size_t(slot)];
    const int foot=footCells(u.type);
    const NavGrid& grid=navFor(u.type);
    // The band is the strict centre test's (Weapon::centreReach: no footprint
    // pad for a structure); a ballistic shell is also held to its arc (each
    // spot is solved below).
    const bool arc=w.ballisticClass();
    const int64_t hi=int64_t(w.range)-16,lo=std::max<int64_t>(int64_t(w.minRange)+16,
        8*(std::max(t.type->footX,t.type->footZ)+foot)+8);
    const int64_t ux=u.x.floorInt(),uz=u.z.floorInt(),tx=t.x.floorInt(),tz=t.z.floorInt();
    const int ucx=footprintCell(u.x,foot),ucz=footprintCell(u.z,foot);
    const bool needLoS=w.needsClearLine() && !t.type->canFly;
    // The origin height the unit would stand at on a spot: a boat keeps its
    // own (sea level or its dive depth); a ground body follows the terrain
    // with its present clearance over it.
    auto terrainY=[&](Fixed x,Fixed z) {
        return heights_.empty() ? Fixed() : Fixed::fromInt(retailTerrainHeight(x.v,z.v,terW_,terH_,
            [&](int hx,int hz) { return heights_[size_t(hz)*size_t(terW_)+size_t(hx)]; }));
    };
    const Fixed clearance=u.type->domain==UnitType::Domain::Water ? Fixed() : u.groundY-terrainY(u.x,u.z);
    auto spotY=[&](int cx,int cz) {
        return u.type->domain==UnitType::Domain::Water ? u.groundY :
            terrainY(footprintWaypoint(cx,foot),footprintWaypoint(cz,foot))+clearance;
    };
    std::vector<std::pair<int64_t,int>> spots;
    if (hi>lo) for (int cz=ucz-kRepositionReach;cz<=ucz+kRepositionReach;++cz)
        for (int cx=ucx-kRepositionReach;cx<=ucx+kRepositionReach;++cx) {
            if (cx<0 || cz<0 || cx>=terW_ || cz>=terH_) continue;
            const int64_t px=footprintWaypoint(cx,foot).floorInt(),pz=footprintWaypoint(cz,foot).floorInt();
            const int64_t rx=px-tx,rz=pz-tz,r2=rx*rx+rz*rz;
            if (r2>hi*hi || r2<lo*lo) continue;
            if (std::abs(px-u.blockedX)<=2*kBlockedSpot && std::abs(pz-u.blockedZ)<=2*kBlockedSpot) continue;
            if (!grid.fits(cx,cz,foot) || !passable(u.type,cx,cz)) continue;
            if (arc && !ballisticInRange(footprintWaypoint(cx,foot),spotY(cx,cz),footprintWaypoint(cz,foot),t,w))
                continue;
            spots.push_back({(px-ux)*(px-ux)+(pz-uz)*(pz-uz),cz*terW_+cx});
        }
    std::sort(spots.begin(),spots.end());
    int tested=0;
    for (const auto& [d2,cell]:spots) {
        if (tested>=kRepositionChords) break;
        const Fixed x=footprintWaypoint(cell%terW_,foot),z=footprintWaypoint(cell/terW_,foot);
        if (needLoS) {
            const bool naval=u.type->domain==UnitType::Domain::Water || t.type->domain==UnitType::Domain::Water;
            if (!(naval?navalSight_:nav_).losBetween(x.toFloat(),z.toFloat(),t.x.toFloat(),t.z.toFloat(),
                    std::max(u.type->footX,u.type->footZ)/2,std::max(t.type->footX,t.type->footZ)/2)) continue;
        }
        ++tested;
        if (!shotChordClear(u,x,z,t,slot)) continue;
        if (!pathExists(u.type,x.toFloat(),z.toFloat(),u.x.toFloat(),u.z.toFloat())) continue;
        ++u.repositions;
        u.blockedShots=0;
        u.searchFailed=0;
        // The leg World::order builds for a point move, put in front: the
        // attack behind it keeps its own leg and resumes on arrival.
        if(u.scriptAimTarget)clearScriptWeaponTarget(u);
        cancelPath(u);
        Order move{x,z,0};
        move.clickX=x;move.clickZ=z;
        move.groundMission=true;
        move.mission.flags=0x3000400u;
        move.controller=++nextMovementController_;
        move.goal=true;
        move.issuedTick=tickCounter_;
        if (isLegionPathfinding(pathfindingMode_)) move.setIdentity(stampIdentity(u,ConvoyClass::Move,x,z));
        u.orders.insert(u.orders.begin(),move);
        resetGroundSegment(u,u.orders.front());
        if (u.routeStamp<0 || uint32_t(u.routeStamp)<=tickCounter_-6u) u.routeStamp=0;
        requestPath(u,x.toFloat(),z.toFloat());
        noteOrders(u.id);
        return true;
    }
    // Nowhere better in reach: fire on from here, still written off (the
    // sweep keeps preferring a target it can hit), and look again after
    // kBlockedShots more.
    u.searchFailed=1;
    u.blockedShots=kBlockedShots;
    return false;
}

// The hover attacker's response. Retail's hover mission (41de5d) keeps the
// flyer's bearing from the target at every wake, so a Thirsha whose bolts
// stop on a standing stone between it and the target hovers there and fires
// into the stone for as long as the order lasts. Once its shots are written
// off from here it moves round the ring instead: the nearest bearing, in
// 1/32 turns either way, whose chord from the hover point's muzzle height to
// the target's SweetSpot is clear. The flyer keeps that point until it can
// have flown there (the wake redraw takes its bearing from where it is);
// the walk budget and the failed-search rule are the ground mover's.
bool World::hoverForShot(Unit& u,const Unit& t,int slot,const std::array<int32_t,3>& centre,
                         int distance,int altitude) {
    if (u.repositions>=kMaxRepositions || heights_.empty()) return false;
    if (u.searchFailed && u.blockedShots<2*kBlockedShots) return false;
    if (slot<0 || size_t(slot)>=u.type->weapons.size()) return false;
    auto& order=u.orders.front();
    const int64_t lift=int64_t(queryUnitScriptPoint(u.id,false,slot)[1])-int64_t(u.flightY.v);
    const auto aim=queryUnitScriptPoint(t.id,true);
    const uint16_t bearing=uint16_t(retailDirection(Fixed::raw(std::bit_cast<int32_t>(uint32_t(u.x.v)-uint32_t(centre[0]))),
                                                    Fixed::raw(std::bit_cast<int32_t>(uint32_t(u.z.v)-uint32_t(centre[2])))).v);
    const int radius=std::max(distance,16);
    for (int k=1;k<=16;++k)
        for (const int side:{1,-1}) {
            if (k==16 && side<0) continue;   // the far side, once
            const uint16_t angle=uint16_t(bearing+side*k*2048);
            const int32_t x=centre[0]+retailScaledSine(angle,Fixed::fromInt(radius).v);
            const int32_t z=centre[2]+retailScaledCosine(angle,Fixed::fromInt(radius).v);
            if (x<0 || z<0 || (x>>20)>=hW_ || (z>>20)>=hH_) continue;
            if (std::abs((x>>16)-u.blockedX)<=2*kBlockedSpot && std::abs((z>>16)-u.blockedZ)<=2*kBlockedSpot) continue;
            const int ground=std::max(seaLevel_,retailTerrainHeight(x,z,terW_,terH_,
                [&](int hx,int hz) { return heights_[size_t(hz)*terW_+hx]; }));
            const int32_t y=Fixed::fromInt(std::min(511,ground+altitude)).v;
            if (!chordClear({x,int64_t(y)+lift,z},{aim[0],aim[1],aim[2]},u.type->weapons[size_t(slot)],
                            0,8*std::max(t.type->footX,t.type->footZ)+8,t)) continue;
            ++u.repositions;
            u.blockedShots=0;
            u.searchFailed=0;
            order.flightGoal=RetailFlightGoal{{x,y,z},0x68,angle,0};
            const int64_t dx=(int64_t(x)-u.x.v)>>16,dz=(int64_t(z)-u.z.v)>>16;
            const int64_t span=std::max(std::abs(dx),std::abs(dz))+std::min(std::abs(dx),std::abs(dz))/2;
            const int64_t speed=std::max<int64_t>(u.type->maxVel.v,1<<14);
            order.hoverAttackRefresh=tickCounter_+uint32_t(std::min<int64_t>((span<<16)/speed,600))+30;
            return true;
        }
    u.searchFailed=1;
    u.blockedShots=kBlockedShots;
    return false;
}

bool World::slotBadTarget(const Unit& u,int slot,const Unit& t) const {
    return categoryMatch(u.type->badTargetCats[size_t(slot)],*t.type);
}

// 40fc4b..40fd2c: a swept target is kept while it is a live, non-allied,
// eligible body. Range is NOT re-checked: a target that walked away stays
// assigned (the weapon update just declines to aim at it).
bool World::sweepKeepsTarget(const Unit& u,int slot,const Unit& t) const {
    const Weapon& w=u.type->weapons[size_t(slot)];
    if (!t.alive() || t.hp<=Fixed() || !t.type || allied(u.player,t.player) || t.embarked()) return false;
    if (w.melee && t.type->canFly) return false;
    if (slotBadTarget(u,slot,t)) return false;
    if (w.status==Weapon::Status::Paralyzed && t.paralyzedFor>0) return false;
    if (shotBlockedHere(u,t)) return false;   // engine deviation: see noteShotImpact
    return true;
}

int World::retailSelectTarget(Unit& u,int slot,bool sweep) {
    if (slot<0 || slot>=int(std::min<size_t>(3,u.type->weapons.size()))) return 0;
    const Weapon& w=u.type->weapons[size_t(slot)];
    const int64_t radius=sweep ? int64_t(w.range) : std::max<int64_t>(u.type->sight,w.range);
    // 411ae0 over the owner's enemy list +5 (hostile units it saw at the last
    // 7-tick rebuild, in unit order): live, by live centre distance. Retail
    // re-checks nothing else; a unit carried or converted since the rebuild
    // is dropped here too.
    // The list is id-sorted (see rebuildVisibleLists), so the spatial grid
    // gathers the near units and membership is a binary search; the result
    // is the list's own order.
    std::vector<int> candidates;
    const auto listed=visibleEnemies(u.player);
    if (listed.empty()) return 0;
    uint64_t enemies=~uint64_t(0);
    for (int player=0;player<numPlayers() && player<64;++player)
        if (allied(player,u.player)) enemies&=~(uint64_t(1)<<player);
    forEachNear(u.x.toFloat(),u.z.toFloat(),float(radius)+1.0f,[&](int idx) {
        const Unit& e=units_[size_t(idx)];
        if (!e.alive() || e.hp<=Fixed() || !e.type || e.embarked() || allied(e.player,u.player)) return;
        if (retailCentreDist2(u,e)>radius*radius) return;
        if (!std::binary_search(listed.begin(),listed.end(),e.id)) return;
        candidates.push_back(e.id);
    },enemies);
    std::sort(candidates.begin(),candidates.end());
    int32_t best=INT32_MAX,bestBad=INT32_MAX;
    int pick=0,pickBad=0;
    const bool mobile=!u.type->isStructure();
    for (int iteration=0;iteration<50 && !candidates.empty();++iteration) {
        const size_t index=fireRand(uint32_t(candidates.size()));
        const int id=candidates[index];
        candidates.erase(candidates.begin()+long(index));
        const Unit* tp=unit(id);
        if (!tp || !tp->alive() || tp->hp<=Fixed()) continue;
        const Unit& t=*tp;
        // shootme (UnitDef+0x260 bit 15) is required unless the shooter is
        // AI-owned. Retail's global override ([[62d558]]+7) is not identified
        // and is treated as unset.
        if (!t.type->shootMe && !players_[size_t(u.player)].automaticGates) continue;
        const int32_t eff=int32_t(w.damageVs(t.type));
        if (!u.type->kamikaze && !w.melee && !(eff>0 && slotInRange(u,t,slot) && slotClearShot(u,t,slot)))
            continue;   // 52ad60
        if (categoryMatch(u.type->noTargetCats,*t.type)) continue;
        if (w.status==Weapon::Status::Stoned && (t.type->cantBeStoned || t.incapacitated())) continue;
        if (w.status==Weapon::Status::Frozen && (t.type->cantBeFrozen || t.incapacitated())) continue;
        if (w.mindControl && (t.type->cantBeCaptured || t.incapacitated())) continue;
        if (w.status==Weapon::Status::Paralyzed && t.paralyzedFor>0) continue;
        if (w.melee && t.type->canFly) continue;
        if (w.toAir && !t.type->canFly) continue;
        if (w.noAir && t.type->canFly) continue;
        const bool insideMin=slotInsideMinRange(u,t,slot);
        if (insideMin && !mobile) continue;
        if (eff<=0) continue;   // 531e50
        // Score, lower wins, in retail's wrapping int32 arithmetic.
        uint32_t score;
        if (u.type->fireAtWillRandom) {
            const uint32_t n=uint32_t(INT32_MAX/eff);
            score=fireRand(n)/2;
            score+=fireRand(n);
        } else {
            const uint32_t q=uint32_t(int32_t(retailCentreDist2(u,t))/eff);
            score=fireRand(q)/2;
            score+=fireRand(q);
            if (w.melee) { score+=fireRand(q);score+=fireRand(q);score+=fireRand(q); }
        }
        if (mobile) {
            // Turn penalty, on the turn rate the mover's terrain bits scale
            // (412a77..412ae2: road, else water multiplier).
            int32_t tr=retailEffectiveTurnRate(uint16_t(u.type->turnRate),u.type->roadMult.v,
                                               u.type->waterMult.v,u.groundTerrainFlags);
            if (tr<1000) {
                if (tr<=0) tr=1;
                const int32_t d=std::abs(int32_t(int16_t(uint16_t(
                    retailDirection(t.x-u.x,t.z-u.z).v-u.heading.v))));
                const uint32_t p=uint32_t(d*250/tr/eff);
                score+=fireRand(p);
                score+=fireRand(p);
            }
        }
        if (insideMin) score*=10u;
        if (t.incapacitated()) score*=10u;
        const int32_t s=int32_t(score);
        if (slotBadTarget(u,slot,t)) { if (s<bestBad) { bestBad=s;pickBad=id; } }
        else if (s<best) { best=s;pick=id; }
    }
    return pick ? pick : pickBad;
}

// 40f330's slot part, for one player: a budget of N/30+1 counted units per
// call from a cursor that walks the player's slot range and wraps. Dead and
// status-locked slots cost nothing; live unarmed units cost budget but get no
// sweep. The walk stops when it returns to the slot it started on, so with
// fewer live units than the budget that slot is skipped until the counts
// change -- an emulated retail quirk, kept on purpose.
void World::tickFireSweep(int player) {
    Player& owner=players_[size_t(player)];
    std::vector<int> pool;
    int capacity=0;
    if (retailAllocation_ && retailEntityPools_) {
        const auto [first,count]=(*retailEntityPools_)[size_t(player)];
        capacity=count;
        pool.resize(size_t(std::max(0,count)));
        for (int i=0;i<count;++i) pool[size_t(i)]=first+i;
    } else {
        capacity=unitCap_>0 ? unitCap_ : kRetailSweepPool;
        if (size_t(player)<sweepIds_.size()) pool.swap(sweepIds_[size_t(player)]);
        std::sort(pool.begin(),pool.end());
    }
    if (pool.empty()) return;
    const int budget=capacity/30+1;
    const bool aiOwner=computerOwned(player);
    // The constructor (40fed0) leaves the cursor on the pool's last slot; a
    // restored pool has a real one, a synthetic pool walks from its end.
    const int32_t start=owner.sweepCursor==INT32_MAX && retailAllocation_ && retailEntityPools_ ?
        pool.back() : owner.sweepCursor;
    const auto after=std::upper_bound(pool.begin(),pool.end(),start);
    long position=long(after-pool.begin())-1;
    const bool present=position>=0 && pool[size_t(position)]==start;
    if (position<0) position=long(pool.size())-1;
    int used=0;
    int32_t cursor=start;
    for (size_t step=1;step<=pool.size();++step) {
        const size_t i=size_t(position+long(step))%pool.size();
        if (present && step==pool.size()) break;   // back at the start slot
        Unit* u=unit(pool[i]);
        if (!u || !u->alive() || u->hp<=Fixed() || !u->type || u->player!=player || u->incapacitated()) continue;
        if (!u->type->weapons.empty()) {
            // 40f411: the switch block, after the armed test and before the
            // fireState test, so the new slot is swept in this same visit.
            if (aiOwner && u->type->weaponSwitching) retailAiWeaponSwitch(*u);
            syncSlotMode(*u);
            if (u->fireState==2 && u->slotAuto && !u->type->canCapture) {
                for (int s=0;s<int(std::min<size_t>(3,u->type->weapons.size()));++s) {
                    if (!(u->slotAuto>>s&1)) continue;
                    const Weapon& w=u->type->weapons[size_t(s)];
                    if (w.cursorAirstrike || (w.commandFire && !aiOwner)) continue;
                    if (u->type->weaponSwitching && s!=u->weaponSlot) continue;
                    if (const int held=u->slotTarget[size_t(s)]) {
                        const Unit* t=unit(held);
                        if (t && sweepKeepsTarget(*u,s,*t)) continue;
                        // 51a9a0 retires a dead or invalid target.
                        if (!t || !t->alive() || t->hp<=Fixed()) dropSlotTarget(*u,s);
                    }
                    const int found=retailSelectTarget(*u,s,true);
                    if (found) setSlotTarget(*u,s,found);
                    else dropSlotTarget(*u,s);
                }
            }
        }
        if (++used>=budget) { cursor=pool[i];break; }
    }
    owner.sweepCursor=cursor;
}

// 52ae90 for auto slots: aim and fire at the slot target without braking,
// steering or stopping. Only a stopped mover pivots to its facing request.
void World::tickSlotWeapons(Unit& u) {
    syncSlotMode(u);
    if (!u.slotAuto || u.type->weapons.empty() || (u.type->onOffable && !u.active) || u.type->canCapture) {
        u.faceRequest=-1;
        return;
    }
    bool aiming=false;
    for (int s=0;s<int(std::min<size_t>(3,u.type->weapons.size()));++s) {
        if (!(u.slotAuto>>s&1) || !u.slotTarget[size_t(s)]) continue;
        if (u.type->weaponSwitching && s!=u.weaponSlot) continue;
        Unit* t=unit(u.slotTarget[size_t(s)]);
        if (!t || !t->alive() || t->hp<=Fixed() || !t->type) { dropSlotTarget(u,s);continue; }
        // 52fe30 refuses to aim inside minrange or out of range; the target
        // is kept either way.
        if (slotInsideMinRange(u,*t,s) || !slotInRange(u,*t,s) || !slotClearShot(u,*t,s)) continue;
        aiming=true;
        // The request points where the slot aims: the led SweetSpot, or a
        // melee target's origin (retailSlotFacing).
        const auto solution=queryWeaponAim(u.id,t->id,s);
        const Bam desired=solution ? retailHeadingToPort(solution->facing) : retailDirection(t->x-u.x,t->z-u.z);
        const bool facing=slotNeedsFacing(*u.type);
        if (facing) u.faceRequest=desired.v&0xffff;
        if (tickScriptWeapon(u,*t,s,true,solution ? &*solution : nullptr)) continue;
        const Weapon& w=u.type->weapons[size_t(s)];
        if (u.reloads[size_t(s)]>0) continue;
        if (facing && std::abs(bamDiff(desired,u.heading))>std::max(512,int(uint16_t(w.aimTol)))) continue;
        fire(u,*t,s);
    }
    if (!aiming) { u.faceRequest=-1;return; }
    // 4d9b6a: the request turns the body only while the navigator is idle.
    if (u.faceRequest>=0 && !u.type->canFly && !u.type->isStructure() && u.orders.empty() &&
        u.speed==Fixed() && u.type->turnInPlaceRate>0) {
        const int32_t turn=bamDiff(Bam(u.faceRequest),u.heading);
        u.heading=u.heading+Bam(std::clamp(turn,-u.type->turnInPlaceRate,u.type->turnInPlaceRate));
        u.turnReqBam=turn;
    }
}

// 409a50 part (b): a unit that returns fire hands the attacker to every auto
// slot that reaches it, unless the slot's current target is still a good
// one. No RNG. The idle retaliation mission (part a, 4d7de0) is not ported;
// findTarget and the ground-response path cover most of it.
void World::slotHitResponse(Unit& victim,const Unit& attacker) {
    if (!victim.alive() || victim.hp<=Fixed() || !victim.type || victim.type->weapons.empty() ||
        victim.fireState==0 || victim.incapacitated() || isNeutralPlayer(victim.player) ||
        !attacker.alive() || !attacker.type || allied(victim.player,attacker.player) ||
        victim.type->canCapture) return;
    syncSlotMode(victim);
    if (!victim.slotAuto) return;
    for (int s=0;s<int(std::min<size_t>(3,victim.type->weapons.size()));++s) {
        if (!(victim.slotAuto>>s&1)) continue;
        if (victim.type->weapons[size_t(s)].commandFire) continue;
        if (victim.type->weaponSwitching && s!=victim.weaponSlot) continue;
        if (slotInsideMinRange(victim,attacker,s) || !slotInRange(victim,attacker,s) ||
            !slotClearShot(victim,attacker,s)) continue;
        if (const Unit* t=unit(victim.slotTarget[size_t(s)]);
            t && t->alive() && t->hp>Fixed() && slotInRange(victim,*t,s) && slotClearShot(victim,*t,s) &&
            !slotInsideMinRange(victim,*t,s) && !slotBadTarget(victim,s,*t)) continue;
        if (victim.slotTarget[size_t(s)]!=attacker.id) setSlotTarget(victim,s,attacker.id);
    }
}

void World::guardSlotRetarget(Unit& u,const Unit& attacker) {
    if (!u.type || u.type->weapons.empty() || !attacker.alive() || !attacker.type) return;
    syncSlotMode(u);
    if (!u.slotAuto) return;
    // Follow_Ground keeps a current target that is inside its minimum range
    // (52adf0) out; VTOL_Follow tests only 52ad60.
    const bool flying=u.type->canFly;
    for (int s=0;s<int(std::min<size_t>(3,u.type->weapons.size()));++s) {
        if (!(u.slotAuto>>s&1)) continue;
        if (u.type->weapons[size_t(s)].commandFire) continue;
        if (u.type->weaponSwitching && s!=u.weaponSlot) continue;
        if (const Unit* t=unit(u.slotTarget[size_t(s)]);
            t && t->alive() && t->hp>Fixed() && (flying || !slotInsideMinRange(u,*t,s)) &&
            slotInRange(u,*t,s) && slotClearShot(u,*t,s) && !slotBadTarget(u,s,*t)) continue;
        if (u.slotTarget[size_t(s)]!=attacker.id) setSlotTarget(u,s,attacker.id);
    }
}

// 40f411..40fb92 host: the per-slot facts, the selected slot's target (the
// 51a9a0 lookup) and the enemy/ally counts around it, from the owner's 7-tick
// visible lists by live centre distance (411ae0/411dd0).
void World::retailAiWeaponSwitch(Unit& u) {
    const int sel=u.weaponSlot;
    const int n=int(std::min<size_t>(3,u.type->weapons.size()));
    if (sel<0 || sel>=n) return;
    int targetId=(u.slotAuto>>sel&1) ? u.slotTarget[size_t(sel)] : u.scriptAimTarget;
    Unit* target=targetId ? unit(targetId) : nullptr;
    if (targetId && (!target || !target->alive() || !target->type)) {
        if (u.slotAuto>>sel&1) dropSlotTarget(u,sel);   // 51a9a0 retires it
        target=nullptr;
    }
    const bool live=target && target->hp>Fixed();
    const float costMult=u.type->noVeteran ? 1.0f : u.vetMul();
    RetailWeaponSwitchInput in;
    in.selected=sel;
    in.target=live;
    in.targetHp=live ? int32_t(int16_t(target->hp.floorInt())) : 0;
    in.lowMonarch=u.type->commander && u.hp.floorInt()<int(unsigned(u.type->maxHp)/3u);
    for (int s=0;s<n;++s) {
        const Weapon& w=u.type->weapons[size_t(s)];
        auto& slot=in.slots[size_t(s)];
        const float cost=w.manaCost/costMult;
        slot.present=true;
        slot.affordable=cost<0.01f || double(cost)<=u.mana;
        slot.aoe=int32_t(uint16_t(w.aoe));
        slot.mindControl=w.mindControl;
        slot.damage=w.damage;
        const bool stone=w.status==Weapon::Status::Stoned,freeze=w.status==Weapon::Status::Frozen;
        const bool paralyzer=w.dmgType==4;
        slot.costlyOrSpecial=stone || freeze || w.mindControl || paralyzer || slot.aoe>=200 ||
            double(u.type->maxMana)*0.1<double(cost);
        if (live) {
            const Unit& t=*target;
            const bool locked=t.stonedFor>0 || t.frozenFor>0;
            slot.usable=!(stone && (t.type->cantBeStoned || locked)) &&
                        !(freeze && (t.type->cantBeFrozen || locked)) &&
                        !(w.mindControl && (t.type->cantBeCaptured || locked)) &&
                        !(paralyzer && (t.type->immuneToParalyzer || t.paralyzedFor>0));
        }
    }
    auto count=[&](const std::vector<int>& list,int32_t radius) {
        const int64_t r2=int64_t(radius)*radius;
        int found=0;
        for (const int id:list) {
            const Unit* o=unit(id);
            if (o && o->alive() && o->hp>Fixed() && retailCentreDist2(*target,*o)<=r2) ++found;
        }
        return found;
    };
    const Player& owner=players_[size_t(u.player)];
    const int choice=retailWeaponSwitchChoice(in,
        [&](int32_t r){return count(owner.visibleEnemies,r);},
        [&](int32_t r){return count(owner.visibleAllies,r);},
        [&](int bound){return int(gameRand(int32_t(bound)));});
    if (choice>=0) switchWeaponSlot(u,choice,false);
}

bool World::retailIssueAutoAttack(Unit& u,int targetId,bool forceReturn,bool forced) {
    if (targetId==u.id) return false;
    if (u.moveState==0 && !forceReturn && !forced) return false;   // Defensive/Hold Position: part (b) only
    if (u.fireState==0 && !forced) return false;                   // Hold Fire
    const Unit* t=unit(targetId);
    // 4de530 kind 3 (no attack mission for this pair) -> the closest host test.
    if (!t || !canAttackTarget(u,*t)) return false;
    const RetailGroundPoint pos{int16_t(u.x.floorInt()),int16_t(u.z.floorInt())};
    Order attack{Fixed(),Fixed(),targetId};
    attack.autoTarget=!forced;attack.goal=true;attack.issuedTick=tickCounter_;
    if (u.type->canMove && (forceReturn || (u.moveState==1 && !forced))) {
        // [Attack(leash, origin), return Move to the hit position (response
        // mode 1), the previous head].
        Order back{Fixed::fromInt(pos.x),Fixed::fromInt(pos.z),0};
        back.goal=true;back.issuedTick=tickCounter_;
        back.mission.flags=0x3000400u;
        if (u.type->canFly) back.flightMoveMission=true;
        else {
            back.groundMission=true;
            back.groundResponse.mode=1;back.groundResponse.goal=back.groundResponse.origin=pos;
            back.segmentX=Fixed::fromInt(pos.x);back.segmentZ=Fixed::fromInt(pos.z);
            back.hasSegment=true;back.controller=++nextMovementController_;
        }
        attack.chaseLeashed=true;
        attack.chaseLeash=uint16_t(std::clamp(u.type->leash,0,65535));
        attack.chaseOrigin=pos;
        u.orders.insert(u.orders.begin(),back);
    }
    u.orders.insert(u.orders.begin(),attack);
    setSlotsManual(u,kSlotsAttack);   // the attack mission owns the weapons
    cancelPath(u);u.routeStamp=-1;
    noteOrders(u.id);
    return true;
}

void World::retailHitResponse(Unit& e,const Unit* a,int dmgType) {
    if (dmgType==12 || dmgType==13) return;   // 51a2f2/51a333 never call 409a50
    // 409a59: event 0x10 to the victim's listeners (519a90) before any gate,
    // attacker or not; a guard hears it through attackedTick (guardAlarm).
    e.attackedTick=tickCounter_+1;
    if (!e.alive() || !e.type || e.hp<=Fixed()) return;
    if (a && (!a->alive() || !a->type || a->hp<=Fixed())) a=nullptr;
    // 409a8a: an AI Monarch hit, attacker or not, rolls the threat deadline;
    // one hit while it builds cancels the build so it can answer.
    if (e.type->commander && computerOwned(e.player)) {
        uint32_t until=tickCounter_+30;
        for (int i=0;i<3;++i) until+=gameRand(300);
        players_[size_t(e.player)].aiThreatUntil=until;
        const bool building=e.buildSiteId || (!e.orders.empty() &&
            (e.orders.front().buildType || e.orders.front().repairTarget));
        if (building && a!=&e) stop(e.id);
    }
    if (!a || a==&e) return;
    if (isNeutralPlayer(e.player) || !validUnitOwner(e.player)) return;
    if (e.type->weapons.empty() && !e.type->kamikaze) return;
    if (e.incapacitated()) return;
    if (allied(e.player,a->player)) return;
    // (a) 409ba8..409e4b: the retaliation mission's gates.
    const bool mobile=e.type->canMove && !e.type->isStructure();
    const size_t leg=e.orders.empty() ? 0 : currentLeg(e.orders);
    const bool patrolling=!e.orders.empty() && leg<e.orders.size() && e.orders[leg].patrol &&
        !e.orders.front().targetId;
    const bool idle=e.orders.empty() && !e.retailBuild && e.buildQueue.empty();
    bool mission=false;
    if (!e.type->weapons.empty() && (idle || (mobile && patrolling))) {
        const int s=e.type->weaponSwitching ? std::clamp(e.weaponSlot,0,int(std::min<size_t>(3,e.type->weapons.size()))-1) : 0;
        const Weapon& w=e.type->weapons[size_t(s)];
        const bool categoriesOk=!categoryMatch(e.type->noTargetCats,*a->type) && !slotBadTarget(e,s,*a);
        const int32_t eff=int32_t(w.damageVs(a->type));
        bool reach=eff>0 && slotInRange(e,*a,s);
        if (!reach && mobile) {
            const int32_t leash=int32_t(uint16_t(std::clamp(e.type->leash,0,65535)));
            const int32_t oct=retailOctDistance(a->x.floorInt()-e.x.floorInt(),a->z.floorInt()-e.z.floorInt());
            reach=oct<(leash*9)/10+(w.melee ? 0 : w.range);
        }
        const bool usable=canAttackTarget(e,*a) && !(w.melee && a->type->canFly) &&
            !(w.toAir && !a->type->canFly) && !(w.noAir && a->type->canFly) && eff>0;
        if (categoriesOk && reach && usable) {
            const bool flag=e.fireState==2 && mobile && patrolling;
            mission=retailIssueAutoAttack(e,a->id,flag);
        }
    }
    if (!mission) slotHitResponse(e,*a);   // (b)
}

void World::tickCombat(Unit& u, float dt, bool& groundMovementHandled) {
    if (u.type->onOffable && !u.active) return;   // explicitly powered down

    if (!u.standbyActive && !u.guardNoMoveActive) acquireTarget(u, false);
    const int activeTarget=!u.orders.empty() && !u.orders.front().guard ? u.orders.front().targetId : 0;
    if(u.scriptAimTarget && u.scriptAimTarget!=activeTarget)clearScriptWeaponTarget(u);
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
        // A ground guard keeps to Follow_Ground's ring round its ward (guardRing):
        // it walks to the ring's point on its own bearing and stands anywhere in
        // the band, so it never parks in a factory's yard.
        bool escorted;
        if (!u.type->canFly) {
            const RetailRingGoal ring=guardRing(u,*t);
            escorted=ring.accepts(footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ));
            if (!escorted) {
                const auto point=ring.navigationPoint(u.x,u.z,u.type->footX,u.type->footZ);
                end.x=point.x;end.z=point.z;
            }
        } else {
            const float dx = (t->x - u.x).toFloat(), dz = (t->z - u.z).toFloat();
            escorted = dx * dx + dz * dz <= 70 * 70;
        }
        if (escorted) {
            if (!u.type->canFly && !u.type->isStructure()) {
                brakeGround(u);
                groundMovementHandled=true;
                if (legion_ && isLegionPathfinding(pathfindingMode_)) legion_->noteEngaged(u);   // W5 step 0 counter
            } else if (!u.type->canFly || !o.mission.stage) u.speed=fxMax(Fixed(),u.speed-u.type->brake);
            // (VTOL_Follow's orbit never brakes near the guarded unit.)
        }
        return;   // movement walks toward o when out of reach
    }

    Unit* target = unit(u.orders.front().targetId);
    // Auto-acquisition and conversion attacks require a hostile target throughout
    // the order, not just when it starts. Another caster may have turned it since
    // then. Ordinary explicit attacks retain their intentional friendly-fire use.
    const bool requiresEnemy=u.orders.front().autoTarget || u.type->canCapture ||
        std::any_of(u.type->weapons.begin(),u.type->weapons.end(),
                    [](const Weapon& w){return w.mindControl;});
    if (!target || !target->alive() || (requiresEnemy && allied(u.player,target->player)) ||
        (target->hp<=Fixed() && !(target->retailSite && target->underConstruction))) {
        // Clear both the order and script aim before a pending release can fire
        // at a dying target or an ally that has just been converted.
        if(u.scriptAimTarget)clearScriptWeaponTarget(u);
        u.captureProg=0;
        dropLeg(u);
        cancelPath(u);
        u.routeStamp = -1;
        return;
    }
    // Recheck live immunity/capacity and the selected capture spell while aiming too.
    // A queued target may have become protected, embarked or otherwise unusable.
    if (!canAttackTarget(u,*target)) {
        if(u.scriptAimTarget)clearScriptWeaponTarget(u);
        u.captureProg=0;
        dropLeg(u);
        cancelPath(u);
        u.routeStamp = -1;
        return;
    }
    // A leashed auto attack (4d7de0's return path) retires once the unit
    // itself is the leash from where it was hit (Attack_Chase 403d1d, read
    // after the target checks); the return move behind it then walks it back.
    if (const Order& front=u.orders.front();front.chaseLeashed && front.chaseLeash &&
        retailGroundDistance({int16_t(u.x.floorInt()),int16_t(u.z.floorInt())},front.chaseOrigin)>=front.chaseLeash) {
        if(u.scriptAimTarget)clearScriptWeaponTarget(u);
        dropLeg(u);
        cancelPath(u);
        u.routeStamp = -1;
        return;
    }
    int slot = u.type->weapons.empty()
                   ? 0 : std::clamp(u.weaponSlot, 0, int(u.type->weapons.size()) - 1);
    const Weapon* sel = slot < int(u.type->weapons.size()) ? &u.type->weapons[slot] : nullptr;
    // 4de530 gives a flyer whose target's type can fly AirToAir (41ca50,
    // tickAirToAir) for any weapon without the `dropped` key, hoverattack or
    // not (4de981..4de9b8). A Dropped bomb does not count, and no shipped
    // weapon has the key (with it retail has no mission for the pair,
    // 4de6ed; World dogfights anyway). Against a target that cannot fly, a
    // flyer without hoverattack gets AirToGround (41bf80): attack runs
    // (tickAirToGround); one with it hovers (AirToGroundHover 41d820,
    // tickHoverAttack).
    const bool airToAir = u.type->canFly && !u.type->isStructure() && target->type && target->type->canFly;
    const bool hovering = !airToAir && u.type->canFly && (u.type->hoverAttack || (sel && sel->hoverAttack));
    const bool airRun = u.type->canFly && !hovering && !airToAir && !u.type->isStructure();
    if (u.scriptAimTarget!=target->id) {
        u.scriptAimTarget=target->id;
        // An attack must wake the airborne script pose too. Movement alone
        // cannot do this when a landed flyer acquires a target already in range.
        // AirToGround's and AirToAir's first stage does it (416c50).
        if (u.type->canFly && !airRun && !airToAir) notifyUnitScript(u,"BeginFlight");
    }
    float dx = (target->x - u.x).toFloat(), dz = (target->z - u.z).toFloat();
    float dist = std::sqrt(dx * dx + dz * dz);
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
    // The reach it fights at (combatReach, which findTarget shares so a unit
    // that may not chase never acquires what is dropped below): the selected
    // weapon's range for a switcher, the longest when they all fire, padded for
    // a structure target only for a melee weapon (Weapon::centreReach); the
    // arc for a ballistic weapon.
    const bool allWeapons = !u.type->weaponSwitching && u.type->weapons.size() > 1;
    const CombatReach cr = combatReach(u,*target,dist);
    const int arcSlot = cr.slot;
    const Weapon* approach = cr.weapon;
    const bool arcApproach = cr.arc;
    const bool inReach = cr.in;
    // Just inside the origin arc's reach the muzzle-to-SweetSpot solve can
    // still fail (queryWeaponAim launches nothing then): a unit free to move
    // keeps closing there rather than stand without a shot. That solve
    // turns with the aiming pose, so a unit that may not move keeps the
    // target until its aimed weapon has had no arc for kBlockedShots
    // reloads (noteNoArc), and drops it then (below).
    const auto arcShort = [&] {
        if (!arcApproach || !inReach) return false;
        // Only a marginal arc is asked: one that would fail with the target
        // 128 px higher (more than a muzzle and a SweetSpot sit off the origins).
        if (ballisticInRange(u.x,(u.type->canFly ? u.flightY : u.groundY)-Fixed::fromInt(128),u.z,*target,*approach))
            return false;
        const auto aim = queryWeaponAim(u.id,target->id,arcSlot);
        return aim && aim->unreachable;
    };
    // Melee: retail closes to footprint adjacency (see meleeInRange above);
    // `range` and LoS play no part.
    bool adj = sel && sel->melee && target->type &&
               meleeInRange(u.type, target->type, dx, dz);
    // Ranged units whose shot flies the line need a clear line to shoot; a
    // wall between them means close in / reposition rather than firing through
    // it (melee & flyers are exempt). Ballistic shots use retail's pitched 3D
    // arc and terrain collision, so a Ballistic weapon -- lobpreferred or not:
    // an archer's arrow, a Trebuchet's stone -- does not need this separate 2D
    // line test: the trajectory itself determines whether it clears the
    // obstruction (and noteShotImpact counts it when it does not). Decided by
    // the weapon: the approach slot's for closing in and for the drop below,
    // each firing slot's own for its shot (slotAims).
    const bool groundPair = !u.type->canFly && target->type && !target->type->canFly;
    const bool approachNeedsLine = approachNeedsClearLine(u,*target);
    bool needLoS = approachNeedsLine;
    if (groundPair && !needLoS) {
        if (allWeapons) {
            for (const Weapon& sw : u.type->weapons) needLoS = needLoS || sw.needsClearLine();
        } else if (sel) {
            needLoS = sel->needsClearLine();
        }
    }
    // LoS gates ONLY in-range firing and in-range repositioning; an out-of-range
    // chaser advances regardless (the `dist > best` move test below
    // short-circuits before `los` is read, and the fire gate is never reached out of
    // range). So defer the raycast until we're actually close enough for it to
    // matter -- identical result, but a marching army (the bulk of a big battle, all
    // out of range) stops paying for a per-tick line-of-sight cast it never uses.
    bool los = true;
    if (needLoS && inReach)
        los = combatLineOfSight(u,*target);
    // A static unit cannot chase -- and neither may one whose move standing order
    // says hold position. Both drop an AUTO-acquired target that walks out of
    // reach; an explicitly ordered attack is unaffected, because being told to go
    // kill something is exactly the case the standing order does not govern.
    const bool mayChase = u.type->canMove &&
                          (u.moveState != 0 || !u.orders.front().autoTarget || u.orders.front().chaseLeashed);
    if (!mayChase && !inReach) {
        dropLeg(u);
        cancelPath(u);
        u.routeStamp = -1;
        return;
    }
    // One that may not chase and has no clear line from here for the weapon
    // it fights with would hold an AUTO target for ever without a shot (a
    // tower behind a wall that went up; Hold Position behind a building), and
    // nothing else would get shot at: it drops it, and findTarget, which
    // applies the same test from the same spot, does not pick it again. An
    // explicitly ordered attack is kept: the order says which target.
    if (!mayChase && inReach && approachNeedsLine && !los && u.orders.front().autoTarget) {
        if(u.scriptAimTarget)clearScriptWeaponTarget(u);
        dropLeg(u);
        cancelPath(u);
        u.routeStamp = -1;
        return;
    }
    // Shots from here keep stopping short of the target on something they
    // cannot destroy (noteShotImpact): a ground mover free to leave its post
    // walks to a spot with a clear shot. So does a Hold Position unit whose
    // auto target interrupted a movement order (an attack-move, a patrol, a
    // move queued behind it): it is on its way anyway. If no spot is left in
    // reach it drops the target and goes on with that movement (findTarget
    // will not pick the target again from this spot). Hold Position with
    // nothing to go on with stays and fires on, as retail.
    if (!airRun && !hovering && sel && !sel->melee && inReach && u.type->canMove &&
        !u.type->canFly && !u.type->isStructure() && shotBlockedHere(u,*target)) {
        const bool onTheWay = u.orders.front().autoTarget && movementQueued(u);
        if ((u.moveState != 0 || onTheWay) && repositionForShot(u,*target,slot)) return;
        if (onTheWay && (u.searchFailed || u.repositions >= kMaxRepositions)) {
            if(u.scriptAimTarget)clearScriptWeaponTarget(u);
            dropLeg(u);
            cancelPath(u);
            u.routeStamp = -1;
            return;
        }
    }
    // A unit that may not move (a building, Hold Position on an auto target)
    // whose aimed shell has no arc from here (noteNoArc wrote the target
    // off) drops it; findTarget and the sweep do not pick it up again from
    // this spot.
    if (!mayChase && shotBlockedHere(u,*target) && arcShort()) {
        if(u.scriptAimTarget)clearScriptWeaponTarget(u);
        dropLeg(u);
        cancelPath(u);
        u.routeStamp = -1;
        return;
    }
    if (hovering) {
        tickHoverAttack(u,*target,sel,slot);
        groundMovementHandled=true; // the flight controller already committed movement
        dx=(target->x-u.x).toFloat(); dz=(target->z-u.z).toFloat();
        dist=std::sqrt(dx*dx+dz*dz);
    }
    // Attack_Chase (403bb0): the attack mission of a body that walks, its
    // selected weapon not melee (Attack_Melee, 404690, closes to contact).
    const bool chaseMission = !u.type->canFly && !u.type->isStructure() && u.type->canMove &&
                              !(sel && sel->melee);
    // An attack run or a dogfight flies in and out of range: its weapons fire
    // (below) when a slot has the target in range, and the mission flies on
    // regardless.
    if (!airRun && !airToAir && ((sel && sel->melee) ? !adj
                            : (!inReach || (approachNeedsLine && !los && mayChase) || (mayChase && arcShort())))) {
        if (hovering) return;
        if (chaseMission) u.orders.front().attackChased = true;
        // Advance toward the target, steering around impassable terrain.
        if (u.repathLeft > 0) --u.repathLeft;
        Order& o = u.orders[currentLeg(u.orders)];
        o.x = target->x;
        o.z = target->z;
        // Refresh the leg's destination, preserving its intermediate waypoints.
        // The shared route service and retry cadence handle chases too.
        return;   // movement handled by the normal move logic
    }
    // Fire a weapon when the target is in ITS [minrange, range] band, within its
    // aimtolerance, with a clear shot, and (unless noairweapon) legal against air.
    // Each weapon is gated on its own terms, because a non-switching unit runs all
    // of them at once and they rarely share a range band. A structure gets no
    // footprint pad here (Weapon::centreReach; combatReach's pad is the melee
    // approach's).
    auto slotAims = [&](int sl) {
        const Weapon& sw = u.type->weapons[size_t(sl)];
        if ((sw.mindControl || u.type->canCapture) && !canCaptureTarget(u.player,*target)) return false;
        if (sw.noAir && target->type && target->type->canFly) return false;
        if (groundPair && sw.needsClearLine() && !los) return false;
        if (sw.melee ? !adj : sw.ballisticClass() ? !ballisticInRange(u,*target,sw) : dist > sw.range)
            return false;
        return dist >= sw.minRange;
    };
    auto forEachSlot = [&](auto&& f) {
        if (u.type->weapons.empty()) return;
        if (allWeapons)
            for (int sl = 0; sl < int(u.type->weapons.size()); ++sl) f(sl);
        else if (sel)
            f(slot);
    };
    // Attack_Chase gives the selected slot its target in stage 1 (404406:
    // 4d4d40 stops the navigator, 51a700 sets the target). An order that
    // starts in reach runs stage 1 at its first dispatch; after a walk, two
    // ticks after the arrival stopped the body: the controller posts its
    // events from the mover, stage 2 takes them at the next dispatch and
    // sleeps a tick (403f2c..403f4f). The weapon update (52ae90) runs before
    // the missions, so it starts AimWeapon the tick after stage 1, the unit's
    // COB pass runs it straight away, and only then does the mover pivot to
    // the facing request 52fe30 posted. World runs the COB pass first and
    // the weapons after it, so the same order is: aim on the stage 1 tick
    // (World's aim start reaches the script at the next tick's COB pass), and
    // face from the tick after. Until the weapons aim the body stands still.
    // The gap is what lets a walker whose AimWeapon waits on its walk
    // controller fire: VERMAGE's waits for static 7, which restore_x sets
    // when the controller finds the body stopped and not yet aiming; aiming
    // on the tick it stops leaves the controller in restore_legs, and the
    // aim waits for ever. (Emulated end to end: docs/retail-engine.md,
    // "Attack_Chase: the stop comes before the aim".)
    // Both ride the front order, so a route that lands on a walk's last tick
    // (as one does after a single step) leaves the arrival unheld. Kept on
    // the goal leg through replaceLeg instead, the hold also follows those
    // steps, and Kirenna stalls in 26 attack-sweep cases she fires in now:
    // retail's slots stay automatic until stage 1, so the fire-at-will sweep
    // aims during such a walk, and World's are manual for the whole order.
    bool aimHeld = false, facingHeld = false;
    if (chaseMission) {
        Order& o = u.orders.front();
        if (o.attackAimTick == Order::kAttackUnaimed) o.attackAimTick = tickCounter_ + (o.attackChased ? 2u : 0u);
        aimHeld = tickCounter_ < o.attackAimTick;
        facingHeld = tickCounter_ <= o.attackAimTick;
    }
    // The attack controller stops navigation and supplies a facing target.
    // Ground movement brakes and commits once; pivoting starts only at rest.
    // A pivoting ground unit turns toward the request its aiming slots post in
    // 52fe30 -- the led SweetSpot, or a melee target's origin -- and the last
    // slot to post wins (52ae90 walks them in order).
    std::optional<Bam> request;
    if (!facingHeld && !u.type->canFly && slotNeedsFacing(*u.type))
        forEachSlot([&](int sl) {
            if (!slotAims(sl)) return;
            if (const auto aim=queryWeaponAim(u.id,target->id,sl)) request=retailHeadingToPort(aim->facing);
        });
    const Bam want = request ? *request : u.type->canFly ? fxAtan2(Fixed::fromFloat(dx),Fixed::fromFloat(dz))
        : retailDirection(target->x-u.x,target->z-u.z);
    const int32_t diff = bamDiff(want, u.heading);
    // Legion only: a ring member whose braked body would plug a narrow approach
    // walks on past the shot to deeper ground first (LegionNavigator::walkOnShot).
    const bool walkOn=!u.type->canFly && !u.type->isStructure() &&
        legion_ && isLegionPathfinding(pathfindingMode_) && legion_->walkOnShot(u);
    if (walkOn) {
        // movement walks the route on; the weapons below still fire if they aim
    } else if (!u.type->canFly && !u.type->isStructure()) {
        if (legion_ && isLegionPathfinding(pathfindingMode_)) legion_->noteEngaged(u);   // W5 step 0 counter
        if (facingHeld) brakeGround(u);
        else if (u.type->domain==UnitType::Domain::Water && !u.type->turnInPlaceRate) {
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
    } else if (!hovering && !airRun && !airToAir && !u.type->isStructure()) {
        u.speed=fxMax(Fixed(),u.speed-u.type->brake);
        if (u.speed==Fixed()) {
            // Flyers use their flight turn rate while aiming too. Many have no
            // ground turn-in-place rate and otherwise remain stuck off-axis.
            const int32_t maxTurn=u.type->canFly ? u.type->turnRate : u.type->turnInPlaceRate;
            u.heading=u.heading+Bam(std::clamp(diff,-maxTurn,maxTurn));
            u.turnReqBam=diff;
        }
    }
    auto tryFire = [&](int sl) {
        // An auto slot (AirToGround's and AirToAir's unselected ones) fires
        // at its own target (tickSlotWeapons), not the order's.
        if (u.slotAuto>>sl&1) return;
        if (!slotAims(sl)) return;
        const Weapon& sw = u.type->weapons[size_t(sl)];
        if(tickScriptWeapon(u,*target,sl))return;
        if(u.reloads[size_t(sl)]>0)return;
        // A bomb is let go, not aimed -- retail's Dropped weapon overrides its aim
        // virtual with a no-op, so the bomber is always considered on target (which
        // a hovering flyer with momentum could otherwise almost never satisfy).
        if (!u.type->isStructure() && sw.kind != Weapon::Kind::Dropped &&
            std::abs(diff) >= std::max(sw.aimTol, int32_t(0.03f / kCobAngle))) return;
        fire(u, *target, sl);
    };
    if (aimHeld) return;
    // 52ae90 runs the weapons before the missions and the mover.
    forEachSlot(tryFire);
    // cancapture: a charmer converts the target after sustained contact (~3s) or
    // once it is worn down, rather than killing it.
    if (u.type->canCapture && canCaptureTarget(u.player,*target) && (!(airRun || airToAir) || inReach)) {
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
    if (airRun && !u.orders.empty() && u.orders.front().targetId==target->id) {
        tickAirToGround(u,*target,sel);
        groundMovementHandled=true;
    }
    if (airToAir && !u.orders.empty() && u.orders.front().targetId==target->id) {
        tickAirToAir(u,*target,sel);
        groundMovementHandled=true;
    }
}

void World::setManaSharing(int fromPlayer, int toPlayer, bool enabled) {
    if (fromPlayer < 0 || fromPlayer >= numPlayers() || toPlayer < 0 ||
        toPlayer >= numPlayers() || fromPlayer == toPlayer || !allied(fromPlayer,toPlayer) ||
        players_[size_t(fromPlayer)].defeated || players_[size_t(toPlayer)].defeated) return;
    auto& mask=players_[size_t(fromPlayer)].manaShareMask;
    if (enabled) mask |= uint8_t(1u << toPlayer);
    else mask &= uint8_t(~(1u << toPlayer));
}

bool World::canGiveUnit(int unitId, int fromPlayer, int toPlayer) const {
    if (fromPlayer < 0 || fromPlayer >= numPlayers() || toPlayer < 0 ||
        toPlayer >= numPlayers() || fromPlayer == toPlayer || !allied(fromPlayer, toPlayer) ||
        players_[size_t(fromPlayer)].defeated || players_[size_t(toPlayer)].defeated) return false;
    const Unit* u = unit(unitId);
    // Retail Diplomacy::ShareUnits (0x4b6a80) excludes airborne units,
    // passengers, loaded carriers and commanders. Never transfer unfinished sites.
    return u && u->alive() && u->hp > Fixed() && u->type && u->player == fromPlayer &&
        !u->type->commander && !u->underConstruction && u->flightGroundMode != 2 &&
        !u->inTransport && u->cargo.empty() && !atUnitCap(toPlayer) && !atTypeCap(toPlayer, u->type);
}

bool World::giveUnit(int unitId, int fromPlayer, int toPlayer) {
    if (!canGiveUnit(unitId, fromPlayer, toPlayer)) return false;
    Unit& u = *unit(unitId);
    cancelBuilds(unitId);
    stop(unitId);
    if (players_[size_t(fromPlayer)].unitCount > 0) --players_[size_t(fromPlayer)].unitCount;
    ++players_[size_t(toPlayer)].unitCount;
    transferEntitySlot(u,toPlayer);
    u.player = toPlayer;
    u.rally.clear();
    u.selfDestructT = -1;
    u.squad = 0;
    u.lastHitBy = 0;
    u.lastHitPlayer = -1;
    u.captureProg = 0;
    gPlayersValid_ = false;
    return true;
}

// Native 4cc3c0 searches the primary unit grid, not terrain/build eligibility.
int World::scenarioCreate(const UnitType* type,int player,int lx,int lz,int hx,int hz) {
    if (!type || !validUnitOwner(player) || atUnitCap(player)) return 0;
    lx=std::max(0,lx);lz=std::max(0,lz);
    hx=std::min(terW_-1,hx);hz=std::min(terH_-1,hz);
    int cx=int(int64_t(lx)+(int64_t(hx)-lx)/2);
    int cz=int(int64_t(lz)+(int64_t(hz)-lz)/2);
    std::set<std::pair<int,int>> occupied;
    for (const auto& u:units_) {
        if (!u.alive() || u.hp<=Fixed() || !u.type || u.embarked() ||
            (!u.type->isStructure() && u.flightGroundMode!=1)) continue;
        const int ox=footprintOrigin(u.x,u.type->footX),oz=footprintOrigin(u.z,u.type->footZ);
        // Native primary insertion requires the complete footprint strictly
        // inside the far edge. Partial/off-map bodies occupy no primary cells.
        if (ox<0 || oz<0 || ox+u.type->footX>=terW_ || oz+u.type->footZ>=terH_) continue;
        bool opened=false;
        if (const auto script=unitScripts_.find(u.id);script!=unitScripts_.end()) opened=script->second.yardOpen;
        for (int z=std::max(lz,oz);z<=std::min(hz,oz+u.type->footZ-1);++z)
            for (int x=std::max(lx,ox);x<=std::min(hx,ox+u.type->footX-1);++x) {
                if (u.type->isStructure() && !u.type->yardMap.empty()) {
                    const char yard=u.type->yardMap.at(size_t(z-oz)*u.type->footX+x-ox);
                    // 4c0f77..4c0fdd: . =0, y/Y=0x29/31, c/C=0x2d/35;
                    // insertion tests bit 4 when closed, bit 2 when opened.
                    if (yard=='.' || yard=='y' || yard=='Y' || (!opened && yard=='O') ||
                        (opened && (yard=='c' || yard=='C'))) continue;
                }
                occupied.emplace(z,x);
            }
    }
    bool found=false;
    for (int z=lz;z<=hz && !found;++z) for (int x=lx;x<=hx;++x)
        if (!occupied.contains({z,x})) {cx=x;cz=z;found=true;break;}
    // Match native fixed-point wrapping for malformed/out-of-map fallback
    // positions without signed overflow or float-to-int overflow.
    const auto coordinate=[](int cell,int footprint) {
        return Fixed::raw(std::bit_cast<int32_t>(uint32_t(cell)*0x100000u+uint32_t(footprint)*0x80000u)).toFloat();
    };
    return spawn(type,coordinate(cx,type->footX),coordinate(cz,type->footZ),0.f,player);
}

void World::scenarioDestroy(int unitId,uint8_t deathType) {
    auto* target=unit(unitId);
    if (!target || !target->alive() || !target->type) return;
    // Detach every current worker before retiring the site, so no later worker
    // can restore its HP. Keep unrelated queued production available afterward.
    for (auto& builder:units_) {
        if (builder.id==unitId) continue;
        if (builder.buildSiteId==unitId || (builder.retailBuild && builder.retailBuild->target==unitId)) {
            builder.buildSiteId=0;
            cancelBuilds(builder.id);
            builder.productionSiteId=0;
        }
        if (builder.repairId==unitId) cancelBuilds(builder.id);
    }
    // A scripted kill is a death too: the factory's unfinished output goes
    // with it (killProductionOutput), here at once like the factory itself.
    killProductionOutput(*target,true);
    cancelBuilds(unitId);stop(unitId);
    auto& u=*unit(unitId);
    const int owner=u.player;
    u.hp=Fixed();u.deathType=deathType;u.severity=0;u.corpseStatue=-1;
    u.retailSite.reset();u.beingBuilt=false;
    beginUnitDeath(u);
    if (u.type->isStructure()) blockFoot(*u.type,u.x.toFloat(),u.z.toFloat(),false);
    retireCorpse(u);
    u.speed=Fixed();u.selfDestructT=-1;
    updateBodyIndex(u);
    for (auto& plane:searchGrades_) refreshSearchRect(plane,footprintOrigin(u.x,u.type->footX),
        footprintOrigin(u.z,u.type->footZ),u.type->footX,u.type->footZ);
    if (validUnitOwner(owner)) {
        ++players_[size_t(owner)].losses;
        if (players_[size_t(owner)].unitCount>0) --players_[size_t(owner)].unitCount;
    }
    if (!scoreAutomaticDisabled_ && !u.underConstruction && validUnitOwner(u.lastHitPlayer) && u.lastHitPlayer!=owner) {
        auto& score=players_[size_t(u.lastHitPlayer)].score;
        score=std::bit_cast<int32_t>(uint32_t(score)+uint32_t(u.type->experiencePoints));
    }
    const int killOwner=creditedOwner(u.lastHitBy);
    if(validUnitOwner(killOwner) && !allied(killOwner,owner))++players_[size_t(killOwner)].kills;
    if (auto* killer=unit(u.lastHitBy);killer && killer->type) {
        if (killer->alive() && killer->type->canMove && !killer->type->noVeteran) {
            ++killer->xp;killer->veteran=std::min(10,killer->xp);
        }
    }
    if (scenario_) scenario_->unitDied(*this,unitId);
    if (mission_) mission_->unitDied(*this,unitId);
}

void World::scenarioAdjustHealth(int unitId,uint16_t amount,bool heal) {
    auto* u=unit(unitId);
    if (!u || !u->type || !u->alive() || (u->hp<=Fixed() && !u->underConstruction)) return;
    const int64_t delta=int64_t(amount)*Fixed::kOne;
    if (heal) u->hp=Fixed::raw(int32_t(std::min<int64_t>(Fixed::fromInt(u->maximumHp()).v,int64_t(u->hp.v)+delta)));
    else {
        // Native HP subtraction is a wrapping 16-bit word operation. Express
        // the corresponding fixed-point wrap as unsigned arithmetic, never UB.
        const int32_t next=std::bit_cast<int32_t>(uint32_t(u->hp.v)-(uint32_t(amount)<<16));
        u->overkill=Fixed::raw(next<0 ? int32_t(std::min<int64_t>(-int64_t(next),INT32_MAX)) : 0);
        u->hp=Fixed::raw(std::max(0,next));
    }
    if (u->retailSite) {
        auto& progress=u->retailSite->progress;
        progress.hp=uint16_t(u->hp.floorInt());
        // Native healing leaves construction progress untouched; damage writes
        // 1 - HP/maxHP while unfinished, before testing for a lethal result.
        if (!heal && progress.remaining!=0)
            progress.remaining=float(1.0-double(progress.hp)/std::max(1,u->maximumHp()));
    }
    if (!heal) {
        u->deathType=13;u->lastHitBy=0;u->lastHitPlayer=-1;
        if (u->underConstruction && u->hp<=Fixed()) scenarioDestroy(unitId,13);
    }
}

// Native CRT ownership uses 514da0, which preserves health rather than applying
// charm's half-health floor. Keep stable engine IDs while removing owner commands.
bool World::scenarioTransfer(int unitId, int newPlayer) {
    Unit* u = unit(unitId);
    if (!u || !u->type || !u->alive() || u->hp <= Fixed() ||
        !validUnitOwner(newPlayer) || u->player == newPlayer || atUnitCap(newPlayer)) return false;
    const int oldPlayer = u->player;
    // Retail transfers allocate a new object; old construction workers lose
    // their target. Stable engine IDs must not keep those old jobs attached.
    for (auto& builder:units_) if (builder.id!=unitId &&
        (builder.buildSiteId==unitId || builder.repairId==unitId ||
         (builder.retailBuild && builder.retailBuild->target==unitId))) {
        if (builder.buildSiteId==unitId) builder.buildSiteId=0;
        cancelBuilds(builder.id);builder.productionSiteId=0;
    }
    if (u->retailSite) u->retailSite->builder=0;
    cancelBuilds(unitId);
    stop(unitId);
    if (validUnitOwner(oldPlayer) && players_[size_t(oldPlayer)].unitCount > 0)
        --players_[size_t(oldPlayer)].unitCount;
    ++players_[size_t(newPlayer)].unitCount;
    if (mission_) mission_->unitCaptured(*this, unitId);
    transferEntitySlot(*u,newPlayer);
    u->player = newPlayer;
    u->rally.clear();
    u->selfDestructT = -1;
    u->squad = 0;
    u->lastHitBy = 0;
    u->lastHitPlayer = -1;
    u->captureProg = 0;
    gPlayersValid_ = false;
    return true;
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
    if (mission_) mission_->unitCaptured(*this,t.id);
    // Retail's ownership change (514da0) kills the old object, so a factory's
    // BuildingBuild aborts and its unfinished output dies (killProductionOutput).
    killProductionOutput(t,true);
    transferEntitySlot(t,newPlayer);
    t.player = newPlayer;
    gPlayersValid_=false;
    t.orders.clear();
    t.hp = fxMax(t.hp, Fixed::fromFloat(t.type->maxHp * 0.5f));
    t.lastHitBy = 0; t.lastHitPlayer = -1;
    t.squad = 0;             // no longer in its old owner's control group
    t.buildQueue.clear();    // and not still producing for them
    t.buildProgress = 0; t.buildFraction = 0;
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
    ++placementEpoch_;
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

    // A pure block can only split components, and a split is settled locally -- so
    // building placements need not throw away a whole-map labelling that costs ~90ms to
    // rebuild on a 2048x2048 grid (~5.7ms on the 512x512 maps this was first measured
    // on). Entries that cannot be carried keep their old (now stale) version and get
    // rebuilt by the next query, exactly as before.
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
    paths_.clear();legion_.reset();
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

// One nonzero cell's term in a plane's XOR checksum.
static uint64_t searchCellWord(size_t index,uint8_t grade) {
    if (!grade) return 0;
    uint64_t v=(uint64_t(index)<<4)|grade;
    v=(v^(v>>30))*0xbf58476d1ce4e5b9ull;
    v=(v^(v>>27))*0x94d049bb133111ebull;
    return v^(v>>31);
}

void World::setSearchCell(SearchGradePlane& plane,size_t index,uint8_t value) {
    plane.checksum^=searchCellWord(index,plane.cells[index])^searchCellWord(index,value);
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

// mapFeatureGrade's body, inline for the search-plane cell loops.
static inline int featureGradeAt(const std::vector<RetailMapFeatureCell>& cells,
                                 const std::vector<RetailMapFeatureType>& types,int hW,int cx,int cz) {
    const auto& cell=cells[size_t(cz)*hW+cx];
    uint16_t index=cell.feature;
    int ox=cx,oz=cz;
    if (index==0xfffe) {
        ox-=cell.backX;oz-=cell.backZ;
        index=cells[size_t(oz)*hW+ox].feature;
        if (index>=0xfffa) index=0xffff;
    }
    if (index==0xffff) return 7;
    if (index>=types.size()) return 0;
    const auto& type=types[index];
    return type.blocking ? (type.clearable ? 1 : 0) : 7;
}

// Out of line, as before featureGradeAt existed: inlined into the placement
// scorers' loops it measured slower there.
[[gnu::noinline]] int World::mapFeatureGrade(int cx,int cz) const {
    return featureGradeAt(mapPlacementCells_,mapPlacementTypes_,hW_,cx,cz);
}

void World::rebuildBodyIndex() const {
    // Clear only populated buckets, before resizing for a possible terrain
    // change. Sparse armies on large maps should not scan every empty bucket.
    for(size_t tile:bodyUsedTiles_)bodyTiles_[tile].clear();
    bodyUsedTiles_.clear();
    bodyTilesW_=(hW_+7)/8; bodyTilesH_=(hH_+7)/8;
    bodyTiles_.resize(size_t(bodyTilesW_)*bodyTilesH_);
    bodyTileBounds_.assign(units_.size(),{-1,-1,-1,-1});
    bodyFootprints_.resize(units_.size());
    bodyIndexValid_=true;
    TAK_PASS();
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
    // positives; a cell keeps its ground-word holder, else the last eligible
    // unit in vector order.
    for (int tz=bounds[1];tz<bounds[3];++tz)
        for (int tx=bounds[0];tx<bounds[2];++tx) {
            const size_t tile=size_t(tz)*bodyTilesW_+tx;
            auto& bucket=bodyTiles_[tile];
            if(bucket.empty())bodyUsedTiles_.push_back(tile);
            bucket.push_back(int(index));
        }
}

// ---- Retail's ground word +0 (5066f0 / 506aa0 / 5062d0) -------------------
// A placement event stamps a body's footprint into the cells it may hold
// (5066f0, after 506aa0 for a move). A cell whose holder is live stays with
// the holder: the holder gains +0x130 bit 0x400000 (it refused someone) and
// the newcomer 0x800000 (it was refused). That is retail's rule for owner
// kinds 1 and 2, this machine's human and AI. A remote player's body (kind 3)
// is overwritten instead; World simulates every seat locally, so no seat is
// kind 3 and that branch is not ported. A body that leaves (506aa0) clears
// only the cells it holds, and if it refused anyone, every refused body over
// its old rectangle is restamped (5065b0 -> 5097f0 -> 5062d0), in sector-list
// order, before the leaver stamps its new cells.
bool World::groundHolderLive(int id) const {
    const Unit* u=id>0 ? unit(id) : nullptr;
    return u && u->alive() && !u->embarked();
}

// The structure branch's yard mask (5066f0's bit 2 open / bit 4 closed). The
// shipped yard characters . c C o S w decode as: '.' never; c and C only while
// the yard is closed; o, S, w always. An unparsed yardmap blocks everywhere.
bool World::groundStampsCell(const Unit& u,int dx,int dz) const {
    const auto& type=*u.type;
    if (!type.isStructure() || type.yardMap.empty()) return true;
    const char yard=type.yardMap.at(size_t(dz)*type.footX+dx);
    if (yard=='.') return false;
    if (yard!='c' && yard!='C') return true;
    return !(size_t(u.id)<scriptYardById_.size() && scriptYardById_[size_t(u.id)]);
}

// One cell of 5066f0/5062d0: a live holder keeps it, else the body takes it.
bool World::groundClaimCell(Unit& u,size_t cell) {
    int32_t& holder=groundOwner_[cell];
    if (holder && groundHolderLive(holder)) {
        unit(holder)->ground.flags|=Unit::GroundStamp::kRefusing;
        u.ground.flags|=Unit::GroundStamp::kRefused;
        return false;
    }
    holder=u.id;
    return true;
}

void World::groundStamp(Unit& u) {
    if (groundOwner_.empty() || !u.type || !u.alive() || u.embarked()) return;
    auto& g=u.ground;
    const int fx=u.type->footX,fz=u.type->footZ;
    const int x0=footprintOrigin(u.x,fx),z0=footprintOrigin(u.z,fz);
    g.x=int16_t(x0);g.z=int16_t(z0);g.fx=uint8_t(fx);g.fz=uint8_t(fz);
    // 506650 relinks the body when its sector changes: off the map (any edge
    // the footprint reaches, 506725..506760) or position >> 23 on each axis.
    if (x0<0 || z0<0 || x0+fx>=hW_ || z0+fz>=hH_) {
        if (!(g.flags&Unit::GroundStamp::kOffMap) || !(g.flags&Unit::GroundStamp::kLinked)) g.link=++groundLinkSerial_;
        g.flags|=Unit::GroundStamp::kLinked|Unit::GroundStamp::kOffMap;
        return;
    }
    const int sx=u.x.v>>23,sz=u.z.v>>23;
    if (!(g.flags&Unit::GroundStamp::kLinked) || (g.flags&Unit::GroundStamp::kOffMap) || sx!=g.sectorX || sz!=g.sectorZ) {
        g.sectorX=int16_t(sx);g.sectorZ=int16_t(sz);g.link=++groundLinkSerial_;
    }
    g.flags=uint8_t((g.flags|Unit::GroundStamp::kLinked)&~Unit::GroundStamp::kOffMap);
    const bool structure=u.type->isStructure();
    // A mobile body is stamped only while landed (+0x130 & 3 == 1).
    if (!structure && u.type->canFly && u.flightGroundMode!=1) return;
    for (int dz=0;dz<fz;++dz) for (int dx=0;dx<fx;++dx)
        if (!structure || groundStampsCell(u,dx,dz)) groundClaimCell(u,size_t(z0+dz)*hW_+x0+dx);
}

void World::groundUnstamp(Unit& u) {
    if (groundOwner_.empty()) return;
    auto& g=u.ground;
    if ((g.flags&Unit::GroundStamp::kLinked) && !(g.flags&Unit::GroundStamp::kOffMap))
        for (int z=g.z;z<g.z+g.fz;++z) for (int x=g.x;x<g.x+g.fx;++x) {
            if (x<0 || z<0 || x>=hW_ || z>=hH_) continue;
            auto& holder=groundOwner_[size_t(z)*hW_+x];
            if (holder==u.id) holder=0;
        }
    g.flags&=uint8_t(~Unit::GroundStamp::kRefused);
    if (g.flags&Unit::GroundStamp::kRefusing) {
        g.flags&=uint8_t(~Unit::GroundStamp::kRefusing);
        groundRestampRect(g.x,g.z,g.fx,g.fz);
    }
}

void World::groundRemove(Unit& u) {
    groundUnstamp(u);
    u.ground.flags&=uint8_t(~(Unit::GroundStamp::kLinked|Unit::GroundStamp::kOffMap));
}

// 5062d0: a refused body claims what has come free, and a structure lets go
// of the yard cells its yard state no longer stamps. A cell the body already
// holds counts as a live holder, so it marks itself both ways (5063b2).
void World::groundRestamp(Unit& u) {
    auto& g=u.ground;
    if (!(g.flags&Unit::GroundStamp::kRefused)) return;
    g.flags&=uint8_t(~Unit::GroundStamp::kRefused);
    const bool structure=u.type->isStructure();
    if (!structure && u.type->canFly && u.flightGroundMode!=1) return;
    for (int dz=0;dz<g.fz;++dz) for (int dx=0;dx<g.fx;++dx) {
        const int x=g.x+dx,z=g.z+dz;
        if (x<0 || z<0 || x>=hW_ || z>=hH_) continue;
        const size_t cell=size_t(z)*hW_+x;
        if (!structure || groundStampsCell(u,dx,dz)) groundClaimCell(u,cell);
        else if (groundOwner_[cell]==u.id) groundOwner_[cell]=0;
    }
}

// 5097f0 with the 5062d0 visitor: sectors column by column from one before
// the rectangle's to one after, each list newest link first, every body whose
// stamped rectangle overlaps. Only refused bodies act, and a restamp can mark
// no other body refused, so the refused set is fixed up front.
void World::groundRestampRect(int x,int z,int w,int h) {
    const int sx0=(int16_t(x)>>3)-1,sx1=((int16_t(x)+int16_t(w))>>3)+1;
    const int sz0=(int16_t(z)>>3)-1,sz1=((int16_t(z)+int16_t(h))>>3)+1;
    const int sectorsW=(hW_+7)/8,sectorsH=(hH_+7)/8;
    std::vector<Unit*> refused;
    for (auto& u:units_) {
        const auto& g=u.ground;
        if (!(g.flags&Unit::GroundStamp::kRefused) || !(g.flags&Unit::GroundStamp::kLinked) ||
            (g.flags&Unit::GroundStamp::kOffMap)) continue;
        if (g.sectorX<sx0 || g.sectorX>sx1 || g.sectorZ<sz0 || g.sectorZ>sz1 ||
            unsigned(g.sectorX)>=unsigned(sectorsW) || unsigned(g.sectorZ)>=unsigned(sectorsH)) continue;
        if (!(x<g.x+g.fx && x+w>g.x && z<g.z+g.fz && z+h>g.z)) continue;
        refused.push_back(&u);
    }
    std::sort(refused.begin(),refused.end(),[](const Unit* a,const Unit* b) {
        const auto& p=a->ground; const auto& q=b->ground;
        if (p.sectorX!=q.sectorX) return p.sectorX<q.sectorX;
        if (p.sectorZ!=q.sectorZ) return p.sectorZ<q.sectorZ;
        return p.link>q.link;
    });
    for (Unit* u:refused) groundRestamp(*u);
}

// A fresh grid (new terrain): every placed body stamped again in unit order.
void World::groundRebuild() {
    groundOwner_.assign(hW_>0 && hH_>0 ? size_t(hW_)*hH_ : 0,0);
    groundLinkSerial_=0;
    for (auto& u:units_) u.ground={};
    for (auto& u:units_) groundStamp(u);
}

World::SearchBodyRect World::searchBodyRect(int x,int z,int w,int h,int ignoreId) const {
    SearchBodyRect result{x,z,w,h,BodyCells(size_t(w)*h)};
    TAK_PROBE(++probe::tl.bodyRects; if (size_t(w)*h>BodyCells::kInline) ++probe::tl.bodyRectsHeap);
    auto stamp=[&](const Unit& u,const std::array<int,4>* bounds=nullptr) {
        if (u.id==ignoreId || !u.alive() || u.embarked() || !u.type || (u.type->canFly && u.flightGroundMode!=1)) return;
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
            // The ground word's holder, when it covers the cell. Otherwise
            // (no event stamped it: a body moved outside the events, or one
            // retail keeps off the word at the map edge) the last covering
            // unit in vector order, whatever order the buckets are visited in.
            auto& occupant=result.cells[size_t(cz-z)*w+cx-x];
            const int holder=groundOwner_.empty() || cx<0 || cz<0 || cx>=hW_ || cz>=hH_ ? 0
                : groundOwner_[size_t(cz)*hW_+cx];
            if (!occupant || u.id==holder || (occupant->id!=holder && occupant<&u)) occupant=&u;
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
            TAK_PASS();
            for (const auto& u:units_) stamp(u);
            if (result.cells!=indexed) throw std::runtime_error("body spatial index differs from full occupancy scan");
        }
#endif
    } else { TAK_PASS(); for (const auto& u:units_) stamp(u); }
    return result;
}

bool World::gatePassageAt(int x,int z,const std::vector<int>* gates) const {
    auto passage=[&](const Unit& gate) {
        if (!gate.alive() || gate.embarked() || !gate.type || !gate.type->gate) return false;
        const auto& type=*gate.type;
        const int dx=x-footprintOrigin(gate.x,type.footX),dz=z-footprintOrigin(gate.z,type.footZ);
        if (dx<0 || dz<0 || dx>=type.footX || dz>=type.footZ || type.yardMap.empty()) return false;
        const char yard=type.yardMap.at(size_t(dz)*type.footX+dx);
        return yard=='c' || yard=='C';
    };
    // A synced gate list (tickAutomaticGates) holds every unit the walk would
    // accept. Other callers (path searches, which may run on workers) walk.
    if (gates) {
        for (const int id:*gates) if (passage(*unit(id))) return true;
        return false;
    }
    TAK_PASS();
    for (const auto& gate:units_) if (passage(gate)) return true;
    return false;
}

bool World::gateWantsOpen(const Unit& gate,const std::vector<int>& gates) const {
    const int x=footprintOrigin(gate.x,gate.type->footX),z=footprintOrigin(gate.z,gate.type->footZ);
    const auto bodies=searchBodyRect(x-1,z-1,gate.type->footX+2,gate.type->footZ+2);
    for (const auto* body:bodies.cells) {
        if (!body || body==&gate || body->player!=gate.player || body->type->isStructure()) continue;
        if (body->speed.v) return true;
        const int bx=footprintOrigin(body->x,body->type->footX),bz=footprintOrigin(body->z,body->type->footZ);
        for (int dz=0;dz<body->type->footZ;++dz)
            for (int dx=0;dx<body->type->footX;++dx)
                if (gatePassageAt(bx+dx,bz+dz,&gates)) return true;
    }
    return false;
}

void World::tickAutomaticGates() {
    // Gate policy runs at simulation cadence. The skirmish AI scheduler is
    // intentionally independent of retail's planner; the proximity rule is not.
    // The gates alone, in unit order (UnitClassLists). Indexed: the scripts
    // setActive runs could in principle lengthen the list (they never spawn).
    syncUnitLists();
    for (size_t k=0;k<unitLists_.gates.size();++k) {
        auto& gate=*unit(unitLists_.gates[k]);
        if (!gate.alive() || gate.underConstruction ||
            !players_[size_t(gate.player)].automaticGates) continue;
        setActive(gate.id,gateWantsOpen(gate,unitLists_.gates));
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

// The per-cell reference: rates every cell's footprint and clearance ring
// with rawSearchGrade. refreshSearchRect must equal it (TAK_VERIFY_SEARCH_PLANE
// checks every refresh in a Debug binary, and pathblock_test compares both).
void World::refreshSearchRectPerCell(SearchGradePlane& plane,int x,int z,int w,int h) {
    const int width=plane.nav->width(),height=plane.nav->height();
    const int x0=std::max(0,x-plane.footX),z0=std::max(0,z-plane.footZ);
    const int x1=std::min(width,x+w+1),z1=std::min(height,z+h+1);
    if (x1<=x0 || z1<=z0) return;
    const auto bodies=searchBodyRect(x0-1,z0-1,x1-x0+plane.footX+2,z1-z0+plane.footZ+2);
    for (int cz=z0;cz<z1;++cz) for (int cx=x0;cx<x1;++cx) {
        const int grade=retailCachedFootprintGrade(cx,cz,plane.footX,plane.footZ,
            [&](int qx,int qz,int qw,int qh) {
                TAK_PROBE(++probe::tl.refreshRawGrades);
                return rawSearchGrade(plane,qx,qz,qw,qh,&bodies);
            });
        const size_t index=size_t(cz)*width+cx;
        setSearchCell(plane,index,uint8_t((plane.cells[index]&8)|grade));
    }
}

namespace {
void lowerBytes(uint8_t* __restrict dst,const uint8_t* __restrict src,int n) {
    for (int x=0;x<n;++x) dst[x]=src[x]<dst[x] ? src[x] : dst[x];
}

// out(x,z) = minimum of cell over [x,x+w) x [z,z+h) on a width x height grid,
// where that box fits (0 elsewhere). Separate buffers, so the byte loops vectorize.
void searchBoxMin(const std::vector<uint8_t>& cell,int width,int height,int w,int h,
                  std::vector<uint8_t>& rows,std::vector<uint8_t>& out) {
    out.assign(cell.size(),0);
    if (w>width || h>height) return;
    rows.resize(cell.size());
    const int span=width-w+1;
    for (int z=0;z<height;++z) {
        const uint8_t* src=&cell[size_t(z)*width];
        uint8_t* dst=&rows[size_t(z)*width];
        std::copy_n(src,span,dst);
        for (int i=1;i<w;++i) lowerBytes(dst,src+i,span);
    }
    for (int z=0;z+h<=height;++z) {
        uint8_t* dst=&out[size_t(z)*width];
        std::copy_n(&rows[size_t(z)*width],width,dst);
        for (int j=1;j<h;++j) lowerBytes(dst,&rows[size_t(z+j)*width],width);
    }
}

// retailCachedFootprintGrade from box minima: the footprint, then four
// clearance strips that together cover the ring round it. Some strip's bounds
// test fails exactly when that ring leaves the map. foot() is the minimum over
// the footprint at (cx,cz), ring() the minimum over (footX+2)x(footZ+2) at
// (cx-1,cz-1); each is read only where its box lies inside the map.
template<class Foot,class Ring>
int boxMinFootprintGrade(int cx,int cz,int footX,int footZ,int width,int height,Foot foot,Ring ring) {
    int grade=cx+footX>=width || cz+footZ>=height ? 0 : foot();
    if (grade>4 && (cx<1 || cz<1 || cx+footX+1>=width || cz+footZ+1>=height || ring()<6)) grade=4;
    return grade;
}
} // namespace

// rawSearchGrade for one cell, without its rectangle bounds test or bodies:
// the feature grade, then the terrain grade.
inline int World::searchCellGrade(const NavGrid& nav,int cx,int cz,bool hasMapCells) const {
    int grade=7;
    if (hasMapCells) grade=featureGradeAt(mapPlacementCells_,mapPlacementTypes_,hW_,cx,cz);
    if (grade) grade=std::min(grade,nav.terrainGrade(cx,cz,!hasMapCells));
    return grade;
}

// Every rating refreshSearchRectPerCell makes is the minimum of one per-cell
// grade over a rectangle (an early zero return is that minimum too), as in
// buildSearchPlane. So over the window the refreshed cells' footprints and rings
// cover, rate each cell once (feature, terrain, then the body searchBodyRect
// keeps there, graded as rawSearchGrade grades it), take the two box minima,
// and write the output cells in the same order. Each window cell is graded
// once instead of once per footprint and strip that covers it.
void World::refreshSearchRect(SearchGradePlane& plane,int x,int z,int w,int h) {
    const int width=plane.nav->width(),height=plane.nav->height();
    const int footX=plane.footX,footZ=plane.footZ;
    const int x0=std::max(0,x-footX),z0=std::max(0,z-footZ);
    const int x1=std::min(width,x+w+1),z1=std::min(height,z+h+1);
    if (x1<=x0 || z1<=z0) return;
    TAK_PROBE(++probe::tl.refreshRects; probe::tl.refreshGradeEvals+=uint64_t(x1-x0)*uint64_t(z1-z0));
    if (footX<1 || footZ<1 || width!=hW_ || height!=hH_) { refreshSearchRectPerCell(plane,x,z,w,h); return; }
#ifndef NDEBUG
    static const bool verify=std::getenv("TAK_VERIFY_SEARCH_PLANE")!=nullptr;
    std::vector<uint8_t> before,expected;
    uint64_t checksumBefore=0,checksumExpected=0;
    if (verify) {
        // Run the reference, keep its cells, then put the window back.
        for (int cz=z0;cz<z1;++cz) for (int cx=x0;cx<x1;++cx) before.push_back(plane.cells[size_t(cz)*width+cx]);
        checksumBefore=plane.checksum;
        refreshSearchRectPerCell(plane,x,z,w,h);
        checksumExpected=plane.checksum;
        size_t i=0;
        for (int cz=z0;cz<z1;++cz) for (int cx=x0;cx<x1;++cx,++i) {
            uint8_t& c=plane.cells[size_t(cz)*width+cx];
            expected.push_back(c);c=before[i];
        }
        plane.checksum=checksumBefore;
    }
#endif
    const bool hasMapCells=mapPlacementCells_.size()==size_t(hW_)*hH_ && !mapPlacementCells_.empty();
    // The cells the footprints [x0,x1+footX-1) and rings [x0-1,x1+footX) read,
    // clipped to the map (a box past the map edge is rated 0 or 4 by the bounds
    // rules, never read).
    const int gx0=std::max(0,x0-1),gz0=std::max(0,z0-1);
    const int gx1=std::min(width,x1+footX),gz1=std::min(height,z1+footZ);
    const int gw=gx1-gx0,gh=gz1-gz0;
    // The reference's body rectangle, which holds the window.
    const auto bodies=searchBodyRect(x0-1,z0-1,x1-x0+footX+2,z1-z0+footZ+2);
    thread_local std::vector<uint8_t> cell,rows,foot,ring;
    cell.resize(size_t(gw)*gh);
    TAK_PROBE(probe::tl.refreshCellsRated+=uint64_t(gw)*uint64_t(gh));
    for (int cz=gz0;cz<gz1;++cz) for (int cx=gx0;cx<gx1;++cx) {
        int grade=searchCellGrade(*plane.nav,cx,cz,hasMapCells);
        const Unit* body=bodies.cells[size_t(cz-bodies.z)*bodies.width+cx-bodies.x];
        if (grade && body) {
            int value=-1;
            if (body->type->gate && !body->type->yardMap.empty()) {
                const int bx=cx-footprintOrigin(body->x,body->type->footX);
                const int bz=cz-footprintOrigin(body->z,body->type->footZ);
                const char yard=body->type->yardMap.at(size_t(bz)*body->type->footX+bx);
                // 506416 marks only c/C gate passages; 508b54 grades them
                // specially before the ordinary stationary-body rejection.
                if (yard=='c' || yard=='C') value=3;
            }
            if (value<0) value=retailCachedBodyGrade(!body->type->isStructure(),
                body->id==plane.preparation.requestSlot,body->groundGradeTick,
                plane.preparation.recent,plane.preparation.stale);
            grade=std::min(grade,value);
        }
        cell[size_t(cz-gz0)*gw+cx-gx0]=uint8_t(grade);
    }
    searchBoxMin(cell,gw,gh,footX,footZ,rows,foot);
    searchBoxMin(cell,gw,gh,footX+2,footZ+2,rows,ring);
    for (int cz=z0;cz<z1;++cz) for (int cx=x0;cx<x1;++cx) {
        const int grade=boxMinFootprintGrade(cx,cz,footX,footZ,width,height,
            [&] { return foot[size_t(cz-gz0)*gw+cx-gx0]; },
            [&] { return ring[size_t(cz-1-gz0)*gw+cx-1-gx0]; });
        const size_t index=size_t(cz)*width+cx;
        setSearchCell(plane,index,uint8_t((plane.cells[index]&8)|grade));
    }
#ifndef NDEBUG
    if (verify) {
        size_t i=0;
        for (int cz=z0;cz<z1;++cz) for (int cx=x0;cx<x1;++cx,++i)
            if (plane.cells[size_t(cz)*width+cx]!=expected[i])
                throw std::runtime_error("box-minimum search plane refresh differs from the per-cell reference");
        if (plane.checksum!=checksumExpected)
            throw std::runtime_error("box-minimum search plane refresh checksum differs from the per-cell reference");
    }
#endif
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

// The first build of a plane, and any rebuild after a whole-grid nav edit.
// It must equal refreshSearchRect over the whole map, which rates every
// cell's footprint and clearance ring cell by cell: on a large map that cost
// one tick 40-66 ms. Every rating it makes is the minimum of one per-cell
// grade over a rectangle (an early zero return is that minimum too), so the
// same plane comes from one pass of per-cell grades, one stamp of the bodies
// and two box minima: on a 768x768 map 37-49 ms becomes about 3.5 ms.
// Derived state only: the cells and checksum equal the per-cell build's
// (pathblock_test checks it, and TAK_VERIFY_SEARCH_PLANE does on every
// build in a Debug binary).
void World::buildSearchPlane(SearchGradePlane& plane) {
    const NavGrid& nav=*plane.nav;
    const int width=nav.width(),height=nav.height();
    const int footX=plane.footX,footZ=plane.footZ;
    plane.preparation={}; plane.checksum=0;
    plane.cells.assign(size_t(std::max(width,0))*std::max(height,0),0);
    plane.navVersion=nav.version();
    if (width<=0 || height<=0 || footX<1 || footZ<1 || width!=hW_ || height!=hH_) {
        refreshSearchRectPerCell(plane,0,0,width,height);
        return;
    }
    const bool hasMapCells=mapPlacementCells_.size()==size_t(hW_)*hH_ && !mapPlacementCells_.empty();
    // rawSearchGrade for one cell, without its rectangle bounds test: the
    // feature and terrain grades, then the body searchBodyRect keeps there.
    std::vector<uint8_t> cell(size_t(width)*height);
    for (int cz=0;cz<height;++cz) for (int cx=0;cx<width;++cx)
        cell[size_t(cz)*width+cx]=uint8_t(searchCellGrade(nav,cx,cz,hasMapCells));
    // searchBodyRect keeps the ground word's holder on a cell, else the last
    // eligible unit in vector order: stamping in that order, overwriting all
    // but a holder's value, leaves the same body's grade.
    std::vector<uint8_t> body(cell.size(),0xff),held(groundOwner_.empty() ? 0 : cell.size(),0);
    bool anyBody=false;
    TAK_PASS();
    for (const auto& u:units_) {
        if (!u.alive() || u.embarked() || !u.type || (u.type->canFly && u.flightGroundMode!=1)) continue;
        const auto& type=*u.type;
        const int ux=footprintOrigin(u.x,type.footX),uz=footprintOrigin(u.z,type.footZ);
        const int x0=std::max(0,ux),z0=std::max(0,uz);
        const int x1=std::min(width,ux+type.footX),z1=std::min(height,uz+type.footZ);
        if (x0>=x1 || z0>=z1) continue;
        const uint8_t grade=uint8_t(retailCachedBodyGrade(!type.isStructure(),u.id==plane.preparation.requestSlot,
            u.groundGradeTick,plane.preparation.recent,plane.preparation.stale));
        const bool yards=type.isStructure() && !type.yardMap.empty();
        const bool gate=type.gate && !type.yardMap.empty();
        bool yardOpen=false;
        if (yards) {
            const auto script=unitScripts_.find(u.id);
            yardOpen=script!=unitScripts_.end() && script->second.yardOpen;
        }
        for (int cz=z0;cz<z1;++cz) for (int cx=x0;cx<x1;++cx) {
            uint8_t value=grade;
            if (yards || gate) {
                const char yard=type.yardMap.at(size_t(cz-uz)*type.footX+cx-ux);
                if (yards && (yard=='.' || ((yard=='c' || yard=='C') && yardOpen))) continue;
                // 506416 marks only c/C gate passages; 508b54 grades them
                // specially before the ordinary stationary-body rejection.
                if (gate && (yard=='c' || yard=='C')) value=3;
            }
            const size_t index=size_t(cz)*width+cx;
            if (!held.empty()) {
                if (held[index]) continue;
                held[index]=groundOwner_[index]==u.id;
            }
            body[index]=value;
            anyBody=true;
        }
    }
    if (anyBody) lowerBytes(cell.data(),body.data(),int(cell.size()));
    std::vector<uint8_t> rows,foot,ring;
    searchBoxMin(cell,width,height,footX,footZ,rows,foot);
    searchBoxMin(cell,width,height,footX+2,footZ+2,rows,ring);
    for (int cz=0;cz<height;++cz) for (int cx=0;cx<width;++cx) {
        const int grade=boxMinFootprintGrade(cx,cz,footX,footZ,width,height,
            [&] { return foot[size_t(cz)*width+cx]; },
            [&] { return ring[size_t(cz-1)*width+cx-1]; });
        if (!grade) continue;
        const size_t index=size_t(cz)*width+cx;
        plane.cells[index]=uint8_t(grade);
        plane.checksum^=searchCellWord(index,uint8_t(grade));
    }
}

bool World::searchGradeBuildMatchesReference() {
    for (const auto& existing:searchGrades_) {
        SearchGradePlane fast,reference;
        fast.nav=reference.nav=existing.nav;
        fast.footX=reference.footX=existing.footX;
        fast.footZ=reference.footZ=existing.footZ;
        buildSearchPlane(fast);
        reference.cells.assign(size_t(reference.nav->width())*reference.nav->height(),0);
        refreshSearchRectPerCell(reference,0,0,reference.nav->width(),reference.nav->height());
        if (fast.cells!=reference.cells || fast.checksum!=reference.checksum) return false;
    }
    return true;
}

bool World::searchGradeRefreshMatchesReference(int x,int z,int w,int h) {
    for (const auto& existing:searchGrades_) {
        SearchGradePlane fast=existing,reference=existing;
        refreshSearchRect(fast,x,z,w,h);
        refreshSearchRectPerCell(reference,x,z,w,h);
        if (fast.cells!=reference.cells || fast.checksum!=reference.checksum) return false;
    }
    return true;
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
        buildSearchPlane(plane);
#ifndef NDEBUG
        static const bool verify=std::getenv("TAK_VERIFY_SEARCH_PLANE")!=nullptr;
        if (verify && !searchGradeBuildMatchesReference())
            throw std::runtime_error("separable search plane build differs from the per-cell reference");
#endif
    }
    activeSearchGrade_=index;
    if (!searchGradeBatch_ || !searchGradeBodiesValid_) {
        if (paths_.profiling()) ++searchGradeBodyRebuilds_;
        searchGradeBodies_.clear();
        TAK_PASS();
        for (const auto& u:units_) {
            if (!u.type || !u.alive() || u.embarked() || (u.type->canFly && u.flightGroundMode!=1)) continue;
            searchGradeBodies_.push_back({u.id,0x1000001u,u.groundGradeTick,!u.type->isStructure(),
                footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ),
                u.type->footX,u.type->footZ});
        }
        std::sort(searchGradeBodies_.begin(),searchGradeBodies_.end(),
            [](const auto& a,const auto& b) { return a.slot<b.slot; });
        searchGradeBodiesValid_=searchGradeBatch_;
    }
    RetailGradeBody request;
    const auto body=std::lower_bound(searchGradeBodies_.begin(),searchGradeBodies_.end(),id,
        [](const auto& b,int slot) { return b.slot<slot; });
    if (body!=searchGradeBodies_.end() && body->slot==id) request=*body;
    plane.preparation.prepare(tickCounter_,request,searchGradeBodies_,lastRetry,
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

void World::rebuildOccupancy() { rebuildSpatial(false,true); }
void World::rebuildGrid() { rebuildSpatial(true,false); }
uint32_t World::rebuildGridAndOccupancy() { return rebuildSpatial(true,true); }

// The spatial hash (rebuildGrid) and the body occupancy grid (rebuildOccupancy)
// in one walk over the units when both are wanted (WE D4). Neither reads the
// other's grid, so every bucket chain, stamp and touched list comes out exactly
// as from the two separate walks.
uint32_t World::rebuildSpatial(bool grid, bool occupancy) {
    if (units_.size() < kSpatialMinParallel || !simPool().workers()) return rebuildSpatialSerial(grid, occupancy);
    const uint32_t live = rebuildSpatialParallel(grid, occupancy);
#ifndef NDEBUG
    if (verifyPool()) {
        const auto head = gHead_, next = gNext_; const auto touched = gTouched_;
        const auto players = gPlayers_, blocks = gBlockPlayers_;
        const auto occ = occ_; const auto occTouched = occTouched_;
        const bool valid = gPlayersValid_; const int w = gW_, h = gH_;
        const float ox = gOx_, oz = gOz_;
        const uint32_t serialLive = rebuildSpatialSerial(grid, occupancy);
        if (serialLive != live || occ != occ_ || occTouched != occTouched_ || w != gW_ || h != gH_ ||
            (w > 0 && (head != gHead_ || touched != gTouched_ || players != gPlayers_ || blocks != gBlockPlayers_ ||
                       valid != gPlayersValid_ || ox != gOx_ || oz != gOz_)))
            poolMismatch("rebuildSpatial", tickCounter_);
        if (grid && w > 0)
            for (size_t i = 0; i < next.size(); ++i)
                if (next[i] != gNext_[i] && spatialRec_[i].kind & 1) poolMismatch("rebuildSpatial gNext_", tickCounter_);
    }
#endif
    return live;
}

uint32_t World::rebuildSpatialSerial(bool grid, bool occupancy) {
    // ---- spatial hash: bounds and buckets
    // Include ALL alive units (buildings too) so combat can target structures.
    auto inGrid = [](const Unit& u) {
        return u.alive() && !u.embarked() && u.type;
    };
    if (grid) {
        gCell_ = 32.0f;
        float minx = 1e30f, minz = 1e30f, maxx = -1e30f, maxz = -1e30f;
        bool any = false;
        TAK_PASS();
        for (const auto& u : units_) {
            if (!inGrid(u)) continue;
            any = true;
            minx = std::min(minx, float(u.x.toFloat())); maxx = std::max(maxx, float(u.x.toFloat()));
            minz = std::min(minz, float(u.z.toFloat())); maxz = std::max(maxz, float(u.z.toFloat()));
        }
        if (!any) { gW_ = gH_ = 0; grid = false; }
        else {
            gOx_ = minx - gCell_;
            gOz_ = minz - gCell_;
            gW_ = int((maxx - minx) / gCell_) + 3;
            gH_ = int((maxz - minz) / gCell_) + 3;
            const size_t cells=size_t(gW_)*gH_;
            if (gHead_.size()!=cells) {
                gHead_.assign(cells,-1);gPlayers_.assign(cells,0);
            } else for (size_t cell:gTouched_) {gHead_[cell]=-1;gPlayers_[cell]=0;}
            gTouched_.clear();
            gPlayersValid_=true;
            // 8x8-cell block masks for forEachNear's block skip (WE A2b): the OR of the cell masks
            // in each block, so a zero block bit means no cell in the block has that player.
            gBlockW_ = (gW_ + 7) / 8;
            gBlockPlayers_.assign(size_t(gBlockW_) * size_t((gH_ + 7) / 8), 0);
            gNext_.assign(units_.size(), -1);
        }
    }
    // ---- occupancy: clear
    if (occupancy) {
        occW_ = terW_;
        occH_ = terH_;
        if (occW_ <= 0 || occH_ <= 0) {
            occ_.clear(); occTouched_.clear(); occW_ = occH_ = 0; occupancy = false;
        } else {
            const size_t cells=size_t(occW_)*occH_;
            if (occ_.size()!=cells) occ_.assign(cells,0);
            else for (size_t cell:occTouched_) occ_[cell]=0;
            occTouched_.clear();
        }
    }
    // Occupancy is rebuilt in deterministic order, moving bodies first and parked
    // ones last. Subsequent steps update ownership immediately; two units cannot
    // reserve the same destination using a stale snapshot of this grid.
    // Moving bodies are stamped as they are met and the parked ones' indices are
    // kept, then stamped in the same unit order (WE D4: one walk, was two).
    // Whether a body holds its cell (speed == 0) cannot change while the grid is
    // stamped, so the stamps, and occTouched_, come out exactly as from the
    // two-pass walk.
    auto stamp = [&](const Unit& u) {
        // Stamp the whole footprint, centre-anchored to match NavGrid::fits.
        const int fx=u.type->footX,fz=u.type->footZ;
        int cx = footprintOrigin(u.x, fx), cz = footprintOrigin(u.z, fz);
        for (int j = 0; j < fz; ++j)
            for (int i = 0; i < fx; ++i) {
                int x = cx + i, z = cz + j;
                if (x < 0 || z < 0 || x >= occW_ || z >= occH_) continue;
                const size_t cell = size_t(z) * size_t(occW_) + size_t(x);
                setOccupant(cell,u.id);
            }
    };
    occParked_.clear();
    uint32_t live = 0;
    TAK_PASS();
    for (size_t i = 0; i < units_.size(); ++i) {
        const Unit& u = units_[i];
        if (!u.alive() || !u.type) continue;
        ++live;
        if (u.embarked()) continue;
        if (grid) {
            int cx = std::clamp(int((u.x.toFloat() - gOx_) / gCell_), 0, gW_ - 1);
            int cz = std::clamp(int((u.z.toFloat() - gOz_) / gCell_), 0, gH_ - 1);
            int c = cz * gW_ + cx;
            if (gHead_[size_t(c)]<0) gTouched_.push_back(size_t(c));
            gNext_[i] = gHead_[size_t(c)];
            gHead_[size_t(c)] = int(i);
            if (unsigned(u.player)<64) {
                gPlayers_[size_t(c)]|=uint64_t(1)<<u.player;
                gBlockPlayers_[size_t(cz>>3)*size_t(gBlockW_)+size_t(cx>>3)]|=uint64_t(1)<<u.player;
            }
            else gPlayersValid_=false; // unrepresentable owners require the full traversal
        }
        if (!occupancy) continue;
        if (u.type->canFly || u.type->isStructure()) continue;   // structures are in nav_
        if (u.underConstruction) continue;
        if (unitHoldsCell(u)) occParked_.push_back(uint32_t(i));
        else stamp(u);
    }
    for (uint32_t i : occParked_) stamp(units_[i]);
    return live;
}

// The same rebuild on the SimPool (WE E4.3, P6). Below kSpatialMinParallel
// units, or with no workers, the serial walk above runs: the pass is then a few
// tens of microseconds and a fork-join would cost more.
//
//  1. The unit chunks read the units (several cache lines each, the cost of
//     the serial walk) into compact records, spatialRec_[i], with per-chunk
//     bounds and live counts merged in chunk order.
//  2. The occupancy grid is cleared in chunks of occTouched_ (relaxed atomic
//     stores of 0: a cell may be listed more than once).
//  3. Serial walks over the records then stamp the bodies (moving ones in
//     unit order, then parked ones) and link the spatial hash in unit order,
//     with the serial walk's float cell arithmetic.
// So every chain, stamp and list is the serial one at any worker count
// (TAK_VERIFY_POOL checks it). Stamping and linking in parallel were tried and
// measured slower: banded stamping was unbalanced (armies fill a few bands),
// and chunk-local chains merged in order cost more than the walk they split.
uint32_t World::rebuildSpatialParallel(bool grid, bool occupancy) {
    SimPool& pool = simPool();
    const size_t n = units_.size();
    spatialRec_.resize(n);
    const size_t chunks = (n + SimPool::kUnitChunk - 1) / SimPool::kUnitChunk;
    spatialChunks_.assign(chunks, SpatialChunk{1e30f, 1e30f, -1e30f, -1e30f, 0, false});
    if (occupancy && (terW_ <= 0 || terH_ <= 0)) {
        occW_ = occH_ = 0; occ_.clear(); occTouched_.clear(); occupancy = false;
    }
    if (occupancy) { occW_ = terW_; occH_ = terH_; }
    pool.forChunks(n, SimPool::kUnitChunk, 0, [&](size_t k, size_t begin, size_t end, unsigned) {
        SpatialChunk c = spatialChunks_[k];
        for (size_t i = begin; i < end; ++i) {
            const Unit& u = units_[i];
            SpatialRec& r = spatialRec_[i];
            r.kind = 0;
            if (!u.alive() || !u.type) continue;
            ++c.live;
            if (u.embarked()) continue;
            r.id = u.id;
            if (grid) {
                const float x = float(u.x.toFloat()), z = float(u.z.toFloat());
                c.any = true;
                c.minx = std::min(c.minx, x); c.maxx = std::max(c.maxx, x);
                c.minz = std::min(c.minz, z); c.maxz = std::max(c.maxz, z);
                r.x = x; r.z = z;
                r.player = unsigned(u.player) < 64 ? uint8_t(u.player) : uint8_t(0xff);
                r.kind = 1;
            }
            if (!occupancy) continue;
            if (u.type->canFly || u.type->isStructure()) continue;   // structures are in nav_
            if (u.underConstruction) continue;
            const int fx = u.type->footX, fz = u.type->footZ;
            const int ox = footprintOrigin(u.x, fx), oz = footprintOrigin(u.z, fz);
            r.fx = uint8_t(fx); r.fz = uint8_t(fz);
            r.ox = int16_t(ox); r.oz = int16_t(oz);
            r.kind |= unitHoldsCell(u) ? 4 : 2;
        }
        spatialChunks_[k] = c;
    });
    uint32_t live = 0;
    bool any = false;
    float minx = 1e30f, minz = 1e30f, maxx = -1e30f, maxz = -1e30f;
    for (const SpatialChunk& c : spatialChunks_) {
        live += c.live;
        if (!c.any) continue;
        any = true;
        minx = std::min(minx, c.minx); maxx = std::max(maxx, c.maxx);
        minz = std::min(minz, c.minz); maxz = std::max(maxz, c.maxz);
    }
    if (occupancy) {
        const size_t cells = size_t(occW_) * occH_;
        if (occ_.size() != cells) occ_.assign(cells, 0);
        else pool.forChunks(occTouched_.size(), 4096, 0, [&](size_t, size_t begin, size_t end, unsigned) {
            for (size_t i = begin; i < end; ++i)
                __atomic_store_n(&occ_[occTouched_[i]], int32_t(0), __ATOMIC_RELAXED);   // not std::atomic_ref: Apple libc++ lacks it
        });
        occTouched_.clear();
        auto stamp = [&](const SpatialRec& r) {
            for (int j = 0; j < r.fz; ++j)
                for (int i = 0; i < r.fx; ++i) {
                    int x = r.ox + i, z = r.oz + j;
                    if (x < 0 || z < 0 || x >= occW_ || z >= occH_) continue;
                    setOccupant(size_t(z) * size_t(occW_) + size_t(x), r.id);
                }
        };
        occParked_.clear();
        TAK_PASS();
        for (size_t i = 0; i < n; ++i) {
            const SpatialRec& r = spatialRec_[i];
            if (r.kind & 4) occParked_.push_back(uint32_t(i));
            else if (r.kind & 2) stamp(r);
        }
        for (uint32_t i : occParked_) stamp(spatialRec_[i]);
    }
    // ---- spatial hash: bounds and buckets (as rebuildSpatial)
    if (grid) {
        gCell_ = 32.0f;
        if (!any) { gW_ = gH_ = 0; grid = false; }
        else {
            gOx_ = minx - gCell_;
            gOz_ = minz - gCell_;
            gW_ = int((maxx - minx) / gCell_) + 3;
            gH_ = int((maxz - minz) / gCell_) + 3;
            const size_t cells=size_t(gW_)*gH_;
            if (gHead_.size()!=cells) {
                gHead_.assign(cells,-1);gPlayers_.assign(cells,0);
            } else for (size_t cell:gTouched_) {gHead_[cell]=-1;gPlayers_[cell]=0;}
            gTouched_.clear();
            gPlayersValid_=true;
            gBlockW_ = (gW_ + 7) / 8;
            gBlockPlayers_.assign(size_t(gBlockW_) * size_t((gH_ + 7) / 8), 0);
            gNext_.assign(units_.size(), -1);
        }
    }
    if (!grid) return live;
    TAK_PASS();
    for (size_t i = 0; i < n; ++i) {
        const SpatialRec& r = spatialRec_[i];
        if (!(r.kind & 1)) continue;
        int cx = std::clamp(int((r.x - gOx_) / gCell_), 0, gW_ - 1);
        int cz = std::clamp(int((r.z - gOz_) / gCell_), 0, gH_ - 1);
        int c = cz * gW_ + cx;
        if (gHead_[size_t(c)]<0) gTouched_.push_back(size_t(c));
        gNext_[i] = gHead_[size_t(c)];
        gHead_[size_t(c)] = int(i);
        if (r.player != 0xff) {
            gPlayers_[size_t(c)]|=uint64_t(1)<<r.player;
            gBlockPlayers_[size_t(cz>>3)*size_t(gBlockW_)+size_t(cx>>3)]|=uint64_t(1)<<r.player;
        }
        else gPlayersValid_=false;
    }
    return live;
}

#ifndef NDEBUG
void World::probeNear(float x, float z, float radius, uint64_t players, bool disk) const {
    auto& c = probe::tl;
    if (!disk) ++c.nearScans;
    const int r = int(radius / gCell_) + 1;
    const int cx = int((x - gOx_) / gCell_), cz = int((z - gOz_) / gCell_);
    const float reach = (radius + 2.0f) * (radius + 2.0f);
    const bool blocks = gPlayersValid_ && gBlockW_ > 0 && !gBlockPlayers_.empty();
    for (int dz = -r; dz <= r; ++dz) {
        const int gz = cz + dz;
        if (gz < 0 || gz >= gH_) continue;
        int lastBlock = -1;
        for (int dx = -r; dx <= r; ++dx) {
            const int gx = cx + dx;
            if (gx < 0 || gx >= gW_) continue;
            const int b = (gz >> 3) * gBlockW_ + (gx >> 3);
            if (disk) {
                const float x0 = gOx_ + float(gx) * gCell_, z0 = gOz_ + float(gz) * gCell_;
                const float ex = std::max({x0 - x, 0.0f, x - (x0 + gCell_)});
                const float ez = std::max({z0 - z, 0.0f, z - (z0 + gCell_)});
                if (ex * ex + ez * ez > reach) {
                    ++c.nearCellsOutsideDisk;
                    if (!blocks || (gBlockPlayers_[size_t(b)] & players)) ++c.nearCellsTestedOutsideDisk;
                }
                continue;
            }
            ++c.nearCells;
            const size_t cell = size_t(gz) * gW_ + gx;
            if (gPlayersValid_ && !(gPlayers_[cell] & players)) ++c.nearCellsMasked;
            if (!blocks) continue;
            if (!(gBlockPlayers_[size_t(b)] & players)) {
                ++c.nearCellsBlockSkippable;
                if (b != lastBlock) ++c.nearBlocksSkipped;   // one jump per block run in a row
                lastBlock = b;
            }
        }
    }
}

void World::verifyNear(float x, float z, float radius, uint64_t players) const {
    // Both walks exactly as forEachNear runs them, recording unit indices instead of calling fn.
    std::vector<int> plain, skipped;
    scanNear<false, false>(x, z, radius, players, [&](int i) { plain.push_back(i); });
    scanNear<true, false>(x, z, radius, players, [&](int i) { skipped.push_back(i); });
    if (plain != skipped)
        throw std::runtime_error("forEachNear: the block-skipping scan visits a different unit sequence "
                                 "than the plain scan (TAK_VERIFY_NEAR, tick " + std::to_string(tickCounter_) + ")");
}

#endif

std::string World::lodestoneUpgradeBase(const UnitType& type) {
    if (!type.onMana || !type.isStructure()) return {};
    // Explicit retail pairs: do not treat arbitrary modded mana buildings as upgrades.
    static constexpr const char* factions[]={"ara","tar","ver","zon","cre"};
    for (const char* faction:factions)
        if (type.id==std::string(faction)+"mana") return std::string(faction)+"lode";
    return {};
}

const Unit* World::lodestoneUpgradeSource(const UnitType* type,float x,float z,int player) const {
    if (!type || player<0 || manaSpots_.empty()) return nullptr;
    const std::string base=lodestoneUpgradeBase(*type);
    if (base.empty()) return nullptr;
    const std::pair<float,float>* spot=nullptr;
    float best=24.0f*24.0f;
    for (const auto& candidate:manaSpots_) {
        const float dx=candidate.first-x,dz=candidate.second-z;
        if (dx*dx+dz*dz<best) {best=dx*dx+dz*dz;spot=&candidate;}
    }
    if (!spot) return nullptr;
    for (const auto& u:units_) {
        if (!u.alive() || u.hp<=Fixed() || u.underConstruction || u.player!=player ||
            !u.type || u.type->id!=base || u.type->side!=type->side) continue;
        const float dx=u.x.toFloat()-spot->first,dz=u.z.toFloat()-spot->second;
        if (dx*dx+dz*dz<24.0f*24.0f) return &u;
    }
    return nullptr;
}

bool World::canPlace(const UnitType* type, float x, float z, int player) const {
    return placementCheck(type,x,z,player,nullptr);
}

bool World::canPlaceOrder(const UnitType* type, float x, float z, int player) const {
    // The plain test first: it reads the body index, the yielding one walks
    // every unit and only runs for a site something stands on.
    return placementCheck(type,x,z,player,nullptr) ||
           (player>=0 && placementCheck(type,x,z,player,nullptr,nullptr,0,false,player));
}

std::vector<uint8_t> World::placementCells(const UnitType* type,const std::atomic_bool* cancel) {
    const auto cancelled=[&] {return cancel && cancel->load(std::memory_order_relaxed);};
    if(cancelled() || terW_<=0 || terH_<=0)return {};
    std::vector<uint8_t> result(size_t(terW_)*terH_);
    if(!type)return result;
    // This private analysis world is stationary throughout the batch. Building
    // footprints use the existing body index; mobile placement uses centers,
    // including airborne/embarked units exactly as the scalar predicate does.
    struct Restore {bool& value;bool old;~Restore(){value=old;}} restore{bodyIndexEnabled_,bodyIndexEnabled_};
    bodyIndexEnabled_=true;bodyIndexValid_=false;
    constexpr int bucketSize=128;
    const int bw=(terW_*16+bucketSize-1)/bucketSize,bh=(terH_*16+bucketSize-1)/bucketSize;
    std::vector<std::vector<int>> buckets;
    if(!type->isStructure()) {
        buckets.resize(size_t(bw)*bh);
        TAK_PASS();
        for(size_t i=0;i<units_.size();++i) {
            if(cancelled())return {};
            const auto& u=units_[i];if(!u.alive())continue;
            const int bx=std::clamp(u.x.floorInt()/bucketSize,0,bw-1);
            const int bz=std::clamp(u.z.floorInt()/bucketSize,0,bh-1);
            buckets[size_t(bz)*bw+bx].push_back(int(i));
        }
    }
    const float radius=16.0f*float(std::max(type->footX,type->footZ))/2+12;
    std::vector<int> candidates;
    for(int z=0;z<terH_;++z) {
        if(cancelled())return {};
        for(int x=0;x<terW_;++x) {
            const float px=x*16.f+8,pz=z*16.f+8;
            candidates.clear();
            if(!buckets.empty()) {
                const int x0=std::clamp(int((px-radius)/bucketSize),0,bw-1),x1=std::clamp(int((px+radius)/bucketSize),0,bw-1);
                const int z0=std::clamp(int((pz-radius)/bucketSize),0,bh-1),z1=std::clamp(int((pz+radius)/bucketSize),0,bh-1);
                for(int bz=z0;bz<=z1;++bz)for(int bx=x0;bx<=x1;++bx) {
                    const auto& bucket=buckets[size_t(bz)*bw+bx];
                    candidates.insert(candidates.end(),bucket.begin(),bucket.end());
                }
            }
            result[size_t(z)*terW_+x]=placementCheck(type,px,pz,-1,nullptr,type->isStructure()?nullptr:&candidates);
        }
    }
    return result;
}

bool World::placementCheck(const UnitType* type, float x, float z, int player,
                           std::vector<int>* clearFeatures,const std::vector<int>* candidates,int ignoreId,
                           bool ignoreMobile,int yieldPlayer) const {
    if (!type) return false;
    const Unit* replacing=lodestoneUpgradeSource(type,x,z,player);
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
        TAK_PASS();
        for (const auto& u : units_) {
            if (&u==replacing || !u.alive() || !u.type || !u.type->onMana) continue;
            float dx = u.x.toFloat() - sx, dz = u.z.toFloat() - sz;
            if (dx * dx + dz * dz < 44.0f * 44.0f) return false;   // deposit taken
        }
    }
    // Check the domain-appropriate grid so water units (Kraken) require water
    // and land units require land, rather than always testing the ground grid.
    const NavGrid& grid = navFor(type);
    // MobileBuild's retry query (ignoreMobile, from startBuild) also passes a
    // feature that a reclaim has already finished and that is only fading out
    // (494ce0, 34 ticks): ours, so a build queued behind its own clearing
    // reclaims (queueBuild) waits out the fade instead of being dropped.
    const bool passFading=ignoreMobile && !clearFeatures && fadingFeatures_;
    const auto clearObstacle=[&](int gx,int gz) {
        if (!clearFeatures && !passFading) return false;
        bool found=false;
        for (const auto& f:features_) {
            if (!f.alive || !f.blocks) continue;
            const int ox=footprintOrigin(f.x,f.fx),oz=footprintOrigin(f.z,f.fz);
            if (gx<ox || gz<oz || gx>=ox+f.fx || gz>=oz+f.fz) continue;
            if (passFading) { if (!f.fadeLeft) return false; found=true; continue; }
            if (!featureReclaimable(f)) return false;
            found=true;
            if (std::find(clearFeatures->begin(),clearFeatures->end(),f.id)==clearFeatures->end())
                clearFeatures->push_back(f.id);
        }
        return found;
    };
    const auto walkable=[&](const NavGrid& target,int gx,int gz) {
        if (target.walkable(gx,gz)) return true;
        if (!target.terrainWalkable(gx,gz)) return false;
        if (replacing) {
            const int ox=footprintOrigin(replacing->x,replacing->type->footX);
            const int oz=footprintOrigin(replacing->z,replacing->type->footZ);
            if (gx>=ox && gz>=oz && gx<ox+replacing->type->footX && gz<oz+replacing->type->footZ) {
                const auto& yard=replacing->type->yardMap;
                const char cell=yard.empty() ? 'o' : yard[size_t(gz-oz)*replacing->type->footX+gx-ox];
                if (cell=='o' || cell=='O') return true;
            }
        }
        return clearObstacle(gx,gz);
    };
    int cx = footprintOrigin(x, type->footX), cz = footprintOrigin(z, type->footZ);
    // Retail 507400 checks the entire building rectangle before its yard masks.
    // Even ignored '.' rows need the one-cell map border: the yard transition
    // at 507ae0 uses these same bounds and otherwise can never open the factory.
    if (type->isStructure() && (cx < 1 || cz < 1 ||
        cx + type->footX >= terW_ || cz + type->footZ >= terH_)) return false;
    // Retain the legacy mixed-yard check for custom shoreline structures.
    bool waterYard = false;
    if (!type->yardMap.empty())
        for (char c : type->yardMap) if (c == 'w' || c == 'W') { waterYard = true; break; }
    // Native yard decoding gives w/C/Y the water bit (0x10), including the
    // Sea Fort's closed C cells. For a wholly water-based yard, 507400 sets
    // the foundation at sea-waterline and rejects any cell whose four-corner
    // maximum rises above it. A ship's minimum sailing depth is irrelevant.
    // Keep mixed land/water custom yards on their existing placement path.
    const bool waterOnlyYard = !type->yardMap.empty() &&
        type->yardMap.find_first_of("wCY") != std::string::npos &&
        type->yardMap.find_first_not_of("wCY. ") == std::string::npos;
    if (waterOnlyYard) {
        const int ceiling = seaLevel_ - uint8_t(type->waterline);
        for (int j = 0; j < type->footZ; ++j)
            for (int i = 0; i < type->footX; ++i) {
                const char cell = type->yardMap[size_t(j) * type->footX + i];
                if (cell == '.' || cell == ' ') continue;
                const int gx = cx + i, gz = cz + j;
                if (gx < 0 || gz < 0 || gx + 1 >= terW_ || gz + 1 >= terH_)
                    return false;
                const size_t at = size_t(gz) * terW_ + gx;
                if (!obst_.empty() && obst_[at] && !clearObstacle(gx,gz)) return false;
                const int high = std::max({heights_[at], heights_[at + 1],
                                          heights_[at + terW_], heights_[at + terW_ + 1]});
                if (high > ceiling) return false;
            }
    } else if (waterYard) {
        for (int j = 0; j < type->footZ; ++j)
            for (int i = 0; i < type->footX; ++i) {
                char c = type->yardMap[size_t(j) * type->footX + i];
                if (c == 'w' || c == 'W') {
                    if (!walkable(navWater_,cx + i, cz + j)) return false;   // slipway needs water
                } else if (c == '.' || c == ' ') {
                    continue;                                                // not part of the footprint
                } else if (!walkable(nav_,cx + i, cz + j)) {
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
                if (!walkable(grid,cx + i, cz + j)) return false;
            }
    } else {
        for (int j = 0; j < type->footZ; ++j)
            for (int i = 0; i < type->footX; ++i)
                if (!walkable(grid,cx + i, cz + j)) return false;
    }
    if (type->isStructure()) {
        // Retail's building branch (507400) checks occupied footprint cells.
        // A radius based on the longest side rejects builders standing beside
        // a rectangular building after they reach its construction perimeter.
        const auto tested=[&](size_t i) {
            if (!type->yardMap.empty() && type->yardMap[i]=='.') return false;
            // Native Y has no occupancy bits; w and C both test bodies.
            return !(waterOnlyYard && type->yardMap[i]=='Y');
        };
        if (ignoreMobile || yieldPlayer>=0) {
            // MobileBuild's second test (405824..405911) ignores every mobile
            // unit: only another building's occupied cells refuse the site.
            // The body plane keeps one occupant per cell, which a unit
            // standing in a yard would hide, so stamp buildings alone. A
            // build order's test (canPlaceOrder) stamps the mobile bodies too,
            // as searchBodyRect does, but not the player's own that step aside.
            for (const auto& u:units_) {
                if ((replacing && u.id==replacing->id) || !u.alive() || u.embarked() || !u.type) continue;
                const bool mobile=!u.type->isStructure();
                if (mobile && (ignoreMobile || (u.type->canFly && u.flightGroundMode!=1) ||
                               yieldsToSite(u,yieldPlayer))) continue;
                const int ux=footprintOrigin(u.x,u.type->footX),uz=footprintOrigin(u.z,u.type->footZ);
                for (int j=std::max(cz,uz);j<std::min(cz+type->footZ,uz+u.type->footZ);++j)
                    for (int i=std::max(cx,ux);i<std::min(cx+type->footX,ux+u.type->footX);++i) {
                        if (!tested(size_t(j-cz)*type->footX+i-cx)) continue;
                        if (!mobile && !u.type->yardMap.empty()) {
                            const char yard=u.type->yardMap.at(size_t(j-uz)*u.type->footX+i-ux);
                            if (yard=='.') continue;
                            if (yard=='c' || yard=='C') {
                                const auto script=unitScripts_.find(u.id);
                                if (script!=unitScripts_.end() && script->second.yardOpen) continue;
                            }
                        }
                        return false;
                    }
            }
            return true;
        }
        const auto bodies=searchBodyRect(cx,cz,type->footX,type->footZ,replacing ? replacing->id : 0);
        for (size_t i=0;i<bodies.cells.size();++i)
            if (tested(i) && bodies.cells[i]) return false;
        return true;
    }
    const auto overlaps=[&](const Unit& u) {
        if (!u.alive() || u.id==ignoreId) return false;
        if (ignoreMobile && !(u.type && u.type->isStructure())) return false;
        if (yieldsToSite(u,yieldPlayer)) return false;
        float dx = u.x.toFloat() - x, dz = u.z.toFloat() - z;
        float min = 16.0f * float(std::max(type->footX, type->footZ)) / 2 + 12;
        return dx * dx + dz * dz < min * min;
    };
    if(candidates) {for(int index:*candidates)if(overlaps(units_[size_t(index)]))return false;}
    else {TAK_PASS();for(const auto& u:units_)if(overlaps(u))return false;}
    return true;
}

bool World::clearableForPlacement(const UnitType* type, float x, float z,
                                  std::vector<int>& out, int player) const {
    out.clear();
    if (!type || !type->isStructure()) return false;
    // A build order's test: the player's own units that step aside do not count.
    if (placementCheck(type,x,z,player,&out,nullptr,0,false,player)) return true;
    out.clear();
    return false;
}

void World::setBuildRestrictions(std::vector<std::string> types, bool enabled) {
    for (auto& type : types)
        for (char& c : type) if (c >= 'A' && c <= 'Z') c += 'a'-'A';
    std::sort(types.begin(),types.end());
    types.erase(std::unique(types.begin(),types.end()),types.end());
    buildRestrictionsEnabled_ = enabled;
    buildRestrictions_ = enabled ? std::move(types) : std::vector<std::string>{};
}

bool World::buildAllowed(const UnitType* type) const {
    return type && (!buildRestrictionsEnabled_ ||
        std::binary_search(buildRestrictions_.begin(),buildRestrictions_.end(),type->id));
}

int World::startBuild(int builderId, const UnitType* type, float x, float z,
                      Approach approach, bool* mobileBlocked) {
    if (mobileBlocked) *mobileBlocked=false;
    if (!buildAllowed(type)) return 0;
    Unit* b = unit(builderId);
    // A mobile builder only: a building may carry canMove=1 in its FBI but can never
    // place structures, and a site still under construction is not yet a builder.
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || b->type->isStructure() ||
        b->underConstruction)
        return 0;
    const Unit* replacing=lodestoneUpgradeSource(type,x,z,b->player);
    if (!replacing && atUnitCap(b->player)) return 0;
    if (atTypeCap(b->player, type)) return 0;   // totalallowed: already fielding one
    if (!canPlace(type, x, z, b->player)) {
        if (mobileBlocked)
            *mobileBlocked=placementCheck(type,x,z,b->player,nullptr,nullptr,0,true);
        return 0;
    }
    std::optional<Unit::LodestoneReplacement> replacement;
    if (replacing) {
        replacement=Unit::LodestoneReplacement{replacing->type,replacing->x,replacing->z,replacing->heading};
        Unit* old=unit(replacing->id);
        blockFoot(*old->type,old->x.toFloat(),old->z.toFloat(),false);
        groundRemove(*old);
        old->deadFor=kRetiredTicks; // consumed, not killed: no wreck, death sound or kill credit
        old->orders.clear();
        --players_[size_t(old->player)].unitCount;
        gPlayersValid_=false;bodyIndexValid_=false;
    }
    int8_t bsquad = b->squad;   // capture before spawn may realloc units_
    // Preserve retail birth orientation. Factory scripts query this heading to
    // counter-rotate their build pads into the fixed yard where appropriate.
    int id = spawn(type, x, z, std::nullopt, b->player);
    Unit* site = unit(id);
    site->underConstruction = true;
    site->lodestoneReplacement=replacement;
    site->buildBegun=bool(replacement);
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
            script->second.state().startArguments(file,file.scriptIndex("StartBuilding"),
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
    OrdersNote note{*this,builderId};
    if (!buildAllowed(type)) return;
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || b->type->isStructure() ||
        b->underConstruction || !type) return;
    std::vector<int> clearing;
    if (!canPlaceOrder(type,x,z,b->player) &&
        (!b->type->canReclaim || !b->type->canMove ||
         !clearableForPlacement(type,x,z,clearing,b->player))) return;
    // Stable nearest-first order, computed authoritatively on every peer.
    std::sort(clearing.begin(),clearing.end(),[&](int a,int c) {
        const auto distance=[&](int id) {
            const auto* f=feature(id);
            const int64_t dx=int64_t(f->x.v)-b->x.v,dz=int64_t(f->z.v)-b->z.v;
            return dx*dx+dz*dz;
        };
        const auto da=distance(a),dc=distance(c);
        return da!=dc ? da<dc : a<c;
    });
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
    for (int id:clearing) reclaim(builderId,id,true);
    b->orders.push_back(makeBuildOrder(*b,type,Fixed::fromFloat(x),Fixed::fromFloat(z)));
}

void World::queueManaBuildArea(int builderId,const UnitType* type,
                             float x0,float z0,float x1,float z1,bool queue) {
    OrdersNote note{*this,builderId};
    Unit* b=unit(builderId);
    if (!buildAllowed(type) || !type->onMana || !b || !b->alive() || !b->type ||
        !b->type->isBuilder || b->type->isStructure() || b->underConstruction || b->repeatType ||
        !std::isfinite(x0) || !std::isfinite(z0) || !std::isfinite(x1) || !std::isfinite(z1)) return;
    const float limitX=float(std::max(0,hW_-1)*16),limitZ=float(std::max(0,hH_-1)*16);
    ManaBuildArea area;
    area.type=type;
    area.minX=Fixed::fromFloat(std::clamp(std::min(x0,x1),0.0f,limitX));
    area.maxX=Fixed::fromFloat(std::clamp(std::max(x0,x1),0.0f,limitX));
    area.minZ=Fixed::fromFloat(std::clamp(std::min(z0,z1),0.0f,limitZ));
    area.maxZ=Fixed::fromFloat(std::clamp(std::max(z0,z1),0.0f,limitZ));
    if (!queue) {cancelPath(*b);cancelBuilds(builderId);b->orders.clear();}
    Order o;
    o.x=area.minX;o.z=area.minZ;o.goal=true;o.issuedTick=tickCounter_;o.manaBuildArea=area;
    b->orders.push_back(o);
}

void World::tickManaBuildArea(Unit& b) {
    if (b.orders.empty() || !b.orders.front().manaBuildArea) return;
    auto& area=*b.orders.front().manaBuildArea;
    const int width=hW_/2,height=hH_/2;
    const auto explored=[&](Fixed x,Fixed z) {
        const int cx=x.floorInt()/32,cz=z.floorInt()/32;
        return navigationExplored_.empty() || (cx>=0 && cz>=0 && cx<width && cz<height &&
            (navigationExplored_[size_t(cz)*width+cx] & (1u<<b.player)));
    };
    const auto tried=[&](uint32_t i){return std::binary_search(area.visited.begin(),area.visited.end(),i);};
    const auto visit=[&](uint32_t i) {
        const auto at=std::lower_bound(area.visited.begin(),area.visited.end(),i);
        if (at==area.visited.end() || *at!=i) area.visited.insert(at,i);
    };
    if (area.exploring) {
        // Scouting is over (arrived, or its move failed). A deposit that is
        // still unexplored is given up, so a failed scout never repeats.
        area.exploring=false;
        if (area.nextSpot<manaSpots_.size()) {
            const auto [sx,sz]=manaSpots_[area.nextSpot];
            if (!explored(Fixed::fromFloat(sx),Fixed::fromFloat(sz))) visit(area.nextSpot);
        }
    }
    // Deposits another allied builder has claimed: a lodestone build in its
    // queue, or the deposit its own area job is scouting. Two builders on
    // overlapping areas therefore split the deposits instead of racing to
    // the same one. Unit order is the deterministic units_ order.
    struct Claim {const UnitType* type;Fixed x,z;};
    std::vector<Claim> claims;std::vector<uint32_t> scouted;
    TAK_PASS();
    for (const auto& u:units_) {
        if (u.id==b.id || !u.alive() || !allied(u.player,b.player)) continue;
        bool firstArea=true;
        for (const auto& o:u.orders) {
            if (o.buildType && o.buildType->onMana) claims.push_back({o.buildType,o.buildX,o.buildZ});
            if (o.manaBuildArea && firstArea) {
                firstArea=false;
                if (o.manaBuildArea->exploring) scouted.push_back(o.manaBuildArea->nextSpot);
            }
        }
    }
    const auto claimed=[&](uint32_t i,Fixed x,Fixed z) {
        if (std::find(scouted.begin(),scouted.end(),i)!=scouted.end()) return true;
        for (const auto& c:claims)
            if (footprintWaypoint(footprintCell(x,c.type->footX),c.type->footX)==c.x &&
                footprintWaypoint(footprintCell(z,c.type->footZ),c.type->footZ)==c.z) return true;
        return false;
    };
    // Greedy nearest neighbour, re-chosen from the builder's CURRENT position
    // after every deposit (so a Shift-queued area starts where the previous
    // order ended, and deposits built by others drop out as it goes). Rank:
    // deposits on the builder's own nav component first, then exact squared
    // straight-line distance in 16.16, then spot index.
    int64_t best=-1;bool bestKnown=false;
    std::tuple<int,uint64_t,uint32_t> bestKey{};
    for (uint32_t i=0;i<manaSpots_.size();++i) {
        if (tried(i)) continue;
        const auto [sx,sz]=manaSpots_[i];
        const Fixed x=Fixed::fromFloat(sx),z=Fixed::fromFloat(sz);
        if (x<area.minX || x>area.maxX || z<area.minZ || z>area.maxZ || claimed(i,x,z)) continue;
        const bool known=explored(x,z);
        const bool reachable=b.type->canFly || pathExists(b.type,sx,sz,b.x.toFloat(),b.z.toFloat());
        // An unexplored deposit we cannot walk to cannot be scouted: give it up.
        if (!known && !reachable) {visit(i);continue;}
        const int64_t dx=int64_t(x.v)-b.x.v,dz=int64_t(z.v)-b.z.v;
        const std::tuple<int,uint64_t,uint32_t> key{reachable?0:1,uint64_t(dx*dx)+uint64_t(dz*dz),i};
        if (best<0 || key<bestKey) {best=i;bestKey=key;bestKnown=known;}
    }
    if (best<0) {b.orders.erase(b.orders.begin());return;}
    const uint32_t spot=uint32_t(best);
    const auto [sx,sz]=manaSpots_[spot];
    const Fixed x=Fixed::fromFloat(sx),z=Fixed::fromFloat(sz);
    if (!bestKnown) {
        area.exploring=true;area.nextSpot=spot;
        const Order approach=makeBuildOrder(b,area.type,x,z);
        const size_t oldSize=b.orders.size();
        order(b.id,approach.x.toFloat(),approach.z.toFloat(),true);
        if (b.orders.size()>oldSize) {
            if (b.type->canFly) {
                // An ordinary queued flight move may retire 80..144px
                // before its point. Scouting must reach the actual approach
                // so that its following area job can see the deposit.
                auto& scout=b.orders.back();scout.flightMoveMission=false;
                scout.flightGoal=RetailFlightGoal{{scout.x.v,b.flightY.v,scout.z.v}};
                if (auto script=unitScripts_.find(b.id);script!=unitScripts_.end() && !script->second.activated) {
                    script->second.activated=true;notifyUnitScript(b,"Activate");
                }
                notifyUnitScript(b,"BeginFlight");
            }
            std::rotate(b.orders.begin(),b.orders.begin()+oldSize,b.orders.end());
        }
        return;
    }
    visit(spot);
    const size_t oldSize=b.orders.size();
    // Reuse normal placement, clearing, construction and upgrade orders.
    // Move the appended child work ahead of the area and its queued tail.
    queueBuild(b.id,area.type,sx,sz,true);
    if (b.orders.size()>oldSize)
        std::rotate(b.orders.begin(),b.orders.begin()+oldSize,b.orders.end());
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
    // A reclaim leg's rectangle belongs to tickReclaim.
    if (!goal.buildType) return false;
    if (goal.buildRectangle && goal.mission.stage==0) {
        // A queued build computes its approach when it becomes active,
        // after earlier movement/build orders have changed our position.
        const auto [x,z]=goal.buildRectangle->navigationCell(
            footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ));
        goal.x=Fixed::fromInt(x*16+u.type->footX*8);
        goal.z=Fixed::fromInt(z*16+u.type->footZ*8);
        if (u.type->canFly) {
            // 41ef00 takes off before approaching an unplaced site. Its first
            // controller targets the snapped site with radius 2*builddistance;
            // the tighter, facing orbit belongs to the later working stages.
            if (auto script=unitScripts_.find(u.id);script!=unitScripts_.end() && !script->second.activated) {
                script->second.activated=true;notifyUnitScript(u,"Activate");
            }
            notifyUnitScript(u,"BeginFlight");
            if (u.flightGroundMode!=2) { u.flightGroundMode=2;groundMove(u); }   // 51b3b0: taking off unstamps
            goal.x=goal.buildX;goal.z=goal.buildZ;
            goal.flightGoal=RetailFlightGoal{{goal.x.v,0,goal.z.v},0x30,0,
                int16_t(uint16_t(u.type->buildDist)*2)};
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
    // Retire the authoritative job as well as its presentation target.
    // Otherwise replacement orders leave tickRetailConstruction running.
    if(b->retailBuild) {
        if(b->retailBuild->working)notifyUnitScript(*b,"StopBuilding");
        b->retailBuild.reset();
        b->constructionHolding=false;
        b->standbyAllowed=true;
    }
    stopWorkAnimation(*b);
    // Drop every pending build from the order queue (see queueBuild): a fresh
    // move/attack/stop cancels queued construction. A ground builder's
    // HelpBuild leg is such a job too (a flyer's is only its approach).
    const bool groundHelp=b->type && !b->type->canFly;
    b->orders.erase(std::remove_if(b->orders.begin(), b->orders.end(),
                                   [groundHelp](const Order& o) {
                                       return o.buildType != nullptr || o.reclaimFeat != 0 || o.reclaimArea || o.manaBuildArea ||
                                              o.repairTarget != 0 || (groundHelp && o.helpSite != 0);
                                   }),
                    b->orders.end());
    b->reclaimId = 0;            // a fresh move/attack/stop drops any reclaim job
    b->repairId = 0;             // ...and any repair job
    if (b->buildSiteId) {
        Unit* site = unit(b->buildSiteId);
        // A site that never actually started building is just a ghost — remove
        // it so its marker/ghost doesn't linger.
        // Retail GetBuilt sites own their abandonment/decay lifecycle, even
        // before their first affordable work tick. Only legacy ghosts vanish.
        if (site && site->underConstruction && !site->buildBegun && !site->retailSite) {
            if (site->type && site->type->isStructure()) {
                blockFoot(*site->type, site->x.toFloat(), site->z.toFloat(), false);
            }
            site->underConstruction = false;
            groundRemove(*site);
            site->deadFor = kRetiredTicks;   // fully gone (painter skips the corpse window)
        }
        b->buildSiteId = 0;
    }
}

bool World::canHelpBuild(const Unit& b, const Unit& site) const {
    // 0x519e60, emulated: the target is alive, unfinished and not the helper,
    // and has the helper's OWNER -- an ally's site does not qualify. The helper
    // is a builder with a mover (a structure never helps); a builderlimited
    // one needs the target's type on its own build menu. Nothing about the
    // helper's queue, repeat build or current job is consulted.
    if (b.id==site.id || !b.alive() || !b.type || !b.type->isBuilder || b.type->isStructure() ||
        !site.alive() || !site.type || !site.underConstruction || site.hp<=Fixed() || site.player!=b.player)
        return false;
    const auto& menu=b.type->buildMenu;
    return !b.type->builderLimited || std::find(menu.begin(),menu.end(),site.type->id)!=menu.end();
}

bool World::canRepairUnit(const Unit& b, const Unit& t) const {
    // World::repair's own acceptance (0x519d40: a damaged, finished unit of
    // an ally, never the repairer itself), for a builder with a mover. Its
    // flying branch refuses a target under the sea.
    return b.id!=t.id && b.alive() && b.type && b.type->isBuilder && b.type->canMove &&
           !b.type->isStructure() && t.alive() && t.type && !t.underConstruction && !t.embarked() &&
           t.hp>Fixed() && allied(t.player,b.player) && t.hp<Fixed::fromFloat(t.type->maxHp) &&
           (!b.type->canFly || repairTopAboveSea(t));
}

bool World::repairTopAboveSea(const Unit& t) const {
    return (t.type->modelTop>>16)+(t.type->canFly ? t.flightY : t.groundY).floorInt()>=seaLevel_;
}

Unit* World::productionOwnerOf(const Unit& site) {
    if (!site.productionOwner || !site.underConstruction || site.retailSite) return nullptr;
    Unit* p=unit(site.productionOwner);
    if (!p || !p->alive() || p->productionSiteId!=site.id || p->player!=site.player ||
        p->buildQueue.empty() || p->buildQueue.front()!=site.type) return nullptr;
    return p;
}

// A ground helper's leg is the HelpBuild itself (0x405cf0, emulated
// 2026-10-30): when it becomes current, stage 0 installs the rectangle
// controller round the site's footprint (prepareHelpApproach), so each
// helper walks to the nearest cell where its footprint touches the site's,
// from wherever it comes. (Ours walked every helper to one point south of
// the site and worked from builddistance.) A flyer's VTOL_HelpBuild stage 1
// (0x41f7f0) installs one point goal at the site's centre that accepts within
// 2 x builddistance (flags 0x30): no VTOL_Move, so none of its draws.
void World::helpApproach(Unit& b, const Unit& site, bool queue) {
    const int builderId=b.id,siteId=site.id;
    if (!b.type->canFly) {
        if (!queue) { cancelPath(b); b.orders.clear(); }
        Order o;
        o.x=site.x; o.z=site.z;   // until stage 0 installs the approach
        o.goal=true;
        o.issuedTick=tickCounter_;
        b.orders.push_back(o);
        noteOrders(builderId);
    } else {
        if (!queue) { cancelPath(b); b.orders.clear(); }
        Order o;
        o.x=site.x; o.z=site.z;
        o.goal=true;
        o.issuedTick=tickCounter_;
        o.flightGoal=RetailFlightGoal{{site.x.v,0,site.z.v},0x30,0,int16_t(2*std::max(0,int(b.type->buildDist)))};
        b.orders.push_back(o);
        noteOrders(builderId);
    }
    Unit* u=unit(builderId);
    if (u && !u->orders.empty()) u->orders.back().helpSite=siteId;
}

void World::assist(int builderId, int siteId, bool queue) {
    Unit* b = unit(builderId);
    Unit* site = unit(siteId);
    if (!b || !site || b->underConstruction || !canHelpBuild(*b,*site)) return;
    // Latch onto the existing site; tickConstruction walks there and works at
    // this builder's rate. A factory's or conjurer's output shares its
    // producer's progress (workProduction); any other site grows from its hp.
    // A ground helper's leg is its job: queued behind other orders, it is
    // latched when it becomes current (the dispatch, as a repair is).
    if (b->type->canFly) {
        b->buildSiteId = siteId;
        b->buildStuckT = 0; b->buildStuckD = INT32_MAX;   // fresh job: reset the reach watchdog
    }
    // A repeating conjurer's move is a rally for its output (World::order);
    // this approach is its own.
    const UnitType* repeat=b->repeatType; b->repeatType=nullptr;
    helpApproach(*b,*site,queue);
    b=unit(builderId); b->repeatType=repeat;
    if (!b->type->canFly && currentLeg(b->orders)+1==b->orders.size()) b->buildSiteId=siteId;
}

// Sites never change after setup and buildings never move, but every sacred-income
// building asks each tick: a scan of every site per footprint cell was 5-10% of a
// late 8-AI game's tick (2,863 sites). The buckets keep the scan's answer.
static constexpr int kSacredBucketShift=3;

void World::setSacredSites(std::vector<SacredSite> sites) {
    sacredSites_=std::move(sites);
    sacredBucketStart_.clear();sacredBucketSites_.clear();
    sacredBx0_=sacredBz0_=sacredBw_=sacredBh_=0;
    int bx0=INT32_MAX,bz0=INT32_MAX,bx1=INT32_MIN,bz1=INT32_MIN;
    for (const auto& s:sacredSites_) {
        if (s.fx<=0 || s.fz<=0) continue;   // covers no cell
        bx0=std::min(bx0,s.x>>kSacredBucketShift);bx1=std::max(bx1,(s.x+s.fx-1)>>kSacredBucketShift);
        bz0=std::min(bz0,s.z>>kSacredBucketShift);bz1=std::max(bz1,(s.z+s.fz-1)>>kSacredBucketShift);
    }
    if (bx0>bx1) return;
    sacredBx0_=bx0;sacredBz0_=bz0;sacredBw_=bx1-bx0+1;sacredBh_=bz1-bz0+1;
    sacredBucketStart_.assign(size_t(sacredBw_)*size_t(sacredBh_)+1,0);
    auto each=[&](const SacredSite& s,auto&& f) {
        if (s.fx<=0 || s.fz<=0) return;
        for (int bz=(s.z>>kSacredBucketShift)-bz0;bz<=((s.z+s.fz-1)>>kSacredBucketShift)-bz0;++bz)
            for (int bx=(s.x>>kSacredBucketShift)-bx0;bx<=((s.x+s.fx-1)>>kSacredBucketShift)-bx0;++bx)
                f(size_t(bz)*size_t(sacredBw_)+size_t(bx));
    };
    for (const auto& s:sacredSites_) each(s,[&](size_t b) { ++sacredBucketStart_[b+1]; });
    for (size_t b=1;b<sacredBucketStart_.size();++b) sacredBucketStart_[b]+=sacredBucketStart_[b-1];
    sacredBucketSites_.resize(sacredBucketStart_.back());
    std::vector<uint32_t> fill(sacredBucketStart_.begin(),sacredBucketStart_.end()-1);
    for (size_t i=0;i<sacredSites_.size();++i)
        each(sacredSites_[i],[&](size_t b) { sacredBucketSites_[fill[b]++]=uint32_t(i); });
}

// The last site in sacredSites_ order whose footprint holds cell (x, z), or none.
const SacredSite* World::sacredSiteAt(int x,int z) const {
    const int bx=(x>>kSacredBucketShift)-sacredBx0_,bz=(z>>kSacredBucketShift)-sacredBz0_;
    if (bx<0 || bz<0 || bx>=sacredBw_ || bz>=sacredBh_) return nullptr;
    const size_t b=size_t(bz)*size_t(sacredBw_)+size_t(bx);
    for (uint32_t k=sacredBucketStart_[b+1];k>sacredBucketStart_[b];--k) {
        const SacredSite& s=sacredSites_[sacredBucketSites_[k-1]];
        if (x>=s.x && x<s.x+s.fx && z>=s.z && z<s.z+s.fz) return &s;
    }
    return nullptr;
}

float World::sacredIncomeMultiplier(int x,int z,int fx,int fz) const {
    // 51d68e..51d7b6 scans x then z. Count positive sacred-site cells and
    // compare with the last encountered site's full footprint. Each cell reads
    // the last-listed site that holds it.
    const SacredSite* last=nullptr;
    int covered=0;
    for (int dx=0;dx<fx;++dx) for (int dz=0;dz<fz;++dz) {
        const SacredSite* s=sacredSiteAt(x+dx,z+dz);
        if (s && s->multiplier>0) { ++covered;last=s; }
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
        corpseAnchors_.erase(cz*terW_+cx);
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
    // 512fbe/512fff: the corpse is installed with the dying unit's owner (cell
    // +7), where every map feature gets 0xa. Area clear reads it under fog.
    if (!placeMapFeature(feature,uint8_t(corpse.player))) return false;
    corpseFootprints_[corpse.id]={cx,cz,type};
    corpseAnchors_[cz*terW_+cx]=corpse.id;
    corpse.corpseBlocks=definition.blocking;
    if (definition.blocking) blockCells(cx,cz,definition.fx,definition.fz,true);
    // 512ee0 passes the original position and orientation to 495360.
    // corpseadjust moves the blocked cells, not the rendered model.
    return true;
}

void World::retireCorpse(Unit& corpse) {
    const auto found=corpseFootprints_.find(corpse.id);
    if (found!=corpseFootprints_.end()) {
        const auto footprint=found->second;
        corpseFootprints_.erase(found);
        corpseAnchors_.erase(footprint.z*terW_+footprint.x);
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
    ++placementEpoch_;notePlacementDirty(cx,cz,type.footX,type.footZ);
    forgetCorpseAt(cx,cz);
    retailMapRemove(cx,cz,type,[&](int x,int z)->RetailMapFeatureCell& {
        return mapPlacementCells_.at(size_t(z)*hW_+x);
    });
    for (auto& plane:searchGrades_) refreshSearchRect(plane,cx,cz,type.footX,type.footZ);
}

bool World::placeMapFeature(const Feature& f,uint8_t owner) {
    if (mapPlacementCells_.empty()) return true;
    RetailMapFeatureType type;
    type.footX=f.fx;type.footZ=f.fz;type.blocking=f.blocks;type.clearable=true;
    if (f.type>=0) {
        const auto& definition=featTypes_.at(size_t(f.type));
        type.name=definition.name;type.indestructible=definition.indestructible;
        type.projectileHeight=definition.projectileHeight;type.treeOrRock=definition.treeOrRock;
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
    // Only a change to the placement cells is a new static epoch: a refused
    // install that replaced nothing leaves every plane valid.
    bool replaced=false;
    const bool placed=retailMapInstall(hW_,hH_,cx,cz,uint16_t(index),mapPlacementTypes_,[&](int x,int z)->RetailMapFeatureCell& {
            return mapPlacementCells_.at(size_t(z)*hW_+x);
        },[&](int x,int z,uint16_t previous) {
            replaced=true;
            forgetCorpseAt(x,z);
            const auto& oldType=mapPlacementTypes_[previous];
            notePlacementDirty(x,z,oldType.footX,oldType.footZ);
            if (oldType.blocking) blockCells(x,z,oldType.footX,oldType.footZ,false);
            for (auto& plane:searchGrades_) refreshSearchRect(plane,x,z,oldType.footX,oldType.footZ);
            auto it=featureIdx_.find(z*hW_+x);
            if (it!=featureIdx_.end()) features_[it->second].alive=false;
            bumpFeatGen();
        },owner);
    // Feature grades can change without changing the legacy obstacle bit (for
    // example, clearable -> indestructible). Refresh the actual search cache.
    if(placed||replaced)++placementEpoch_;
    if(placed)notePlacementDirty(cx,cz,f.fx,f.fz);
    if (placed) for (auto& plane:searchGrades_) refreshSearchRect(plane,cx,cz,f.fx,f.fz);
    return placed;
}

void World::addFeature(int id, float x, float z, float energy,
                       int fx, int fz, bool blocks, int type, bool placeOnMap) {
    Feature f;
    f.id = id; f.x = Fixed::fromFloat(x); f.z = Fixed::fromFloat(z);
    f.fx = fx; f.fz = fz; f.energy = energy; f.blocks = blocks;
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
    if (f.fadeLeft) { f.fadeLeft = 0; --fadingFeatures_; }
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
    f.energy = nt.energy;
}

void World::igniteFeature(Feature& f) {
    if (f.burn || !f.alive || f.type < 0 || f.fadeLeft) return;   // fading: see damageFeature
    const FeatType& ft = featTypes_[size_t(f.type)];
    if (!ft.flamable || !ft.hasBurnAnim) return;
    static const bool kBurnLog = tak::devEnv("TAK_BURNLOG") != nullptr;
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

void World::reclaimArea(int builderId,float x0,float z0,float x1,float z1,bool queue) {
    OrdersNote note{*this,builderId};
    Unit* b=unit(builderId);
    if (!b || !b->alive() || !b->type || !b->type->isBuilder ||
        !b->type->canMove || !b->type->canReclaim ||
        !std::isfinite(x0) || !std::isfinite(z0) || !std::isfinite(x1) || !std::isfinite(z1)) return;
    const float limitX=float(std::max(0,hW_-1)*16),limitZ=float(std::max(0,hH_-1)*16);
    RetailReclaimArea area;
    area.minX=Fixed::fromFloat(std::clamp(std::min(x0,x1),0.0f,limitX)).v;
    area.maxX=Fixed::fromFloat(std::clamp(std::max(x0,x1),0.0f,limitX)).v;
    area.minZ=Fixed::fromFloat(std::clamp(std::min(z0,z1),0.0f,limitZ)).v;
    area.maxZ=Fixed::fromFloat(std::clamp(std::max(z0,z1),0.0f,limitZ)).v;
    if (!queue) { cancelPath(*b); b->orders.clear(); }
    Order o;
    o.x=Fixed::raw(area.minX);o.z=Fixed::raw(area.minZ);
    o.goal=true;o.issuedTick=tickCounter_;o.reclaimArea=area;
    b->orders.push_back(o);
}

void World::tickReclaimArea(Unit& b) {
    if (b.orders.empty() || !b.orders.front().reclaimArea) return;
    auto& area=*b.orders.front().reclaimArea;
    // 40681b: stage 0 records where the builder stands when the order first
    // runs (not when it was issued) as the last target, then sleeps a tick.
    if (area.stage==0) {
        area.lastX=b.x.v;area.lastZ=b.z.v;area.stage=1;
        return;
    }
    // 4067ae: stage 1 approaches the builder's position clamped into the box,
    // even from inside it (that circle is satisfied on the next navigator
    // pass), and stage 2 runs when the approach arrives or fails.
    if (area.stage==1) {
        area.stage=2;
        const Fixed x=Fixed::raw(std::clamp(b.x.v,area.minX,area.maxX));
        const Fixed z=Fixed::raw(std::clamp(b.z.v,area.minZ,area.maxZ));
        if (b.type->canFly) {
            // VTOL_RECLAIMAREA (420c10) is not ported: flyers keep a plain move.
            if (x==b.x && z==b.z) return;
            auto pending=std::move(b.orders);
            b.orders.clear();
            order(b.id,x.toFloat(),z.toFloat(),true);
            b.orders.insert(b.orders.end(),std::make_move_iterator(pending.begin()),
                            std::make_move_iterator(pending.end()));
            return;
        }
        Order approach{x,z,0};
        approach.goal=approach.groundMission=approach.reclaimAreaApproach=true;
        approach.issuedTick=tickCounter_;
        approach.controller=++nextMovementController_;
        cancelPath(b);
        b.orders.insert(b.orders.begin(),approach);
        resetGroundSegment(b,b.orders.front());
        return;
    }
    // 509cc0's fog test (509d9f..509e6d), for the builder's own player so it
    // is the same on every peer. Current view: the allied sight footprints,
    // re-evaluated on demand (never the client's async display fog). Explored:
    // the simulation's Mapping plane, or the whole map when the room (or a
    // power code) revealed it. Without line of sight only explored counts.
    const int width=hW_/2,height=hH_/2;
    const bool losOn=!roomLosOff_ && !powerLosOff(b.player);
    const bool allExplored=(roomMapRevealed_ && !powerCodes_) || powerMappingOff(b.player);
    const bool planeExplored=navigationExplored_.size()==size_t(width)*height;
    std::vector<uint8_t> visible;
    if (losOn) {
        visible.assign(size_t(width)*height,0);
        TAK_PASS();
        for (const auto& u:units_) {
            // Cargo footprints are live too (4f6a60 has no attachment test).
            if (!u.alive() || !u.type || u.underConstruction || !allied(u.player,b.player)) continue;
            RetailSightFootprint sight=u.sightFootprint;
            sight.active=false;
            // Before the first exploration pass, initialize the same authored sight.
            if (sight.x<0 || sight.z<0) {
                sight.distance=int16_t(sightDistance(*u.type));sight.sightHeight=u.type->sightHeight;
                sight.x=int16_t(u.x.floorInt()/32);sight.z=int16_t(u.z.floorInt()/32);
                const Fixed y=u.type->canFly?u.flightY:u.groundY;
                sight.eyeHeight=std::max(y.floorInt(),seaLevel_+1)+sight.sightHeight;
            }
            retailSightFootprint(sight,true,false,width,height,uint8_t(b.player),
                [&](int x,int z){return explorationHeights_[size_t(z)*width+x];},
                [&](int x,int z,int,uint16_t){visible[size_t(z)*width+x]=1;},
                [&](int x,int z,bool){return !visible[size_t(z)*width+x];});
        }
    }
    const auto seen=[&](int cx,int cz) {
        const int vx=cx/2,vz=cz/2;
        if (vx>=width || vz>=height) return false;
        const size_t cell=size_t(vz)*width+vx;
        const bool explored=allExplored || (planeExplored && (navigationExplored_[cell]>>b.player & 1));
        // The owner byte of the SAMPLED cell, before any tail resolves.
        const uint8_t owner=mapPlacementCells_.empty() ? kRetailWorldFeatureOwner
            : mapPlacementCells_[size_t(cz)*hW_+cx].owner;
        return retailReclaimAreaSeen(losOn,losOn && visible[cell],explored,owner);
    };
    // Resolve footprint cells to their actual anchor. Selecting only centres
    // misses multi-cell trees and rubble intersecting the selection rectangle.
    std::unordered_map<int,int> targets;
    for (const auto& f:features_) {
        if (!f.alive || !f.id || f.fadeLeft) continue;
        if (f.type>=0 && (size_t(f.type)>=featTypes_.size() || !featTypes_[size_t(f.type)].reclaimable)) continue;
        const int x=footprintOrigin(f.x,f.fx),z=footprintOrigin(f.z,f.fz);
        targets.emplace(z*hW_+x,f.id);
    }
    for (const auto& [id,p]:corpseFootprints_) {
        const Unit* corpse=unit(id);
        if (!corpse || corpse->alive() || corpse->deadFor>=corpse->corpseUntil || corpse->corpseFading ||
            p.type<0 || size_t(p.type)>=featTypes_.size() || !featTypes_[size_t(p.type)].reclaimable) continue;
        targets.emplace(p.z*hW_+p.x,-id);
    }
    auto targetAt=[&](int32_t x,int32_t z) {
        const int cx=x>>20,cz=z>>20;
        if (cx<0 || cz<0 || cx>=hW_ || cz>=hH_ || !seen(cx,cz)) return 0;
        int ax=cx,az=cz;
        if (!mapPlacementCells_.empty()) {
            const auto& cell=mapPlacementCells_[size_t(cz)*hW_+cx];
            if (cell.feature==0xfffe) {ax-=cell.backX;az-=cell.backZ;}
            else if (cell.feature>=mapPlacementTypes_.size()) return 0;
        }
        const auto found=targets.find(az*hW_+ax);
        return found==targets.end()?0:found->second;
    };
    const auto pick=retailReclaimAreaPick(area,b.x.v,b.z.v,hW_,hH_,
        [&](int32_t x,int32_t z){return targetAt(x,z)!=0;});
    // 5: nothing left to clear. 8: the pick is the last target's cell again
    // (its child just ended without clearing it, say it was unreachable).
    if (pick.step!=RetailReclaimAreaStep::Child) { b.orders.erase(b.orders.begin());return; }
    const int target=targetAt(pick.x,pick.z);
    area.stage=1;
    // Keep the area behind one ordinary reclaim child. When the child ends
    // (cleared, failed, or another builder removed the feature) stage 1
    // approaches again and picks from the NEW position.
    const size_t oldSize=b.orders.size();
    reclaim(b.id,target,true);
    if (b.orders.size()>oldSize) std::rotate(b.orders.begin(),b.orders.end()-1,b.orders.end());
    else b.orders.erase(b.orders.begin());   // 4de530 found no mission: 8
}

void World::reclaim(int builderId, int featureId, bool queue) {
    OrdersNote note{*this,builderId};
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
        if (!c || c->id == builderId || !c->type || (c->alive() && c->hp > Fixed()) || c->corpseFading)
            return;
        int ct = c->corpseStatue >= 0 ? c->corpseStatue : corpseTypeOf(c->type);
        if (ct < 0 || !featTypes_[size_t(ct)].reclaimable) return;
        tx = c->x.toFloat(); tz = c->z.toFloat();
        static const bool kRcLog = tak::devEnv("TAK_BURNLOG") != nullptr;
        if (kRcLog)
            std::fprintf(stderr, "corpse-reclaim ORDER: b%d -> %s (%d)\n",
                         builderId, c->type->id.c_str(), -featureId);
    } else {
        const Feature* f = feature(featureId);
        if (!f || !featureReclaimable(*f)) return;
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

// VTOL_RECLAIM (420xxx) is not ported, so a flying reclaimer keeps our old
// acceptance: within 24 px plus half the target plus builddistance of its
// centre. Ground reclaimers use retail's rectangle (prepareReclaimApproach).
bool World::flyerReclaimInReach(const Unit& b,int target) const {
    int ax=0,az=0,fw=1,fh=1;float energy=0;
    if (!reclaimTargetFootprint(target,ax,az,fw,fh,energy)) return false;
    const float dx=float(ax*16+fw*8)-b.x.toFloat(),dz=float(az*16+fh*8)-b.z.toFloat();
    const float reach=24.0f+8.0f*float(std::max(fw,fh))+(b.type->buildDist>0 ? b.type->buildDist : 0.0f);
    return dx*dx+dz*dz<=reach*reach;
}

// 4930d0 resolves the mission cell to its feature's anchor and reads the
// footprint from the feature definition. A corpse is a feature too: its
// installed corpse footprint, never the dead unit's own.
bool World::reclaimTargetFootprint(int target,int& x,int& z,int& fx,int& fz,float& energy) const {
    if (target<0) {
        const auto found=corpseFootprints_.find(-target);
        if (found==corpseFootprints_.end() || found->second.type<0 ||
            size_t(found->second.type)>=featTypes_.size()) return false;
        const FeatType& type=featTypes_[size_t(found->second.type)];
        x=found->second.x;z=found->second.z;fx=type.fx;fz=type.fz;energy=type.energy;
        return true;
    }
    const Feature* f=feature(target);
    if (!f || !f->alive) return false;
    x=footprintOrigin(f->x,f->fx);z=footprintOrigin(f->z,f->fz);fx=f->fx;fz=f->fz;energy=f->energy;
    return true;
}

// Each work wake looks the target up again (4930d0 -> 496ea0: the type's
// reclaimable flag and the object's +0x5d bit 0x10). The bit is gone once
// another builder's reclaim has started its fade, and while the feature burns
// (4949c0 clears it).
bool World::reclaimTargetValid(int target) const {
    if (target<0) {
        const Unit* c=unit(-target);
        if (!c || !c->type || c->alive() || c->corpseFading || c->deadFor>=c->corpseUntil ||
            !corpseFootprints_.contains(c->id)) return false;
        const int ct=corpseFootprints_.at(c->id).type;
        return ct>=0 && size_t(ct)<featTypes_.size() && featTypes_[size_t(ct)].reclaimable;
    }
    const Feature* f=feature(target);
    return f && featureReclaimable(*f) && !f->burn;
}

// 406250 stage 0, when the RECLAIM leg first becomes current: resolve the
// target (4930d0), then 4d4e80 installs the rectangle controller around its
// footprint (no builddistance anywhere) with wait mask 0x700. The approach
// is the one MobileBuild uses (prepareBuildApproach), the same controller.
void World::prepareReclaimApproach(Unit& u) {
    if (u.orders.empty()) return;
    auto& goal=u.orders[currentLeg(u.orders)];
    if (!goal.reclaimFeat || goal.mission.stage!=0) return;
    int ax=0,az=0,fw=1,fh=1;float energy=0;
    if (!reclaimTargetFootprint(goal.reclaimFeat,ax,az,fw,fh,energy) || !reclaimTargetValid(goal.reclaimFeat)) {
        // A body still dying has no corpse feature yet (ours accepts the
        // order early); wait for it to settle. Anything else is gone.
        if (goal.reclaimFeat<0) {
            const Unit* c=unit(-goal.reclaimFeat);
            if (c && c->type && !c->corpseFading &&
                ((c->alive() && c->hp<=Fixed()) ||
                 (!c->alive() && c->deadFor<c->corpseAnimationTicks() && c->deadFor<c->corpseUntil))) return;
        }
        stopWorkAnimation(u);
        u.reclaimId=0;
        dropLeg(u);
        return;
    }
    goal.mission.stage=1;
    goal.reclaimCountdown=0;
    if (u.type->canFly) { goal.mission.waitMask=0; return; }   // flyerReclaimInReach
    goal.buildRectangle=retailReclaimRectangle(ax,az,fw,fh,u.type->footX,u.type->footZ);
    const auto [x,z]=goal.buildRectangle->navigationCell(
        footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ));
    goal.x=Fixed::fromInt(x*16+u.type->footX*8);
    goal.z=Fixed::fromInt(z*16+u.type->footZ*8);
    goal.segmentX=Fixed::fromInt(u.x.floorInt());
    goal.segmentZ=Fixed::fromInt(u.z.floorInt());
    goal.hasSegment=true;
    goal.mission.waitMask=0x700;
    goal.controller=++nextMovementController_;
    goal.navigationExhausted=goal.navigationConsumed=false;
    goal.mission.pending&=~0x3700u;
    if (u.routeStamp<0 || uint32_t(u.routeStamp)<=tickCounter_-6u) u.routeStamp=0;
    cancelPath(u);
    requestPath(u,goal.x.toFloat(),goal.z.toFloat());
}

// 494fd0 at stage 5: the feature fades out (494ce0) and leaves the plane with
// no featuredead replacement, so a reclaimed corpse leaves no rubble. While
// it fades it still blocks but is no longer a target. No mana.
void World::finishReclaim(Unit&,int target) {
    if (target<0) {
        Unit* c=unit(-target);
        if (!c) return;
        c->corpseFading=true;
        // deadFor advances later this tick: the body retires kReclaimFadeTicks on.
        c->corpseUntil=c->deadFor+int32_t(kReclaimFadeTicks)+1;
        return;
    }
    auto it=featureIdx_.find(target);
    if (it==featureIdx_.end()) return;
    Feature& f=features_[it->second];
    if (!f.fadeLeft) ++fadingFeatures_;
    f.fadeLeft=kReclaimFadeTicks;
    bumpFeatGen();   // no longer a reclaim target: the viewer's cursor follows
}

void World::tickFeatureFades() {
    if (!fadingFeatures_) return;
    for (auto& f:features_) {
        if (!f.fadeLeft || --f.fadeLeft) continue;
        --fadingFeatures_;
        if (!f.alive) continue;
        f.alive=false;
        removeMapFeature(footprintOrigin(f.x,f.fx),footprintOrigin(f.z,f.fz));
        bumpFeatGen();   // reclaimed away: stop drawing it
        if (f.blocks)   // free the ground cells it occupied (setupMatch blocked nav_)
            blockCells(footprintOrigin(f.x,f.fx),footprintOrigin(f.z,f.fz),f.fx,f.fz,false);
    }
}

// Retail RECLAIM (406250) on the current leg. Stage 1 waits for the approach
// (0x100/0x400 arrival, 0x200 failure); stage 2 is the turn child and the
// INBUILDSTANCE wait; stages 3 and 4 are the two-tick work wakes (406529);
// stage 5 removes the target. Once work has started nothing checks distance
// again: a builder pushed away keeps working, as in retail.
void World::tickReclaim(Unit& b) {
    if (b.orders.empty() || b.orders[currentLeg(b.orders)].reclaimFeat!=b.reclaimId) {
        // Another order is current (a fresh one, or one pushed ahead): the
        // job resumes when its leg is current again.
        stopWorkAnimation(b);
        b.reclaimId=0;
        return;
    }
    const int target=b.reclaimId;
    const auto quit=[&] {
        // 4062a5: the order ends silently (StopBuilding), the next one runs.
        stopWorkAnimation(b);
        dropLeg(b);
        b.reclaimId=0;
    };
    size_t leg=currentLeg(b.orders);
    if (b.orders[leg].mission.stage==0) return;   // prepareReclaimApproach
    int ax=0,az=0,fw=1,fh=1;float energy=0;
    if (b.orders[leg].mission.stage==1) {
        auto& goal=b.orders[leg];
        if (b.type->canFly) {
            if (!reclaimTargetValid(target)) { quit(); return; }
            if (!flyerReclaimInReach(b,target)) return;
        } else {
            const uint32_t events=(b.missionEvents | goal.mission.pending) & goal.mission.waitMask;
            if (!events) return;
            b.missionEvents&=~events;goal.mission.pending&=~events;goal.mission.waitMask=0;
            // 40638e: a failed approach ends the child, with no builddistance
            // fallback (MobileBuild has one; RECLAIM does not).
            if (events&0x200) { quit(); return; }
        }
        if (!reclaimTargetValid(target) || !reclaimTargetFootprint(target,ax,az,fw,fh,energy)) { quit(); return; }
        // Work starts: the approach's waypoints go, the leg is the order.
        cancelPath(b);
        if (leg) { b.orders.erase(b.orders.begin(),b.orders.begin()+long(leg));leg=0; }
        auto& work=b.orders.front();
        work.reclaimCountdown=retailReclaimCountdown(energy);
        const Fixed tx=Fixed::fromInt(ax*16+fw*8),tz=Fixed::fromInt(az*16+fh*8);
        work.reclaimHeading=uint16_t(retailDirection(b.x-tx,b.z-tz).v);
        work.mission.stage=2;
        b.speed=Fixed();
        // The stance producer RepairUnit also calls (406411): StartBuilding.
        startWorkAnimation(b,tx,tz);
    } else if (!reclaimTargetFootprint(target,ax,az,fw,fh,energy)) { quit(); return; }
    auto& work=b.orders[leg];
    const Fixed tx=Fixed::fromInt(ax*16+fw*8),tz=Fixed::fromInt(az*16+fh*8);
    if (work.mission.stage==2) {
        // The FacingNoFormation child (skipped when turninplacerate is 0),
        // then 4d4b90: wait for COB INBUILDSTANCE (SET 5, our `ready`).
        if (!b.type->canFly && b.type->turnInPlaceRate>0 &&
            portHeadingToRetail(b.heading)!=work.reclaimHeading) {
            const int32_t diff=bamDiff(retailHeadingToPort(work.reclaimHeading),b.heading);
            b.heading=b.heading+Bam(std::clamp(diff,-b.type->turnInPlaceRate,b.type->turnInPlaceRate));
            b.turnReqBam=diff;b.speed=Fixed();
            return;
        }
        if (const auto script=unitScripts_.find(b.id);script!=unitScripts_.end() && !script->second.ready) return;
        work.mission.stage=3;
        work.mission.waitMask=0;
        work.mission.deadline=tickCounter_;
    }
    if (tickCounter_<work.mission.deadline) return;
    if (!reclaimTargetValid(target)) { quit(); return; }
    startWorkAnimation(b,tx,tz);
    if (retailReclaimRemoval(work.mission)) {
        finishReclaim(b,target);
        static const bool kRecLog = tak::devEnv("TAK_BURNLOG") != nullptr;
        if (kRecLog)
            std::fprintf(stderr, "reclaimed %d at %d,%d (tick %u)\n", target, ax, az, tickCounter_);
        quit();
        return;
    }
    if (retailReclaimWork(work.mission,work.reclaimCountdown,tickCounter_).particle)
        emitConstruction(b,false);
    // A wake with countdown left keeps the reclaimer visible for 300 ticks
    // (Reclaim 0x406576, VTOL_Reclaim 0x420b02).
    if (work.reclaimCountdown>0) blockCloak(b,300);
    if (target<0) {
        // 4065xx: a corpse being reclaimed does not decay (object+0x3c reset
        // to decomposetime*30 on every wake).
        Unit* c=unit(-target);
        const int ct=corpseFootprints_.at(c->id).type;
        const int decompose=featTypes_[size_t(ct)].decomposeTicks;
        if (decompose>0 && c->corpseUntil!=INT32_MAX)
            c->corpseUntil=int32_t(std::min(int64_t(INT32_MAX),int64_t(c->deadFor)+decompose));
    }
}

// A mobile builder's guard (Follow_Ground 0x407f90, stage 1; emulated). First
// the guarded unit itself: damaged and repairable -> RepairUnit on it; its own
// unfinished body (HelpBuild eligible) -> HelpBuild on it. Else what it is
// working on now: the target of its MobileBuild / BuildingBuild / HelpBuild,
// or of its RepairUnit. Before both, any guard (armed or not) answers its
// guarded unit's attacker (guardDefend). The job goes AHEAD of the guard, which stays queued
// under it and resumes when the job ends; nothing gates on distance. A look
// that finds nothing sleeps 15 + rand(15) ticks (the mission's own wait; the
// draw is the sim's one game RNG, as every retail mission's is); after an
// insert the guard looks again on its first tick back. A builder still
// walking to its MobileBuild site (no site begun yet) is just followed. The
// ground guard's list is MobileBuild, BuildingBuild, HelpBuild and RepairUnit
// only (0x408431..0x408486): a flying producer's VTOL_MobileBuild, its output
// or its VTOL_RepairUnit is not helped from the ground, though a guarded
// flyer's own unfinished or damaged body is.
//
// A flyer's guard is VTOL_Follow (0x418860, emulated): tickFlyingGuard.
void World::tickGuardHelp(Unit& u) {
    if (u.orders.empty() || !u.type || u.type->isStructure() ||
        u.underConstruction || u.embarked() || u.incapacitated() || u.repeatType ||
        u.buildSiteId || u.repairId || u.reclaimId) return;
    if (u.type->canFly) { tickFlyingGuard(u); return; }
    const size_t end=currentLeg(u.orders);
    if (!u.orders[end].guard) return;
    const Unit* g=unit(u.orders[end].targetId);
    if (!g || !g->alive() || !g->type) return;   // tickCombat retires the guard
    // Stage 1 sleeps on wait mask 0x718: the guarded unit's event 0x10 wakes
    // the look at once, and the look answers it first.
    const bool alarm=guardAlarm(u,*g);
    if (alarm) {
        u.guardHeard=g->attackedTick;
        if (guardDefend(u,*g)) { u.guardHelpTick=tickCounter_+1; return; }
    }
    if (!u.type->isBuilder || (!alarm && tickCounter_<u.guardHelpTick)) return;
    int help=0,mend=0;
    guardHelpPick(u,*g,help,mend);
    if (!help && !mend) { u.guardHelpTick=tickCounter_+15+pathRand(15); return; }
    u.guardHelpTick=tickCounter_+1;
    guardInsertJob(u,help,mend);
}

// The guard's look for a job, Follow_Ground's and VTOL_Follow's alike (see
// above): the guarded unit's repair or its own help, else its build, its
// output or its repair target. The ground guard's list has no VTOL mission.
void World::guardHelpPick(Unit& u, const Unit& g, int& help, int& mend) {
    const bool flying=u.type->canFly;
    // A repair the guard could never walk to (a damaged flyer over water or
    // a cliff) is not taken on: the guard just follows. Retail's RepairUnit
    // has no give-up of its own; this keeps the guard from parking forever.
    // A flyer reaches anything: canRepairUnit already refuses (0x519d40's
    // flying branch) a target under the sea, and as in VTOL_Follow that
    // refusal lets the look go on to the next checks.
    auto reachable=[&](const Unit& t) {
        return flying || pathExists(u.type,t.x.toFloat(),t.z.toFloat(),u.x.toFloat(),u.z.toFloat());
    };
    if (canRepairUnit(u,g)) { if (reachable(g)) mend=g.id; }
    else if (canHelpBuild(u,g)) help=g.id;
    else if ((flying || !g.type->canFly) && g.buildSiteId) {
        if (const Unit* s=unit(g.buildSiteId); s && s->buildBegun && canHelpBuild(u,*s)) help=s->id;
    } else if ((flying || !g.type->canFly) && g.productionSiteId && !g.buildQueue.empty()) {
        if (const Unit* s=unit(g.productionSiteId); s && canHelpBuild(u,*s)) help=s->id;
    } else if ((flying || !g.type->canFly) && g.repairId) {
        if (const Unit* t=unit(g.repairId); t && canRepairUnit(u,*t) && reachable(*t)) mend=t->id;
    }
}

// Follow_Ground's escort ring (0x407f90; emulated reading of stage 0, 0x4087fd,
// and the follow at 0x4087b5): round the ward's position, from the inner
// radius (max(ward footX, footZ) + guard footX + 2) x 16 px out to 3 cells
// more, installed through 4d4e10 as the 4e28f0 ring controller -- the one
// PARK uses -- centred on the guard-footprint origin under the ward. The
// ground guard steers to its point on the guard's own bearing (4e33b0) and
// stands anywhere in the band. Ours walked to the ward's centre and stopped
// within 70 px of it, which for a factory is its yard: the guard parked on
// the pad and the next output could never be placed (BUILD stage 2, 401deb).
// Retail re-aims the ring at each look (15 + rand(15) ticks) and not at all
// round a ward with no mover once the controller has reported; ours re-aims
// it every tick, which for a structure is the same ring.
RetailRingGoal World::guardRing(const Unit& u, const Unit& g) const {
    const int inner=(std::max(g.type->footX,g.type->footZ)+u.type->footX+2)*16;
    const int outer=inner+3*16;
    return {footprintOrigin(g.x,u.type->footX),footprintOrigin(g.z,u.type->footZ),
            inner,outer,retailCircleRadiusSquared(outer)};
}

// The guard's step aside for a job put ahead of it: the guard stays one
// order (its route corners belong to the old walk) at the front.
Order& World::guardStandAside(Unit& u) {
    const size_t end=currentLeg(u.orders);
    u.orders.erase(u.orders.begin(),u.orders.begin()+long(end));
    Order& guard=u.orders.front();
    guard.hasSegment=false;guard.flightGoal.reset();guard.controller=0;
    guard.navigationExhausted=guard.navigationConsumed=false;
    // VTOL_Follow drops its controller (4d4d40(0)) and clears its wait mask
    // before the insert: it wakes only at the dispatcher's sleep.
    if (u.type->canFly) guard.mission.waitMask=0;
    return guard;
}

// The guarded unit's attacker, the first check of a look of Follow_Ground
// (stage 1, 0x40814b) and VTOL_Follow (stage 2, 0x418bb1), run only when the
// look woke on event 0x10 (409a50 on the guarded unit; emulated). The
// attacker is the guarded unit's last (+0xf8, our lastHitBy): alive, not
// dying, not allied to the guard and not of a type in the guard's
// notargetcategory (UnitDef +0x9a). Then 4d7de0(guard, attacker, explicit 1,
// 0, auto 0, 0): with an attack mission for the pair (4de530 kind 3) a plain
// attack goes ahead of the guard -- whatever the guard's Hold Fire or Hold
// Position, with no leash and no return move -- and the guard resumes when
// it ends (the look returns 3: the dispatcher's 15 + rand(30) sleep). Without
// one, a guard not on Hold Fire hands the attacker to its auto slots
// (guardSlotRetarget) and the look goes on. Whether the attack went in.
bool World::guardDefend(Unit& u, const Unit& g) {
    const Unit* a=g.lastHitBy ? unit(g.lastHitBy) : nullptr;
    if (!a || !a->alive() || a->hp<=Fixed() || !a->type || allied(a->player,u.player) ||
        categoryMatch(u.type->noTargetCats,*a->type)) return false;
    if (a->id==u.id || !canAttackTarget(u,*a)) {
        if (u.fireState!=0) guardSlotRetarget(u,*a);
        return false;
    }
    Order& guard=guardStandAside(u);
    if (u.type->canFly) guard.mission.deadline=tickCounter_+15+pathRand(30);
    return retailIssueAutoAttack(u,a->id,false,true);
}

// Puts the guard's job ahead of its guard order, which stays queued under it.
void World::guardInsertJob(Unit& u, int help, int mend) {
    guardStandAside(u);
    std::vector<Order> rest;rest.swap(u.orders);
    cancelPath(u);u.routeStamp=0;
    if (mend) {
        repair(u.id,mend,true);
        if (!u.orders.empty() && u.orders.back().repairTarget==mend) u.orders.back().guardRepair=true;
    } else {
        u.buildSiteId=help;
        u.buildStuckT=0;u.buildStuckD=INT32_MAX;
        helpApproach(u,*unit(help),false);
    }
    u.orders.insert(u.orders.end(),std::make_move_iterator(rest.begin()),std::make_move_iterator(rest.end()));
    noteOrders(u.id);
}

bool World::yieldsToSite(const Unit& k, int player) const {
    if (player<0 || k.player!=player || !k.alive() || !k.type || k.type->isStructure() ||
        k.underConstruction || k.embarked() || k.incapacitated() || k.repeatType ||
        k.buildSiteId || k.repairId || k.reclaimId) return false;
    // Idle (an empty queue) or guarding: a unit already walking somewhere,
    // fighting or working keeps its order, and the builder waits for it as
    // retail's does.
    return k.orders.empty() || k.orders[currentLeg(k.orders)].guard;
}

// A point just outside the rectangle [left,right) x [top,bottom) (world px)
// for `k` to walk to: clear of it by the body's half size plus a final goal's
// arrival circle (tickGroundMission: 16 px or the footprint) and 8. The side
// nearest (rx,rz) first, level with the unit, then sliding along that side a
// body at a time: a point another body stands on, or one in `taken` (points
// already given out, footprint cells), is skipped, or a crowd would each walk
// into the one ahead of it, give the move up and stay. A side marked in
// `avoid` is the last choice. False if no side has a reachable point.
bool World::pointOutside(const Unit& k,int left,int right,int top,int bottom,int rx,int rz,
                         const std::array<bool,4>& avoid,const std::vector<std::array<int,4>>& taken,
                         int& bestX,int& bestZ) const {
    const int kx=k.x.floorInt(),kz=k.z.floorInt();
    const int foot=std::max(k.type->footX,k.type->footZ)*8;
    const int gapX=k.type->footX*8+std::max(16,foot)+8,gapZ=k.type->footZ*8+std::max(16,foot)+8;
    const std::array<std::pair<int,int>,4> sides{{{left-gapX,kz},{right+gapX,kz},{kx,top-gapZ},{kx,bottom+gapZ}}};
    int best=-1;int64_t bestScore=0;
    for (int i=0;i<4;++i)
        for (int slot=0;slot<9;++slot) {
            const int along=(slot+1)/2*(slot%2 ? 1 : -1);
            const int px=sides[size_t(i)].first+(i<2 ? 0 : along*k.type->footX*16);
            const int pz=sides[size_t(i)].second+(i<2 ? along*k.type->footZ*16 : 0);
            if (px<16 || pz<16 || px>=terW_*16-16 || pz>=terH_*16-16) continue;
            const int64_t score=int64_t(px-rx)*(px-rx)+int64_t(pz-rz)*(pz-rz)+
                (avoid[size_t(i)] ? int64_t(1)<<40 : 0);
            if (best>=0 && score>=bestScore) continue;
            const int ox=footprintOrigin(Fixed::fromInt(px),k.type->footX);
            const int oz=footprintOrigin(Fixed::fromInt(pz),k.type->footZ);
            bool clash=false;
            for (const auto& t:taken)
                clash|=ox<t[0]+t[2] && t[0]<ox+k.type->footX && oz<t[1]+t[3] && t[1]<oz+k.type->footZ;
            if (clash) continue;
            const auto bodies=searchBodyRect(ox,oz,k.type->footX,k.type->footZ,k.id);
            if (std::any_of(bodies.cells.begin(),bodies.cells.end(),[](const Unit* b){return b!=nullptr;})) continue;
            if (!k.type->canFly && (!passable(k.type,px/16,pz/16) ||
                !pathExists(k.type,float(px),float(pz),float(kx),float(kz)))) continue;
            best=i;bestX=px;bestZ=pz;bestScore=score;
        }
    return best>=0;
}

// Engine deviation (user-requested, the 0.9.8 doorway report): a ground
// helper walks until its footprint touches the unit it helps (HelpBuild,
// tickGroundHelp), which for a factory's output is inside the factory's
// yard, and retail leaves it standing there when the help ends, where its
// body can hold the door or the next output's pad (BUILD stage 2 never
// shoves it off). A helper whose help has ended with nothing else to do,
// standing in a factory's rectangle, walks out past the nearest side, to
// the point clearBuildSite would send it to.
void World::leaveFactoryYard(int helperId) {
    Unit* k=unit(helperId);
    // Nothing else to do: a repeating or producing conjurer has (and World::order
    // would turn the step-out into its output's rally).
    if (!k || !k->alive() || !k->type || k->type->canFly || !k->orders.empty() || k->embarked() ||
        k->repeatType || k->productionSiteId || !k->buildQueue.empty()) return;
    const int ux=footprintOrigin(k->x,k->type->footX),uz=footprintOrigin(k->z,k->type->footZ);
    const Unit* yard=nullptr;
    forEachNear(k->x.toFloat(),k->z.toFloat(),256.0f,[&](int index) {
        const Unit& f=units_[size_t(index)];
        if (yard || !f.alive() || !f.type || !f.type->isStructure() || !f.type->producesUnits() ||
            f.underConstruction) return;
        const int fx=footprintOrigin(f.x,f.type->footX),fz=footprintOrigin(f.z,f.type->footZ);
        if (ux<fx+f.type->footX && fx<ux+k->type->footX && uz<fz+f.type->footZ && fz<uz+k->type->footZ) yard=&f;
    });
    if (!yard) return;
    const int fx=footprintOrigin(yard->x,yard->type->footX),fz=footprintOrigin(yard->z,yard->type->footZ);
    int px=0,pz=0;
    if (!pointOutside(*k,fx*16,(fx+yard->type->footX)*16,fz*16,(fz+yard->type->footZ)*16,
                      k->x.floorInt(),k->z.floorInt(),{},{},px,pz)) return;
    order(helperId,float(px),float(pz),false);
}

// Engine deviation (user-requested): retail's MobileBuild/FlyingBuild only
// wait out the mobile units on a site, 31 tries 10 ticks apart, and drop the
// order (405824..405911, 41f301..41f3e5); nothing in retail ever moves a
// standing body (BUGGER_OFF is stored and never acted on). Ours, on each of
// those tries, sends the builder player's own idle and guarding units that
// stand on the site to just outside it: past the nearest side for an idle
// unit, past the side nearest the guarded unit for a guard, whose guard
// order stays queued under the move and resumes when it arrives. A side the
// builder stands beyond is the last choice: the builder's body would stop
// the unit in the site, and a point another body stands on, or another
// mover was sent to, gives way to the next one along that side. Other
// players' units, and own units walking, fighting or working, still make
// the builder wait as before.
void World::clearBuildSite(const Unit& builder, const UnitType* type, Fixed x, Fixed z) {
    if (!type) return;
    int left,right,top,bottom;   // the site's rectangle, world px
    const auto blocks=[&](const Unit& k) {
        if (!type->isStructure()) {
            // placementCheck's circle for a mobile site, which counts a
            // flyer in the air too (overlaps), so canPlaceOrder accepted it.
            // In raw 16.16 units: whole-pixel floors would round a body a
            // fraction inside the circle on its low side out of it.
            const int64_t dx=int64_t(k.x.v)-x.v,dz=int64_t(k.z.v)-z.v;
            const int64_t min=int64_t(8*std::max(type->footX,type->footZ)+12)<<16;
            return dx*dx+dz*dz<min*min;
        }
        if (k.type->canFly && k.flightGroundMode!=1) return false;   // searchBodyRect's bodies
        const int cx=footprintOrigin(x,type->footX),cz=footprintOrigin(z,type->footZ);
        const int ux=footprintOrigin(k.x,k.type->footX),uz=footprintOrigin(k.z,k.type->footZ);
        for (int j=std::max(cz,uz);j<std::min(cz+type->footZ,uz+k.type->footZ);++j)
            for (int i=std::max(cx,ux);i<std::min(cx+type->footX,ux+k.type->footX);++i)
                if (type->yardMap.empty() || type->yardMap[size_t(j-cz)*type->footX+i-cx]!='.') return true;
        return false;
    };
    if (type->isStructure()) {
        const int cx=footprintOrigin(x,type->footX),cz=footprintOrigin(z,type->footZ);
        left=cx*16;right=(cx+type->footX)*16;top=cz*16;bottom=(cz+type->footZ)*16;
    } else {
        const int min=8*std::max(type->footX,type->footZ)+12;
        left=x.floorInt()-min;right=x.floorInt()+min;top=z.floorInt()-min;bottom=z.floorInt()+min;
    }
    std::vector<int> movers;
    for (const auto& k:units_)
        if (k.id!=builder.id && yieldsToSite(k,builder.player) && blocks(k)) movers.push_back(k.id);
    // Points already given to a mover in this pass, in footprint cells.
    std::vector<std::array<int,4>> taken;
    for (int id:movers) {
        Unit& k=*unit(id);
        const Unit* guarded=k.orders.empty() ? nullptr : unit(k.orders[currentLeg(k.orders)].targetId);
        const int bx=builder.x.floorInt(),bz=builder.z.floorInt();
        const std::array<bool,4> builderSide{bx<left,bx>=right,bz<top,bz>=bottom};
        const int rx=guarded ? guarded->x.floorInt() : k.x.floorInt(),rz=guarded ? guarded->z.floorInt() : k.z.floorInt();
        int bestX=0,bestZ=0;
        if (!pointOutside(k,left,right,top,bottom,rx,rz,builderSide,taken,bestX,bestZ)) continue;
        taken.push_back({footprintOrigin(Fixed::fromInt(bestX),k.type->footX),
                         footprintOrigin(Fixed::fromInt(bestZ),k.type->footZ),k.type->footX,k.type->footZ});
        const float px=float(bestX),pz=float(bestZ);
        if (k.orders.empty()) {order(id,px,pz,false);continue;}
        OrdersNote note{*this,id};
        guardStandAside(k);
        std::vector<Order> rest;rest.swap(k.orders);
        cancelPath(k);k.routeStamp=0;
        order(id,px,pz,false);
        Unit& moved=*unit(id);
        moved.orders.insert(moved.orders.end(),std::make_move_iterator(rest.begin()),std::make_move_iterator(rest.end()));
    }
}

// A flyer's guard: VTOL_Follow (0x418860, emulated), the guard mission of
// every flyer (the order dispatch 0x4de530 picks it by canfly alone, armed
// or builder). Each look:
//  - off the map (unit +0xa4 is the off-map cell): a point 800 px from the
//    flyer toward the map (clamped 192 px inside), accepting at 128, and a
//    wait for that controller's events only (4188bc..4189b9);
//  - stage 0 (41918b): Activate and BeginFlight (416c50), the orbit angle
//    rand(0x10000); stage 1 (519b10(3), World's slot mode) and stage 2 follow
//    in the same dispatch, with no events;
//  - stage 2: the guard's help and repair look (guardHelpPick, a builder's;
//    canHelpBuild / canRepairUnit refuse any other flyer), Follow_Ground's
//    checks in the same order with the VTOL build and repair missions added
//    to the guarded unit's list, so a flying guard helps ground and flying
//    producers alike. Every target counts as reachable, but VTOL_RepairUnit
//    refuses one whose model top is under the sea (0x519d40, flying branch).
//    Its jobs are VTOL_HelpBuild / VTOL_RepairUnit (approach to 2 x
//    builddistance, then orbit; tickConstruction, tickRepair). A job goes
//    ahead of the guard, and the guard sleeps 15 + rand(30) ticks (an insert
//    returns 3, which the dispatcher 0x4d8511 turns into that sleep);
//  - else the idle orbit (419053): on a controller event (0x100 arrival, 0x400
//    release) the angle steps back by 0x4000 + rand(0x2000); a point
//    controller (flags 0x30) accepting at 128 px goes to the guarded unit's
//    position less (sin, cos)(angle) x radius -- 320 px, or the active
//    weapon's range + 160 when armed (unit +0x130 0x8000000; derived from the
//    type's weapons, as the AI port does); wait mask 0x718, and a look again
//    after a fixed 15 ticks (no draw). The point follows the guarded unit
//    from look to look; reaching it wakes the look at once.
//  - first in stage 2, a look woken by the guarded unit's event 0x10 answers
//    its attacker (guardDefend), before the help and repair look;
// Not ported: the landed step-out (unit +0x12a >= 5) and the stage-2 attack
// scan (51d1e0), whose work World's target acquisition does for a guard.
void World::tickFlyingGuard(Unit& u) {
    const size_t end=currentLeg(u.orders);
    if (!u.orders[end].guard) return;
    const Unit* g=unit(u.orders[end].targetId);
    if (!g || !g->alive() || !g->type) return;   // tickCombat retires the guard
    Order& o=u.orders[end];
    // The dispatcher's wake: a new mission at once, then the look's deadline
    // (the mission's own, so a later guard never inherits it) or a controller
    // event the mission waits on.
    const uint32_t events=(o.mission.pending|(guardAlarm(u,*g) ? 0x10u : 0u))&o.mission.waitMask&0x710u;
    const bool fresh=o.mission.stage==0 && !o.mission.waitMask;
    if (!fresh && tickCounter_<o.mission.deadline && !events) return;
    o.mission.pending&=~events;
    if (events&0x10u) u.guardHeard=g->attackedTick;
    o.mission.waitMask=0;
    auto install=[&](RetailFlightVector point) {
        // 4e40e0 + 4e4540(0x80); 4d4d40 clears the pending controller events.
        o.flightGoal=RetailFlightGoal{point,0x30,0,128};
        o.mission.pending&=~0x3700u;
    };
    const RetailFlightVector position{u.x.v,u.flightY.v,u.z.v};
    // 5066f0: off the map when the footprint does not fit the cell grid.
    const int ox=footprintOrigin(u.x,u.type->footX),oz=footprintOrigin(u.z,u.type->footZ);
    if (!(ox>=0 && oz>=0 && ox+u.type->footX<hW_ && oz+u.type->footZ<hH_)) {
        RetailFlightVector inside=position;
        auto clamp=[](int32_t& v,int32_t size) {
            if (v<0xc00000) v=0xc00000;
            else if (const int32_t edge=std::bit_cast<int32_t>(uint32_t(size+0xff40)<<16);v>edge) v=edge;
        };
        // 50ef03: the playable map is 32 px narrower and 128 px shorter.
        clamp(inside.x,hW_*16-32);clamp(inside.z,hH_*16-128);
        // 51b340(unit, point): the heading of the unit as seen from the point.
        const uint16_t heading=uint16_t(retailDirection(
            Fixed::raw(std::bit_cast<int32_t>(uint32_t(position.x)-uint32_t(inside.x))),
            Fixed::raw(std::bit_cast<int32_t>(uint32_t(position.z)-uint32_t(inside.z)))).v);
        install(retailAirRunPoint(position,position.y,heading,800));
        o.mission.waitMask=0x700;
        o.mission.deadline=0xffffffffu;
        return;
    }
    bool woken=(events&0x700u)!=0,attacked=(events&0x10u)!=0;
    if (o.mission.stage==0) {
        if (auto script=unitScripts_.find(u.id);script!=unitScripts_.end() && !script->second.activated) {
            script->second.activated=true;notifyUnitScript(u,"Activate");
        }
        notifyUnitScript(u,"BeginFlight");
        o.guardOrbitAngle=uint16_t(pathRand(0x10000));
        o.mission.stage=2;
        woken=attacked=false;
    }
    if (attacked && guardDefend(u,*g)) return;   // `o` is not read past this point
    int help=0,mend=0;
    if (u.type->isBuilder) guardHelpPick(u,*g,help,mend);
    if (help || mend) {
        o.mission.deadline=tickCounter_+15+pathRand(30);
        guardInsertJob(u,help,mend);   // `o` is not read past this point
        return;
    }
    if (woken) o.guardOrbitAngle=uint16_t(o.guardOrbitAngle-0x4000u-pathRand(0x2000));
    // 519ac0: a switcher's selected slot, otherwise the first slot.
    int32_t radius=320;
    if (!u.type->weapons.empty()) {
        const int slot=u.type->weaponSwitching
            ? std::clamp(u.weaponSlot,0,int(std::min<size_t>(3,u.type->weapons.size()))-1) : 0;
        radius=u.type->weapons[size_t(slot)].range+160;
    }
    const int32_t gy=(g->type->canFly && g->flightGroundMode==2 ? g->flightY : g->groundY).v;
    install(retailAirRunPoint({g->x.v,gy,g->z.v},gy,o.guardOrbitAngle,uint32_t(std::max(0,radius))));
    o.mission.waitMask=0x718;
    o.mission.deadline=tickCounter_+15;
}

void World::tickPatrolRepair(Unit& b) {
    if (!patrolRepairs_ || !b.type || !b.type->isBuilder || !b.type->canMove || b.type->isStructure() ||
        b.type->workerTime<=0 || b.underConstruction || b.embarked() || b.incapacitated() ||
        b.buildSiteId || b.repairId || b.reclaimId || b.repeatType || !b.buildQueue.empty() ||
        hasQueuedWork(b) || b.orders.empty() || !b.orders.front().patrol ||
        (uint32_t(b.id)+tickCounter_)%30!=0) return;
    const float radius=std::max(0,sightDistance(*b.type));
    const float x=b.x.toFloat(),z=b.z.toFloat();
    float bestDistance=radius*radius;int target=0;
    forEachNear(x,z,radius,[&](int index) {
        const auto& t=units_[size_t(index)];
        if (!t.alive() || !t.type || t.id==b.id || t.underConstruction || t.embarked() ||
            !allied(b.player,t.player) || t.hp<=Fixed() || t.hp>=Fixed::fromInt(t.type->maxHp)) return;
        const float dx=t.x.toFloat()-x,dz=t.z.toFloat()-z,d=dx*dx+dz*dz;
        if (d>bestDistance || (d==bestDistance && target && t.id>target)) return;
        if (b.type->canFly ? !repairTopAboveSea(t) : !pathExists(b.type,t.x.toFloat(),t.z.toFloat(),x,z)) return;
        bestDistance=d;target=t.id;
    });
    if (!target) return;
    // Retain the patrol's canonical point, without its old route corners. The
    // resumed patrol gets a fresh controller and keeps all following waypoints.
    const size_t end=currentLeg(b.orders);
    Order patrol=b.orders[end];
    if (patrol.missionTarget) {patrol.x=patrol.missionTarget->first;patrol.z=patrol.missionTarget->second;}
    const uint32_t flags=patrol.mission.flags;
    patrol.mission={};patrol.mission.flags=flags;patrol.controller=0;
    patrol.navigationExhausted=patrol.navigationConsumed=false;patrol.hasSegment=false;
    patrol.flightGoal.reset();
    cancelPath(b);b.routeStamp=0;
    b.orders.erase(b.orders.begin(),b.orders.begin()+long(end));b.orders.front()=patrol;
    const size_t oldSize=b.orders.size();
    repair(b.id,target,true);
    if (b.orders.size()>oldSize) {
        auto& job=b.orders.back();job.patrolRepair=true;job.clickX=b.x;job.clickZ=b.z;
        std::rotate(b.orders.begin(),b.orders.end()-1,b.orders.end());
    }
}

void World::repair(int builderId, int targetId, bool queue) {
    OrdersNote note{*this,builderId};
    Unit* b = unit(builderId);
    Unit* t = unit(targetId);
    // RepairUnit stage 0 ends at once for a unit with no mover (0x407289):
    // a building never repairs, whatever its canmove says (the Keep's is 1).
    if (!b || !b->alive() || !b->type || !b->type->isBuilder || !b->type->canMove || b->type->isStructure()) return;
    if (!t || !t->alive() || !t->type || t->id == b->id || t->underConstruction ||
        !allied(t->player, b->player) || t->hp >= Fixed::fromFloat(t->type->maxHp))
        return;
    // VTOL_RepairUnit's stage 0 (0x519d40, flying branch): never under the sea.
    if (b->type->canFly && !repairTopAboveSea(*t)) return;
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
        stopWorkAnimation(b);
        if (!b.orders.empty()) {
            if (b.orders[currentLeg(b.orders)].patrolRepair) dropLeg(b);
            else if (b.orders.front().repairTarget) b.orders.erase(b.orders.begin());
        }
        b.repairId = 0;
    };
    if (!b.type) { endRepair(); return; }
    // `t->hp <= Fixed()` is NOT covered by alive(): that reads deadFor, which is only set
    // by the death sweep in tick(). A unit whose hp has already reached zero this
    // tick still looks alive here, and tickRepair runs BEFORE the sweep -- so
    // without this a builder repairs a unit back off zero and it never dies.
    if (!t || !t->alive() || !t->type || t->hp <= Fixed() || t->underConstruction ||
        t->embarked() || !allied(t->player, b.player) || t->hp >= Fixed::fromFloat(t->type->maxHp)) {
        if (!b.type->canFly) {
            // A ground repair's leg is the job: it goes, whatever ended it.
            stopWorkAnimation(b);
            if (!b.orders.empty() && b.orders[currentLeg(b.orders)].repairTarget==b.repairId) dropLeg(b);
            b.repairId = 0;
            return;
        }
        endRepair();
        return;
    }
    if (!b.type->canFly) { tickGroundRepair(b,*t,dt); return; }
    if (!b.orders.empty()) {
        const auto& job=b.orders[currentLeg(b.orders)];
        if (job.patrolRepair) {
            const float lx=(t->x-job.clickX).toFloat(),lz=(t->z-job.clickZ).toFloat();
            const float leash=std::max(0,sightDistance(*b.type));
            if (lx*lx+lz*lz>leash*leash) {endRepair();return;}
        }
    }
    tickFlyingRepair(b,*t,dt);
}

// RepairUnit (0x407190) for a ground builder, on its leg (emulated 2026-10-30).
// Stage 0 (0x407289) zeroes the give-up count and stage 1 (0x4072ec,
// installRepairApproach) keeps where the target stands, installs the
// rectangle controller round its footprint and sleeps 7 + rand(15) ticks or
// until the approach reports (mask 0x708). Stage 2 (0x40733b) is the look:
//  - the target moving (its MoveRate tier): 1 on the count, 3 when it is in
//    builddistance reach (retailBuildWithinReach); past 10 the repair ends
//    (REPAIRSUNSUCCESSFULL), else the approach is re-aimed after sleeping
//    7 + rand(15) ticks in reach, 1 out of it;
//  - an approach event (arrival, or failure): in reach the work starts, out
//    of reach the approach is re-aimed at once -- a still target is never
//    given up on;
//  - the timer: re-aimed only if the target's cell changed since the install.
// Stages 3 and 4 work every tick and measure no distance (a bumped repairer
// keeps repairing). A target that starts moving stops the work and the
// stance (0x4d4b10), adds 3 and re-aims next tick: no repair is ever done to
// a moving target. A patrol's detour has a leash (mission +0x56: the
// builder's sightdistance from where it left the patrol), checked on every
// wake: the repair ends silently once the builder itself is that far from
// there. Ours walked to where the target stood at the order, repaired from
// builddistance while it walked, and re-aimed only a patrol's or guard's
// repair, once a second; the leash measured the target, past it.
// Unit chatter has no channel here: the give-ups are silent.
void World::tickGroundRepair(Unit& b, Unit& t, float dt) {
    if (b.orders.empty() || b.orders[currentLeg(b.orders)].repairTarget!=b.repairId) {
        // Another order is current (one pushed ahead of the job, or a stale
        // latch): the job starts again from its approach when its leg is.
        stopWorkAnimation(b);
        for (auto& o:b.orders)
            if (o.repairTarget==b.repairId) {
                o.buildRectangle.reset();o.controller=0;o.mission.stage=0;
                o.mission.waitMask=0;o.mission.pending=0;o.mission.deadline=0xffffffffu;
                o.navigationExhausted=o.navigationConsumed=false;o.hasSegment=false;
                break;
            }
        b.repairId=0;
        return;
    }
    size_t index=currentLeg(b.orders);
    Order* leg=&b.orders[index];
    if (leg->mission.stage==0) return;   // prepareRepairApproach, on this unit's dispatch
    const auto end=[&] {
        stopWorkAnimation(b);
        dropLeg(b);
        b.repairId=0;
    };
    // +0x56: only a patrol's detour has a leash (0 = none).
    const auto leashed=[&] {
        const int32_t leash=sightDistance(*b.type);
        if (!leg->patrolRepair || leash<=0) return false;
        const RetailGroundPoint here{int16_t(b.x.floorInt()),int16_t(b.z.floorInt())};
        const RetailGroundPoint origin{int16_t(leg->clickX.floorInt()),int16_t(leg->clickZ.floorInt())};
        return retailGroundDistance(here,origin)>=uint32_t(leash);
    };
    if (leg->mission.stage<4) {
        auto& m=leg->mission;
        if (tickCounter_>=m.deadline) { m.deadline=0xffffffffu;m.pending|=1; }
        const uint32_t events=(b.missionEvents|m.pending)&m.waitMask;
        if (!events) return;
        b.missionEvents&=~events;m.pending&=~events;m.waitMask=0;
        if (leashed()) { end(); return; }
        if (m.stage==1) { installRepairApproach(b,*leg,t); return; }
        const bool inReach=retailBuildWithinReach(b.x,b.z,t.x,t.z,int16_t(b.type->footX),int16_t(b.type->footZ),
            int16_t(t.type->footX),int16_t(t.type->footZ),uint16_t(b.type->buildDist));
        if (moveRateMoving(t)) {
            if (++leg->repairLooks>10) { end(); return; }
            if (inReach) { leg->repairLooks+=2;m.sleep(tickCounter_,7+pathRand(15)); }
            else m.sleep(tickCounter_,1);
            m.stage=1;
            return;
        }
        if (!(events&0x700)) {
            // The timer: a target still in the cell it was approached in
            // keeps the approach (0x407588).
            if (footprintOrigin(leg->buildX,t.type->footX)!=footprintOrigin(t.x,t.type->footX) ||
                footprintOrigin(leg->buildZ,t.type->footZ)!=footprintOrigin(t.z,t.type->footZ))
                installRepairApproach(b,*leg,t);
            else { m.waitMask=0x700;m.sleep(tickCounter_,7+pathRand(15)); }
            return;
        }
        if (!inReach) { installRepairApproach(b,*leg,t); return; }
        // Arrived (or failed in reach): 4d4d40(0) drops the controller, and
        // the route's corners with it; the leg is the job from here.
        cancelPath(b);
        if (index) { b.orders.erase(b.orders.begin(),b.orders.begin()+long(index));index=0;leg=&b.orders.front(); }
        leg->controller=0;leg->navigationExhausted=true;
        leg->mission.stage=4;
        // Native RepairUnit requests the build stance once it reaches the
        // target (40749b -> 4d4ab0), before facing and before any affordable
        // repair work.
        startWorkAnimation(b,t.x,t.z);
    } else {
        if (leashed()) { end(); return; }
        if (moveRateMoving(t)) {
            stopWorkAnimation(b);
            leg->repairLooks=uint8_t(std::min(255,leg->repairLooks+3));
            leg->mission.stage=1;leg->mission.sleep(tickCounter_,1);
            return;
        }
        startWorkAnimation(b,t.x,t.z);
    }
    b.speed = Fixed();
    const float dx = (t.x - b.x).toFloat(), dz = (t.z - b.z).toFloat();
    const Bam want = fxAtan2(Fixed::fromFloat(dx), Fixed::fromFloat(dz));
    // Stopped: pivot at turninplacerate, not the moving turn rate.
    const int32_t bPivotMax = b.type->turnInPlaceRate;
    const int32_t diff = bamDiff(want, b.heading);
    b.heading = b.heading + Bam(std::clamp(diff, -bPivotMax, bPivotMax));
    b.turnReqBam = diff;   // builder pivot request -> TurnDirection anim (icd 0x4d9593)
    // RepairUnit's work stage keeps a ground repairer visible for 150 ticks on
    // every work tick, before the repair is paid (icd 0x4076ae, then 0x429d90).
    blockCloak(b, 150);
    if (payRepair(b,t,dt)) end();   // mended: on to the next order
}

// RepairUnit stage 1 (0x4072ec): where the target stands now (+0x22), the
// rectangle controller round its footprint (+0x74 cell, +0x78 footprint, the
// one HelpBuild and MobileBuild use: the repairer walks until its footprint
// touches the target's), and a sleep of 7 + rand(15) ticks under mask 0x708.
void World::installRepairApproach(Unit& u, Order& goal, const Unit& t) {
    goal.buildX=t.x;goal.buildZ=t.z;
    goal.buildRectangle=retailReclaimRectangle(footprintOrigin(t.x,t.type->footX),footprintOrigin(t.z,t.type->footZ),
                                               t.type->footX,t.type->footZ,u.type->footX,u.type->footZ);
    const auto [x,z]=goal.buildRectangle->navigationCell(
        footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ));
    goal.x=Fixed::fromInt(x*16+u.type->footX*8);
    goal.z=Fixed::fromInt(z*16+u.type->footZ*8);
    goal.segmentX=Fixed::fromInt(u.x.floorInt());
    goal.segmentZ=Fixed::fromInt(u.z.floorInt());
    goal.hasSegment=true;
    goal.mission.stage=2;
    goal.mission.waitMask=0x700;
    goal.mission.sleep(tickCounter_,7+pathRand(15));
    goal.controller=++nextMovementController_;
    goal.navigationExhausted=goal.navigationConsumed=false;
    goal.mission.pending&=~0x3700u;
    if (u.routeStamp<0 || uint32_t(u.routeStamp)<=tickCounter_-6u) u.routeStamp=0;
    cancelPath(u);
    requestPath(u,goal.x.toFloat(),goal.z.toFloat());
}

// RepairUnit stage 0 (0x407289) once the leg is current: the give-up count
// starts at 0, and stage 1 installs the approach on the same dispatch.
void World::prepareRepairApproach(Unit& u) {
    if (u.orders.empty() || !u.type || u.type->canFly || !u.repairId) return;
    Order& goal=u.orders[currentLeg(u.orders)];
    if (goal.repairTarget!=u.repairId || goal.mission.stage!=0) return;
    const Unit* t=unit(goal.repairTarget);
    if (!t || !t->type) return;   // tickRepair ends the job
    goal.repairLooks=0;
    installRepairApproach(u,goal,*t);
}

bool World::moveRateMoving(const Unit& t) const {
    const UnitScript* script=unitScript(t.id);
    return script ? script->movementRate!=0 : t.speed.v!=0;
}

// One tick of repair work, paid from the owner's pool; true once the target
// is at full health.
bool World::payRepair(Unit& b, Unit& t, float dt) {
    float total = t.type->buildTime / std::max(b.type->workerTime, 0.01f);
    Player& tm = players_[size_t(b.player)];
    float cost = t.type->buildCost * dt / std::max(total, 0.01f);
    const double share = tm.constructionShare(cost);
    if (share <= 0) return false;   // can't afford: pause the repair
    tm.payMana(std::min(tm.mana, double(cost) * share));
    float mend = t.type->maxHp * dt / std::max(total, 0.01f);
    if (share < 1) mend = float(double(mend) * share);
    t.hp = fxMin(Fixed::fromFloat(t.type->maxHp), t.hp + Fixed::fromFloat(mend));
    // Native RepairUnit 4076d0..4076f0 emits only after successful repair:
    // falling particles at the worker and rising particles at the target.
    emitConstruction(b, false);
    emitConstruction(t, true);
    return t.hp >= Fixed::fromFloat(t.type->maxHp);
}

// VTOL_RepairUnit (0x420e60; emulated). Stage 1 flies one point goal at the
// target that accepts strictly inside 2 x builddistance, centre to centre
// (no footprint half, no floor); arrival drops the weapon targets, faces the
// target and starts the build animation, then the flyer orbits it at
// builddistance -/+ 7 like VTOL_HelpBuild (tickConjureHover, with the repair
// re-pick rule). It works while the integer pixel distance is at most
// 2 x builddistance; beyond that the mission restarts and flies back in. No
// cloak block (that is the ground RepairUnit's).
void World::tickFlyingRepair(Unit& b, Unit& t, float dt) {
    auto endRepair = [&] {
        stopWorkAnimation(b);
        if (!b.orders.empty()) {
            if (b.orders[currentLeg(b.orders)].patrolRepair) dropLeg(b);
            else if (b.orders.front().repairTarget) b.orders.erase(b.orders.begin());
        }
        b.repairId = 0;
    };
    const int reach=2*std::max(0,int(b.type->buildDist));
    const Fixed ddx=t.x-b.x,ddz=t.z-b.z;
    const bool admitted=b.conjureHoverTarget==t.id;
    const double fdx=double(ddx.v)/65536.0,fdz=double(ddz.v)/65536.0;
    // Stage 5 (0x42139b) measures whole pixels: the integer parts of both
    // positions (+0x6a, +0x72), a square root truncated to an integer.
    const int64_t px=int64_t(t.x.floorInt())-b.x.floorInt(),pz=int64_t(t.z.floorInt())-b.z.floorInt();
    const bool inReach=admitted ? isqrt64(uint64_t(px*px+pz*pz))<=uint64_t(reach)
                                : fdx*fdx+fdz*fdz<double(reach)*reach;
    if (!inReach) {
        if (admitted) {
            stopWorkAnimation(b);
            b.conjureHoverTarget=0;b.conjureHoverGoal.reset();
        }
        // Out of reach the mission restarts at stage 0, which checks 0x519d40
        // again: a target now under the sea ends the job.
        if (!repairTopAboveSea(t)) { endRepair(); return; }
        // The approach: the repair entry itself, re-aimed at the target.
        if (b.orders.empty()) { repair(b.id,t.id,false); if (b.orders.empty()) { b.repairId=0; return; } }
        const size_t end=currentLeg(b.orders);
        Order& job=b.orders[end];
        job.x=t.x;job.z=t.z;
        if (!job.flightGoal || job.flightGoal->flags!=0x30 || job.flightGoal->radius!=reach)
            job.flightGoal=RetailFlightGoal{{t.x.v,0,t.z.v},0x30,0,int16_t(reach)};
        return;
    }
    // 421218: 519b70(3) drops every weapon target, then the work stance
    // (startWorkAnimation does both).
    startWorkAnimation(b,t.x,t.z);
    if (!admitted) {
        // A patrol detour or a guard's repair keeps its own entry as the job.
        if (!b.orders.empty() && (b.orders[currentLeg(b.orders)].patrolRepair ||
                                  b.orders[currentLeg(b.orders)].guardRepair)) {
            const size_t end=currentLeg(b.orders);
            if (b.orders[end].repairTarget==b.repairId)
                b.orders.erase(b.orders.begin(),b.orders.begin()+long(end));
        }
    }
    if (!b.orders.empty() && b.orders.front().repairTarget==t.id) b.orders.front().flightGoal.reset();
    b.constructionHolding=true;
    tickConjureHover(b,t,true);
    if (payRepair(b,t,dt)) endRepair();
}

void World::startEmote(int player, int unitId, bool disco, bool append) {
    OrdersNote note{*this,unitId};
    if (player < 0 || player >= int(players_.size())) return;
    const auto* u = unit(unitId);
    if (!u || !u->alive() || u->player != player || !u->type || u->type->isStructure()) return;
    auto& p = players_[size_t(player)];
    if (!append || !p.emoteSequence || p.emoteDisco != disco) {
        p.emoteUnits.clear();
        ++p.emoteSequence;
        p.emoteDisco = disco;
    }
    // Large selections may arrive over several server ticks. They join one
    // trigger; only the viewer's real-time clock controls its duration.
    p.emoteUnits.insert(unitId);
}

void World::startDisco(int player, int unitId, bool append) { startEmote(player, unitId, true, append); }
void World::startHeadbang(int player, int unitId, bool append) { startEmote(player, unitId, false, append); }

bool World::discoTarget(int player, int unitId) const {
    return player >= 0 && player < int(players_.size()) &&
           players_[size_t(player)].emoteSequence && players_[size_t(player)].emoteDisco &&
           (!unitId || players_[size_t(player)].emoteUnits.contains(unitId));
}

bool World::headbangTarget(int player, int unitId) const {
    return player >= 0 && player < int(players_.size()) &&
           players_[size_t(player)].emoteSequence && !players_[size_t(player)].emoteDisco &&
           (!unitId || players_[size_t(player)].emoteUnits.contains(unitId));
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
    if (rising && u.lodestoneReplacement && u.underConstruction)
        rising=u.constructionFraction()>=0.5f;
    ++u.constructionEmissions[rising ? 1 : 0];
    const UnitType* effectType=u.lodestoneReplacement && u.underConstruction &&
        u.constructionFraction()<0.5f ? u.lodestoneReplacement->type : u.type;
    if (!u.constructionEmitter) {
        if (!u.cosmeticConstructionEmitter) {
            u.cosmeticConstructionEmitter.emplace();
            u.constructionVisualRandom=uint32_t(u.id);
        }
        auto& emitter=*u.cosmeticConstructionEmitter;
        const double halfX=double(effectType->footX)*8*65536;
        const double halfZ=double(effectType->footZ)*8*65536;
        emitter.radius=int32_t(effectType->buildMovementCode==0 ? std::min(halfX,halfZ) :
            std::sqrt(halfX*halfX+halfZ*halfZ)+0.5);
        emitter.height=effectType->modelTop;
        emitter.capacity=uint32_t(std::max(0,emitter.radius>>16))/(effectType->buildMovementCode==1 ? 4u : 1u);
        const size_t before=emitter.particles.size();
        // 4f1310: the rising ceiling is the owner's own world Y (unit+0x6c) plus
        // the height -- the same origin the client draws the particles from.
        const int32_t ownerY=(u.type->canFly ? u.flightY : u.groundY).v;
        u.cosmeticConstructionEmitter->emit(1,ownerY,rising,[&] {
            u.constructionVisualRandom=u.constructionVisualRandom*0x343fdu+0x269ec3u;
            return (u.constructionVisualRandom>>16)&0x7fffu;
        });
        if (effectType!=u.type) for (size_t i=before;i<emitter.particles.size();++i) {
            emitter.particles[i].x+=u.lodestoneReplacement->x.v-u.x.v;
            emitter.particles[i].z+=u.lodestoneReplacement->z.v-u.z.v;
        }
        return;
    }
    // Captured-retail restore path (hashed): still the nearest height corner, not
    // the owner's world Y the cosmetic emitter uses above.
    const int x=std::clamp(u.x.floorInt()/16,0,std::max(0,terW_-1));
    const int z=std::clamp(u.z.floorInt()/16,0,std::max(0,terH_-1));
    const int32_t y=u.type->canFly ? u.flightY.v :
        int32_t(heights_.empty() ? 0 : heights_[size_t(z)*terW_+x])*65536;
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
                setOccupant(size_t(z+dz)*occW_+x+dx,site.id);
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
                    if (!builder->rally.empty())
                        throw std::runtime_error("factory rally completion host is not ported");
                    const uint32_t first=random(6),second=random(3);
                    if (u.type->canFly) { w.queueFlyerPark(u.id,builder->id,(first+second+6)*16); return 5; }
                    Order order;order.goal=order.groundMission=true;order.park.emplace();
                    order.park->target=builder->id;
                    order.mission.flags=0x200u; // PARK definition retains its live target.
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
            if (!job.flying) {
                job.activityDeadline=std::max(job.activityDeadline,w.tickCounter_+300u);
                w.blockCloak(b,300);   // the same +0xcc, on the unit (0x405bc8)
            }
            if (state.progress.remaining==0) return 1;
            m.sleep(w.tickCounter_,1);m.waitMask|=0xa;
            if (job.flying) { m.stage=5;return 4; }
            return 2;
        }
    } host{*this,builder};
    retailDispatchMissions(tickCounter_,builder.missionEvents,host);
    noteOrders(builder.id);
}

void World::tickHoverAttack(Unit& u,const Unit& target,const Weapon* weapon,int slot) {
    auto& order=u.orders.front();
    const int distance=weapon && weapon->hoverAttackDistance ? weapon->hoverAttackDistance : u.type->hoverAttackDistance;
    const int altitude=weapon && weapon->hoverAttackAltitude ? weapon->hoverAttackAltitude : u.type->hoverAttackAltitude;
    if (!order.flightGoal || !order.hoverAttackRefresh || tickCounter_>=order.hoverAttackRefresh) {
        // 41de3f: the ring is drawn around the selected weapon's led SweetSpot
        // (51aa50), taken afresh at every mission wake.
        const auto centre=retailLeadPoint(u,target,weapon);
        order.flightGoal=retailHoverAttackGoal({u.x.v,u.flightY.v,u.z.v},
            {centre[0],centre[1],centre[2]},distance,[&](int n) {return pathRand(uint32_t(n));});
        order.hoverAttackRefresh=tickCounter_+15+pathRand(15);
        // Engine deviation: shots from here keep stopping on something the
        // weapon cannot destroy (noteShotImpact): move round the ring.
        if (shotBlockedHere(u,target)) hoverForShot(u,target,slot,centre,distance,altitude);
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
    // The target controller (4e4140, flags 0xa3) heads the flyer at the
    // target's SweetSpot, unled (4e4330 -> 4dd4f0, 51b340), while 52fff0
    // holds fire until the body is within tolerance of the led one.
    const auto sweetSpot=queryUnitScriptPoint(target.id,true);
    goal.heading=uint16_t(retailDirection(Fixed::raw(std::bit_cast<int32_t>(uint32_t(u.x.v)-uint32_t(sweetSpot[0]))),
                                          Fixed::raw(std::bit_cast<int32_t>(uint32_t(u.z.v)-uint32_t(sweetSpot[2])))).v);
    order.x=Fixed::raw(goal.point.x);order.z=Fixed::raw(goal.point.z);
    tickFlightMovement(u,true);
    notifyFlightOccupancy(u);
}

// AirToGround (41bf80, retailAirToGround): the mission of a flyer without
// hoverattack attacking a target that cannot fly. It runs on the attack
// order's own state, then the mover: a point controller is let go on arrival
// (524c10 posts 0x100, the navigator's release 0x400), and the body flies on
// to the destination it left until the mission installs the next one.
void World::tickAirToGround(Unit& u,const Unit& target,const Weapon* selected) {
    // 519ac0: a switcher's selected slot, otherwise the first slot.
    const Weapon* weapon=u.type->weaponSwitching ? selected
                       : u.type->weapons.empty() ? nullptr : &u.type->weapons.front();
    struct Host {
        World& w; Unit& u; const Unit& target; const Weapon* weapon;
        bool enabled() const { return u.alive() && !u.incapacitated() && !u.underConstruction && !u.embarked(); }
        RetailMissionState* head() {
            return !u.orders.empty() && u.orders.front().targetId==target.id ? &u.orders.front().mission : nullptr;
        }
        bool hasNext(const RetailMissionState&) const { return u.orders.size()>1; }
        void idle() {}
        uint32_t random(int n) { return w.pathRand(uint32_t(n)); }
        void remove(RetailMissionState&) {
            if(u.scriptAimTarget)w.clearScriptWeaponTarget(u);
            w.dropLeg(u);w.cancelPath(u);u.routeStamp=-1;
        }
        void rotate(RetailMissionState&) {}
        void clear() { w.cancelPath(u);u.orders.clear();u.routeStamp=-1; }
        void beginFlight() {
            // 416c50: Activate, BeginFlight, and take-off (the mover below).
            if (auto script=w.unitScripts_.find(u.id);script!=w.unitScripts_.end() && !script->second.activated) {
                script->second.activated=true;w.notifyUnitScript(u,"Activate");
            }
            w.notifyUnitScript(u,"BeginFlight");
            // Retail posts controller events to the controller's mission
            // (4e2470), so a new mission starts with none. World's coast after
            // a move posts them to the unit (tickRetainedFlightMovement); they
            // must not wake this mission's first controller.
            u.missionEvents&=~0x3700u;
            // 416c57: 519b70(3), every slot manual.
            for (int s=0;s<3;++s) w.setSlotMode(u,s,false);
        }
        // Stage 1 (41c308): 519b10(3), every slot automatic. Stage 2
        // (41c31f, and again whenever stage 7 sends the run back to it):
        // 519b70 makes the selected slot (519ac0) manual and 51a700 gives it
        // the target (51a770, a ground point, has no World counterpart: an
        // attack order here always has a unit). The other slots pick their
        // own targets (the sweep, tickSlotWeapons) for the rest of the
        // mission, until its destructor's 519b10(3) (4d6da0).
        void slotsAuto() {
            for (int s=0;s<3;++s) w.setSlotMode(u,s,true);
            u.orders.front().mixedSlots=true;
        }
        void aimSlot() {
            const int selected=u.type->weaponSwitching ? std::clamp(u.weaponSlot,0,2) : 0;
            w.setSlotMode(u,selected,false);
            w.setSlotTarget(u,selected,target.id);
        }
        void install(const RetailFlightGoal& goal) {
            // 4d4d40 also clears the mission's pending controller events.
            auto& order=u.orders.front();
            order.flightGoal=goal;order.mission.pending&=~0x3700u;
        }
        int handle(RetailMissionState& m,uint32_t events) {
            auto& order=u.orders.front();
            RetailAirRun run;
            run.position={u.x.v,u.flightY.v,u.z.v};
            run.target={target.x.v,target.flightGroundMode==2 ? target.flightY.v : target.groundY.v,target.z.v};
            run.heading=portHeadingToRetail(u.heading);
            run.range=weapon ? std::max(2,int32_t(weapon->range)) : 2;
            run.runLength=u.type->attackRunLength;
            // 5066f0: off the map when the footprint does not fit the cell grid.
            const int ox=footprintOrigin(u.x,u.type->footX),oz=footprintOrigin(u.z,u.type->footZ);
            run.onMap=ox>=0 && oz>=0 && ox+u.type->footX<w.hW_ && oz+u.type->footZ<w.hH_;
            // 50ef03: the playable map is 32 px narrower and 128 px shorter.
            run.width=w.hW_*16-32;run.height=w.hH_*16-128;
            return retailAirToGround(m,order.airRunCount,w.tickCounter_,events,run,*this);
        }
    } host{*this,u,target,weapon};
    retailDispatchMissions(tickCounter_,u.missionEvents,host);
    if (u.orders.empty() || u.orders.front().targetId!=target.id) { tickFlightBody(u); notifyFlightOccupancy(u); return; }
    auto& order=u.orders.front();
    if (u.flightGroundMode!=2) { u.flightGroundMode=2;groundMove(u); }   // 51b3b0: taking off unstamps
    // The mission's controllers are the navigator's destination now; a move's
    // coast (retainedFlightGoal) must not resume once the attack ends.
    u.retainedFlightGoal.reset();u.retainedFlightControllerActive=false;
    const int cruise=flightGround(u)+u.type->cruiseAlt;
    const RetailFlightVector position{u.x.v,u.flightY.v,u.z.v};
    if (order.flightGoal) {
        const auto& goal=*order.flightGoal;
        // 4e41d0: a point controller without an altitude answers the
        // flyer's cruise height over the ground under it.
        auto destination=goal.point;destination.y=Fixed::fromInt(std::min(cruise,511)).v;
        u.flightNavigation=retailFlightNavigation(position,u.flightNavigation.destination,destination,
            u.flightNavigation.heading,Fixed::fromInt(cruise).v,false,(goal.flags&0x40)!=0,goal.heading);
        if (goal.accepts(position)) { order.mission.pending|=0x500; order.flightGoal.reset(); }
    }
    tickFlightBody(u);
    notifyFlightOccupancy(u);
}

// AirToAir (41ca50, retailAirToAir): the mission of a flyer attacking a unit
// whose type can fly, and VTOL_Evade (41e3d0, retailAirEvade), which it puts
// ahead of itself to break off. Both run on the attack order (Order::mission
// and Order::airToAir). Their controller is the navigator's destination: a
// point controller (Order::flightGoal: back onto the map, or an evade line),
// let go on arrival as AirToGround's is, or the moving controller
// (RetailInterceptGoal), which the navigator asks for its point every tick
// (4e48d0 moves it on), flies at cruise height and never lets go.
void World::tickAirToAir(Unit& u,const Unit& target,const Weapon* selected) {
    // 519ac0: a switcher's selected slot, otherwise the first slot.
    const Weapon* weapon=u.type->weaponSwitching ? selected
                       : u.type->weapons.empty() ? nullptr : &u.type->weapons.front();
    if (!u.orders.front().airToAir) u.orders.front().airToAir.emplace();
    struct Host {
        World& w; Unit& u; const Unit& target; const Weapon* weapon;
        bool evading=false;   // the handler running is VTOL_Evade's
        int retargetTo=0;     // 4d7de0's new attack, put ahead once the dispatch is done
        Order* order() {
            if (u.orders.empty()) return nullptr;
            Order& o=u.orders.front();
            return o.targetId==target.id && o.airToAir ? &o : nullptr;
        }
        bool enabled() const { return u.alive() && !u.incapacitated() && !u.underConstruction && !u.embarked(); }
        RetailMissionState* head() {
            Order* o=order();
            if (!o) return nullptr;
            return o->airToAir->evade ? &o->airToAir->evade->mission : &o->mission;
        }
        bool hasNext(const RetailMissionState&) const { return u.orders.size()>1; }
        void idle() {}
        uint32_t random(int n) { return w.pathRand(uint32_t(n)); }
        void remove(RetailMissionState& m) {
            Order* o=order();
            if (o && o->airToAir->evade && &m==&o->airToAir->evade->mission) {
                // 4d6da0: the evade's controller goes with it; AirToAir,
                // left at stage 0, takes off again and installs its own.
                auto& a=*o->airToAir;
                if (a.evadeController) { o->flightGoal.reset();a.intercept.reset();a.evadeController=false; }
                // 4d6e26: and unless its +0x5a has 0x8000, 519b10(3) makes
                // every slot automatic (the selected one drops its target).
                if (!(m.flags&0x8000u)) for (int s=0;s<3;++s) w.setSlotMode(u,s,true);
                a.evade.reset();
                return;
            }
            if(u.scriptAimTarget)w.clearScriptWeaponTarget(u);
            w.dropLeg(u);w.cancelPath(u);u.routeStamp=-1;
        }
        void rotate(RetailMissionState&) {}
        void clear() { w.cancelPath(u);u.orders.clear();u.routeStamp=-1; }
        void beginFlight() {
            // 416c50, as AirToGround's stage 0 (tickAirToGround).
            if (auto script=w.unitScripts_.find(u.id);script!=w.unitScripts_.end() && !script->second.activated) {
                script->second.activated=true;w.notifyUnitScript(u,"Activate");
            }
            w.notifyUnitScript(u,"BeginFlight");
            u.missionEvents&=~0x3700u;
            // 416c57: 519b70(3), every slot manual.
            for (int s=0;s<3;++s) w.setSlotMode(u,s,false);
        }
        void aim() {
            // Stage 1 (41d5ae): 519b10(3) makes every slot automatic, then
            // 519b70 and 51a700 make the selected one (519ac0) manual and give
            // it the target. The others pick their own targets (the sweep,
            // tickSlotWeapons) for as long as the mission runs: an Aerial
            // Juggernaut's cannons bomb the ground while its arrows fight.
            const int selected=u.type->weaponSwitching ? std::clamp(u.weaponSlot,0,2) : 0;
            for (int s=0;s<3;++s) w.setSlotMode(u,s,true);
            w.setSlotMode(u,selected,false);
            w.setSlotTarget(u,selected,target.id);
            order()->mixedSlots=true;
        }
        RetailFlightVector lead() {
            const auto led=w.retailLeadPoint(u,target,weapon);   // 51aa50
            return {led[0],led[1],led[2]};
        }
        bool retarget() {
            // 4d8370 is World's target scan, as the ground response's; then
            // 4d7de0(unit, found, 0, 0, 1, 0), retailIssueAutoAttack's gates.
            const int found=w.findTarget(u,true,true,false);
            if (!found || found==target.id) return false;
            const Unit* t=w.unit(found);
            if (u.moveState==0 || u.fireState==0 || !t || !w.canAttackTarget(u,*t)) return false;
            retargetTo=found;
            return true;
        }
        RetailMissionState& installer() {
            Order& o=*order();
            return evading ? o.airToAir->evade->mission : o.mission;
        }
        void release() {
            // 4d4d40(mission, 0): the navigator lets AirToAir's controller go.
            Order& o=*order();
            o.flightGoal.reset();o.airToAir->intercept.reset();o.airToAir->evadeController=false;
        }
        bool evade() { order()->airToAir->evade.emplace();return true; }
        void install(const RetailFlightGoal& goal) {
            // 4d4d40 also clears the mission's pending controller events.
            Order& o=*order();
            o.flightGoal=goal;o.airToAir->intercept.reset();o.airToAir->evadeController=evading;
            installer().pending&=~0x3700u;
        }
        void install(const RetailInterceptGoal& goal) {
            Order& o=*order();
            o.airToAir->intercept=goal;o.flightGoal.reset();o.airToAir->evadeController=evading;
            installer().pending&=~0x3700u;
        }
        int handle(RetailMissionState& m,uint32_t events) {
            Order& o=*order();
            auto& a=*o.airToAir;
            const RetailFlightVector position{u.x.v,u.flightY.v,u.z.v};
            const RetailFlightVector there{target.x.v,target.flightGroundMode==2 ? target.flightY.v : target.groundY.v,
                                           target.z.v};
            // 5066f0: off the map when the footprint does not fit the cell grid.
            const int ox=footprintOrigin(u.x,u.type->footX),oz=footprintOrigin(u.z,u.type->footZ);
            const bool onMap=ox>=0 && oz>=0 && ox+u.type->footX<w.hW_ && oz+u.type->footZ<w.hH_;
            // The selected weapon's airtoairdistance (+0xc6), else the type's (+0x25a).
            const uint16_t distance=weapon && weapon->airToAirDistance ? weapon->airToAirDistance
                                                                       : u.type->airToAirDistance;
            evading=a.evade && &m==&a.evade->mission;
            if (evading) {
                RetailAirEscape in;
                in.position=position;in.heading=portHeadingToRetail(u.heading);in.target=there;
                in.maxSpeed=u.baseSpeed.v;in.typeSpeed=u.type->maxVel.v;in.targetTypeSpeed=target.type->maxVel.v;
                in.turnRate=uint16_t(u.type->turnRate);in.targetTurnRate=uint16_t(target.type->turnRate);
                in.distance=distance;in.onMap=onMap;
                // 50ef03: the playable map is 32 px narrower and 128 px shorter.
                in.width=w.hW_*16-32;in.height=w.hH_*16-128;
                return retailAirEvade(*a.evade,w.tickCounter_,events,in,*this);
            }
            RetailAirDuel duel;
            duel.position=position;duel.heading=portHeadingToRetail(u.heading);
            duel.target=there;duel.targetHeading=portHeadingToRetail(target.heading);
            // The target mover's velocity: a landed flyer's kernel holds none.
            if (target.flightGroundMode==2)
                duel.targetVelocity={target.flightVelocity.x,target.flightVelocity.y,target.flightVelocity.z};
            duel.speed=u.speed.v;duel.maxSpeed=u.baseSpeed.v;duel.distance=distance;
            duel.automatic=o.autoTarget;   // mission +0x52, 4d7de0's auto argument
            duel.onMap=onMap;duel.width=w.hW_*16-32;duel.height=w.hH_*16-128;
            return retailAirToAir(m,a.count,w.tickCounter_,events,duel,*this);
        }
    } host{*this,u,target,weapon};
    retailDispatchMissions(tickCounter_,u.missionEvents,host);
    if (host.retargetTo) retailIssueAutoAttack(u,host.retargetTo,false,false);
    if (u.orders.empty() || u.orders.front().targetId!=target.id || !u.orders.front().airToAir) {
        tickFlightBody(u);notifyFlightOccupancy(u);return;
    }
    auto& order=u.orders.front();
    auto& a=*order.airToAir;
    if (u.flightGroundMode!=2) { u.flightGroundMode=2;groundMove(u); }   // 51b3b0: taking off unstamps
    // The missions' controllers are the navigator's destination now; a move's
    // coast (retainedFlightGoal) must not resume once the attack ends.
    u.retainedFlightGoal.reset();u.retainedFlightControllerActive=false;
    const int cruise=flightGround(u)+u.type->cruiseAlt;
    const RetailFlightVector position{u.x.v,u.flightY.v,u.z.v};
    // Arrival events go to the mission that installed the controller.
    RetailMissionState& owner=a.evadeController && a.evade ? a.evade->mission : order.mission;
    if (a.intercept) {
        // 524af0 asks the moving controller (type 3) for its point every
        // tick and flies it at cruise height whatever the distance; inside
        // 320 px its heading (4e4b70) steers. Within 48 px it posts 0x100
        // and stays (+0x2c answers 1).
        auto& goal=*a.intercept;
        const auto destination=goal.target(uint16_t(u.type->turnRate));
        const uint16_t heading=goal.headingFor(position,portHeadingToRetail(u.heading));
        u.flightNavigation=retailFlightNavigation(position,u.flightNavigation.destination,destination,
            u.flightNavigation.heading,Fixed::fromInt(cruise).v,true,true,heading);
        if (goal.accepts(position)) owner.pending|=0x100;
    } else if (order.flightGoal) {
        const auto& goal=*order.flightGoal;
        // 4e41d0: a point controller without an altitude answers the
        // flyer's cruise height over the ground under it.
        auto destination=goal.point;destination.y=Fixed::fromInt(std::min(cruise,511)).v;
        u.flightNavigation=retailFlightNavigation(position,u.flightNavigation.destination,destination,
            u.flightNavigation.heading,Fixed::fromInt(cruise).v,false,(goal.flags&0x40)!=0,goal.heading);
        if (goal.accepts(position)) { owner.pending|=0x500;order.flightGoal.reset();a.evadeController=false; }
    }
    tickFlightBody(u);
    notifyFlightOccupancy(u);
}

// Flying construction (retail 41ef00, stages 4--6) wanders near builddistance
// while facing the site. Use the same flight controller as ordinary flight;
// this is construction mission behavior, independent of ground navigation.
// VTOL_RepairUnit (0x420e60) orbits the same way; its stage 4 re-picks when
// the flyer is still, else -- with the target still -- on rand(5)==0, else
// on rand(50)==0 (0x421302..0x42133d).
void World::tickConjureHover(Unit& b, const Unit& site, bool repair) {
    if (b.conjureHoverTarget!=site.id) {
        b.conjureHoverTarget=site.id;
        b.conjureHoverGoal.reset();
    }
    const auto moving=[](const Unit& x) { return x.speed.v || x.turnReqBam; };
    if (!b.conjureHoverGoal || !moving(b) ||
        (repair && !moving(site) && pathRand(5)==0) || pathRand(50)==0) {
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
    // Native construction controllers detach when their orbit point is
    // reached, but the mission keeps the flyer moving while it brakes and
    // waits to choose the next point. Keep running the body during that gap.
    tickFlightMovement(b,true);
    b.conjureHoverGoal=b.orders.front().flightGoal;
    b.orders.erase(b.orders.begin());
    notifyFlightOccupancy(b);
}

// Construction growth never takes a site past its maximum health, and the sum is
// done in 64 bits: a mod's very short build time (or a large maxdamage) makes one
// tick's growth as large as the whole bar, and hp + growth wrapped the 16.16 word.
// (Retail derives a site's hp from its progress fraction, 429af0, so it is
// bounded by maxdamage by construction.)
static Fixed addCapped(Fixed hp,Fixed grow,int maxHp) {
    const int64_t cap=int64_t(std::clamp(maxHp,0,32767))*Fixed::kOne;
    return Fixed::raw(int32_t(std::min<int64_t>(cap,int64_t(hp.v)+grow.v)));
}

// A HelpBuild ends with its target, however it ends: a builder still walking
// there drops the approach instead of finishing the walk (a guard's order,
// queued under it, then resumes). A ground helper's leg is the job itself
// and goes the same way.
static void endHelp(World& w, Unit& b, int siteId) {
    if (!b.orders.empty() && b.orders[World::currentLeg(b.orders)].helpSite==siteId) w.dropLeg(b);
}

// The leg of a ground builder's HelpBuild on `siteId` (its job), or -1.
// While a build order is current the latch is that MobileBuild's own (a help
// queued on the site it is building waits its turn and ends with the site).
static int groundHelpLeg(const Unit& b, int siteId) {
    if (!b.type || b.type->canFly || !siteId || b.orders.empty()) return -1;
    if (b.orders[World::currentLeg(b.orders)].buildType) return -1;
    for (size_t i=0;i<b.orders.size();++i)
        if (b.orders[i].helpSite==siteId) return int(i);
    return -1;
}

void World::tickConstruction(Unit& b, float dt) {
    Unit* site = unit(b.buildSiteId);
    if (!site || !site->alive() || !site->underConstruction) {
        // A help job holds no build order of its own: the queued build after
        // it (if any) is not this job's to retire.
        const bool help=groundHelpLeg(b,b.buildSiteId)>=0;
        endHelp(*this,b,b.buildSiteId);
        b.buildSiteId = 0;
        if (!help) popBuildOrder(b);
        else leaveFactoryYard(b.id);
        return;
    }
    site->beingBuilt = true;   // a builder is assigned (walking or working): no decay
    if (const int leg=groundHelpLeg(b,site->id);leg>=0) { tickGroundHelp(b,*site,size_t(leg),dt); return; }
    float dx = (site->x - b.x).toFloat(), dz = (site->z - b.z).toFloat();
    float half = 16.0f * float(std::max(site->type->footX, site->type->footZ)) / 2;
    // Reach = the builder's FBI builddistance (to the site edge) when it has one,
    // else the default footprint-derived range.
    float reach = std::max(half + 40.0f,
                           b.type->buildDist > 0 ? b.type->buildDist + half : 0.0f);
    float gd = dx * dx + dz * dz;
    // Flying construction admits the site at 2*builddistance (the approach
    // goal accepts strictly inside it), then works while entering its tighter
    // orbit and never measures again (VTOL_HelpBuild 0x41f7f0 stage 5 works
    // from any distance; emulated). Admission is the hover latched on this
    // site: a flyer helping a site someone else began must fly there first.
    const float flyingReach=2.0f*b.type->buildDist;
    const bool outOfReach=b.type->canFly
        ? b.conjureHoverTarget!=site->id && gd>=flyingReach*flyingReach
        : gd>reach*reach;
    if (outOfReach) {                                  // out of range: still walking there
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
                groundRemove(*site);
                site->deadFor = kRetiredTicks;
            }
            endHelp(*this,b,b.buildSiteId);
            b.buildSiteId = 0;
            popBuildOrder(b);
            
        }
        return;
    }
    b.buildStuckT = 0; b.buildStuckD = INT32_MAX;      // in range: reset the watchdog
    // MobileBuild's and HelpBuild's work stages (icd 0x405bc8, 0x4060bb) keep a
    // ground builder visible for 300 ticks on every work tick, paid or not;
    // FlyingBuild's do not.
    if (!b.type->canFly) blockCloak(b, 300);
    // HelpBuild's work stage owns the weapons, like the MobileBuild placement.
    setSlotsManual(b,kSlotsWork);
    site->buildBegun = true;   // in range: the site starts materialising now
    // Drop the APPROACH leg, but never the queued build order itself: that entry
    // IS the job, and it is what holds the rest of the queue back until the
    // building is finished (popBuildOrder retires it when the job ends).
    // Nor a guard: a guarding builder's help sits ahead of its guard order,
    // which resumes when the help ends (Follow_Ground 0x407f90).
    if (b.orders.empty() || (!b.orders.front().buildType && !b.orders.front().guard)) dropLeg(b);
    b.constructionHolding = true;
    if (b.type->canFly) tickConjureHover(b,*site);
    else b.speed = Fixed();
    // Face what we're building/conjuring: turn toward the site at the unit's turn
    // rate (a building has turnRate 0, so it simply doesn't rotate).
    if (!b.type->canFly) turnBuilderToSite(b,*site);
    const int builderId=b.id;
    if (constructionWork(b,*site,dt)) {
        Unit* builder=unit(builderId);   // finishing an output can reallocate units_
        endHelp(*this,*builder,builder->buildSiteId);
        builder->buildSiteId=0;
        popBuildOrder(*builder);
    }
}

// HelpBuild (0x405cf0) for a ground builder, on its leg (emulated 2026-10-30).
// Stage 0 installs the rectangle controller round the site's footprint
// (prepareHelpApproach). Stage 1 waits on the approach alone (mask 0x708, no
// timer): arrival starts the work; a failed approach (0x200) starts it only
// within MobileBuild's reach of the site (retailBuildWithinReach), else the
// help ends (UNITCHATTER_CANNOTGETTHERE; no unit-chatter channel here, so
// silently). Ours gave up after 8 s without 20 px of progress. The work
// stage (stage 3; ours skips the stance wait of stage 2, as before) never
// measures distance again: a helper pushed away keeps working. A leg that is
// not current waits; one an order went ahead of starts again from stage 0.
void World::tickGroundHelp(Unit& b, Unit& site, size_t index, float dt) {
    if (index!=currentLeg(b.orders)) {
        Order& leg=b.orders[index];
        if (leg.mission.stage) {
            leg.buildRectangle.reset();leg.controller=0;leg.mission.stage=0;
            leg.mission.waitMask=0;leg.mission.pending=0;
            leg.navigationExhausted=leg.navigationConsumed=false;leg.hasSegment=false;
        }
        return;
    }
    Order* leg=&b.orders[index];
    if (leg->mission.stage==0) return;   // prepareHelpApproach, on this unit's dispatch
    if (leg->mission.stage==1) {
        auto& m=leg->mission;
        const uint32_t events=(b.missionEvents|m.pending)&m.waitMask;
        if (!events) return;
        b.missionEvents&=~events;m.pending&=~events;m.waitMask=0;
        if ((events&0x200) && !retailBuildWithinReach(b.x,b.z,leg->buildX,leg->buildZ,
                int16_t(b.type->footX),int16_t(b.type->footZ),
                int16_t(site.type->footX),int16_t(site.type->footZ),uint16_t(b.type->buildDist))) {
            endHelp(*this,b,site.id);
            b.buildSiteId=0;
            return;
        }
        // 4d4d40(0): the controller goes, and the route's corners with it;
        // the leg is the job from here.
        cancelPath(b);
        if (index) { b.orders.erase(b.orders.begin(),b.orders.begin()+long(index));leg=&b.orders.front(); }
        leg->controller=0;leg->navigationExhausted=true;
        leg->mission.stage=3;
    }
    // HelpBuild's work stage (0x4060bb) keeps a ground builder visible for
    // 300 ticks on every work tick, paid or not, and owns the weapons.
    blockCloak(b, 300);
    setSlotsManual(b,kSlotsWork);
    site.buildBegun = true;
    b.constructionHolding = true;
    b.speed = Fixed();
    turnBuilderToSite(b,site);
    const int builderId=b.id,siteId=site.id;
    if (constructionWork(b,site,dt)) {
        Unit* helper=unit(builderId);   // finishing an output can reallocate units_
        endHelp(*this,*helper,siteId);
        helper->buildSiteId=0;
        leaveFactoryYard(builderId);
    }
}

// HelpBuild stage 0 (0x405d70) when its leg is current: the helper may still
// help the site (0x519e60; a site finished or gone ends the job), then
// 4d4e80 installs the rectangle controller round the site's footprint (its
// +0x74 cell, its type's footprint) with wait mask 0x708. The job latches
// here when an assist was queued behind other orders.
void World::prepareHelpApproach(Unit& u) {
    if (u.orders.empty() || !u.type || u.type->canFly) return;
    Order& goal=u.orders[currentLeg(u.orders)];
    if (!goal.helpSite || goal.mission.stage!=0) return;
    const Unit* site=unit(goal.helpSite);
    if (!site || !canHelpBuild(u,*site)) {
        if (u.buildSiteId==goal.helpSite) u.buildSiteId=0;
        dropLeg(u);
        return;
    }
    u.buildSiteId=goal.helpSite;
    goal.buildX=site->x;goal.buildZ=site->z;
    goal.buildRectangle=retailReclaimRectangle(footprintOrigin(site->x,site->type->footX),
        footprintOrigin(site->z,site->type->footZ),site->type->footX,site->type->footZ,u.type->footX,u.type->footZ);
    const auto [x,z]=goal.buildRectangle->navigationCell(
        footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ));
    goal.x=Fixed::fromInt(x*16+u.type->footX*8);
    goal.z=Fixed::fromInt(z*16+u.type->footZ*8);
    goal.segmentX=Fixed::fromInt(u.x.floorInt());
    goal.segmentZ=Fixed::fromInt(u.z.floorInt());
    goal.hasSegment=true;
    goal.mission.stage=1;
    goal.mission.waitMask=0x700;
    goal.controller=++nextMovementController_;
    goal.navigationExhausted=goal.navigationConsumed=false;
    goal.mission.pending&=~0x3700u;
    if (u.routeStamp<0 || uint32_t(u.routeStamp)<=tickCounter_-6u) u.routeStamp=0;
    cancelPath(u);
    requestPath(u,goal.x.toFloat(),goal.z.toFloat());
}

// One tick of a builder's construction work on `site` (a MobileBuild job or a
// HelpBuild). True once the site is complete; the caller ends the job.
// Finishing a production output can reallocate units_: `b` and `site` are
// not valid after a true return.
bool World::constructionWork(Unit& b, Unit& site, float dt) {
    float total = site.type->buildTime / std::max(b.type->workerTime, 0.01f);
    // Record the current build rate so an interrupted conjure decays at this speed.
    // hp per TICK, in the same fixed-point hp is kept in (it is subtracted from hp
    // directly in decayConstruction).
    site.conjureRate = Fixed::fromFloat(site.type->maxHp * 0.95f
                                         / (std::max(total, 0.01f) * kTick));
    Player& tm = players_[size_t(b.player)];
    if (Unit* producer=productionOwnerOf(site)) {
        // A factory's or conjurer's output has ONE progress, its producer's
        // (retail 429af0: every worker takes its workertime-scaled share off
        // the site's remaining fraction +0x108 and pays for exactly that).
        // This helper's tick of work, buildtime/workertime seconds for the
        // whole unit, is converted into the producer's 1/65536-tick units, so
        // the output finishes once, sooner, for its buildcost in total.
        bool done=false;
        const int32_t ptotal=productionTotal(*producer,site.type);
        if (gInstantBuild) {
            producer->buildProgress=ptotal;producer->buildFraction=0;
            site.hp=Fixed::fromFloat(site.type->maxHp);
            done=true;
        } else {
            const double request=std::min({double(ptotal)*65536.0,double(UINT32_MAX),
                double(ptotal)*65536.0*double(dt)/double(std::max(total,0.01f))});
            const double cost=double(site.type->buildCost)/double(ptotal)*request/65536.0;
            const double share=tm.constructionShare(cost);
            if (share<=0) return false;
            done=workProduction(*producer,site,site.type,uint32_t(request*std::min(share,1.0)));
        }
        emitConstruction(b,false);
        if (!done) return false;
        finishProduction(producer->id,site);
        return true;
    }
    if (gInstantBuild) {
        site.hp = Fixed::fromFloat(site.type->maxHp);   // finishes this tick, free
        if (site.lodestoneReplacement) site.lodestoneReplacement->completedWork=1;
    } else if (site.lodestoneReplacement) {
        auto& progress=site.lodestoneReplacement->completedWork;
        // Scale one tick of work by the share FIRST, then clamp it to what is
        // left (retail 429af0 subtracts allocation * work and completes at
        // remaining <= 0). Clamping first and scaling after made the last tick
        // cover only share * gap: the gap shrank geometrically, and once it was
        // one ulp a share <= 0.5 rounded away, so the site sat at full hp and
        // completedWork 1-2^-53 until the allocation rose above one half.
        // The request is the full tick (retail records it before scaling).
        double work=double(dt)/std::max(total,0.01f);
        const double cost=site.type->buildCost*work;
        const double share=tm.constructionShare(cost);
        if (share<=0) return false;
        if (share<1) work*=share;
        const double gap=1.0-progress;
        if (work>=gap) {
            work=gap;
            tm.payMana(std::min(tm.mana,site.type->buildCost*gap));
            progress=1.0;
        } else {
            tm.payMana(std::min(tm.mana,cost*share));
            progress=std::min(1.0,progress+work);
        }
        site.hp=addCapped(site.hp,Fixed::fromFloat(float(site.type->maxHp*0.95*work)),site.type->maxHp);
    } else {
        float cost = site.type->buildCost * dt / std::max(total, 0.01f);
        const double share = tm.constructionShare(cost);
        if (share <= 0) return false;
        tm.payMana(std::min(tm.mana, double(cost) * share));
        float grow = site.type->maxHp * 0.95f * dt / std::max(total, 0.01f);
        if (share < 1) grow = float(double(grow) * share);
        site.hp=addCapped(site.hp,Fixed::fromFloat(grow),site.type->maxHp);
    }
    emitConstruction(b,false);
    emitConstruction(site,true);
    if (site.lodestoneReplacement ? site.lodestoneReplacement->completedWork>=1 :
        site.hp >= Fixed::fromFloat(site.type->maxHp)) {
        site.hp = Fixed::fromFloat(site.type->maxHp);
        site.underConstruction = false;
        return true;
    }
    return false;
}

void World::decayConstruction(Unit& u, float dt) {
    // No builder worked this conjure this tick: it "un-conjures", losing HP at the
    // rate it was last built, and vanishes at zero (no corpse -- it was never
    // finished). A site that never materialised falls back to its nominal rate.
    const Fixed rate = u.conjureRate > Fixed()
                     ? u.conjureRate
                     : Fixed::fromFloat(u.type->maxHp * 0.95f
                                        / (std::max(u.type->buildTime, 0.01f) * kTick));
    // A retail site keeps taking hits at or below zero, so applyHit may have
    // saturated its hp at -INT32_MAX; subtract in 64 bits so this cannot wrap.
    u.hp = Fixed::raw(int32_t(std::clamp<int64_t>(int64_t(u.hp.v) - rate.v, -int64_t(INT32_MAX), INT32_MAX)));
    if (u.hp > Fixed()) return;
    if (u.type->isStructure()) {
        blockFoot(*u.type, u.x.toFloat(), u.z.toFloat(), false);
    }
    u.underConstruction = false;
    groundRemove(u);
    u.deadFor = kRetiredTicks;   // fully gone, no death anim
}

static void retireProductionItem(Unit& builder,size_t index) {
    for(auto& order:builder.orders)
        if(order.transportProductionAhead>index)--order.transportProductionAhead;
    builder.buildQueue.erase(builder.buildQueue.begin()+long(index));
}

void World::train(int builderId, const UnitType* type, int count) {
    if (!buildAllowed(type)) return;
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
        radius=std::max(radius,std::max(sightDistance(*u->type),weapon.range));
        if (u->orders.empty() || !u->orders[currentLeg(u->orders)].groundMission || u->orders[currentLeg(u->orders)].park)
            unsupported("non-move member order");
    }
    auto distance=[](int32_t ax,int32_t az,int32_t bx,int32_t bz) {
        const int64_t x=std::bit_cast<int32_t>(uint32_t(ax)-uint32_t(bx));
        const int64_t z=std::bit_cast<int32_t>(uint32_t(az)-uint32_t(bz));
        return std::bit_cast<int32_t>(uint32_t((x*x)>>32)+uint32_t((z*z)>>32));
    };
    const int threatRadius=(radius*3)/2;
    TAK_PASS();
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
    TAK_PASS();
    for (const auto& enemy:units_) if (enemy.alive() && !allied(owner,enemy.player)) {
        const int64_t dx=std::bit_cast<int32_t>(uint32_t(center->first)-uint32_t(enemy.x.v));
        const int64_t dz=std::bit_cast<int32_t>(uint32_t(center->second)-uint32_t(enemy.z.v));
        const int32_t distance=std::bit_cast<int32_t>(uint32_t((dx*dx)>>32)+uint32_t((dz*dz)>>32));
        if (distance<=radius*radius) unsupported("nearby threat response");
    }
    unsigned population=0;
    TAK_PASS();
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
        if (idx == 0) {   // canceling the in-progress front: a factory's output dies at once
            killProductionOutput(*b,true);
            b->buildProgress = 0; b->buildFraction = 0; b->productionSiteId = 0;
        }
        retireProductionItem(*b,size_t(idx));
    }
    // If the queue no longer holds a type set to infinite-repeat, stop repeating it
    // so the count actually reaches zero instead of refilling next tick.
    if (b->repeatType == type) {
        bool any = false;
        for (const auto* q : b->buildQueue) if (q == type) { any = true; break; }
        if (!any) {
            b->repeatType = nullptr;
            // A mobile builder holds a rally only while it repeats: once it is
            // given back, a later limited queue must not inherit a plan the
            // player can no longer see or change (its orders command it again).
            if (b->type && !b->type->isStructure()) b->rally.clear();
        }
    }
}

void World::setRepeat(int builderId, const UnitType* type) {
    if (!buildAllowed(type)) return;
    Unit* b = unit(builderId);
    if (!b || !b->alive() || !type) return;
    if (b->repeatType == type) {
        // ctrl+click the +++ icon again: stop now and clear what's pending.
        b->repeatType = nullptr;
        killProductionOutput(*b,true);
        b->buildQueue.clear();
        if (b->type && !b->type->isStructure()) b->rally.clear();   // as in dequeue()
        for(auto& order:b->orders)order.transportProductionAhead=0;
        b->buildProgress = 0; b->buildFraction = 0;
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
    // The emitters alone, in unit order (UnitClassLists: every unit whose type
    // has auras). Nothing here spawns.
    syncUnitLists();
    for (const int id : unitLists_.emitters) {
        Unit& s = *unit(id);
        if (!s.alive() || s.embarked() || s.underConstruction || !s.active || s.incapacitated())
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
                tm.requestMana(cost);   // the HUD's -N shows the whole pulse asked for
                if (cost > tm.mana) {   // short on mana: heal proportionally less
                    prog *= tm.mana / std::max(cost, 1e-6f);
                    cost = tm.mana;
                }
                tm.payMana(cost);
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
    auto relaxBuffs = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            Unit& u = units_[i];
            if (!u.alive()) continue;
            u.atkBuff += (1.0f - u.atkBuff) * relax;
            u.armBuff += (1.0f - u.armBuff) * relax;
        }
    };
    // Each unit relaxes its own buffs: on the SimPool for a big army (WE E4.3).
    if (units_.size() >= kPostMinParallel && simPool().workers())
        simPool().forChunks(units_.size(), SimPool::kUnitChunk, 0,
                            [&](size_t, size_t begin, size_t end, unsigned) { relaxBuffs(begin, end); });
    else {
        TAK_PASS();
        relaxBuffs(0, units_.size());
    }
    // The emitters alone, in unit order (UnitClassLists); see tickHealAuras.
    syncUnitLists();
    // Per emitter, in forEachNear order, every unit it refreshes and the
    // value. The values depend only on positions, owners and liveness, none
    // of which the pass changes, so they can be gathered first and applied
    // after, in the serial order (a unit both buffed and debuffed takes a max
    // and a min, which do not commute).
    auto gather = [&](const Unit& s, auto&& hit) {
        if (!s.alive() || s.embarked() || s.underConstruction || !s.active || s.incapacitated())
            return;
        for (const Aura& a : s.type->auras) {
            if (a.kind == Aura::Kind::Joy || a.radius <= 0) continue;  // morale: not modelled
            float r2 = a.radius * a.radius;
            forEachNear(s.x.toFloat(), s.z.toFloat(), a.radius, [&](int idx) {
                const Unit& e = units_[size_t(idx)];
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
                hit(uint32_t(idx), a.kind == Aura::Kind::Armor, a.affectsEnemy, amt);
            });
        }
    };
    auto apply = [&](uint32_t idx, bool armor, bool affectsEnemy, float amt) {
        Unit& e = units_[idx];
        float& buff = armor ? e.armBuff : e.atkBuff;
        buff = affectsEnemy ? std::min(buff, amt) : std::max(buff, amt);
    };
    const auto& emitters = unitLists_.emitters;
    if (units_.size() >= kPostMinParallel && emitters.size() >= 2 * kAuraEmitterChunk && simPool().workers()) {
        // WE E4.3 (P6): the gathers on the SimPool, a few emitters per chunk.
        const size_t chunks = (emitters.size() + kAuraEmitterChunk - 1) / kAuraEmitterChunk;
        if (auraChunks_.size() < chunks) auraChunks_.resize(chunks);
        simPool().forChunks(emitters.size(), kAuraEmitterChunk, 0, [&](size_t k, size_t begin, size_t end, unsigned) {
            auto& hits = auraChunks_[k];
            hits.clear();
            for (size_t i = begin; i < end; ++i)
                gather(*unitNoRepair(emitters[i]), [&](uint32_t idx, bool armor, bool affectsEnemy, float amt) {
                    hits.push_back({idx, uint8_t(armor | (affectsEnemy << 1)), amt});
                });
        });
#ifndef NDEBUG
        if (verifyPool()) {
            std::vector<AuraHit> serial;
            for (const int id : emitters)
                gather(*unit(id), [&](uint32_t idx, bool armor, bool affectsEnemy, float amt) {
                    serial.push_back({idx, uint8_t(armor | (affectsEnemy << 1)), amt});
                });
            size_t at = 0;
            for (size_t k = 0; k < chunks; ++k)
                for (const AuraHit& h : auraChunks_[k]) {
                    if (at >= serial.size() || serial[at].index != h.index || serial[at].flags != h.flags ||
                        std::bit_cast<uint32_t>(serial[at].amt) != std::bit_cast<uint32_t>(h.amt))
                        poolMismatch("tickAuras gathers", tickCounter_);
                    ++at;
                }
            if (at != serial.size()) poolMismatch("tickAuras gathers", tickCounter_);
        }
#endif
        for (size_t k = 0; k < chunks; ++k)
            for (const AuraHit& h : auraChunks_[k]) apply(h.index, h.flags & 1, h.flags & 2, h.amt);
    } else {
        for (const int id : emitters) gather(*unit(id), apply);
    }
}

// Retail's revive work counter (icd 0x406c2e, 0x4201e9): 65536 x buildtime /
// (workertime x 0.03333333), times 0.3 for a resurrect, through _ftol (5d3d54),
// which keeps the LOW word of a 64-bit fistp: past 2^31 it wraps (araking
// resurrecting a buildtime-100000 vermage starts at 1603273293, not 5.9e9),
// and a caster with no workertime (infinity: the indefinite, low word 0)
// finishes on its first tick.
static int32_t retailReviveWork(float buildTime, float workerTime, bool resurrect) {
    double work = double(buildTime);
    if (resurrect) work *= 0.3;
    return int32_t(retailFtol(work / (double(workerTime) * 0.03333333) * 65536.0));
}

void World::tickAbilities(float) {
    // A corpse is a dead unit whose body still lies on the field: the death
    // anim has finished (4s) and decomposetime hasn't expired. Whether it can
    // be RAISED again is the corpse def's `resurrectable` (81 of the 150
    // shipped corpse defs; reclaiming works on any corpse).
    auto isCorpse = [](const Unit& c) {
        // Statues stand from the instant of death (retail: severity 0, no Dying
        // anim); ordinary bodies appear after the 4s death animation.
        const int from = c.corpseAnimationTicks();
        return c.type && !c.alive() && !c.corpseFading && c.deadFor >= from && c.deadFor < c.corpseUntil;
    };
    auto corpseDef = [&](const Unit& c) -> const FeatType* {
        int ct = c.corpseStatue >= 0 ? c.corpseStatue : corpseTypeOf(c.type);
        return ct >= 0 ? &featTypes_[size_t(ct)] : nullptr;
    };
    auto retire = [&](Unit& c) { retireCorpse(c); };
    struct Revive { const UnitType* type; float x, z; int player; bool animate; };
    std::vector<Revive> revives;
    // Revives finished this tick, per player: they spawn after the loop, so
    // the limits below count them in.
    std::vector<int> pendingRevives(players_.size(), 0);
    auto reviveRefused = [&](int player, const UnitType* out) {
        if (!out || player < 0 || player >= int(players_.size())) return true;
        if (unitCap_ > 0 && players_[size_t(player)].unitCount + pendingRevives[size_t(player)] >= unitCap_)
            return true;
        if (out->totalAllowed <= 0) return false;
        int n = 0;
        for (const auto& r : revives) n += r.player == player && r.type == out;
        for (const auto& o : units_)
            if (o.alive() && o.type == out && o.player == player) ++n;
        return n >= out->totalAllowed;
    };
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
    // A unit that cannot act this tick drops its channel; one that can, and is a
    // caster or a reclaimer, is visited by the caster loop below. No caster's
    // work changes these tests for another unit (it debits or credits mana,
    // retires corpses -- dead units, which stay dead -- and queues revives that
    // spawn after the loop), so they can all be taken before the loop.
    auto actor = [](Unit& u) {
        if (!u.alive() || !u.type || u.underConstruction || u.incapacitated() ||
            !u.orders.empty()) {
            if (u.reviveTarget) u.reviveTarget = 0;   // interrupted: channel drops
            return false;
        }
        bool caster = u.type->canResurrect || (u.type->canAnimate && u.type->animateType);
        return caster || u.type->canReclaim;
    };
    corpseIdx_.clear();
    actorIdx_.clear();
    if (units_.size() >= kPostMinParallel && simPool().workers()) {
        // WE E4.3 (P6): both lists on the SimPool, each chunk listing its own
        // units in order, concatenated in chunk order (= ascending index).
        const size_t chunks = (units_.size() + SimPool::kUnitChunk - 1) / SimPool::kUnitChunk;
        if (abilityChunks_.size() < chunks) abilityChunks_.resize(chunks);
        simPool().forChunks(units_.size(), SimPool::kUnitChunk, 0, [&](size_t k, size_t begin, size_t end, unsigned) {
            auto& c = abilityChunks_[k];
            c.corpses.clear(); c.actors.clear();
            for (size_t j = begin; j < end; ++j) {
                if (isCorpse(units_[j])) c.corpses.push_back(uint32_t(j));
                if (actor(units_[j])) c.actors.push_back(uint32_t(j));
            }
        });
        for (size_t k = 0; k < chunks; ++k) {
            corpseIdx_.insert(corpseIdx_.end(), abilityChunks_[k].corpses.begin(), abilityChunks_[k].corpses.end());
            actorIdx_.insert(actorIdx_.end(), abilityChunks_[k].actors.begin(), abilityChunks_[k].actors.end());
        }
#ifndef NDEBUG
        if (verifyPool()) {
            std::vector<uint32_t> corpses, actors;
            for (size_t j = 0; j < units_.size(); ++j) {
                if (isCorpse(units_[j])) corpses.push_back(uint32_t(j));
                if (actor(units_[j])) actors.push_back(uint32_t(j));
            }
            if (corpses != corpseIdx_ || actors != actorIdx_) poolMismatch("tickAbilities lists", tickCounter_);
        }
#endif
    } else {
        TAK_PASS();
        for (size_t j = 0; j < units_.size(); ++j) {
            if (isCorpse(units_[j])) corpseIdx_.push_back(uint32_t(j));
            if (actor(units_[j])) actorIdx_.push_back(uint32_t(j));
        }
    }
    // No early return when the list is empty, deliberately. The per-caster loop below
    // does more than search for corpses: it also DROPS a channel whose target has gone
    // (interrupted, out of range, or decomposed). Returning here skipped that, so if the
    // last corpse decomposed under a channeling caster its reviveTarget survived, and
    // the caster then spent the tick the next corpse appeared clearing the stale target
    // instead of acquiring. Only the SEARCH is guaranteed to have no work, and with an
    // empty corpseIdx_ that inner loop is already a no-op -- so the walk stays O(n),
    // which is the whole point of collecting the corpses once. (Found by review.)

    for (const uint32_t ai : actorIdx_) {
        const size_t i = ai;
        Unit& u = units_[i];

        // A revive is a CHANNEL, not an instant: retail's Resurrect task (icd
        // 0x4068b0; VTOL_Resurrect 0x41fe70 is the same). It costs NOTHING and
        // requests nothing -- the task never reaches 0x429af0/0x429d90 or the
        // resource object -- but each tick's work is the owner's allocation
        // (0x406d3b: work -= ftol(allocation x 65536)), so a starved economy
        // slows it and an empty, contested store stops it. At the end
        // (0x406e79) a computer player whose allocation is under 0.7 abandons
        // it unraised.
        if (u.reviveTarget) {
            Unit* c = unit(u.reviveTarget);
            if (!c || !isCorpse(*c)) { u.reviveTarget = 0; continue; }
            float dx = (c->x - u.x).toFloat(), dz = (c->z - u.z).toFloat();
            if (dx * dx + dz * dz > kR * kR * 4) { u.reviveTarget = 0; continue; }
            const bool animate = u.reviveMode == 2;
            const UnitType* out = animate ? u.type->animateType : c->type;
            const float allocation = players_[size_t(u.player)].allocation();
            // 32-bit sub, signed test, as retail: a counter ftol wrapped negative
            // finishes at once, and nothing here is C++ signed overflow.
            u.reviveWork = int32_t(uint32_t(u.reviveWork) - retailFtol(double(allocation) * 65536.0));
            // Work left: the caster stays visible 300 ticks (0x406e45, 0x420477).
            if (u.reviveWork > 0) { blockCloak(u, 300); continue; }
            u.reviveTarget = 0;
            if (computerOwned(u.player) && double(allocation) < 0.7) continue;
            // 0x406f0c creates the unit through 0x512060, which refuses one
            // past the type's totalallowed or the player's unit limit: the
            // revive ends unraised (UNITCHATTER_NOMOREUNITS) and the corpse
            // stays. Earlier revives this tick count as raised (`pending`).
            if (reviveRefused(u.player, out)) continue;
            ++pendingRevives[size_t(u.player)];
            revives.push_back({out, c->x.toFloat(), c->z.toFloat(), u.player, animate});
            retire(*c);
            continue;
        }
        // Ours, not retail (whose revive is only ever an order): the idle
        // search. It takes up no revive the end would refuse -- a computer
        // caster while its allocation is under 0.7 (the 0x406e79 abandon), any
        // caster at its unit limit -- or it would take the same corpse up
        // again on its next tick, for as long as the shortage lasted.
        const bool casterIdle = !(computerOwned(u.player) &&
            double(players_[size_t(u.player)].allocation()) < 0.7);

        // Corpses only -- collected once above, in ascending unit id.
        for (uint32_t ci : corpseIdx_) {
            const size_t j = size_t(ci);
            Unit& c = units_[j];
            if (j == i || !isCorpse(c)) continue;   // re-check: an earlier caster may have taken it
            float dx = (c.x - u.x).toFloat(), dz = (c.z - u.z).toFloat();
            if (dx * dx + dz * dz > kR * kR) continue;
            const FeatType* cd = corpseDef(c);
            if (!cd) continue;
            if (casterIdle && u.type->canResurrect && c.player == u.player && cd->resurrectable &&
                !reviveRefused(u.player, c.type)) {
                // Resurrect: 0.3 x the corpse's buildtime (retail 0x406c2e,
                // +0x56 clear); the unit returns at 10% HP.
                u.reviveTarget = c.id;
                u.reviveMode = 1;
                u.reviveWork = retailReviveWork(c.type->buildTime, u.type->workerTime, true);
                break;
            } else if (casterIdle && u.type->canAnimate && u.type->animateType &&
                       cd->resurrectable && !reviveRefused(u.player, u.type->animateType)) {
                // Animate (tarpries animatetype=MONGHOUL): ANY resurrectable
                // corpse -- friend or foe -- rises as the caster's creature,
                // at the animatetype's FULL buildtime (no 0.3: +0x56 is set)
                // and FULL HP (retail 0x406c2e/0x407038).
                u.reviveTarget = c.id;
                u.reviveMode = 2;
                u.reviveWork = retailReviveWork(u.type->animateType->buildTime, u.type->workerTime, false);
                break;
            } else if (u.type->canReclaim && cd->reclaimable) {
                // Ours, not retail RECLAIM (406250, which pays nothing either):
                // an idle reclaimer clears a nearby body to deny resurrection.
                // Whether retail does this at all is unverified.
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
        static const bool kRevLog = tak::devEnv("TAK_BURNLOG") != nullptr;
        if (kRevLog)
            std::fprintf(stderr, "%s: %s for p%d at %.0f,%.0f\n",
                         r.animate ? "animate" : "resurrect",
                         r.type->id.c_str(), r.player, r.x, r.z);
    }
}

void World::updateNavigationExploration() {
    const int squaresW=((hW_/2)+(1<<kViewDirtyShift)-1)>>kViewDirtyShift;
    const int squaresH=((hH_/2)+(1<<kViewDirtyShift)-1)>>kViewDirtyShift;
    if (viewDirtyW_!=squaresW || viewDirty_.size()!=size_t(squaresW)*squaresH) {
        viewDirtyW_=squaresW;viewDirty_.assign(size_t(squaresW)*squaresH,1);
    }
    updateExplorationPlane();
    updateViewCounts();
}

void World::markViewDirty(const RetailSightFootprint& sight) {
    if (viewDirty_.empty()) return;
    const int radius=retailSightRadius(sight),squaresH=int(viewDirty_.size())/viewDirtyW_;
    const int x0=std::max(0,(int(sight.x)-radius)>>kViewDirtyShift),x1=std::min(viewDirtyW_-1,(int(sight.x)+radius)>>kViewDirtyShift);
    const int z0=std::max(0,(int(sight.z)-radius)>>kViewDirtyShift),z1=std::min(squaresH-1,(int(sight.z)+radius)>>kViewDirtyShift);
    for (int z=z0;z<=z1;++z)
        for (int x=x0;x<=x1;++x) __atomic_store_n(&viewDirty_[size_t(z)*viewDirtyW_+x],uint8_t(1),__ATOMIC_RELAXED);
}

void World::markViewDirtyAll() { std::fill(viewDirty_.begin(),viewDirty_.end(),uint8_t(1)); }

void World::updateExplorationPlane() {
    if (explorationHeights_.empty()) return;
    const int width=hW_/2,height=hH_/2;
    constexpr int blockShift=kExplorationBlockShift;
    const int blocksW=(width+(1<<blockShift)-1)>>blockShift;
    const int blocksH=(height+(1<<blockShift)-1)>>blockShift;
    // Derived per-block AND of the owner masks: a block whose every cell
    // already holds a unit's viewer bits needs no per-cell traversal.
    auto summarize=[&](const std::vector<uint16_t>& mask,std::vector<uint16_t>& blocks,int bx,int bz) {
        uint16_t all=0xffff;
        const int zh=std::min(height,(bz+1)<<blockShift),xh=std::min(width,(bx+1)<<blockShift);
        for (int z=bz<<blockShift;z<zh;++z)
            for (int x=bx<<blockShift;x<xh;++x) all&=mask[size_t(z)*width+x];
        blocks[size_t(bz)*blocksW+bx]=all;
    };
    auto summarizeAll=[&](const std::vector<uint16_t>& mask,std::vector<uint16_t>& blocks) {
        blocks.assign(size_t(blocksW)*blocksH,0);
        for (int bz=0;bz<blocksH;++bz) for (int bx=0;bx<blocksW;++bx) summarize(mask,blocks,bx,bz);
    };
    // Rebuilt every pass (one linear read of the plane) so writes to the
    // plane from outside this function can never leave it stale.
    summarizeAll(navigationExplored_,explorationBlocks_);
    std::vector<uint16_t> playerViewers(players_.size(),0);
    for (size_t owner=0;owner<players_.size();++owner)
        for (size_t viewer=0;viewer<players_.size() && viewer<16;++viewer)
            if (allied(int(owner),int(viewer))) playerViewers[owner]|=uint16_t(1u<<viewer);
    // One plane shared by every chunk. Exploration only adds bits: a footprint
    // depends on its own unit alone, and covered/needed may skip only work whose
    // bits are already set, so any interleaving of the reveals yields the same
    // plane (O9). With workers the cells and block summaries are accessed
    // atomically (relaxed): a summary computed from a cell read before another
    // chunk's OR is only lower, i.e. it skips less, never more.
    auto explore=[&](size_t begin,size_t end,auto shared) {
        constexpr bool kShared=decltype(shared)::value;
        auto load=[](const uint16_t& v)->uint16_t {
            if constexpr (kShared) return __atomic_load_n(&v,__ATOMIC_RELAXED); else return v;
        };
        auto summarizeBlock=[&](int bx,int bz) {
            uint16_t all=0xffff;
            const int zh=std::min(height,(bz+1)<<blockShift),xh=std::min(width,(bx+1)<<blockShift);
            for (int z=bz<<blockShift;z<zh;++z)
                for (int x=bx<<blockShift;x<xh;++x) all&=load(navigationExplored_[size_t(z)*width+x]);
            uint16_t& out=explorationBlocks_[size_t(bz)*blocksW+bx];
            if constexpr (kShared) __atomic_store_n(&out,all,__ATOMIC_RELAXED); else out=all;
        };
        for (size_t index=begin;index<end;++index) {
            auto& u=units_[index];
            // 4f6a60..4f6ac0 has no attachment test: transport cargo explores
            // with its own sight from the attach point its mover committed.
            if (!u.alive() || !u.type || u.underConstruction) continue;
            const int16_t distance=int16_t(sightDistance(*u.type));
            if (u.sightFootprint.distance!=distance || u.sightFootprint.sightHeight!=u.type->sightHeight) {
                u.sightFootprint={};
                u.sightFootprint.distance=distance;
                u.sightFootprint.sightHeight=u.type->sightHeight;
            }
            const uint16_t viewers=size_t(u.player)<playerViewers.size() ? playerViewers[size_t(u.player)] : 0;
            const Fixed y=u.type->canFly?u.flightY:u.groundY;
            const RetailSightFootprint before=u.sightFootprint;
            // Exploration only adds bits. Already revealed blocks and cells
            // need no terrain calculation, and removal never changes this map.
            retailUpdateExploration(u.sightFootprint,u.x,y,u.z,uint8_t(seaLevel_),width,height,blockShift,
                [&](int x,int z){return explorationHeights_[size_t(z)*width+x];},
                [&](int bx,int bz){return (load(explorationBlocks_[size_t(bz)*blocksW+bx])&viewers)==viewers;},
                [&](int x,int z){return (load(navigationExplored_[size_t(z)*width+x])&viewers)!=viewers;},
                [&](int x,int z){
                    uint16_t& cell=navigationExplored_[size_t(z)*width+x];
                    if constexpr (kShared) __atomic_fetch_or(&cell,viewers,__ATOMIC_RELAXED); else cell|=viewers;
                },
                [&](int bx,int bz){summarizeBlock(bx,bz);});
            if (u.sightFootprint.active && (before.x!=u.sightFootprint.x || before.z!=u.sightFootprint.z ||
                before.eyeHeight!=u.sightFootprint.eyeHeight || !before.active)) markViewDirty(u.sightFootprint);
        }
    };
    // Below about 4k units the whole pass is ~0.1 ms: a fork-join (and the
    // atomics) would cost more than it saves, most of all in single player,
    // where the client's and the local referee's sims share the cores.
    SimPool& pool=simPool();
    constexpr size_t kMinParallel=4096;
    if (!pool.workers() || units_.size()<kMinParallel) {
        explore(0,units_.size(),std::false_type{});
        return;
    }
    pool.forChunks(units_.size(),SimPool::kUnitChunk,kMinParallel,[&](size_t,size_t begin,size_t end,unsigned) {
        explore(begin,end,std::true_type{});
    });
}

// The per-player current-view counters (player +0x88). One pass per tick
// after the tick, like exploration (retail runs it once per frame, 52654e ->
// 4f6ae0). Every footprint change is a full removal then a full add over the
// admitted cells, so the plane always equals the sum of the active footprints
// plus the death revealers. Count deltas commute, so chunks add atomically
// (relaxed) in any order to the same result.
void World::updateViewCounts() {
    if (explorationHeights_.empty()) return;
    const int width=hW_/2,height=hH_/2;
    const size_t cells=size_t(width)*height;
    const size_t planes=players_.size();
    if (viewCounts_.size()!=planes*cells) {
        // Fresh plane (first pass, or a player count change): every footprint
        // is re-added from scratch below.
        viewCounts_.assign(planes*cells,0);
        markViewDirtyAll();
        deathSights_.clear();
        for (auto& u:units_) { u.viewSight={};u.viewPlayer=-1; }
    }
    if (!planes) return;
    auto heightAt=[&](int x,int z){return explorationHeights_[size_t(z)*width+x];};
    auto change=[&](RetailSightFootprint& sight,int player,bool add,auto shared) {
        constexpr bool kShared=decltype(shared)::value;
        if (sight.active!=add) markViewDirty(sight);
        uint16_t* plane=viewCounts_.data()+size_t(player)*cells;
        retailSightFootprint(sight,add,false,width,height,uint8_t(player),heightAt,
            [&](int x,int z,int delta,uint16_t) {
                uint16_t& cell=plane[size_t(z)*width+x];
                if constexpr (kShared) __atomic_fetch_add(&cell,uint16_t(delta),__ATOMIC_RELAXED);
                else cell=uint16_t(cell+delta);
            });
    };
    // Deaths first, serially: a dying human-owned unit hands its footprint to
    // the revealer list (4c6750: LosOn, fewer than 20 for that player), any
    // other is removed at once (512ae0). The list keeps unit order.
    for (auto& u:units_) {
        if (u.alive() || !u.viewSight.active) continue;
        const int p=u.viewPlayer;
        const bool human=size_t(p)<planes && !players_[size_t(p)].automaticGates && !isNeutralPlayer(p);
        if (human && std::count_if(deathSights_.begin(),deathSights_.end(),
                [&](const RetailDeathSight& d){return d.player==p;})<20) {
            deathSights_.push_back({u.viewSight,int8_t(p),tickCounter_+60});
            u.viewSight.active=false;
        } else if (size_t(p)<planes) change(u.viewSight,p,false,std::false_type{});
        else u.viewSight.active=false;
        u.viewPlayer=-1;
    }
    auto pass=[&](size_t begin,size_t end,auto shared) {
        for (size_t index=begin;index<end;++index) {
            auto& u=units_[index];
            if (!u.alive() || !u.type || u.underConstruction) continue;
            // 4c67d0 + 4f5a35: an ownership change moves the footprint.
            if (u.viewSight.active && u.viewPlayer!=u.player) {
                if (size_t(u.viewPlayer)<planes) change(u.viewSight,u.viewPlayer,false,shared);
                u.viewSight.active=false;u.viewSight.x=-1;u.viewPlayer=-1;
            }
            const int16_t distance=int16_t(sightDistance(*u.type));
            if (u.viewSight.distance!=distance || u.viewSight.sightHeight!=u.type->sightHeight) {
                if (u.viewSight.active && size_t(u.viewPlayer)<planes) change(u.viewSight,u.viewPlayer,false,shared);
                u.viewSight={};u.viewSight.distance=distance;u.viewSight.sightHeight=u.type->sightHeight;
                u.viewPlayer=-1;
            }
            if (u.player<0 || size_t(u.player)>=planes || isNeutralPlayer(u.player)) continue;
            // 4f6a40 skips a status-locked unit (+0x108: stone, freeze): its
            // footprint is frozen where it stands, not removed.
            if (u.stonedFor>0 || u.frozenFor>0) continue;
            const Fixed y=u.type->canFly?u.flightY:u.groundY;
            uint16_t* plane=viewCounts_.data()+size_t(u.player)*cells;
            constexpr bool kShared=decltype(shared)::value;
            const RetailSightFootprint before=u.viewSight;
            retailUpdateSight(u.viewSight,u.x,y,u.z,uint8_t(seaLevel_),false,width,height,uint8_t(u.player),heightAt,
                [&](int x,int z,int delta,uint16_t) {
                    uint16_t& cell=plane[size_t(z)*width+x];
                    if constexpr (kShared) __atomic_fetch_add(&cell,uint16_t(delta),__ATOMIC_RELAXED);
                    else cell=uint16_t(cell+delta);
                });
            if (before.x!=u.viewSight.x || before.z!=u.viewSight.z || before.eyeHeight!=u.viewSight.eyeHeight ||
                before.active!=u.viewSight.active) {
                if (before.active) markViewDirty(before);
                if (u.viewSight.active) markViewDirty(u.viewSight);
            }
            u.viewPlayer=u.viewSight.active ? int8_t(u.player) : int8_t(-1);
        }
    };
    SimPool& pool=simPool();
    constexpr size_t kMinParallel=4096;
    if (!pool.workers() || units_.size()<kMinParallel) pass(0,units_.size(),std::false_type{});
    else pool.forChunks(units_.size(),SimPool::kUnitChunk,kMinParallel,[&](size_t,size_t begin,size_t end,unsigned) {
        pass(begin,end,std::true_type{});
    });
    // 4c6b30: revealers whose expiry is earlier than now are removed.
    std::erase_if(deathSights_,[&](RetailDeathSight& d) {
        if (d.expiry>=tickCounter_) return false;
        if (size_t(d.player)<planes) change(d.sight,d.player,false,std::false_type{});
        return true;
    });
}

bool World::playerSees(int player,const Unit& u) const {
    if (u.player==player) return true;                       // own units, cloaked or not
    if (!u.alive() || !u.type || u.cloaked || u.embarked()) return false;   // +0x114&4, 0x130&3
    const int width=hW_/2,height=hH_/2;
    const size_t cells=size_t(width)*height;
    if (viewCounts_.empty() || !cells) return true;
    if (player<0 || (size_t(player)+1)*cells>viewCounts_.size()) return false;
    const uint16_t* plane=viewCounts_.data()+size_t(player)*cells;
    // LosOn off (+LOS Off, +NowISee): a unit counts as seen where its cell is
    // explored. Retail tests the VIEW player's explored bit there (4223f0); a
    // power code here is one player's, so it tests that player's own bit.
    const bool explored=powerLosOff(player) && navigationExplored_.size()==cells;
    // Bounding-box corners in non-legacy bounds (4c1410): +/-footX*8,
    // +/-footZ*8, y..y+modelTop, as wrapping 16.16. A corner's cell is the
    // projected (x>>5, (z-(y>>1))>>5) of its whole-pixel words.
    const Fixed y=u.type->canFly?u.flightY:u.groundY;
    const int32_t hx=int32_t(uint32_t(8*u.type->footX)<<16),hz=int32_t(uint32_t(8*u.type->footZ)<<16);
    const int32_t x0=int32_t(uint32_t(u.x.v)-uint32_t(hx)),x1=int32_t(uint32_t(u.x.v)+uint32_t(hx));
    const int32_t z0=int32_t(uint32_t(u.z.v)-uint32_t(hz)),z1=int32_t(uint32_t(u.z.v)+uint32_t(hz));
    const int32_t top=int32_t(uint32_t(y.v)+uint32_t(u.type->modelTop)),bottom=y.v;
    auto seen=[&](int32_t x,int32_t cy,int32_t z) {
        const int px=int16_t(uint32_t(x)>>16),py=int16_t(uint32_t(cy)>>16),pz=int16_t(uint32_t(z)>>16);
        const int cx=px>>5,cz=(pz-(py>>1))>>5;
        if (uint32_t(cx)>=uint32_t(width) || uint32_t(cz)>=uint32_t(height)) return false;
        const size_t cell=size_t(cz)*width+cx;
        return explored ? (navigationExplored_[cell]>>player & 1)!=0 : plane[cell]!=0;
    };
    return seen(x0,top,z0) || seen(x1,top,z0) || seen(x1,bottom,z1) || seen(x0,bottom,z1);
}

// Power codes (sim/powercodes.h), Cmd::PowerCode's effect on every peer and the
// referee at the same tick. Retail applies each to the machine that typed it; here
// the money and vision codes act for the issuing player alone, and the damage
// codes for the whole game (what retail single player does).
void World::applyPowerCode(int player,PowerCode code,PowerArg arg,int unitId) {
    if (!powerCodes_ || player<0 || size_t(player)>=players_.size() || player>=16) return;
    const uint16_t bit=uint16_t(1u<<player);
    Player& owner=players_[size_t(player)];
    switch (code) {
        case PowerCode::Atm: {
            // 425d30: d = capacity - stored (float); stored += d and the lifetime
            // produced total += d. Income and the history are untouched.
            if (owner.retailResources) {
                auto& r=*owner.retailResources;
                const float d=r.capacity-r.stored;
                r.stored=float(r.stored+d);r.totalProduced+=double(d);
                owner.mana=r.stored;
            } else {
                owner.mana=double(owner.manaCapacity());
            }
            break;
        }
        case PowerCode::ManaMe: {
            // 426390: mana += 999999.0f, clamped to the type's maximum, on each of the
            // typer's selected units (one command per unit); units without a
            // personal pool are unchanged.
            Unit* u=unit(unitId);
            if (!u || !u->alive() || !u->type || u->player!=player || u->underConstruction ||
                u->type->maxMana<=0) break;
            u->mana=double(std::min(float(u->mana)+999999.0f,u->type->maxMana));
            break;
        }
        case PowerCode::DoubleShot:
            damageMode_=damageMode_==DamageMode::Double ? DamageMode::Normal : DamageMode::Double;
            break;
        case PowerCode::HalfShot:
            damageMode_=damageMode_==DamageMode::Half ? DamageMode::Normal : DamageMode::Half;
            break;
        case PowerCode::Los:
            // 4258e0: no argument toggles LosOn; "On"/"Off" set it; any other word
            // changes nothing.
            if (arg==PowerArg::Invalid) break;
            if (arg==PowerArg::On || (arg==PowerArg::None && (powerLosOff_ & bit))) powerLosOff_&=uint16_t(~bit);
            else powerLosOff_|=bit;
            break;
        case PowerCode::Mapping:
        case PowerCode::NowISee: {
            const bool reveal=code==PowerCode::NowISee || !(powerMappingOff_ & bit);
            if (code==PowerCode::NowISee) powerLosOff_|=bit;
            if (reveal) {
                // MappingOn off: the whole map explored for this player.
                powerMappingOff_|=bit;
                for (auto& mask:navigationExplored_) mask|=bit;
                markViewDirtyAll();
            } else {
                // 425980 refills the explored grid with 0: past exploration is
                // forgotten, and sight explores again from the next pass (every
                // footprint that explores for this player starts afresh).
                powerMappingOff_&=uint16_t(~bit);
                for (auto& mask:navigationExplored_) mask&=uint16_t(~bit);
                markViewDirtyAll();
                for (auto& u:units_)
                    if (u.type && allied(u.player,player)) u.sightFootprint={};
            }
            break;
        }
        default: break;   // Radar and MakePoster are the client's
    }
}

// 411700: the player's visible-enemy (+5) and visible-ally (+0x15) lists, in
// unit order. Sites still conjuring and neutral scenery stay off the enemy
// list, as they do for every other auto-targeting path here.
void World::rebuildVisibleLists(int player) {
    Player& owner=players_[size_t(player)];
    owner.visibleEnemies.clear();owner.visibleAllies.clear();
    TAK_PASS();
    for (const auto& u:units_) {
        if (!u.alive() || u.hp<=Fixed() || !u.type) continue;
        if (allied(player,u.player)) {
            if (playerSees(player,u)) owner.visibleAllies.push_back(u.id);
        } else if (!u.underConstruction && !isNeutralPlayer(u.player) && playerSees(player,u))
            owner.visibleEnemies.push_back(u.id);
    }
    // units_ is id order in practice; sort so lookups may binary-search.
    std::sort(owner.visibleEnemies.begin(),owner.visibleEnemies.end());
    std::sort(owner.visibleAllies.begin(),owner.visibleAllies.end());
}

SimPool& World::simPool() {
    const unsigned want=serialThreads_ ? 0u : simWorkers_>=0 ? unsigned(simWorkers_) : SimPool::defaultWorkers();
    if (!pool_ || pool_->workers()!=want) pool_=std::make_unique<SimPool>(want);
    return *pool_;
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
        TAK_PASS();
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
static int productionExitDistance(const UnitType* t) {
    // The unchanged retail navigator may finish short of a crowded goal by
    // 50/halfCellTicks cells. Even that accepted stop must clear the birth site.
    const int tolerance=t->canFly ? 0 : ((t->halfCellTicks>0 ? 50/t->halfCellTicks : 0)+1)*16;
    return std::max(60,footCells(t)*8+12+tolerance+16);
}

bool World::exitSpot(const UnitType* t, float fx, float fz, float& outX, float& outZ, int departingId) const {
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
    // Earlier output may still be walking to its exit. Reserve its current leg's
    // destination too, or successive completions choose the same apparently free
    // spot and the later unit stops in the doorway when traffic blocks arrival.
    std::vector<std::pair<float,float>> reserved;
    const Unit* departing=departingId ? unit(departingId) : nullptr;
    if(departing)TAK_PASS();
    if(departing)for(const auto& u:units_) {
        if(!u.alive() || !u.type || u.underConstruction || u.orders.empty())continue;
        const auto& o=u.orders[currentLeg(u.orders)];
        if(o.targetId || o.buildType || o.reclaimFeat || o.repairTarget)continue;
        const auto target=o.missionTarget.value_or(std::pair{o.x,o.z});
        const float x=target.first.toFloat(),z=target.second.toFloat();
        if(std::abs(x-fx)<=256 && std::abs(z-fz)<=256)reserved.emplace_back(x,z);
    }
    for (int r = 0; r <= 12; ++r)
        for (int j = -r; j <= r; ++j)
            for (int i = -r; i <= r; ++i) {
                if (std::max(std::abs(i), std::abs(j)) != r) continue;   // ring only
                const int nx = cx + i, nz = cz + j;
                if (!g.fits(nx, nz, foot)) continue;
                // Centre spacing alone admits points inside a large mobile
                // producer's rectangle. Those children start overlapping it
                // and cannot take even their first collision-checked step.
                const auto bodies=searchBodyRect(nx-t->footX/2,nz-t->footZ/2,t->footX,t->footZ);
                if (std::any_of(bodies.cells.begin(),bodies.cells.end(),[&](const Unit* body) {
                    return body && body->id!=departingId;
                })) continue;
                const float wx = footprintWaypoint(nx, foot).toFloat(), wz = footprintWaypoint(nz, foot).toFloat();
                // Ring search may retreat toward the birth position when its
                // preferred exit is occupied. Keep the destination outside that
                // footprint with enough room for the departing body's full width.
                if(departing) {
                    const float dx=wx-departing->x.toFloat(),dz=wz-departing->z.toFloat();
                    const float distance=float(productionExitDistance(t));
                    if(std::max(std::abs(dx),std::abs(dz))<distance)continue;
                }
                bool taken = false;
                forEachNear(wx, wz, clr, [&](int idx) {
                    const Unit& e = units_[size_t(idx)];
                    if (!e.alive() || !e.type) return;
                    const float dx = e.x.toFloat() - wx, dz = e.z.toFloat() - wz;
                    if (dx * dx + dz * dz < clr * clr) taken = true;
                });
                for(const auto& [x,z]:reserved) {
                    const float dx=x-wx,dz=z-wz;
                    if(dx*dx+dz*dz<clr*clr)taken=true;
                }
                if (taken) continue;
                outX = wx;
                outZ = wz;
                return true;
            }
    return false;
}

// GetBuilt's release (0x402220, emulated): a structure's finished output with no
// queued QMove/QPatrol order gets PARK around the factory, padding
// (rand(6)+rand(3)+3)*16, or +6 for a flyer (0x4023b5..0x4023d3), whose PARK
// is the flight branch (queueFlyerPark). The ground PARK's ring
// (407ca0, retailGroundPark) is footX*16+padding+half the factory's diagonal
// out to three times that. Retail tests no reachability: a yard on a small pond
// could park its ship in the next body of water and leave it on the pad, holding
// the yard. So the ring is kept only when one of its cells is connected to the
// birth point for this unit's movement class (component roots, never raw ids);
// otherwise the output moves to the farthest free connected cell clear of the
// factory within that ring's reach, and stays only if there is none.
void World::parkProductionOutput(int id,int factoryId) {
    const uint32_t first=gameRand(6),second=gameRand(3);
    Unit* u=unit(id);const Unit* f=unit(factoryId);
    if (!u || !f || !u->type || !f->type) return;
    const UnitType* t=u->type;
    if (t->canFly) { queueFlyerPark(id,factoryId,(first+second+6)*16); return; }
    const uint32_t padding=(first+second+3)*16;
    const uint64_t diagonal=uint64_t(int64_t(f->type->footX)*f->type->footX+int64_t(f->type->footZ)*f->type->footZ);
    const int radius=int(uint32_t(t->footX*16)+padding+uint32_t(isqrt64(diagonal*256)/2));
    const RetailRingGoal ring{footprintOrigin(f->x,t->footX),footprintOrigin(f->z,t->footZ),
                              radius,radius*3,retailCircleRadiusSquared(radius*3)};
    const NavGrid& g=navFor(t);
    const int foot=footCells(t);
    const CompGrid* cg=g.empty() ? nullptr : components(g,foot);
    int32_t birth=-1;
    if (cg && !cg->empty()) {
        const int bx=footprintCell(u->x,foot),bz=footprintCell(u->z,foot);
        for (int r=0;r<=2 && birth<0;++r)
            for (int j=-r;j<=r && birth<0;++j) for (int i=-r;i<=r && birth<0;++i)
                if (std::max(std::abs(i),std::abs(j))==r) birth=cg->componentAt(bx+i,bz+j);
    }
    const int reach=(radius*3)/16+2;
    bool ringReachable=birth<0;   // no labelling to test against: keep retail's ring
    for (int dz=-reach;dz<=reach && !ringReachable;++dz)
        for (int dx=-reach;dx<=reach && !ringReachable;++dx)
            ringReachable=ring.accepts(ring.x+dx,ring.z+dz) &&
                cg->componentAt(ring.x+dx+t->footX/2,ring.z+dz+t->footZ/2)==birth;
    if (ringReachable) {
        Order order;order.goal=order.groundMission=true;order.park.emplace();
        order.park->target=factoryId;
        order.mission.flags=0x200u; // PARK definition retains its live target.
        order.park->padding=padding;
        // Legion spreads its outputs over the band (legionParkSpot); the door
        // bearing is the birth point's, seen from the factory's centre (not
        // the ring's, snapped to this footprint's cells: a Navy Yard's pad is
        // only 25 px out).
        if (isLegionPathfinding(pathfindingMode_) && legion_) {
            order.park->spread=true;
            order.park->door=uint16_t(retailDirection(u->x-f->x,u->z-f->z).v);
        }
        u->orders.push_back(order);
        return;
    }
    // Clear of the factory's rectangle by this unit's footprint, free of bodies.
    const int fx0=footprintOrigin(f->x,f->type->footX)-t->footX,fz0=footprintOrigin(f->z,f->type->footZ)-t->footZ;
    const int fx1=fx0+f->type->footX+t->footX,fz1=fz0+f->type->footZ+t->footZ;
    const int ux=footprintOrigin(u->x,t->footX),uz=footprintOrigin(u->z,t->footZ);
    std::optional<std::pair<int,int>> best;int64_t bestDistance=-1;
    for (int dz=-reach;dz<=reach;++dz)
        for (int dx=-reach;dx<=reach;++dx) {
            const int ox=ring.x+dx,oz=ring.z+dz;
            if (ox>fx0 && ox<fx1 && oz>fz0 && oz<fz1) continue;
            if (int64_t(ring.squared(ox,oz))>ring.outerSquared) continue;
            if (cg->componentAt(ox+t->footX/2,oz+t->footZ/2)!=birth) continue;
            const int64_t distance=int64_t(ox-ux)*(ox-ux)+int64_t(oz-uz)*(oz-uz);
            if (distance<=bestDistance) continue;
            const auto bodies=searchBodyRect(ox,oz,t->footX,t->footZ,id);
            if (std::any_of(bodies.cells.begin(),bodies.cells.end(),[](const Unit* b){return b!=nullptr;})) continue;
            best=std::pair{ox,oz};bestDistance=distance;
        }
    if (best) order(id,footprintWaypoint(best->first+t->footX/2,t->footX).toFloat(),
                    footprintWaypoint(best->second+t->footZ/2,t->footZ).toFloat(),false);
}

// The door lane of a spread PARK: a corridor about a door wide (half width
// legionParkLaneHalf) straight out of the factory along `door`, from the
// ring's centre out without end. A body of footprint fx x fz centred at
// (x,z) (Q16 px) is in it when its centre lies ahead of the ring's centre
// and within the half width + its reach (8*max(fx,fz)) + `margin` px of the
// axis.
bool World::legionParkInLane(const RetailParkState& park,const UnitType& factory,
                             int64_t x,int64_t z,int fx,int fz,int margin) {
    if (!park.ring || !park.spread) return false;
    const int64_t vx=x-(int64_t(park.ring->x)*16+fx*8)*65536,vz=z-(int64_t(park.ring->z)*16+fz*8)*65536;
    const int64_t dx=retailScaledSine(park.door,65536),dz=retailScaledCosine(park.door,65536);
    const int64_t along=(vx*dx+vz*dz)>>16,cross=(vx*dz-vz*dx)>>16;
    return along>0 && std::abs(cross)<(int64_t(legionParkLaneHalf(factory))+8*std::max(fx,fz)+margin)*65536;
}

// Legion's spread PARK (ours, not retail). Retail steers a ground PARK to the
// ring's mid-radius along the unit's bearing from the factory (4e33b0) and
// accepts arrival anywhere in the band; every output of one door shares that
// bearing, and Legion's exact solo goal queued them into a column out of the
// door. Here each output claims a spot of its own:
//  * candidates in layers over the band (inner layer first), centres spaced
//    two footprints apart (a footprint's gap, so a later body passes between
//    parked ones), nearest the door bearing first, then alternating sides;
//  * never in the door lane: a corridor straight out of the exit, about a
//    door wide, from the factory out without end, so a new output always has
//    a free way out;
//  * a spot is taken while another unit on its PARK leg has claimed one that
//    comes within a footprint of it, or while any body stands within a
//    footprint of it (bodies, not claims, hold finished outputs' spots);
//  * only cells connected to the unit (component roots, as the reachability
//    test in parkProductionOutput), legal for its footprint;
//  * a full band grows outward with more layers past the outer radius; a
//    spot there completes the PARK on arrival (groundMissionAccepts), which
//    retail's ring would not.
// Integer and retail sine-table math; claims are a set test over all units,
// so their order does not matter. None found: the caller keeps 4e33b0's point.
std::optional<std::pair<int,int>> World::legionParkSpot(const Unit& u,const RetailParkState& park,
                                                       const Unit& factory) const {
    if (!park.ring || !u.type || !factory.type) return std::nullopt;
    const RetailRingGoal& ring=*park.ring;
    const UnitType& t=*u.type;
    const int fx=t.footX,fz=t.footZ,foot=std::max(1,std::max(fx,fz)),cells=footCells(&t);
    const int64_t cx=(int64_t(ring.x)*16+fx*8)*65536,cz=(int64_t(ring.z)*16+fz*8)*65536;
    // The unit's component, searched around it as for the birth point.
    const NavGrid& g=navFor(&t);
    const CompGrid* cg=g.empty() ? nullptr : components(g,cells);
    int32_t home=-1;
    if (cg && !cg->empty()) {
        const int bx=footprintCell(u.x,cells),bz=footprintCell(u.z,cells);
        for (int r=0;r<=2 && home<0;++r)
            for (int j=-r;j<=r && home<0;++j) for (int i=-r;i<=r && home<0;++i)
                if (std::max(std::abs(i),std::abs(j))==r) home=cg->componentAt(bx+i,bz+j);
    }
    // Spots other PARK legs hold, around enough to matter.
    struct Box {int x,z,w,h;};
    std::vector<Box> claims;
    const int64_t reachCells=int64_t(ring.outerTolerance)/16+kLegionParkLayers*2*foot+4*foot+16;
    for (const Unit& v:units_) {
        if (v.id==u.id || !v.alive() || !v.type || v.orders.empty()) continue;
        const Order& o=v.orders[currentLeg(v.orders)];
        if (!o.park || !o.park->spot) continue;
        const auto [sx,sz]=*o.park->spot;
        if (std::abs(int64_t(sx)-ring.x)>reachCells || std::abs(int64_t(sz)-ring.z)>reachCells) continue;
        claims.push_back({sx,sz,v.type->footX,v.type->footZ});
    }
    // At each angle off the door, the side the body is on first: two outputs
    // leaving the lane for opposite sides never cross.
    const bool side=[&] {
        const int64_t vx=int64_t(u.x.v)-cx,vz=int64_t(u.z.v)-cz;
        const int64_t dx=retailScaledSine(park.door,65536),dz=retailScaledCosine(park.door,65536);
        // A positive angle step off the door is a positive cross product.
        return ((vx*dz-vz*dx)>>16)>=0;
    }();
    const int spacing=32*foot;
    const int bandEnd=ring.outerTolerance-8*foot-16;
    int radius=ring.innerRadius+8*foot+16;
    for (int layer=0;layer<kLegionParkLayers;++layer,radius+=spacing) {
        const bool inBand=radius<=bandEnd;
        const int count=int(std::clamp<int64_t>(int64_t(radius)*6283/(1000*int64_t(spacing)),6,64));
        const int step=65536/count;
        for (int j=0;j<count;++j) {
            const int k=(j+1)/2;
            const uint16_t h=uint16_t(int(park.door)+(((j%2)!=0)==side ? k*step : -k*step));
            const int64_t px=cx+retailScaledSine(h,int32_t(radius)*65536);
            const int64_t pz=cz+retailScaledCosine(h,int32_t(radius)*65536);
            // Out of the door lane by a body's reach more than a parked body
            // must keep, and the cell its arrival may stop short.
            if (legionParkInLane(park,*factory.type,px,pz,fx,fz,8*foot+16)) continue;
            const Fixed spotX=Fixed::raw(int32_t(px)),spotZ=Fixed::raw(int32_t(pz));
            const int ox=footprintOrigin(spotX,fx),oz=footprintOrigin(spotZ,fz);
            if (ox<0 || oz<0 || ox+fx>terW_ || oz+fz>terH_) continue;
            if (inBand && !ring.accepts(ox,oz)) continue;
            if (cg && home>=0 && cg->componentAt(footprintCell(spotX,cells),footprintCell(spotZ,cells))!=home) continue;
            const Box around{ox-foot,oz-foot,fx+2*foot,fz+2*foot};
            if (std::any_of(claims.begin(),claims.end(),[&](const Box& b){
                    return around.x<b.x+b.w && b.x<around.x+around.w && around.z<b.z+b.h && b.z<around.z+around.h;}))
                continue;
            const auto bodies=searchBodyRect(around.x,around.z,around.w,around.h,u.id);
            if (std::any_of(bodies.cells.begin(),bodies.cells.end(),[](const Unit* b){return b!=nullptr;})) continue;
            if (!mapPlacementCells_.empty() && !mobilePlacement(t,uint16_t(u.id),ox,oz,true)) continue;
            return std::pair{ox,oz};
        }
    }
    return std::nullopt;
}

// PARK for a flyer (407ca0's flight branch, see retailFlightParkOffset). It is
// queued as the plain VTOL_Move it pushes, carrying the park until that move
// first runs: resolveFlyerPark then draws the angle and fixes the point, as the
// PARK stage does when its mission runs rather than at the release. Retail
// does not test reachability here and needs none: flyers do not hold ground
// while airborne, so no position can leave one on its factory's pad.
void World::queueFlyerPark(int id,int factoryId,uint32_t padding) {
    const Unit* u=unit(id);
    if (!u || !u->type || !u->type->canFly) return;
    order(id,u->x.toFloat(),u->z.toFloat(),false);
    Unit* p=unit(id);
    if (!p || p->orders.empty()) return;
    auto& leg=p->orders.back();
    leg.park.emplace();leg.park->target=factoryId;leg.park->padding=padding;
}

// The park's point, resolved by VTOL_Move's stage 0. Around the target while
// it is live; around the flyer itself, without the diagonal, once it is not
// (0x407f0a). The leg keeps its park (attempts 1: the point is fixed) until it
// retires on arrival, as retail's PARK lasts until its flight's arrival: so the
// AI still sees a parked newborn (AI::idle), free to command, while it flies. Keeping the point on the map is ours: retail lets a park ring
// overhang the edge. An axis that would leave the map is mirrored through the
// centre first, so the flyer still ends a full radius from its factory instead
// of being clamped back towards the pad; the clamp is the last resort.
void World::resolveFlyerPark(Unit& u,Order& leg) {
    const RetailParkState park=*leg.park;
    leg.park->attempts=1;
    const Unit* target=park.target ? unit(park.target) : nullptr;
    const bool live=target && target->alive() && target->type;
    const uint16_t angle=uint16_t(gameRand(65536));
    const uint32_t radius=retailParkRadius(int16_t(u.type->footX),park.padding,live,
        int16_t(live ? target->type->footX : 0),int16_t(live ? target->type->footZ : 0));
    const auto [dx,dz]=retailFlightParkOffset(angle,radius);
    const auto place=[](Fixed centre,int32_t offset,int cells,int foot) {
        int64_t at=int64_t(centre.v)+offset;
        if (cells<=0) return Fixed::raw(int32_t(at));
        const int64_t low=int64_t(foot)*8*65536,high=std::max(low,int64_t(cells)*16*65536-low);
        if (at<low || at>high) at=int64_t(centre.v)-offset;
        return Fixed::raw(int32_t(std::clamp(at,low,high)));
    };
    leg.x=place(live ? target->x : u.x,dx,terW_,u.type->footX);
    leg.z=place(live ? target->z : u.z,dz,terH_,u.type->footZ);
    leg.flightGoal.reset();
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
            const bool refused=!unit.type->canFly && (unit.bodyBlockStreak>=2 || unit.legionStill);
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
        case 26: case 31:
            if (factory.dying) {
                // 51e380 advances a newly written timer in this same owner
                // update. The initial death dispatch is outside that update.
                const int elapsed=unit.alive() ? 0 : unit.deadFor+1;
                const int delay=id==26 ? 1 : (elapsed==0 ? 35 : 34);
                unit.deathAnimationTicks=std::min(unit.deathAnimationTicks,elapsed+delay);
                factory.deathStopValue=id;
            }
            break;
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
                world.scriptYardById_[size_t(unit.id)]=factory.yardOpen;
                // 507c70: mark the structure refused and restamp it, which
                // claims the newly stamped yard cells and frees the others.
                unit.ground.flags|=Unit::GroundStamp::kRefused;
                world.groundRestamp(unit);
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
           piece<0 || size_t(piece)>=factory.peek().pieces.size())return;
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
                if(node.scriptPiece>=0 && size_t(node.scriptPiece)<factory.peek().pieces.size()) {
                    const auto& state=factory.peek().pieces[size_t(node.scriptPiece)];
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
        const std::array<int32_t,3> base={unit.x.v,
            (unit.type->canFly ? unit.flightY : unit.groundY).v,unit.z.v};
        const auto position=retailScriptEffectPosition(base,unit.type->productionModel,
            factory.peek().pieces,piece,portHeadingToRetail(unit.heading),
            unit.groundPitch,unit.groundRoll);
        world.scriptEmissions_.push_back({world.tickCounter_,unit.id,unit.player,piece,code,position});
    }
    uint32_t sound(int name,int32_t flags) {
        // Audio remains client-owned; a dying VM's sounds are presentation
        // events (ScriptSound), in script order.
        if(factory.dying)
            world.scriptSounds_.push_back({world.tickCounter_,unit.id,unit.player,unit.x,unit.z,unit.type,name,flags,
                unit.type->canFly ? unit.flightY : unit.groundY,unit.cloaked || unit.embarked()});
        return 0; // native unit sound host 50def0 always returns zero
    }
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
                noteShotImpact(p.fromId,p.targetId,w,p.position,hit.unitId);
                applyHit(w,Fixed::raw(p.position[0]).toFloat(),Fixed::raw(p.position[2]).toFloat(),
                    p.fromPlayer,p.fromId,unit(hit.unitId),p.position);
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
                noteShotImpact(flame.fromId,flame.targetId,w,flame.position,hit.unitId);
                applyHit(w,Fixed::raw(flame.position[0]).toFloat(),Fixed::raw(flame.position[2]).toFloat(),
                    flame.owner,flame.fromId,unit(hit.unitId),flame.position);
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

struct World::AirOccupancyHost {
    World& w;
    Unit* live(int id) {
        Unit* u=w.unit(id);
        return u && u->type && (u->alive() || id==w.airRetiring_) ? u : nullptr;
    }
    RetailAirOccupant* state(int id) { Unit* u=live(id); return u ? &u->airOccupant : nullptr; }
    bool footprint(int id,int& x,int& z,int& fx,int& fz) {
        const Unit* u=w.unit(id);
        fx=u->type->footX;fz=u->type->footZ;
        x=footprintOrigin(u->x,fx);z=footprintOrigin(u->z,fz);
        return true;
    }
    bool airborne(int id) { const Unit* u=w.unit(id); return u->flightGroundMode==2 && !u->embarked(); }
    template<class F> void forEachFlyer(F f) {
        for (const auto& u:w.units_) if (u.type && u.type->canFly && (u.alive() || u.id==w.airRetiring_)) f(u.id);
    }
};

// 512ae0 -> 5066a0 -> 507050(unit,0): a retiring flyer hands each cell it
// owns to a random overlapping list member. World retires a body from the
// occupancy planes when it dies, so this runs at that transition.
void World::retireAirOccupant(Unit& u) {
    if (!u.type || !u.type->canFly || u.airOccupant.recordedX<0 || airOccupancy_.empty()) return;
    AirOccupancyHost host{*this};
    airRetiring_=u.id;
    airOccupancy_.remove(u.id,host,[&](unsigned n){return gameRand(int32_t(n));});
    airRetiring_=0;
    u.airOccupant.recordedX=u.airOccupant.recordedZ=-9999;
}

// 51da6a..51dbbe: after the unit updates, clear every live flyer's recorded
// footprint (507050(unit,1)), then insert them all (506c40), each pass in
// player/entity order. The grid persists between ticks: the landing predicate
// and projectiles read it.
void World::syncUnitLists() {
    // A restored retail world is re-sorted by id every tick: rebuild then.
    unitLists_.sync(units_,retailAllocation_);
#ifndef NDEBUG
    if (verifyLists()) {
        UnitClassLists fresh;
        fresh.sync(units_);
        if (!(fresh==unitLists_)) {
            std::fprintf(stderr,"TAK_VERIFY_LISTS tick %u: unit class lists differ from a walk of the units\n",tickCounter_);
            std::abort();
        }
    }
#endif
}

std::span<const int> World::updateAirOccupancy() {
    if (airOccupancy_.width()!=hW_ || airOccupancy_.height()!=hH_) airOccupancy_.reset(hW_,hH_);
    std::vector<Unit*> flyers;
    syncUnitLists();
    for (const int id:unitLists_.flyers) {
        Unit& u=*unit(id);
        if (u.alive()) flyers.push_back(&u);
        else if (u.airOccupant.recordedX>=0) retireAirOccupant(u); // a removal path without a death hook
    }
    if (flyers.empty()) return airOccupancy_.cells();
    std::sort(flyers.begin(),flyers.end(),[](const Unit* a,const Unit* b) {
        return a->player!=b->player ? a->player<b->player : a->id<b->id;
    });
    AirOccupancyHost host{*this};
    for (auto* u:flyers) airOccupancy_.clear(u->id,host);
    for (auto* u:flyers) airOccupancy_.insert(u->id,host,[&](unsigned n){return gameRand(int32_t(n));});
    return airOccupancy_.cells();
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
    const auto it=unitScript(unitId);
    if(!it)return point;
    const auto& file=*u->type->script();
    std::array<uint32_t,4> args{0,uint32_t(slot),0,0};
    ScriptHost host{*this,*u,*it};
    it->state().query(file,file.scriptIndex(sweetSpot ? "SweetSpot" : "QueryWeapon"),args,host);
    const int32_t piece=std::bit_cast<int32_t>(args[0]);
    if(piece<0 || size_t(piece)>=it->peek().pieces.size())return point;
    std::array<int32_t,3> offset{};
    if(sweetSpot) {
        // 4dd2a0 over the drawn vertex buffer: the piece's posed bounds centre
        // (retailPieceDrawnCenter), always from the current simulation pose.
        if(size_t(piece)>=u->type->scriptPieceVertices.size())return point;
        // The unit's cache answers while the posed chain is unchanged.
        thread_local RetailDrawnChain chain;
        retailPieceDrawnChain(chain,u->type->productionModel,it->peek().pieces,piece,
            portHeadingToRetail(u->heading),u->groundPitch,u->groundRoll);
        offset=it->sweetSpotCache.get(chain,u->type->scriptPieceVertices[size_t(piece)]);
    } else offset=retailPieceOrigin(u->type->productionModel,it->peek().pieces,piece,
        portHeadingToRetail(u->heading),u->groundPitch,u->groundRoll);
    for(size_t axis=0;axis<3;++axis)
        point[axis]=std::bit_cast<int32_t>(uint32_t(point[axis])+uint32_t(offset[axis]));
    return point;
}

// 51aa50 for a unit target: its SweetSpot (4dd4f0) plus 80% of its mover's
// motion over the shot's flight time from the shooter's origin; no lead for
// dontleadtargets, a zero speed, a target without a mover or no weapon.
std::array<int32_t,3> World::retailLeadPoint(const Unit& u,const Unit& target,const Weapon* weapon) {
    const std::array<int32_t,3> origin{u.x.v,(u.type->canFly ? u.flightY : u.groundY).v,u.z.v};
    const auto sweetSpot=queryUnitScriptPoint(target.id,true);
    if(!weapon)return sweetSpot;
    const auto step=retailGroundStep(target.heading,target.speed);
    const std::array<int32_t,3> velocity=target.type->canFly ?
        std::array<int32_t,3>{target.flightVelocity.x,target.flightVelocity.y,target.flightVelocity.z} :
        std::array<int32_t,3>{step.s.v,0,step.c.v};
    return tak::retailAimLead(origin,sweetSpot,velocity,tak::retailAimSpeed(weapon->projVel),
        weapon->noLead || target.type->isStructure());
}

std::optional<World::WeaponAimSolution> World::queryWeaponAim(int unitId,int targetId,int slot) {
    auto* u=unit(unitId);auto* target=unit(targetId);
    if(!u || !target || !u->type || !target->type || slot<0 || size_t(slot)>=u->type->weapons.size())return {};
    if(!target->alive() || (target->hp<=Fixed() && !(target->retailSite && target->underConstruction)))return {};
    const auto& weapon=u->type->weapons[size_t(slot)];
    const std::array<int32_t,3> origin{u->x.v,(u->type->canFly ? u->flightY : u->groundY).v,u->z.v};
    WeaponAimSolution result;
    result.source=origin;
    result.target={target->x.v,(target->type->canFly ? target->flightY : target->groundY).v,target->z.v};
    const auto heading=portHeadingToRetail(u->heading);
    result.heading=uint16_t(retailDirection(target->x-u->x,target->z-u->z).v-heading);
    const bool aimed=weapon.ballistic || weapon.beam || weapon.kind==Weapon::Kind::Guided;
    if(aimed)result.source=queryUnitScriptPoint(unitId,false,slot);
    // 52fe30 turns the body toward the point the slot aims at: a melee
    // target's origin, every other class's led SweetSpot (51aa50).
    const auto led=weapon.melee ? result.target : retailLeadPoint(*u,*target,&weapon);
    result.facing=retailSlotFacing(origin,result.target,led,weapon.melee);
    if(aimed) {
        const auto source=result.source;
        const int32_t speed=tak::retailAimSpeed(weapon.projVel);
        result.target=led;
        std::array<int32_t,3> delta;
        for(size_t axis=0;axis<3;++axis)
            delta[axis]=std::bit_cast<int32_t>(uint32_t(result.target[axis])-uint32_t(source[axis]));
        const auto angles=retailDirectAim(delta[0],delta[1],delta[2],heading);
        result.heading=angles[0];result.pitch=angles[1];
        if(weapon.ballistic) {
            result.pitch=tak::retailBallisticPitch(float(delta[0])/65536.f,float(delta[1])/65536.f,
                float(delta[2])/65536.f,float(speed)/65536.f,weapon.gravityAdj,weapon.lobPreferred,
                ballisticGravityRaw_);
            // 52bdf0 aims at pitch 0 when the arc cannot reach. Retail never
            // fires then (52bc60 already ruled the target out of range); the
            // muzzle-to-SweetSpot solve can still fail where the origin solve
            // passed, so no shell is launched on it (tickScriptWeapon, fire).
            if(result.pitch==0x8000) {
                result.pitch=0;
                result.unreachable=weapon.ballisticClass();
            }
        }
    }
    return result;
}

void World::clearScriptWeaponTarget(Unit& u,bool fromCommand) {
    const auto it=unitScript(u.id);
    if(it) {
        const auto& file=*u.type->script();
        for(size_t slot=0;slot<std::min(size_t(3),u.type->weapons.size());++slot) {
            // 51a7f0 retires all targets, but only the selected callback can
            // acknowledge/reset a switcher's weapon state.
            if(u.type->weaponSwitching && int(slot)!=u.weaponSlot)continue;
            // AirToGround's and AirToAir's unselected slots are auto: the
            // order's end (4d6da0's 519b10(3)) leaves them and their targets
            // alone.
            if(u.slotAuto!=7 && (u.slotAuto>>slot&1))continue;
            // This callback also retires the slot's swept target, so a later
            // dropSlotTarget (boarding's switch to manual) does not announce
            // TargetCleared a second time.
            u.slotTarget[slot]=0;
            if(fromCommand)u.pendingWeaponAnimations.push_back({tak::RetailWeaponAnimation::Clear,uint8_t(slot),0,0});
            else u.weaponAnimations.add(tak::RetailWeaponAnimation::Clear,int(slot));
            // Native 51a7f0 starts TargetCleared with immediate=0. The callback
            // thread runs in the next regular COB pass, not inline in target
            // retirement; its SET 21 remains script-owned and takes effect there.
            it->state().startArguments(file,file.scriptIndex("TargetCleared"),{uint32_t(slot),0,0,0},1);
        }
    }
    u.scriptAimTarget=0;
}

bool World::tickScriptWeapon(Unit& u,Unit& target,int slot,bool swept,const WeaponAimSolution* given) {
    const auto it=unitScript(u.id);
    if(!it || slot<0 || slot>=3)return false;
    const auto& file=*u.type->script();
    const int aimScript=file.scriptIndex("AimWeapon"),fireScript=file.scriptIndex("FireWeapon");
    // Native callback lookup may fail, but it never substitutes an immediate
    // shot: readiness/release still require the script's SET 22 / SET 23.
    // A caller that already solved this slot this tick (for the body's
    // facing request) hands its solution in.
    const auto solution=given ? std::optional<WeaponAimSolution>(*given) : queryWeaponAim(u.id,target.id,slot);
    if(!solution)return true;
    // A swept (auto-slot) target lives in the slot record, not in the
    // order-driven scriptAimTarget, which tickCombat retires with its order.
    if(!swept)u.scriptAimTarget=target.id;
    u.weaponAimPoints[size_t(slot)]=solution->target;
    u.weaponMuzzlePoints[size_t(slot)]=solution->source;
    auto& aim=u.weaponAim[size_t(slot)];const auto& weapon=u.type->weapons[size_t(slot)];
    // DroppedBallisticWeapon's angle virtual (+0x28, 4d4450) is a bare ret,
    // so 52fd80 leaves its record's angles as they were: AimWeapon starts with
    // them and the gate's angle test always passes. 52ae90 still runs aim
    // start (52fe30) and the gate (52fff0) for the class, so a bomb waits for
    // the script's SET 22 and, on a hover attacker, for its facing request.
    const bool dropped=weapon.kind==Weapon::Kind::Dropped;
    const uint16_t aimHeading=dropped ? aim.heading : solution->heading;
    const uint16_t aimPitch=dropped ? aim.pitch : solution->pitch;
    if(aim.start(aimHeading,aimPitch)) {
        u.weaponAnimations.add(tak::RetailWeaponAnimation::Aim,slot,aimHeading,aimPitch);
        // Native 52fe30 starts AimWeapon with immediate=0. Starting the VM here
        // only queues its thread; the following unit-script phase runs its body.
        it->state().startArguments(file,aimScript,{aimHeading,aimPitch,uint32_t(slot),0},3);
    }
    const bool available=u.reloads[slot]==0 &&
        (u.type->maxMana<=0 || u.mana>=weapon.manaCost);
    // Native readiness (52fff0) compares the body heading with the mover's
    // requested heading, which 52fe30 has just posted toward the slot's point:
    // the led SweetSpot, or a melee target's origin. Only the units 52fe30
    // posts a request for wait on it: a ground mover with a turninplacerate,
    // or a HOVERATTACK flyer. Every other flyer, a ground unit that cannot
    // pivot and a structure fire on COB readiness alone.
    const bool aligned=!slotNeedsFacing(*u.type) ||
        retailFacingAligned(portHeadingToRetail(u.heading),solution->facing,uint16_t(weapon.aimTol));
    // No arc to the aim point: hold the FireWeapon start until there is one.
    // The release (SET 23) launches on the angles accepted here (fire), so a
    // shell never leaves on the flat pitch an unreachable solve aims at.
    const bool ready=aim.ready(aimHeading,aimPitch,uint16_t(weapon.aimTol),available,aligned);
    if(ready && solution->unreachable) {
        // Aimed, loaded, and still no arc: a dud for the blocked-shot count
        // (noteNoArc), one per nominal reload.
        u.reloads[slot]=uint16_t(int32_t(weapon.reload*kTick+0.5f));
        noteNoArc(u,target);
    }else if(ready) {
        const uint16_t nominal=uint16_t(int32_t(weapon.reload*kTick+0.5f));
        u.reloads[slot]=retailWeaponReload(nominal,uint16_t(crtRand(0x530167)));
        const bool all=!u.type->weaponSwitching && u.type->weapons.size()>1;
        u.missionEvents|=retailWeaponFireEvent(0,0x10000,all ? 0xc0000000u : uint32_t(slot)<<30,aim.flags);
        u.fireAnimations|=uint32_t(1)<<slot;
        u.weaponAnimations.add(tak::RetailWeaponAnimation::Fire,slot);
        // Native 530140 also starts FireWeapon with immediate=0. SET 23 can
        // therefore only be observed by a later common weapon update.
        it->state().startArguments(file,fireScript,{uint32_t(slot),0,0,0},1);
    }
    if(aim.flags&16) {
        fire(u,target,slot,true);
        aim.projectileCreated();
    }
    return true;
}

void World::startWorkAnimation(Unit& u,Fixed targetX,Fixed targetZ) {
    // Repair/Reclaim's work stage (next to 4d4ab0) owns the weapons.
    setSlotsManual(u,kSlotsWork);
    if(u.workScriptWorking)return;
    u.workScriptWorking=true;
    if(auto script=unitScript(u.id);script) {
        const auto& file=*u.type->script();
        const uint16_t heading=uint16_t(retailDirection(u.x-targetX,u.z-targetZ).v);
        script->state().startArguments(file,file.scriptIndex("StartBuilding"),
            {uint16_t(heading-portHeadingToRetail(u.heading)),1,0,0},2);
    }
}

void World::stopWorkAnimation(Unit& u) {
    if(!u.workScriptWorking)return;
    u.workScriptWorking=false;
    notifyUnitScript(u,"StopBuilding");
}

void World::notifyUnitScript(Unit& u,const char* name) {
    if (std::strcmp(name,"BeginFlight")==0) ++u.flightBeginCallbackSerial;
    auto it=unitScript(u.id);
    if (!it) return;
    auto& state=*it;
    const auto& file=*u.type->script();
    ScriptHost host{*this,u,state};
    state.state().notify(file,file.scriptIndex(name),host);
}

// Legion (deliberate, not retail): no walking on the spot. A walker's gait is
// its own Create-started controller polling GET 29 (horizontal speed as a
// percentage of its maximum) every 100 ms and playing a whole walk cycle
// (about 1.3 s for araarch) whenever it reads above 5. Retail's mover
// reports a body stopped only after two refused steps in a row (the 51a930
// refusal bit, bodyBlockStreak). Legion instead holds a blocked body (zero
// speed) and moves a jammed one in short, slow shuffles and side-steps
// that do not turn it (an eighth of its speed while it faces away): every
// sample that lands on one of those creeping ticks starts another full
// cycle, so the legs walk while the body all but stands (measured with
// legion_group_motion: 4022 unit-ticks of walk cycles with under 8 px of
// headway per second in the open 60-unit case, Retail 405). So in Legion a
// body that makes no real headway over a window of LegionHeadway::kWindow
// ticks reports stopped (GET 29 = 0, MoveRate 0, like the refusal bit) and
// stands; it walks again on the tick it makes real headway. Headway is NET,
// measured from where the window began: a displacement of kWindow paces (a
// pace is a quarter of the base speed, so two full-speed steps), or a turn
// one way of half the window at the full turn rate (a body pivoting on the
// spot steps round: retail's MoveRate is "slow" while only turning). A body
// stepping back and forth over a cell edge, or turning left and right toward
// two aims that swap, gains neither however long each step is: the 0.8.1
// walk-in-place (0.1-1 px steps every tick in cycles of 1-6 ticks, its net
// turn about nothing) passed the old per-tick rule (a step of a pace, or any
// turn, was headway) on every tick. A window
// spans two such cycles of 4 ticks or three shorter ones; a genuine slow
// walker (a pace a tick, a speed-matched follower at half its cap or more)
// reaches the window's displacement by its end, and a pivot its turn on the
// fourth tick. A window that stands although the body walked a whole reach
// or turned a whole turn in it, three times what that came to (to and fro),
// also marks a livelock (legionShuffle): its navigator then neither flows
// round blockers, nor follows its pinwheel arc, nor passes, so it holds or
// walks down the field and the hold's own ways out start
// (LegionNavigator::drive). A held or merely
// creeping body is still without being a livelock. The livelock ends when
// the body is a body length clear of where it began (16 px per footprint
// cell, plus one cell), not on the next headway: a slow cycle (a side-step
// crept for 50 ticks, undone by a dash back) clears the window's reach on
// every dash. Or after kLock ticks: long enough for the hold's ways out
// (side-step at 6 hold updates, yield at 6 and 12, parting at 12, the
// formation detour at 60), and short enough that a body in a crowd that has
// since moved on flows again (a latch of 120 ticks stood the foot-3 landed
// flyer block's crowd 235 member-ticks against it, base 159, past
// legion_landedflyers' band). Flyers keep retail's rule.
void LegionHeadway::tick(uint8_t& ticks,bool& still,bool& shuffle,int32_t beforeX,int32_t beforeZ,int32_t atX,int32_t atZ,
                         int16_t turnBam,int32_t speed,int32_t rate,int foot) {
    if (!ticks) {x=beforeX;z=beforeZ;turn=path=turned=0;}
    ++ticks;
    const int64_t sx=int64_t(atX)-beforeX,sz=int64_t(atZ)-beforeZ;
    turn+=turnBam;turned+=std::abs(int32_t(turnBam));
    path=int32_t(std::min<int64_t>(INT32_MAX,path+int64_t(isqrt64(uint64_t(sx*sx+sz*sz)))));
    const int64_t dx=int64_t(atX)-x,dz=int64_t(atZ)-z;
    const int64_t reach=int64_t(kWindow)*std::max<int64_t>(1,speed/4);
    const int64_t turnReach=std::max<int64_t>(1,rate)*kWindow/2;
    if (dx*dx+dz*dz>=reach*reach || std::abs(int64_t(turn))>=turnReach) {
        ticks=0;still=false;
    } else if (ticks>=kWindow) {
        ticks=0;still=true;
        // To and fro: a reach walked, or a turn turned, at least three times
        // what it came to (a corner turned and then crept along is not).
        const int64_t net2=dx*dx+dz*dz,walked=path,total=turned;
        if (!shuffle && ((walked>=reach && 9*net2<walked*walked) || (total>=turnReach && 3*std::abs(int64_t(turn))<total))) {
            shuffle=true;lockX=x;lockZ=z;lockTicks=0;
        }
    }
    if (shuffle) {
        const int64_t lx=int64_t(atX)-lockX,lz=int64_t(atZ)-lockZ;
        const int64_t clear=int64_t(foot+1)*16*Fixed::kOne;
        if (lx*lx+lz*lz>=clear*clear || ++lockTicks>=kLock) shuffle=false;
    }
}

void World::updateLegionStill(Unit& u,Fixed beforeX,Fixed beforeZ) {
    if (!isLegionPathfinding(pathfindingMode_) || !u.type || u.type->canFly || u.type->isStructure() ||
        !u.alive()) {
        u.legionWindow=0;u.legionStill=u.legionShuffle=false;
        if (size_t(u.id)<legionBodyWindow_.size()) legionBodyWindow_[size_t(u.id)].ticks=0;
        return;
    }
    // Measured from the start of the tick (Legion's yields step before the
    // unit loop), else from the start of this unit's update.
    if (size_t(u.id)<legionTickStart_.size() && legionTickStartAt_[size_t(u.id)]==tickCounter_) {
        beforeX=Fixed::raw(legionTickStart_[size_t(u.id)][0]);beforeZ=Fixed::raw(legionTickStart_[size_t(u.id)][1]);
    }
    if (size_t(u.id)>=legionWindow_.size()) legionWindow_.resize(size_t(u.id)+1);
    const int32_t rate=int32_t(std::max<int64_t>(1,int64_t(uint16_t(std::max(u.type->turnRate,u.type->turnInPlaceRate)))*
                                                    groundTerrainMultiplier(u).v/65536));
    // A Legion member's turn is its course's (LegionNavigator::courseTurnCorrection): its body faces its
    // steps, and turning to a sideways step is no headway, nor to and fro for the livelock.
    const int32_t course=legion_?legion_->courseTurnCorrection(u.id):0;
    const int foot=std::max(u.type->footX,u.type->footZ);
    const int16_t bodyTurn=std::bit_cast<int16_t>(uint16_t(uint16_t(u.heading.v)-u.tickStartHeadingBam));
    bool courseStill=false;
    legionWindow_[size_t(u.id)].tick(u.legionWindow,courseStill,u.legionShuffle,beforeX.v,beforeZ.v,u.x.v,u.z.v,
        std::bit_cast<int16_t>(uint16_t(uint16_t(bodyTurn)+course)),u.baseSpeed.v,rate,foot);
    // Whether the body stands in its scripts is judged by what it shows: its own steps and its body's turn
    // (Unit::legionStill is display only). By the course's turn, a body facing its way while its course
    // turned round behind it walked on the spot: a creep at the facing cap with no turn to show (slowturn
    // walk_in_place 0 -> 78 at offset 0 with facing, 2026-10-28).
    if (size_t(u.id)>=legionBodyWindow_.size()) legionBodyWindow_.resize(size_t(u.id)+1);
    auto& body=legionBodyWindow_[size_t(u.id)];
    body.w.tick(body.ticks,u.legionStill,body.shuffle,beforeX.v,beforeZ.v,u.x.v,u.z.v,bodyTurn,u.baseSpeed.v,rate,foot);
    // A standing body requests no turn either: the display VM's TurnDirection
    // (the zontroll/zongob MoveWatcher walks on its last argument) stays quiet.
    // turnReqBam is display only, never hashed.
    if (u.legionStill) u.turnReqBam=0;
}

// The mover's turn call-in (4d91b0's tail, 4d9593..4d95de). Every request to
// turn reaches TurnDirection as whole degrees (a signed truncating /182)
// whenever its sign differs from the last one reported, a turn starting or
// ending included, as an immediate call (56c640 -> 56c5f0: a zero-elapsed VM
// pass). The idle navigator (4d9ad0) asks for no turn first and then pivots
// toward a posted facing request, so a pivot reports 0 and the pivot's turn
// on every tick it lasts. These scripts are gameplay, not display: a walker's
// Create-started controller reads its own TurnDirection static as "moving"
// (VERMAGE's MoveWatcher: GET 29 > 5 or the last turn), and that controller
// gates its AimWeapon. The display VM keeps its own copy of these call-ins.
void World::notifyTurnDirection(Unit& u,UnitScript& script) {
    const auto& file=*u.type->script();
    auto report=[&](int32_t request) {
        const int32_t degrees=int32_t(int16_t(request))/182;
        const int32_t last=script.turnDirection;
        if (last*degrees>0 || (last==0 && degrees==0)) return;   // one sign, or both none
        script.turnDirection=int16_t(degrees);
        // A script without the method is not touched (its sleep skip stands).
        const int method=file.scriptIndex("TurnDirection");
        if (method<0) return;
        ScriptHost host{*this,u,script};
        if (script.state().startArguments(file,method,{uint32_t(degrees),0,0,0},1))
            script.state().tick(file,0,host);
    };
    if (script.turnInPlace) {
        script.turnInPlace=false;
        report(0);
    }
    report(u.turnReqBam);
}

void World::notifyMovementRate(Unit& u) {
    if (!u.type || u.type->isStructure() || !u.alive() || u.underConstruction) return;
    // 4dc800 reports the turn from inside the mover, before MoveRate (4db350).
    if (const auto it=unitScript(u.id)) notifyTurnDirection(u,*it);
    const auto multiplier=u.groundTerrainFlags&0x800 ? u.type->roadMult :
        u.groundTerrainFlags&0x1000 ? u.type->waterMult : Fixed::fromInt(1);
    const auto& v=u.flightVelocity;
    const int32_t horizontal=u.type->canFly ? int32_t(isqrt64(
        uint64_t(int64_t(v.x)*v.x)+uint64_t(int64_t(v.z)*v.z))) : u.speed.v;
    int32_t slow=(u.type->animationMoveRate1*multiplier).v;
    int32_t fast=(u.type->animationMoveRate2*multiplier).v;
    if (u.retailBuild && u.retailBuild->flying) {
        slow=u.retailBuild->flyingSlowSpeed;fast=u.retailBuild->flyingFastSpeed;
    }
    const int16_t turn=std::bit_cast<int16_t>(uint16_t(uint16_t(u.heading.v)-u.tickStartHeadingBam));
    const uint32_t rate=retailAnimationMoveRate(u.speed.v,turn,horizontal,
        slow,fast,u.bodyBlockStreak>=2 || u.legionStill,u.embarked());
    if (u.retailBuild && u.retailBuild->flying) {
        auto& flags=u.retailBuild->flyingOwnerFlags;
        flags=(flags&~0xcu)|(rate<<2);
    }
    const auto it=unitScript(u.id);
    if (!it || it->movementRate==rate) return;
    auto& script=*it;script.movementRate=rate;
    const auto& file=*u.type->script();
    ScriptHost host{*this,u,script};
    // Native 4db350 executes this edge at zero elapsed time before occupancy.
    // It can move QueryWeapon pieces (Verharp's emitjim), so it also belongs
    // to the authoritative VM, not just the display animation.
    if (script.state().startArguments(file,file.scriptIndex("MoveRate"),{rate,0,0,0},1))
        script.state().tick(file,0,host);
}

void World::notifyFlightOccupancy(Unit& u) {
    notifyMovementRate(u);
    // 4dc600 reports airborne mode after the mover, with an immediate
    // zero-elapsed script pass. Landed/water transitions need their own host.
    if (u.flightGroundMode!=2) return;
    constexpr uint32_t occupancy=5;
    if (occupancy==u.scriptOccupancy) return;
    const auto it=unitScript(u.id);
    if (it) {
        auto& script=*it;const auto& file=*u.type->script();
        ScriptHost host{*this,u,script};
        if (script.state().startArguments(file,file.scriptIndex("setSFXoccupy"),{occupancy,0,0,0},1))
            script.state().tick(file,0,host);
    }
    u.scriptOccupancy=occupancy;
}

void World::tickUnitScript(Unit& u) {
    auto* script=size_t(u.id)<unitScriptById_.size() ? unitScriptById_[size_t(u.id)] : nullptr;
    if (!script) return;
    if (!u.alive() && (u.deadFor>=kRetiredTicks || !script->dying ||
                       u.deadFor+1>=u.corpseAnimationTicks())) {
        unitScriptById_[size_t(u.id)]=nullptr;
        scriptYardById_[size_t(u.id)]=0;
        unitScripts_.erase(u.id);return;
    }
    // Keep the original VM until Killed can query its state. A dead owner keeps
    // executing Dying, including waits and calls, until its native stop signal.
    if (u.alive() && u.hp<=Fixed() && !(u.retailSite && u.underConstruction)) return;
    auto& factory=*script;
    const auto& file=*u.type->script();
    ScriptHost host{*this,u,factory};
    if (!factory.deathStopValue) {
        // A1 sleep skip (see UnitScript). Production scripts and dying VMs always run.
        const bool skippable=!factory.dying && !u.type->productionScript;
#ifndef NDEBUG
        if (probe::kStats) ++probe::tl.vmTicks;
#endif
        if (skippable && factory.skipTick()) {
#ifndef NDEBUG
            if (probe::kStats) { ++probe::tl.vmSkippable; if (factory.idle()) ++probe::tl.vmEmpty; }
#endif
        } else {
            factory.stateForTick().tick(file,1,host);
            factory.scheduleSkip(skippable && !factory.dying && !factory.deathStopValue);
        }
    }
    if (factory.dying) { updateCorpseWindow(u);return; }
    const bool activated=!u.underConstruction && !u.buildQueue.empty();
    if (u.type->productionScript && activated!=factory.activated) {
        factory.activated=activated;
        factory.state().notify(file,file.scriptIndex(activated ? "Activate" : "Deactivate"),host);
    }
}

void World::UnitScript::scheduleSkip(bool eligible) {
    skipLeft_=0;
    if (!eligible || state_.activePieceMask()) return;
    for (size_t k=64;k<state_.pieces.size();++k) if (state_.pieces[k].active) return;
    // Which ticks would change nothing but sleep counts: RetailVm::run lowers a
    // sleeper's words[3] by the elapsed ticks and wakes it at <= 0, so one left at w
    // runs on the max(w,1)-th tick from now; a thread waiting on a child changes only
    // when that child (in this VM) runs. Anything ready, or waiting on a piece, runs now.
    uint32_t left=kSkipIdle;
    const auto& vm=state_.vm;
    for (uint32_t m=vm.activeThreadMask();m;m&=m-1) {
        const auto& t=vm.threads[size_t(std::countr_zero(m))];
        const uint32_t wait=t.words[0]&0xfff00000u;
        if (wait==0x2400000u) {
            left=std::min(left,uint32_t(std::max(std::bit_cast<int32_t>(t.words[3]),1)-1));
            continue;
        }
        if (wait==0x2800000u) continue;
        return;
    }
    skipLeft_=left;
}

void World::UnitScript::pay([[maybe_unused]] bool touch) {
    if (!debt_) return;
#ifndef NDEBUG
    if (probe::kStats && touch) ++probe::tl.vmDebtFlushes;
    static const bool verify=probe::envOn("TAK_VERIFY_SCRIPT_SKIP");
    std::optional<cob::RetailScriptState> replayed;
    if (verify && file_) {
        // Re-run the skipped ticks for real on a copy. A skipped tick must not reach
        // the host at all (no RNG draw, no query, no piece callback).
        struct Refuse {
            [[noreturn]] static void fail() {
                throw std::runtime_error("TAK_VERIFY_SCRIPT_SKIP: a skipped COB tick reached its host");
            }
            uint32_t random(int32_t) { fail(); }
            uint32_t get(int,const std::array<uint32_t,4>&) { fail(); }
            void set(int,int) { fail(); }
            uint32_t sound(int,int32_t) { fail(); }
            void effect(uint32_t,int,int32_t) { fail(); }
        } refuse;
        replayed.emplace(state_);
        for (uint32_t n=0;n<debt_;++n) replayed->tick(*file_,1,refuse);
    }
#endif
    auto& vm=state_.vm;
    for (uint32_t m=vm.activeThreadMask();m;m&=m-1) {
        auto& t=vm.threads[size_t(std::countr_zero(m))];
        if ((t.words[0]&0xfff00000u)==0x2400000u) t.words[3]-=debt_;
    }
    debt_=0;
#ifndef NDEBUG
    if (replayed) {
        const auto& r=*replayed;
        bool same=r.vm.active==vm.active && r.vm.activeThreadMask()==vm.activeThreadMask() &&
            r.vm.statics==vm.statics && r.pieces==state_.pieces &&
            r.activePieceMask()==state_.activePieceMask();
        for (size_t i=0;same && i<vm.threads.size();++i) same=r.vm.threads[i].words==vm.threads[i].words;
        if (!same) throw std::runtime_error("TAK_VERIFY_SCRIPT_SKIP: paying a COB VM's skipped ticks "
                                            "differs from running them");
    }
#endif
}

void World::updateCorpseWindow(Unit& u) {
    const int starts=u.corpseAnimationTicks();
    u.corpseUntil=starts;
    if (!u.deathHasCorpse || starts==INT32_MAX) return;
    const int ct=u.corpseStatue>=0 ? u.corpseStatue : corpseTypeOf(u.type);
    if (ct<0) return;
    const int duration=u.corpseStatue<0 && isWater(u.x.toFloat(),u.z.toFloat())
        ? 75 : featTypes_[size_t(ct)].decomposeTicks;
    u.corpseUntil=duration>0 ? int32_t(std::min(int64_t(INT32_MAX),int64_t(starts)+duration)) : INT32_MAX;
}

void World::beginUnitDeath(Unit& u) {
    u.deathAnimationTicks=0;
    u.deathHasCorpse=u.corpseStatue>=0;
    auto* script=unitScript(u.id);
    if (u.corpseStatue<0 && script) {
        const auto& file=*u.type->script();
        ScriptHost host{*this,u,*script};
        script->dying=true;script->deathStopValue=0;
        u.deathAnimationTicks=INT32_MAX;
        std::array<uint32_t,4> args{u.severity,0,u.deathType,0};
        // 512610 skips Killed for self destruction and zero-severity packets.
        if (u.severity && u.deathType!=Unit::kDeathSelfDestruct)
            script->state().query(file,file.scriptIndex("Killed"),args,host);
        u.deathHasCorpse=(args[1]&15)==1 && !u.underConstruction && corpseTypeOf(u.type)>=0;
        const bool dying=u.severity && script->state().startArguments(
            file,file.scriptIndex("Dying"),{u.deathType,0,0,0},1);
        if (dying && !script->deathStopValue) script->state().tick(file,0,host);
        if (!dying && script->deathStopValue!=31) u.deathAnimationTicks=0;
    }
    if (u.deathType==9 && !u.underConstruction && corpseTypeOf(u.type)>=0)
        u.deathHasCorpse=true;
    updateCorpseWindow(u);
    if (u.corpseAnimationTicks()==0 && script) {
        unitScriptById_[size_t(u.id)]=nullptr;
        scriptYardById_[size_t(u.id)]=0;
        unitScripts_.erase(u.id);
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
        script->second.state().tick(*site.type->script(),0,host);
    }
}

bool World::canPlaceProduction(int builderId,const UnitType* t,float x,float z) const {
    const auto* producer=unit(builderId);
    if (!producer || !producer->type || !t || t->isStructure()) return false;
    if (!producer->type->productionScript || t->canFly) return canPlace(t,x,z);
    // BUILD stage 2 (401deb) tests the QueryBuildInfo point with 507d10's
    // mobile footprint loop: self 0 and no moving-body exemption, so any
    // ground body on a footprint cell refuses the site, moving or not. The
    // centre radius canPlace applies is sized for the NEW unit alone: a 3x3
    // Knight was admitted 38 px behind a departing 4x4 Catapult, overlapping
    // it, and the footprint-strict mover then held both for ever. An authored
    // pad can also lie near the producer's centre (Creon's shipyard), which
    // that radius would refuse. Open yard cells pass; solid ones block.
    const int ox=footprintOrigin(x,t->footX),oz=footprintOrigin(z,t->footZ);
    if (!mapPlacementCells_.empty()) return mobilePlacement(*t,0,ox,oz,false);
    // Terrain-only harnesses have no native feature plane: their terrain
    // grid stands in for it, without canPlace's centre radius.
    const std::vector<int> noRadius;
    if (!placementCheck(t,x,z,-1,nullptr,&noRadius)) return false;
    const auto bodies=searchBodyRect(ox,oz,t->footX,t->footZ);
    return std::none_of(bodies.cells.begin(),bodies.cells.end(),[](const auto* u){return u!=nullptr;});
}

bool World::productionPosition(int builderId,const UnitType* t,Fixed& spawnX,Fixed& spawnY,Fixed& spawnZ) {
    auto* builder=unit(builderId);
    if(!builder || !builder->type || !t)return false;
    Unit& u=*builder;
    spawnX=u.x;spawnZ=u.z;spawnY=Fixed();
    if (auto it=unitScripts_.find(u.id);u.type->productionScript && it!=unitScripts_.end()) {
        const auto& file=*u.type->productionScript;
        std::array<uint32_t,4> args{0xffffffffu,0,0,0};
        ScriptHost host{*this,u,it->second};
        if (!it->second.state().query(file,file.scriptIndex("QueryBuildInfo"),args,host)) return false;
        const auto offset=retailPieceOrigin(u.type->productionModel,it->second.peek().pieces,
                                           std::bit_cast<int32_t>(args[0]),portHeadingToRetail(u.heading));
        spawnX=u.x+Fixed::raw(offset[0]); spawnZ=u.z+Fixed::raw(offset[2]);
        const int cx=std::clamp(u.x.floorInt()/16,0,std::max(0,terW_-1));
        const int cz=std::clamp(u.z.floorInt()/16,0,std::max(0,terH_-1));
        spawnY=Fixed::fromInt(heights_.empty() ? 0 : heights_[size_t(cz)*terW_+cx])+Fixed::raw(offset[1]);
        if (!canPlaceProduction(builderId,t,spawnX.toFloat(),spawnZ.toFloat())) return false;
    } else {
        const float ex=u.x.toFloat(),ez=u.z.toFloat()+float(u.type->footZ)*8+20;
        float sx=ex,sz=ez;
        if (!exitSpot(t,ex,ez,sx,sz)) return false;
        spawnX=Fixed::fromFloat(sx); spawnZ=Fixed::fromFloat(sz);
    }
    return true;
}

void World::tickProduction(Unit& u, float dt) {
    (void)dt;
    if (u.underConstruction || u.incapacitated() || u.embarked() ||
        u.hp <= Fixed() || u.buildQueue.empty()) return;
    // Production queued after a boarding request must not overtake it either.
    if(!u.orders.empty() && u.orders.front().load &&
       !u.orders.front().transportProductionAhead)return;
    // A conjurer works one job at a time, as retail runs one mission: while
    // it builds or helps elsewhere (buildSiteId) its own output waits, held
    // (protectedSite keeps it from decaying) rather than growing unattended.
    if (!u.type->isStructure() && u.buildSiteId) return;
    const UnitType* t = u.buildQueue.front();
    if (!buildAllowed(t)) return;
    const int producerId=u.id, player=u.player;
    const int32_t total=productionTotal(u,t);
    Unit* site=u.productionSiteId ? unit(u.productionSiteId) : nullptr;
    if (u.productionSiteId && (!site || !site->alive() || (site->hp<=Fixed() && !site->retailSite) || site->player!=player)) {
        u.productionSiteId=0; u.buildProgress=0; u.buildFraction=0;
        return;
    }
    if (!site) {
        if (auto it=unitScripts_.find(u.id);u.type->productionScript && it!=unitScripts_.end() && !it->second.ready) return;
        if (atUnitCap(player) || atTypeCap(player,t)) return;
        // 401df4..401e19: a refused site sleeps the BUILD mission rand(15)+7
        // ticks before stage 2 runs QueryBuildInfo and the footprint test again.
        if (u.type->productionScript && tickCounter_<u.productionRetryTick) return;
        Fixed spawnX,spawnZ,spawnY;
        if (!productionPosition(producerId,t,spawnX,spawnY,spawnZ)) {
            if (u.type->productionScript) u.productionRetryTick=tickCounter_+7+gameRand(15);
            return;
        }
        u.productionRetryTick=0;
        // Preserve the raw script position across the public float spawn API.
        const int siteId=spawn(t,spawnX.toFloat(),spawnZ.toFloat(),std::nullopt,player);
        site=unit(siteId);
        site->x=site->homeX=spawnX; site->z=site->homeZ=spawnZ; site->flightY=spawnY; updateBodyIndex(*site);
        groundMove(*site);
        site->flightSectorX=spawnX.v>>23;site->flightSectorZ=spawnZ.v>>23;
        site->underConstruction=site->buildBegun=true;
        site->productionOwner=producerId;
        site->hp=Fixed::fromFloat(t->maxHp*0.05f);
        if (players_[size_t(player)].retailResources) initializeRetailSite(*site,producerId);
        unit(producerId)->productionSiteId=siteId;
        if (auto it=unitScripts_.find(producerId);it!=unitScripts_.end()) {
            const auto& file=*unit(producerId)->type->script();
            it->second.state().startArguments(file,file.scriptIndex("StartBuilding"),{},2);
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
            producer->productionSiteId=0;producer->buildProgress=0;producer->buildFraction=0;
            retireProductionItem(*producer,0);
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
        const double share=tm.constructionShare(cost);
        // Work in 1/65536 of a tick; a full share is exactly one tick, as before.
        const uint32_t step=share>=1 ? 65536u : uint32_t(share*65536.0);
        if (!workProduction(*producer,*site,t,step)) return;
    }
    finishProduction(producerId,*site);
}

int32_t World::productionTotal(const Unit& producer,const UnitType* t) const {
    return std::max(1,truncSat32(t->buildTime/std::max(producer.type->workerTime,0.01f)*kTick+0.5f));
}

// The product's one progress: the producer's buildProgress + buildFraction,
// in ticks of the PRODUCER's work. Its own tickProduction adds `step` (a full
// tick when it can afford one) and every helper adds its own work converted
// to these units (tickConstruction), so helpers really finish the unit
// sooner. Mana is paid per unit of progress -- buildcost over the whole
// count -- whoever does the work, so the total stays the buildcost.
// True when the product is complete (its hp then corrected to full).
bool World::workProduction(Unit& producer,Unit& site,const UnitType* t,uint32_t step) {
    const int32_t total=productionTotal(producer,t);
    const Fixed work=Fixed::fromFloat(t->maxHp*0.95f/float(total));
    // Never buy past the end of the build. (Alone, the producer is only ever
    // clamped on its last tick, exactly as before helpers shared the count.)
    const int64_t left=int64_t(total-producer.buildProgress)*65536-producer.buildFraction;
    if (left>0) {
        step=uint32_t(std::min<int64_t>(step,left));
        if (!step) return false;
        Player& tm=players_[size_t(producer.player)];
        const double cost=double(t->buildCost)/double(total);
        tm.payMana(std::min(tm.mana,step==65536u ? cost : cost*double(step)/65536.0));
        // Grow the site by what the cumulative work says it should have, so a run
        // of partial ticks adds exactly `work` per whole tick and the completion
        // rounding below still holds.
        const auto grown=[&](int64_t ticks,uint32_t fraction) {
            return int64_t(work.v)*ticks+((int64_t(work.v)*fraction)>>16);
        };
        const int64_t before=grown(producer.buildProgress,producer.buildFraction);
        const uint64_t fraction=uint64_t(producer.buildFraction)+step;
        producer.buildProgress+=int32_t(fraction>>16);
        producer.buildFraction=uint16_t(fraction&0xffffu);
        const int64_t after=grown(producer.buildProgress,producer.buildFraction);
        site.hp=fxMin(Fixed::fromFloat(t->maxHp),site.hp+Fixed::raw(int32_t(after-before)));
        emitConstruction(site,true);
    }
    if (producer.buildProgress<total) return false;
    // Correct accumulated fixed-point rounding without undoing combat damage.
    const Fixed rounding=Fixed::fromFloat(t->maxHp)-
        (Fixed::fromFloat(t->maxHp*0.05f)+Fixed::raw(int32_t(int64_t(work.v)*total)));
    site.hp=fxMin(Fixed::fromFloat(t->maxHp),site.hp+rounding);
    return true;
}

// The product is finished, once: the producer is freed (its next item may
// start this same tick) and the output is released with the producer's exit
// step / PARK and rally, exactly once. An output that already holds orders
// keeps them instead: they can only be the player's (or a script's), given
// while it was unfinished, and the release must not override them.
// A factory that dies with an unfinished output kills the output too
// (icd, emulated 2026-10-28). Death clears every mission of the unit
// (0x512860 -> 0x4d6a50(unit, 1)); the mission destructor 0x4d6da0 calls a
// started mission's handler with flag 2, and only BuildingBuild 0x401c20 -- the
// mission 0x429810 queues for a producer WITHOUT a mover -- acts on it with a
// target: 0x429990 (the completion call) and then 30000 damage of type 11 from
// the factory to the output (0x51a140). Type 11 has severity 0: no Killed, no
// corpse, no death weapon, and the factory is the attacker, so nobody gets the
// kill. MobileBuild, VTOL_MobileBuild and HelpBuild only stop the build
// animation on flag 2: a Zhon producer's output and a builder's site stay
// unfinished and decay as before.
//
// Ours sets the lethal state and lets the death sweep take it: the output was
// spawned after its factory, so it comes later in the same sweep (ids and
// units_ order agree). The completion call is not replayed -- it would only
// release the output an instant before it dies.
void World::killProductionOutput(Unit& producer,bool now) {
    const int siteId=producer.productionSiteId;
    if (!siteId || !producer.type || !producer.type->isStructure()) return;
    producer.productionSiteId=0;producer.buildProgress=0;producer.buildFraction=0;
    Unit* site=unit(siteId);
    if (!site || !site->alive() || !site->type || !site->underConstruction ||
        site->productionOwner!=producer.id || site->player!=producer.player) return;
    site->productionOwner=0;
    site->retailSite.reset();site->beingBuilt=false;
    site->hp=Fixed();
    site->deathType=11;site->lastHitBy=producer.id;site->lastHitPlayer=producer.player;
    if (!now) return;
    // Before the economy walk could decay it away uncounted or a helper could
    // put hit points back: the scripted-kill path, which counts the loss and
    // credits the factory (nobody), then off the ground word as the sweep does.
    scenarioDestroy(siteId,11);
    if (Unit* gone=unit(siteId)) { cancelPath(*gone);groundRemove(*gone);retireAirOccupant(*gone);noteOrders(siteId); }
}

void World::finishProduction(int producerId,Unit& finished) {
    Unit* site=&finished;
    const UnitType* t=site->type;
    Unit* producer=unit(producerId);
    const int id=site->id;
    const float sx=site->x.toFloat(),sz=site->z.toFloat();
    const bool commanded=!site->orders.empty();
    site->underConstruction=false;
    site->productionOwner=0;
    producer->productionSiteId=0;
    producer->buildProgress=0;producer->buildFraction=0;
    retireProductionItem(*producer,0);
    if (auto it=unitScripts_.find(producerId);it!=unitScripts_.end()) {
        const auto& file=*producer->type->script();
        it->second.state().vm.start(file,file.scriptIndex("StopBuilding"));
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
    //
    // A structure's output -- ground, water or flying -- is released as retail's
    // GetBuilt does instead (0x402220, emulated): the factory's queued QMove/QPatrol
    // orders, else PARK around the factory (parkProductionOutput; a flyer's PARK
    // flies to one point on its circle). The step stays for mobile producers'
    // outputs, which retail gives nothing: without it a mobile producer's next
    // output would be built on top of the last.
    const Unit* factory=unit(producerId);
    const bool getBuilt=factory && factory->type->isStructure();
    bool hasExitStep=false;
    if (commanded) {
        // Orders it was given while unfinished stand: no exit step, PARK or rally.
    } else if (getBuilt) {
        if (factory->rally.empty()) parkProductionOutput(id,producerId);
    } else if (float gx = sx, gz = sz + float(productionExitDistance(t)); exitSpot(t, gx, gz, gx, gz,id)) {
        order(id, gx, gz, false);
        hasExitStep=!unit(id)->orders.empty();
    }
    // ...then hand it the factory's RALLY plan. A production building accepts move /
    // fight-move / patrol / attack orders (and queues them) -- it cannot act on them
    // itself, so they describe what its OUTPUT should do. Behind the exit step where
    // there is one; a factory's output, flyers included, takes the rally alone, as
    // GetBuilt copies it with nothing before it.
    if (const Unit* pb = unit(producerId); pb && !commanded)
        for (const Order& ro : pb->rally) {
            Unit* nu = unit(id);
            if (!nu) break;
            // A targeted rally (attack) whose target is already dead is skipped rather
            // than handed over as an order pointing at nothing.
            if (ro.targetId) {
                const Unit* tg = unit(ro.targetId);
                if (!tg || !tg->alive()) continue;
                // Through attack(), as the player's own Attack would go: an
                // output that cannot attack (no weapon) drops the step.
                attack(id,ro.targetId,true);
                continue;
            }
            if (!t->canFly) {
                // Raw rally entries belong to the producer. Give each child
                // the same point controller/arrival handling as a direct order.
                // This also preserves the requested point across partial routes.
                const size_t before=nu->orders.size();
                order(id,ro.x.toFloat(),ro.z.toFloat(),true);
                for (size_t i=before;i<nu->orders.size();++i) {
                    nu->orders[i].attackMove=ro.attackMove;
                    nu->orders[i].patrol=ro.patrol;
                    nu->orders[i].patrolOpen=ro.patrolOpen;
                }
            } else {
                nu->orders.push_back(ro);
                nu->orders.back().issuedTick = tickCounter_;
                // An unarmed, non-building flyer's patrol handler closes its own
                // loop (plainFlightPatrol), as queuePatrol leaves it.
                if (!t->isBuilder && t->weapons.empty()) nu->orders.back().patrolOpen=false;
            }
        }
    // A rally whose every entry was dropped (its attack targets are dead) hands
    // over nothing: park instead, as GetBuilt does when there is nothing to copy,
    // rather than leave the output standing on the pad.
    if (const Unit* pb=unit(producerId),*nu=unit(id); getBuilt && !commanded && pb && !pb->rally.empty() && nu && nu->orders.empty())
        parkProductionOutput(id,producerId);
    // Legion routes the automatic exit as its own mission (LegionMission::
    // Exit): the birthplace lets a held-up exit finish once clear of it.
    if(hasExitStep && isLegionPathfinding(pathfindingMode_) && !t->canFly) {
        auto& orders=unit(id)->orders;const size_t end=currentLeg(orders);
        orders[end].productionExit=std::pair{Fixed::fromFloat(sx),Fixed::fromFloat(sz)};
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
    static const bool kPqLog2 = tak::devEnv("TAK_PATHLOG") != nullptr;
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
            if (!u->orders.empty())
                u->orders[currentLeg(u->orders)].navigationExhausted=true;
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
    // The rectangle is work metadata, never a movement destination. A rejected
    // site leaves it at the head until the next deposit is tried next tick.
    if (!u.orders.empty() && u.orders.front().manaBuildArea) {
        if (u.type->canFly) tickFlightBody(u);
        else brakeGround(u);
        return;
    }
    // MobileBuild stopped navigating at its site while mobile units stand on
    // it (405824..405911): the body brakes and stays for the 10-tick retry.
    // FlyingBuild (41f2a8) has no controller left after arrival: the flyer
    // hovers where it arrived, held as a scripted wait is.
    if (!u.orders.empty() && u.orders.front().buildType && u.orders.front().placementFailures) {
        if (u.type->canFly) holdFlight(u);
        else brakeGround(u);
        return;
    }
    // Queued mobile production already advanced its hover controller this tick.
    if (u.type->canFly && u.orders.empty() && u.productionSiteId &&
        u.conjureHoverTarget==u.productionSiteId) return;
    // A flyer in these holds runs no flight body. They used to brake only the
    // speed, and the wait and ambush kept the velocity too: a Gryphon told to
    // wait mid-flight hung still reporting the 3.4 px/tick it flew in at, and
    // MoveRate stayed at 1-3.
    if (u.orders.empty()) {
        if (u.type->canFly) holdFlight(u);
        else if (!u.type->isStructure()) brakeGround(u);
    } else if (u.orders.front().wait > 0) {
        // SetMission "w N": hold position while the scripted wait counts down.
        if (u.type->canFly) holdFlight(u);
        else if (!u.type->isStructure()) brakeGround(u);
        --u.orders.front().wait;
        if (u.orders.front().wait <= 0.0f) u.orders.erase(u.orders.begin());
    } else if (u.orders.front().waitAttack) {
        // SetMission "wa": ambush -- hold until a non-allied unit is within sight,
        // then release to the next order (usually an attack).
        if (u.type->canFly) holdFlight(u);
        else if (!u.type->isStructure()) brakeGround(u);
        float sight = u.type->sight > 0 ? sightDistance(*u.type) : 200.0f;
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
        // Buildings can retain attack orders outside the combat hold radius.
        // They have no movement controller: even a zero-speed navigator turns
        // the body while steering. Keep script-driven turrets independent.
        if (u.type->isStructure()) return;
        // RECLAIMAREA moves only through its own approach leg and its child
        // reclaims. The area order is no destination (its x/z are the box's
        // corner): never route to it, steer at it or retire it on arrival.
        if (u.orders.front().reclaimArea && !u.type->canFly) { brakeGround(u); return; }
        // Goal satisfaction belongs to the mission, independently of the
        // navigator's next intermediate point (retail 0x4e5150). A stale
        // waypoint can be occupied even after we have reached the goal area.
        //
        // The current leg as it stands at the top of this update. This was a copy of
        // the whole Order every tick for every mover; with the HeapOptional payloads
        // a copy also allocates. It is now a reference, read only while nothing has
        // written to the queue, plus an exact copy taken where the old snapshot and
        // the live order can differ (the completed-mission writes below).
        const Order& leg = u.orders[currentLeg(u.orders)];
        const bool legHasRectangle = leg.buildRectangle.has_value();
        const bool rectangleReached = leg.buildRectangle && leg.buildRectangle->accepts(
            footprintOrigin(u.x,u.type->footX), footprintOrigin(u.z,u.type->footZ));
        if (u.type->canFly) {
            if (leg.buildRectangle && leg.flightGoal &&
                leg.flightGoal->accepts({u.x.v,u.flightY.v,u.z.v})) {
                u.orders[currentLeg(u.orders)].mission.pending |= 0x100;
                // Arrival retires the approach controller, not the independent
                // flight body. Native 4dc800 still moves/climbs on this update.
                tickFlightMovement(u,true);
                notifyFlightOccupancy(u);
                return;
            }
            // May grow the queue: `leg` is not read past this point.
            openFlightPatrol(u);
            tickFlightMovement(u); notifyFlightOccupancy(u); return;
        }
        if(legion_&&isLegionPathfinding(pathfindingMode_)&&legion_->supports(u)) {
            legion_->move(u,maximum);
            return;
        }
        Order* completed = nullptr;
        // A reclaim, help or repair leg keeps its rectangle while it works,
        // its controller already detached: no second arrival.
        if (rectangleReached && !(leg.mission.pending & 0x100) &&
            (leg.controller || (!leg.reclaimFeat && !leg.helpSite && !leg.repairTarget)))
            completed = &u.orders[currentLeg(u.orders)];
        else if (auto* goal = navigationMissionOrder(u);goal && !goal->buildRectangle) {
            if (goal->controller && groundMissionAccepts(u,*goal)) completed = goal;
        }
        // Nothing has written to the queue since `leg` was taken; the completed
        // block below does, so keep the leg's value from before it.
        std::optional<Order> legBefore;
        if (completed) legBefore.emplace(leg);
        const Order& legGoal = legBefore ? *legBefore : leg;
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
        if (!groundMissionOrder(u,true) && legGoal.goal && !legGoal.targetId && !legGoal.buildType &&
            !legGoal.reclaimFeat && !legGoal.repairTarget && !legGoal.helpSite && !legGoal.load && !legGoal.unload &&
            fxLen(legGoal.x - u.x, legGoal.z - u.z) < goalRadius) {
            // dropLeg erases the order `leg` refers to: copy the lap first.
            std::optional<Order> lap;
            if (legGoal.patrol) lap.emplace(legGoal);
            cancelPath(u);
            dropLeg(u);
            u.routeStamp = -1;
            if (lap) u.orders.push_back(std::move(*lap));
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
        if (u.routeStamp < 0 && routeable && !pathPending(u.id)) {
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
                    uint8_t(u.type->halfCellTicks),boat,int16_t(u.type->footX),int16_t(sightDistance(*u.type)),false,
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
            if (t && t->type && guardRing(u,*t).accepts(footprintOrigin(u.x,u.type->footX),footprintOrigin(u.z,u.type->footZ)))
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
        // A ground guard outside its ring keeps steering for the ring's point
        // (accepted above): the ring is 48 px deep, and stopping a footprint
        // short of its middle left a 4x4 guard (3x3 at sea) just outside the
        // band, held there walking in place.
        if (!groundMissionOrder(u,true) && !legHasRectangle && (u.type->canFly || o.goal) && dist < arrive &&
            !(o.guard && !u.type->canFly)) {
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
    inTick_=true;
    struct TickScope { bool& in; ~TickScope(){in=false;} } tickScope{inTick_};
    if (retailAllocation_)
        std::sort(units_.begin(),units_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    // The profiled tick starts here, so navigator upkeep (Legion
    // field and plane work) and feature burning are inside the total.
    std::chrono::steady_clock::time_point _tk0, _sep0, _pro0;
    if (g_phase) { _tk0 = std::chrono::steady_clock::now();
                   g_tcomb = g_scriptMs = g_moveMs = g_navMs = 0;
                   g_visMs = g_burnMs = g_gridMs = g_exploreMs = g_prologueMs = g_compactMs = 0; }
#ifndef NDEBUG
    probe::PmuSample _pmuTick, _pmuSep, _pmuPro;
    if (g_phase && probe::kPmu) { probe::pmuReset(); _pmuTick = probe::pmuRead(); }
    if (probe::kStats) probe::allocMark();
#endif
    if (!tickCounter_) updateNavigationExploration();
    if (isLegionPathfinding(pathfindingMode_)) {
#ifndef NDEBUG
        // TAK_LEGION_VERIFY: after this tick's command application, both
        // convoy indexes equal a rebuild from the table.
        if (LegionNavigator::verifying() && !convoys_.indexesMatch(tickCounter_)) {
            std::fprintf(stderr, "TAK_LEGION_VERIFY tick %u: convoy indexes differ from a rebuild\n", tickCounter_);
            std::abort();
        }
#endif
        // The next commands apply at the new tick: a convoy none of them can
        // join leaves the (hashed) table now.
        convoys_.prune(tickCounter_ + 1);
    }
    ++tickCounter_;
    {
        std::optional<PhaseSpan> _nav;
        if (g_phase) _nav.emplace(g_navMs, probe::kNav);
        if(isLegionPathfinding(pathfindingMode_)) {
            TAK_PASS();
            for(const auto& u:units_) {
                if(size_t(u.id)>=legionTickStart_.size()) {
                    legionTickStart_.resize(size_t(u.id)+1);legionTickStartAt_.resize(size_t(u.id)+1,0);
                }
                legionTickStart_[size_t(u.id)]={u.x.v,u.z.v};legionTickStartAt_[size_t(u.id)]=tickCounter_;
            }
        }
        if(isLegionPathfinding(pathfindingMode_)&&pathService_) {
            if(!legion_)legion_=std::make_unique<LegionNavigator>(*this);
            legion_->tick();
        }
    }
    if (g_phase) _pro0 = std::chrono::steady_clock::now();
#ifndef NDEBUG
    if (g_phase && probe::kPmu) _pmuPro = probe::pmuRead();
#endif
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
#ifndef NDEBUG
    hashTrace();   // TAK_HASHTRACE=lo:hi -- per-component dump, EVERY tick on both peers
#endif
    {
        std::optional<PhaseSpan> _burn;
        if (g_phase) _burn.emplace(g_burnMs, probe::kBurn);
        tickBurning();   // feature fire: spread + burn-out (deterministic, hashed)
        tickFeatureFades();   // reclaimed features leave the plane (494ce0)
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
    // One walk (WE D4) resets the per-tick turn and fire/animation state. The turn
    // reset used to run before hashTrace, tickBurning and the benchmark spawns:
    // none of them reads or writes a unit's heading or these fields (a spawn
    // starts with turnReqBam 0 and tickStartHeadingBam at its heading).
    auto resetTickState = [](Unit& u) {
        u.turnReqBam = 0;   // requested-turn display field, refreshed below
        u.tickStartHeadingBam=uint16_t(u.heading.v);
        u.justFired=false;u.firedWeapons=0;u.fireAnimations=0;u.weaponAnimations.clear();u.justBuilt=0;
        for(const auto& event:u.pendingWeaponAnimations)
            u.weaponAnimations.add(event.kind,event.slot,event.heading,event.pitch,event.damage);
        u.pendingWeaponAnimations.clear();
    };
    // Per unit, the economy walk's work (below): the beingBuilt clear and the
    // site list for sites, and the unit's income, storage and retail demand
    // contributions, handed to emit(kind, player, float, double) in the order
    // the sums take them. It reads only the unit, its build site and player
    // state the walk does not write (buildCache, retailResources).
    auto economyOf = [&](Unit& u, auto&& emit, auto&& listSite) {
        if (!u.alive()) return;
        // A unit under construction contributes no economy until it finishes -- a
        // half-built lodestone must not add its mana income or storage capacity yet.
        if (u.underConstruction) {
            u.beingBuilt = false;
            listSite();
            return;
        }
        if (u.type) {
            const auto& tm = players_[size_t(u.player)];
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
                emit(EconomyKind::SacredIncome,u.player,0.0f,double(income)*double(multiplier));
            } else emit(EconomyKind::Income,u.player,income,0.0);
            emit(EconomyKind::Storage,u.player,storage,0.0);
        }
        const auto& builder=u;
        if (size_t(builder.player)>=players_.size() || builder.embarked()) return;
        const auto& tm=players_[size_t(builder.player)];
        if (!tm.retailResources) return;
        int target=0;
        float worker=0;
        if (builder.retailBuild && builder.retailBuild->working) {
            target=builder.retailBuild->target;worker=builder.retailBuild->workerTime;
        } else if (builder.productionSiteId && !builder.buildQueue.empty()) {
            target=builder.productionSiteId;worker=builder.type->workerTime;
            if (tm.buildCache) for (const auto& entry:tm.buildCache->entries)
                if (entry.type==builder.type && entry.construction) worker=entry.construction->workerTime;
        } else return;
        const auto* site=unitNoRepair(target);
        if (site && site->retailSite && site->alive()) {
            const auto& type=site->retailSite->type;
            emit(EconomyKind::Demand,builder.player,0.0f,double(worker)*double(type.inverseTime)*double(type.cost));
        }
    };
    auto applyEconomy = [&](EconomyKind kind, int player, float f, double d) {
        auto& tm = players_[size_t(player)];
        switch (kind) {
        case EconomyKind::Income: tm.income += f; break;
        case EconomyKind::SacredIncome: tm.income=float(double(tm.income)+d); break;
        case EconomyKind::Storage: tm.storage += f; break;
        case EconomyKind::Demand: {
            float& demand=retailDemand_[size_t(player)];
            demand=float(double(demand)+d);
            break;
        }
        case EconomyKind::Site: break;
        }
    };
    // At kPrologueMinParallel units, with workers, the turn reset and the
    // economy walk's per-unit work run in one job on the SimPool (WE E4.3, P2).
    // Each chunk writes only its own units and its own list of contributions,
    // in unit order; the sums are then taken serially, chunk after chunk, in
    // exactly the serial order. A contribution of zero is left out of the
    // list: every sum starts at +0, a sum of finite floats starting at +0 is
    // never -0, and x + 0 is then exactly x (also through the double round
    // trip of the sacred and demand sums), so the totals are unchanged.
    const bool prologuePool = units_.size() >= kPrologueMinParallel && simPool().workers();
    if (!prologuePool) {
        TAK_PASS();
        for (auto& u : units_) resetTickState(u);
    } else {
        const size_t chunks = (units_.size() + SimPool::kUnitChunk - 1) / SimPool::kUnitChunk;
        if (economyChunks_.size() < chunks) economyChunks_.resize(chunks);
        simPool().forChunks(units_.size(), SimPool::kUnitChunk, 0, [&](size_t k, size_t begin, size_t end, unsigned) {
            auto& items = economyChunks_[k];
            items.clear();
            for (size_t i = begin; i < end; ++i) {
                Unit& u = units_[i];
                resetTickState(u);
                economyOf(u,
                    [&](EconomyKind kind, int player, float f, double d) {
                        if (f != 0.0f || d != 0.0) items.push_back({uint32_t(i), int16_t(player), kind, f, d});
                    },
                    [&] { items.push_back({uint32_t(i), 0, EconomyKind::Site, 0.0f, 0.0}); });
            }
        });
    }
    if (mission_ || scenario_) justDied_.clear();   // deaths this tick, fed to mission/scenario below
    transportEffects_.clear();
    scriptEmissions_.clear();
    scriptSounds_.clear();
    hits_.clear();   // per-tick weapon impacts (drained by the viewer for sounds/fx)
    // Economy: recompute income/storage, apply income.
    for (auto& tm : players_) { tm.income = 0; tm.storage = 0; }
    const size_t unitsAtEconomy = units_.size();
    // One walk (WE D4) does three things per unit, in unit order:
    //  - the beingBuilt clear for sites (factory and mobile construction mark
    //    them again below). It used to run after the god summon; nothing in
    //    between reads beingBuilt or changes alive/underConstruction, and the
    //    summon's spawn starts complete with beingBuilt false.
    //  - the income/storage sums, in the same order;
    //  - the retail construction demand of every retail-resources player (it
    //    was one walk per player). Each player's sum still adds its own builders
    //    in unit order, and nothing between here and the player loop below
    //    changes a unit, so every float comes out the same.
    // It also lists the sites for the decay pass below (A4).
    retailDemand_.assign(players_.size(),0.0f);
    sitesIdx_.clear();
    if (prologuePool) {
        const size_t chunks = (unitsAtEconomy + SimPool::kUnitChunk - 1) / SimPool::kUnitChunk;
#ifndef NDEBUG
        if (verifyPool()) {
            std::vector<EconomyItem> serial;
            for (size_t i = 0; i < units_.size(); ++i)
                economyOf(units_[i],
                    [&](EconomyKind kind, int player, float f, double d) {
                        if (f != 0.0f || d != 0.0) serial.push_back({uint32_t(i), int16_t(player), kind, f, d});
                    },
                    [&] { serial.push_back({uint32_t(i), 0, EconomyKind::Site, 0.0f, 0.0}); });
            size_t at = 0;
            for (size_t k = 0; k < chunks; ++k)
                for (const EconomyItem& item : economyChunks_[k]) {
                    const EconomyItem& o = serial.size() > at ? serial[at] : item;
                    if (serial.size() <= at || o.index != item.index || o.player != item.player || o.kind != item.kind ||
                        std::bit_cast<uint32_t>(o.f) != std::bit_cast<uint32_t>(item.f) ||
                        std::bit_cast<uint64_t>(o.d) != std::bit_cast<uint64_t>(item.d))
                        poolMismatch("economy walk", tickCounter_);
                    ++at;
                }
            if (at != serial.size()) poolMismatch("economy walk", tickCounter_);
        }
#endif
        for (size_t k = 0; k < chunks; ++k)
            for (const EconomyItem& item : economyChunks_[k]) {
                if (item.kind == EconomyKind::Site) sitesIdx_.push_back(item.index);
                else applyEconomy(item.kind, item.player, item.f, item.d);
            }
    } else {
        TAK_PASS();
        for (size_t i = 0; i < units_.size(); ++i)
            economyOf(units_[i], applyEconomy, [&] { sitesIdx_.push_back(uint32_t(i)); });
    }
    // Difficulty income cheat: scale the summed income so the boost flows through the
    // mana accrual below, allied surplus sharing, and god-favour alike. manaMult is 1
    // for everyone but an Absurd AI, so this is an exact no-op (x1.0) otherwise.
    for (auto& tm : players_) {
        tm.income *= tm.manaMult;
        if (tm.scenarioResourceLimit > 0) {
            tm.income = 0;
            tm.storage = tm.scenarioResourceLimit;
        }
    }
    // Apply income, then share the economy across allies: mana that would
    // overflow a player's storage flows to teammates that still have headroom,
    // so a maxed-out ally feeds the team instead of wasting mogrium. It is truly
    // wasted only when the whole team is capped. Deterministic -- collected in
    // player order, shared by lowest fill percentage. A solo/FFA player has no
    // teammate to receive the surplus, so this reduces exactly to the old clamp.
    for (size_t player=0;player<players_.size();++player) {
        auto& tm=players_[player];
        if (!tm.retailResources) { tm.creditMana(tm.income*dt);continue; }
        tm.retailResources->beginTick(tm.income,tm.storage,retailDemand_[player]);
        tm.mana=tm.retailResources->stored;tm.storage=tm.retailResources->capacity;
    }
    const int np = int(players_.size());
    auto cap = [&](int i) { return players_[size_t(i)].manaCapacity(); };
    bool teamDone[kMaxPlayers] = {};
    for (int lead = 0; lead < np; ++lead) {
        int team = players_[size_t(lead)].team;
        if (team < 0 || team >= kMaxPlayers || teamDone[team]) continue;
        teamDone[team] = true;
        uint8_t teamMask=0;
        for (int i=0;i<np;++i)
            if (players_[size_t(i)].team==team) teamMask |= uint8_t(1u<<i);
        // Pool only donors with the same allowed recipients. With the default
        // sharing settings this is the original single team pool and arithmetic.
        std::map<uint8_t,double> pools;
        for (int i = 0; i < np; ++i)
            if (!players_[size_t(i)].retailResources && players_[size_t(i)].team == team && players_[size_t(i)].mana > cap(i)) {
                const auto recipients=uint8_t(players_[size_t(i)].manaShareMask & teamMask);
                pools[recipients] += players_[size_t(i)].mana - cap(i);
                players_[size_t(i)].mana = cap(i);
            }
        for (auto [recipients,pool]:pools) {
            std::array<int, kMaxPlayers> needy{};
            int count = 0;
            for (int i = 0; i < np; ++i)
                if (!players_[size_t(i)].retailResources && !players_[size_t(i)].defeated &&
                    (recipients & (1u<<i)) && players_[size_t(i)].mana < cap(i))
                    needy[size_t(count++)] = i;
            auto fill = [&](int i) { return players_[size_t(i)].mana / double(cap(i)); };
            std::sort(needy.begin(), needy.begin()+count, [&](int a, int b) {
                return fill(a) != fill(b) ? fill(a) < fill(b) : a < b;
            });
            // Raise the emptiest stores together toward the next fill percentage.
            // Equal need shares proportionally to capacity, without slot preference.
            double capacity = 0, stored = 0, level = 0;
            int receiving = 0;
            for (int n = 0; n < count && pool > 0; ++n) {
                const int i = needy[size_t(n)];
                capacity += cap(i);
                stored += players_[size_t(i)].mana;
                receiving = n+1;
                const double next = n+1 < count ? fill(needy[size_t(n+1)]) : 1.0;
                level = std::min(next, (stored+pool)/capacity);
                if (level < next) break;
            }
            for (int n = 0; n < receiving; ++n) {
                const int i = needy[size_t(n)];
                const double give = std::min(pool, std::max(0.0, level*cap(i)-players_[size_t(i)].mana));
                if (give > 0) { players_[size_t(i)].creditMana(give); pool -= give; }
            }
            // Any pool left (every allowed recipient capped) is wasted, as before.
        }
    }
    // Construction allocation for the tick (51d837's allocation, computed after
    // income and the allied share): the stored pool over last tick's full-rate
    // demand, so every consumer advances at the same fraction instead of the first
    // in unit order taking it all. See Player::constructionShare.
    // A retailResources player runs the native object's allocation instead.
    for (auto& tm : players_) {
        const double demand=tm.retailResources ? 0.0 : tm.constructionDemand;
        tm.constructionAllocation=demand>tm.mana ? (tm.mana>0 ? tm.mana/demand : 0.0) : 1.0;
        tm.constructionDemand=0;
    }
    // ...and once it has filled, the god manifests. This has to happen HERE, in the
    // shared sim, and used to happen in the client instead (GameView::simStep polled
    // godReady() and called its own summonGod()). The referee never did it, so from
    // the first summon the server's world held one fewer unit than every client's and
    // the hashes split for the rest of the match -- a desync arriving tens of minutes
    // in, with nothing in the command stream to explain it.
    summonReadyGods();

    // Both factory and mobile construction keep their sites alive this tick
    // (beingBuilt was cleared in the economy walk above).
    // Factory missions run in unit order below. Protect their existing sites
    // from orphan decay while the producer still owns an active build.
    auto protectedSite = [&](const Unit& producer) -> Unit* {
        if (producer.alive() && producer.hp>Fixed() && !producer.incapacitated() &&
            !producer.embarked() && !producer.underConstruction && !producer.buildQueue.empty() && producer.productionSiteId)
            if (auto* site=unitNoRepair(producer.productionSiteId);site && site->alive() &&
                (site->hp>Fixed() || site->retailSite) && site->player==producer.player) return site;
        return nullptr;
    };
    // On the SimPool from kPrologueMinParallel units (WE E4.3): the walk only
    // sets flags to true, and nothing in it reads them, so any order of the
    // (relaxed atomic) stores gives the serial result.
    if (units_.size() >= kPrologueMinParallel && simPool().workers()) {
        simPool().forChunks(units_.size(), SimPool::kUnitChunk, 0, [&](size_t, size_t begin, size_t end, unsigned) {
            for (size_t i = begin; i < end; ++i)
                if (Unit* site = protectedSite(units_[i]))
                    __atomic_store_n(&site->beingBuilt, true, __ATOMIC_RELAXED);   // not std::atomic_ref (Apple libc++)
        });
#ifndef NDEBUG
        if (verifyPool())
            for (const auto& producer : units_)
                if (const Unit* site = protectedSite(producer); site && !site->beingBuilt)
                    poolMismatch("producer site walk", tickCounter_);
#endif
    } else {
        TAK_PASS();
        for (const auto& producer:units_)
            if (Unit* site = protectedSite(producer)) site->beingBuilt=true;
    }
    TAK_PASS();
    for (size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        u.constructionHolding=false;
        if (u.conjureHoverTarget && u.conjureHoverTarget!=u.buildSiteId &&
            u.conjureHoverTarget!=u.productionSiteId && u.conjureHoverTarget!=u.repairId) {
            u.conjureHoverTarget=0;u.conjureHoverGoal.reset();
        }
        if (u.alive() && u.type && u.retailBuild) {
            u.constructionHolding=!u.retailBuild->flying;
            if (auto* site=unit(u.retailBuild->target);site) site->beingBuilt=true;
        } else if (u.alive() && u.type && u.buildSiteId) tickConstruction(u, dt);
        else if (u.alive() && u.type && u.reclaimId) tickReclaim(u);
        else if (u.alive() && u.type && u.repairId) tickRepair(u, dt);
    }
    // Decay walks the sites listed by the economy walk, then every unit spawned
    // since (A4). Between the two, no unit already in units_ becomes alive or
    // under construction (only a fresh spawn does, and spawns append), so these
    // are all the units the full walk would decay, visited in the same order with
    // the same test at the same moment.
    auto decay = [&](Unit& u) {
        if (u.alive() && u.type && u.underConstruction && !u.beingBuilt && !(u.retailSite && u.retailSite->mission))
            decayConstruction(u, dt);
    };
#ifndef NDEBUG
    if (verifyLists()) {
        size_t next = 0;
        for (size_t i = 0; i < unitsAtEconomy; ++i) {
            while (next < sitesIdx_.size() && sitesIdx_[next] < i) ++next;
            const Unit& u = units_[i];
            if (u.alive() && u.type && u.underConstruction && (next == sitesIdx_.size() || sitesIdx_[next] != i)) {
                std::fprintf(stderr, "TAK_VERIFY_LISTS tick %u: site %d (index %zu) missing from the decay list\n",
                             tickCounter_, u.id, i);
                std::abort();
            }
        }
    }
#endif
    for (uint32_t i : sitesIdx_) decay(units_[i]);
    for (size_t i = unitsAtEconomy; i < units_.size(); ++i) decay(units_[i]);


    // Native airborne occupancy (persistent) is rebuilt before projectile updates.
    const auto projectileAircraft=updateAirOccupancy();
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
                noteShotImpact(p.fromId,p.targetId,*p.wsrc,p.position,hitUnit);
                applyHit(*p.wsrc,Fixed::raw(p.position[0]).toFloat(),
                    Fixed::raw(p.position[2]).toFloat(),p.fromPlayer,p.fromId,unit(hitUnit),p.position);
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
                if(hit.code==2) {
                    noteShotImpact(p.fromId,p.targetId,*p.wsrc,state.position,hit.unitId);
                    applyHit(*p.wsrc,p.x.toFloat(),p.z.toFloat(),p.fromPlayer,p.fromId,unit(hit.unitId),p.position);
                }
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
        // Older 2D-only fixture shots may still model a bomb as a terminal
        // effect. Production DroppedBallistic shots take the 3D collision path
        // above and never enter this approximate segment test.
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
                applyHit(*p.wsrc, t->x.toFloat(), t->z.toFloat(), p.fromPlayer, p.fromId, t, {},
                         std::array<int32_t,3>{t->x.v, (t->type && t->type->canFly ? t->flightY : t->groundY).v, t->z.v});
                p.life = -1;
                p.spent = true;   // connected: don't also crater at expiry
            }
        }
    }
    // The expiry fallback also handles any legacy 2D bomb fixture. Normal
    // DroppedBallistic shots retire in the 3D collision path above, where the
    // native impact cell/unit is known and cannot detonate twice.
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
        // A fallback 2D bomb is let go over its target, so whatever it lands on
        // takes the DIRECT hit -- several bombs carry no areaofeffect at all. A
        // missed shell has no such victim: retail's terrain impact carries no hit
        // unit, and a unit there would have been caught by the in-flight test.
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
        // Either way it bursts on the ground where it came down.
        const int ground = heights_.empty() ? 0 : retailTerrainHeight(bp.x.v, bp.z.v, terW_, terH_,
            [&](int x, int z) { return heights_[size_t(z) * size_t(terW_) + size_t(x)]; });
        applyHit(*bp.wsrc, bp.x.toFloat(), bp.z.toFloat(), bp.fromPlayer, bp.fromId, under,
                 {}, std::array<int32_t,3>{bp.x.v, Fixed::fromInt(ground).v, bp.z.v});
    }
    std::erase_if(projectiles_, [](const Projectile& p) { return p.life <= 0; });

    // The spatial hash for this tick (combat acquire + separation), and who is
    // parked where: units are solid (see sim.h). Its walk also counts the live
    // units for acqStride_ below.
    const size_t unitsBeforeGates = units_.size();
    uint32_t live = rebuildGridAndOccupancy();
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
        // The grid walk above counted them. Only tickAutomaticGates ran since:
        // a unit's type is set only by spawn, and its scripts (Activate and
        // Deactivate) can neither kill nor retire a unit -- alive() changes in
        // the unit loop's death sweep and in the construction, reclaim and
        // cancel paths, none reachable from a script. So the count still holds
        // unless something was spawned.
        auto countLive = [&] {
            uint32_t n = 0;
            TAK_PASS();
            for (const auto& u : units_) if (u.alive() && u.type) ++n;
            return n;
        };
        if (units_.size() != unitsBeforeGates) live = countLive();
#ifndef NDEBUG
        if (verifyLists() && live != countLive()) {
            std::fprintf(stderr, "TAK_VERIFY_LISTS tick %u: live count %u differs from a full count %u\n",
                         tickCounter_, live, countLive());
            std::abort();
        }
#endif
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
    // (relative to the goal) keep their own speed to catch up. Only bodies that can walk
    // count: a structure, a unit still under construction or a type with no speed stays
    // in the squad (produced units inherit it) but never sets the centre or the pace --
    // a building's speed of 0 used to pace the whole army to a standstill. Deterministic:
    // the sums are int64 of Fixed raw values, accumulated in unit-index order.
    constexpr float kFormBehind = 48.0f;   // sprint if this far behind the group (goal-relative)
    constexpr float kFormRejoin = 140.0f;  // an idle member at least this far from the centre may rejoin
    struct FormAgg {
        int64_t sx = 0, sz = 0; int n = 0;
        Fixed slowest = Fixed::raw(std::numeric_limits<int32_t>::max());
        int area = 0;   // footprint cells of its mobile members
        int busy = 0;   // mobile members still carrying out an order
        // Its ground members only (Legion's rejoin, W6: flyers neither set the
        // centre nor count).
        int64_t gx = 0, gz = 0; int gn = 0, garea = 0;
        float cx() const { return Fixed::raw(int32_t(sx / n)).toFloat(); }
        float cz() const { return Fixed::raw(int32_t(sz / n)).toFloat(); }
    };
    FormAgg forms[kMaxPlayers][11] = {};   // [player][1..10]; slot 0 unused
    auto formOf = [&](const Unit& u) -> FormAgg* {
        // Retail paces through the native group records instead.
        if (u.squad >= 0 || u.player < 0 || u.player >= kMaxPlayers ||
            pathfindingMode_==PathfindingMode::Retail) return nullptr;
        return &forms[u.player][-u.squad];
    };
    // Retail has no formation aggregates (formOf is null for every unit), so it
    // skips this walk and the rejoin walk below: neither can do anything there.
    const bool formations = pathfindingMode_!=PathfindingMode::Retail;
    if (formations) TAK_PASS();
    if (formations) for (auto& u : units_)
        if (u.alive() && u.type && u.squad < 0 && !u.type->isStructure() &&
            !u.underConstruction && u.baseSpeed.v > 0)
            if (FormAgg* f = formOf(u)) {
                f->sx += u.x.v; f->sz += u.z.v; ++f->n;
                f->slowest = fxMin(f->slowest, u.baseSpeed);
                f->area += u.type->footX * u.type->footZ;
                f->busy += !u.orders.empty();
                if (!u.type->canFly) {
                    f->gx += u.x.v; f->gz += u.z.v; ++f->gn;
                    f->garea += u.type->footX * u.type->footZ;
                }
            }
    if (pathfindingMode_==PathfindingMode::Retail) tickRetailGroups();
    planLegionFlightStations();
    // An idle member standing against an idle same-player body that is nearer
    // the centre is at the crowd's face already: walking in cannot bring it
    // closer, and Legion would settle it there again at once (its "close
    // enough" rule). Re-ordering it anyway looped forever.
    auto pressedAgainstCrowd = [&](const Unit& u, float cx, float cz) {
        const int fx = u.type->footX, fz = u.type->footZ, foot = std::max(fx, fz);
        const int ox = footprintOrigin(u.x, fx), oz = footprintOrigin(u.z, fz);
        const float ux = u.x.toFloat() - cx, uz = u.z.toFloat() - cz, own = ux * ux + uz * uz;
        for (int j = -foot; j < fz + foot; ++j) for (int i = -foot; i < fx + foot; ++i) {
            const int x = ox + i, z = oz + j;
            if (x < 0 || z < 0 || x >= occW_ || z >= occH_) continue;
            const int32_t o = occ_[size_t(z) * occW_ + x];
            if (!o || o == u.id) continue;
            const Unit* b = unit(o);
            if (!b || !b->alive() || b->player != u.player || !b->type || b->type->isStructure() ||
                !b->orders.empty()) continue;
            const float bx = b->x.toFloat() - cx, bz = b->z.toFloat() - cz;
            if (bx * bx + bz * bz < own) return true;
        }
        return false;
    };
    // Legion's rejoin (W6, PLAN 3.5 / C4): the centre and count are the
    // ground's (flyers are never re-ordered), and a formation whose members
    // settled at a destination gathers there, not at its centroid: P is the
    // modal settled-arrival point of its ground members (ties to the lowest
    // point). A body settled at P is never re-ordered; another is re-ordered
    // to P (a fresh order) only if it is beyond twice the settled crowd's
    // reach of P (settleReach, 8x the packed disc plus 4 bodies) and farther
    // from P than the centroid is. With no settled member the centroid rule
    // stands. Candidates are found by the old test first, so P is computed
    // only for a resting formation that has one.
    const bool legionRejoin = isLegionPathfinding(pathfindingMode_) && legion_;
    struct RejoinPoint { bool done = false, has = false; int32_t x = 0, z = 0; int count = 0; };
    std::array<std::array<RejoinPoint, 11>, kMaxPlayers> rejoinPoints{};
    auto rejoinPoint = [&](const Unit& u) -> const RejoinPoint& {
        auto& r = rejoinPoints[size_t(u.player)][size_t(-u.squad)];
        if (r.done) return r;
        r.done = true;
        std::map<std::pair<int32_t,int32_t>,int> counts;
        for (const auto& o : units_) {
            if (!o.alive() || !o.type || o.player != u.player || o.squad != u.squad || o.type->canFly ||
                o.type->isStructure() || o.underConstruction) continue;
            int32_t x, z; int n;
            if (legion_->arrivalPoint(o.id, x, z, n)) ++counts[{x, z}];
        }
        int best = 0;
        for (const auto& [point, n] : counts)
            if (n > best) { best = n; r.has = true; r.x = point.first; r.z = point.second; }
        r.count = best;
        return r;
    };
    if (formations) TAK_PASS();
    if (formations) for (auto& u : units_) {
        if (!u.alive() || !u.type || u.squad >= 0 || !u.orders.empty()) continue;
        if (u.type->isStructure() || u.underConstruction) continue;   // buildings don't rejoin
        if (legionRejoin) {
            if (u.type->canFly) continue;
            FormAgg* f = formOf(u);
            if (!f || f->gn <= 1 || f->busy) continue;
            const float cx = Fixed::raw(int32_t(f->gx / f->gn)).toFloat(), cz = Fixed::raw(int32_t(f->gz / f->gn)).toFloat();
            const float d = detmath::len(u.x.toFloat() - cx, u.z.toFloat() - cz);
            if (d <= kFormRejoin || d * d <= 1024.0f * float(f->garea)) continue;
            const RejoinPoint& p = rejoinPoint(u);
            if (!p.has) {
                if (!pressedAgainstCrowd(u, cx, cz)) order(u.id, cx, cz, false);
                continue;
            }
            int32_t ax, az; int n;
            if (legion_->arrivalPoint(u.id, ax, az, n) && ax == p.x && az == p.z) continue;
            const int64_t body = int64_t(std::max(u.type->footX, u.type->footZ)) * 16;
            const int64_t reach = 4 * body * int64_t(isqrt64(uint64_t(p.count + 1) * 100000000 / 31416)) / 100 + 2 * body;
            const int64_t ux = (int64_t(u.x.v) - p.x) >> 16, uz = (int64_t(u.z.v) - p.z) >> 16;
            const int64_t mx = (f->gx / f->gn - p.x) >> 16, mz = (f->gz / f->gn - p.z) >> 16;
            const int64_t mine = ux * ux + uz * uz;
            if (mine <= reach * reach || mine <= mx * mx + mz * mz) continue;
            const float px = Fixed::raw(p.x).toFloat(), pz = Fixed::raw(p.z).toFloat();
            if (!pressedAgainstCrowd(u, px, pz)) order(u.id, px, pz, false);
            continue;
        }
        FormAgg* f = formOf(u);
        // Only a formation at rest gathers. While members still walk an
        // order, the ones already done are its front (a column through a gap
        // arrives one by one), not stragglers: pulling them back to a centre
        // that still trails behind the gap sent them into the column.
        if (!f || f->n <= 1 || f->busy) continue;
        const float cx = f->cx(), cz = f->cz();
        // A straggler is outside the native formation radius (51d1e0 at level
        // 3: twice the root of the members' footprint area, in cells), never
        // nearer than kFormRejoin. A fixed radius alone called every outer
        // body of a large formation a straggler wherever it settled.
        const float d = detmath::len(u.x.toFloat() - cx, u.z.toFloat() - cz);
        if (d > kFormRejoin && d * d > 1024.0f * float(f->area) && !pressedAgainstCrowd(u, cx, cz))
            order(u.id, cx, cz, false);
    }

    // The prologue ends here: burn and vis have their own buckets.
    if (g_phase) g_prologueMs = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - _pro0).count() - g_burnMs - g_visMs;
#ifndef NDEBUG
    if (g_phase && probe::kPmu) {
        probe::pmuAdd(probe::kPrologue, _pmuPro);
        for (int i = 0; i < 4; ++i)
            probe::tlPmu[probe::kPrologue].v[i] -= probe::tlPmu[probe::kBurn].v[i] + probe::tlPmu[probe::kVis].v[i];
    }
#endif
    TAK_PASS();
    for (size_t unitIndex=0;unitIndex<units_.size();++unitIndex) {
#if defined(__GNUC__) || defined(__clang__)
        // Cache hints for the units ahead, in three stages so that every address
        // is computed from data an earlier stage already fetched (WE D2). Hints
        // only: the same code paths run in the same order on the same values.
        //  - i+4: the Unit's lines 0-8 (WE D1: the hot, per-tick and warm
        //    lines; the cold tail is read by flyers, the dead and rare events).
        //    At 10k+ units units_ does not stay in cache between ticks (WE
        //    E2.1b: once findTarget's candidate walks got cheaper, their
        //    incidental warming of the neighbours' lines was gone and the
        //    misses moved here).
        //  - i+2 (its lines arrived two iterations ago): its orders' predicate
        //    lines (Order line 0, plus the front order's line 1), the line
        //    of its script VM that holds the active-thread mask, and for a
        //    flyer or a dead body the cold lines 9-11 that its update reads
        //    (corpse state, air occupancy, flight vectors, Legion lift).
        //  - i+1: the first line of each ACTIVE script thread only (the mask
        //    fetched one iteration ago), not all sixteen.
        if (unitIndex+4<units_.size()) {
            const char* ahead=reinterpret_cast<const char*>(&units_[unitIndex+4]);
            for (size_t at=0;at<kUnitPrefetchHot;at+=64) __builtin_prefetch(ahead+at,0,3);
        }
        if (unitIndex+2<units_.size()) {
            const Unit& next=units_[unitIndex+2];
            if (!next.alive() || (next.type && next.type->canFly)) {
                const char* cold=reinterpret_cast<const char*>(&next);
                for (size_t at=kUnitPrefetchHot;at<kUnitPrefetchHot+3*64;at+=64) __builtin_prefetch(cold+at,0,3);
            }
            if (const size_t n=next.orders.size()) {
                const Order* o=next.orders.data();
                __builtin_prefetch(o,0,3);
                __builtin_prefetch(reinterpret_cast<const char*>(o)+64,0,3);
                for (size_t k=1;k<n && k<4;++k) __builtin_prefetch(o+k,0,3);
            }
            const size_t id=size_t(next.id);
            if (id<unitScriptById_.size()) if (const auto* script=unitScriptById_[id]) {
                __builtin_prefetch(&script->peek().vm.active,0,3);
                __builtin_prefetch(script->skipLine(),0,3);   // the skip test's line (often the same)
            }
        }
        if (unitIndex+1<units_.size()) {
            const size_t id=size_t(units_[unitIndex+1].id);
            if (id<unitScriptById_.size()) if (const auto* script=unitScriptById_[id]; script && !script->willSkip()) {
                const auto& vm=script->peek().vm;
                for (uint32_t m=vm.activeThreadMask();m;m&=m-1)
                    __builtin_prefetch(vm.threads[size_t(std::countr_zero(m))].words.data(),0,3);
            }
        }
#endif
        if (!units_[unitIndex].type) continue;
        if (units_[unitIndex].alive() && units_[unitIndex].constructionEmitter)
            units_[unitIndex].constructionEmitter->advance();
        if (units_[unitIndex].alive() && units_[unitIndex].cosmeticConstructionEmitter)
            units_[unitIndex].cosmeticConstructionEmitter->advance();
        if (g_phase) {
            PhaseSpan span(g_scriptMs,probe::kScripts);
            tickUnitScript(units_[unitIndex]);
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
            rebuildGridAndOccupancy();
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
                world.updateLegionStill(u,x,z);
                world.notifyMovementRate(u);
                if (u.alive() && !u.embarked() && !u.type->canFly && !u.type->isStructure() &&
                    u.flightGroundMode==1 && (u.type->canHover || u.x!=x || u.z!=z || u.heading!=heading))
                    u.groundY=world.surfaceHeight(u,world.tickCount(),&u.groundPitch,&u.groundRoll);
            }
        } surfaceUpdate{*this,u.id,u.x,u.z,u.heading};
        if (!u.alive()) {
            ++u.deadFor;
            if (u.corpseStatue<0 && u.deadFor==u.corpseAnimationTicks() && u.deadFor<u.corpseUntil) {
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
            killProductionOutput(u);
            if (u.player >= 0 && u.player < int(players_.size()))
                players_[size_t(u.player)].losses++;   // end-of-game "Losses" column
            // Native512c4c uses the player captured by the damaging shot.
            // A killer converted after impact must not move its earlier score.
            if(!scoreAutomaticDisabled_ && u.type && !u.underConstruction && u.lastHitPlayer>=0 && u.lastHitPlayer<int(players_.size()) &&
               u.lastHitPlayer!=u.player) {
                auto& score=players_[size_t(u.lastHitPlayer)].score;
                score=std::bit_cast<int32_t>(uint32_t(score)+uint32_t(u.type->experiencePoints));
            }
            // Award the destroyed unit's experiencepoints to the killer, then set
            // its veteran level = accumulatedXP / the killer's OWN experiencepoints,
            // capped at 10 (retail KINGDOMS.icd). No HP-pool change — veterancy
            // works through the attack/armor multipliers, not a bigger health bar.
            if (u.lastHitBy && u.type) {
                Unit* k = unit(u.lastHitBy);
                // Credit the killer's player with an enemy kill (F4 overlay). Counts
                // even if the killer is a structure or has since died -- what
                // matters is who landed the fatal blow, not that it still lives.
                const int killOwner=creditedOwner(u.lastHitBy);
                if(validUnitOwner(killOwner) && !allied(killOwner,u.player))
                    players_[size_t(killOwner)].kills++;
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
            // the dying unit's OWNER so kills count for the player who built it. Like
            // retail's area pass (529dc0) it hurts allies too; only the source unit
            // is skipped. Deferred to a
            // queue: applyHit walks units_ and can kill others, and we are mid-sweep
            // over units_ -- draining after the loop keeps that safe and keeps the
            // order deterministic (sweep order, which is id order).
            // Native 512d5a requires positive severity for the death weapon.
            // Stone/frozen packets have zero severity (51275f), so they
            // preserve the body without a blast.
            // 52ac00 selects UnitDef+1ba for type 5 and +1b6 otherwise.
            // A missing SELFDESTRUCTAS does not fall back to EXPLODEAS.
            const bool selfDestruct=u.deathType==Unit::kDeathSelfDestruct;
            const Weapon* deathWeapon=selfDestruct
                ? (u.type->hasSelfDestructAs ? &u.type->selfDestructAs : nullptr)
                : (u.type->hasExplodeAs ? &u.type->explodeAs : nullptr);
            if (deathWeapon && u.deathType!=0 &&
                (u.deathType<6 || u.deathType>11) && u.deathType<14)
                deathBlasts_.push_back({deathWeapon, {u.x.v, (u.type->canFly ? u.flightY : u.groundY).v, u.z.v},
                                        u.player, u.id});
            // Killed decides corpse admission; Dying controls when the original
            // body retires. Only then does decomposition or water sinking begin.
            {
                // Retail severity (icd 0x512610): ((overkill% + HP% one second
                // before death) / 2), clamped 1..100. Passed to the COB Killed.
                float okPct = u.overkill.toFloat() * 100.0f / std::max(float(u.maximumHp()), 1.0f);
                u.severity = uint8_t(std::clamp((okPct + float(u.hpPct1s)) * 0.5f,
                                                1.0f, 100.0f));
                if (selfDestruct) u.severity=1;
                else if (u.deathType==0 || (u.deathType>=6 && u.deathType<=11) || u.deathType>=14)
                    u.severity=0;
                // Dying while petrified/frozen leaves the FBI stone=/frozen=
                // STATUE feature instead of the corpse (retail deathType 0xE/0xF
                // path, 0x512d2a) -- blocking, permanent, and resurrectable
                // (raising a statue un-petrifies the unit).
                u.corpseStatue = u.stonedFor > 0 ? statueTypeOf(u.type, false)
                              : u.frozenFor > 0 ? statueTypeOf(u.type, true) : -1;
                static const bool kStatLog2 = tak::devEnv("TAK_BURNLOG") != nullptr;
                if (kStatLog2 && (u.stonedFor > 0 || u.frozenFor > 0))
                    std::fprintf(stderr, "statue edge: %s statue=%d stoned=%d\n",
                                 u.type->id.c_str(), u.corpseStatue, u.stonedFor);
                beginUnitDeath(u);
                if (u.type->isStructure())
                    blockFoot(*u.type,u.x.toFloat(),u.z.toFloat(),false);
            }
            // Drop this unit's per-unit pathfinding state. Ids are never reused, so a
            // dead unit's entries can never match anything again -- they would simply
            // accumulate for the rest of the match.
            retireAirOccupant(u);
            // 5066a0 (World retires a body from the ground word at death).
            groundRemove(u);
            u.deadFor = 0; u.orders.clear(); u.speed = Fixed();
            noteOrders(u.id);
            // Refresh the old body before a corpse offset moves the record.
            for (auto& plane:searchGrades_)
                refreshSearchRect(plane,footprintOrigin(u.x,u.type->footX),
                    footprintOrigin(u.z,u.type->footZ),u.type->footX,u.type->footZ);
            // Frozen/stone deaths bypass the ordinary death animation.
            if (u.corpseAnimationTicks()==0 && u.deadFor<u.corpseUntil) {
                const int corpse=u.corpseStatue>=0 ? u.corpseStatue : corpseTypeOf(u.type);
                if (corpse>=0 && (u.corpseStatue>=0 || !u.underConstruction) && !placeCorpse(u,corpse)) retireCorpse(u);
            }
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
                u.hp = Fixed(); u.lastHitBy = 0; u.lastHitPlayer = -1; u.deathType = Unit::kDeathSelfDestruct;
            }
        }
        // Deadly water (.ota waterdoesdamage): a ground unit standing in it is
        // burned down. Flyers are over it, not in it; ships and amphibians belong
        // there, so anything that can cross water is exempt.
        if (waterDamage_ > 0 && !u.type->canFly && u.type->maxWaterDepth <= 0 &&
            isWater(u.x.toFloat(), u.z.toFloat())) {
            // Saturating: a retail site stays alive at or below zero hp.
            u.hp = Fixed::raw(int32_t(std::clamp<int64_t>(int64_t(u.hp.v) - Fixed::fromFloat(waterDamage_ * dt).v,
                                                          -int64_t(INT32_MAX), INT32_MAX)));
            if (u.hp <= Fixed()) { u.overkill = fxMax(u.overkill, -u.hp); u.deathType = 1; }
        }
        // Self-repair (KINGDOMS.icd 0x51e152..0x51e1bc). healtime is the unit's own
        // WORKER TIME, not seconds per hit point: every eighth tick a finished unit
        // below full health puts healtime * 8/30 build-seconds into itself through
        // the shared repair routine, so it regains maxdamage * healtime / buildtime
        // hit points a second (retailSelfRepairPoints). The data is authored that
        // way: healtime is exactly buildtime/240 on most troops (full health in
        // four minutes) and buildtime/480 on many buildings (eight). Read as
        // seconds per point it gave the Crusades Aramon Keep 0.32 HP/s instead of
        // 49.1, and the Crusades monarchs (216.67 against buildtime 100000) a
        // point every 3.6 minutes. An unfinished unit does not repair itself
        // (retail requires +0x108 == 0); hit points stay whole in retail, so a
        // fraction ours picked up elsewhere rides along unchanged.
        //
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
        if (u.type->healTime != 0 && (tickCounter_ & 7) == 0 && !u.underConstruction &&
            u.hp > Fixed() && u.hp < Fixed::fromInt(u.maximumHp())) {
            const uint32_t maxHp = uint32_t(u.maximumHp());
            const uint16_t whole = uint16_t(u.hp.floorInt());
            const uint16_t healed = retailAddHitPoints(whole,
                retailSelfRepairPoints(u.healCarry, u.type->healTime,
                                       retailInverseBuildTime(u.type->buildTime), maxHp), maxHp);
            u.hp = fxMin(Fixed::fromInt(int32_t(maxHp)), u.hp + Fixed::fromInt(int32_t(healed) - whole));
        }
        if (u.type->maxMana > 0 && u.mana < u.type->maxMana)
            u.mana = std::min(double(u.type->maxMana), u.mana + double(u.type->manaRegen) * double(dt));
        // A WANDERER keeps its spawn point as home -- that anchor is what stops its
        // stroll turning into a migration.
        if (u.orders.empty() && !u.type->wanders) { u.homeX = u.x; u.homeZ = u.z; }
        // Retail dispatches each unit's missions immediately before its mover,
        // not all ground missions before every other unit's activity.
        if (u.constructionHolding && (u.buildSiteId || u.repairId)) continue;
        if (u.legionLift && tickLegionLift(u)) continue;
        // MobileBuild/FlyingBuild standing (or hovering) at its site, waiting
        // out mobile units on it (405824..405911, 41f301..41f3e5): navigation
        // has stopped (tickNavigationMovement only brakes or holds it); only
        // the retry below runs.
        const bool placementWait=!u.orders.empty() &&
            u.orders.front().buildType && !u.orders.front().buildRectangle &&
            u.orders.front().placementFailures>0;
        if (!placementWait) {
            tickManaBuildArea(u);
            tickGuardHelp(u);
            tickPatrolRepair(u);
            if (g_phase) {
                PhaseSpan span(g_moveMs,probe::kMovement);
                tickGroundMission(u);
            } else tickGroundMission(u);
            if (plainFlightPatrol(u)) tickFlightPatrol(u);
            tickGuardNoMove(u);
        }
        // A captured MobileBuild waits for navigator arrival before creating
        // the site. Keep that rectangle attached to the owning goal across
        // every intermediate route, and hand construction over on the next tick.
        if (!u.orders.empty()) {
            auto& goal=u.orders[currentLeg(u.orders)];
            if (placementWait ? tickCounter_>=u.orders.front().placementRetryTick : prepareBuildApproach(u)) {
                if (!placementWait) {
                    const Order arrived=goal;
                    dropLeg(u);
                    u.orders.insert(u.orders.begin(),arrived);
                    u.orders.front().buildRectangle.reset();
                    u.orders.front().x=arrived.buildX;
                    u.orders.front().z=arrived.buildZ+Fixed::fromInt(arrived.buildType->footZ*8+24);
                    cancelPath(u);
                }
                const Order arrived=u.orders.front();
                // Allocate during this unit's dispatch: later units may draw
                // construction particles from the same CRT stream.
                const int builderId=u.id;
                bool mobileBlocked=false;
                const int siteId=startBuild(builderId,arrived.buildType,arrived.buildX.toFloat(),
                                             arrived.buildZ.toFloat(),Approach::None,&mobileBlocked);
                if (siteId) {
                    auto& builder=*unit(builderId);
                    // 405914/41f3e8: a successful placement takes the weapons
                    // (519b70(3) drops every slot target) until the job ends.
                    setSlotsManual(builder,kSlotsWork);
                    if (builder.type->canFly) {
                        // The native unit update runs its independent mover
                        // after FlyingBuild allocates the site. Continue the
                        // accepted navigator output without another controller
                        // or a ground-builder pivot on this same update.
                        tickFlightBody(builder);
                        notifyFlightOccupancy(builder);
                    } else turnBuilderToSite(builder,*unit(siteId));
                } else {
                    // 405824..405911 (FlyingBuild 41f301..41f3e5 alike): a site
                    // refused only by mobile units is retried every 10 ticks
                    // where the builder stands or hovers. Retail
                    // says UNITCHATTER_WAITFORTARGETTOCLEAR on the 7th try and
                    // UNITCHATTER_TARGETBLOCKED when the 31st fails; we have
                    // no unit-chatter channel, so both pass silently. Anything
                    // else (a building, a feature, terrain, caps) adds 999 to
                    // the count there: the order is dropped at once and the
                    // builder moves on to its next order.
                    auto& builder=*unit(builderId);
                    auto& order=builder.orders.front();
                    if (mobileBlocked && ++order.placementFailures<31) {
                        order.placementRetryTick=tickCounter_+10;
                        clearBuildSite(builder,order.buildType,order.buildX,order.buildZ);
                        // FlyingBuild's point controller detached on arrival:
                        // nothing steers the hovering flyer during the wait,
                        // and its hold starts with this update's skipped body.
                        order.flightGoal.reset();
                        if (builder.type->canFly) holdFlight(builder);
                    } else popBuildOrder(builder);
                }
                // spawn invalidates references; re-establish slot order before
                // continuing so a later newborn receives its first update today.
                if (retailAllocation_) {
                    std::sort(units_.begin(),units_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
                }
                bodyIndexValid_=false;
                unitIndex=size_t(std::find_if(units_.begin(),units_.end(),
                    [&](const auto& candidate){return candidate.id==builderId;})-units_.begin());
                rebuildGridAndOccupancy();
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

        // Cloaking spends the UNIT'S OWN mana, never the player's store (icd
        // 0x401160, after the regeneration above: 0x4010d0 on unit +0xd8, so it
        // never shows in the mana bulb). A tick costs cloakcost/30, or
        // cloakcostmoving/30 while the unit's MoveRate tier (+0x130 bits 2-3,
        // set by 4db350 after its mover) is not 0 -- moving or turning; a unit
        // whose pool cannot pay it decloaks and may not cloak again for 90
        // ticks (+0xcc = max(+0xcc, now + 90)), then retries while the order
        // stands. Shots, construction, repair, reclaim and revive work set
        // +0xcc too (blockCloak). Nothing else decloaks: retail reads
        // mincloakdistance only to draw a circle round a cloaked unit (0x4d6530,
        // under the renderer 0x4fc5b0), and the +0x130 & 0x400 veto it tests
        // has no writer.
        if (u.type->canCloak && u.cloakOn) {
            const UnitScript* script = unitScript(u.id);
            const bool moving = script ? script->movementRate != 0 : u.speed.v != 0;
            const float cost = (moving ? u.type->cloakCostMove : u.type->cloakCost) * dt;
            if (u.cloakBlockedUntil > tickCounter_) u.cloaked = false;
            else if (double(cost) <= u.mana) { u.mana -= double(cost); u.cloaked = true; }
            else {
                u.cloakBlockedUntil = std::max(u.cloakBlockedUntil, tickCounter_ + 90u);
                u.cloaked = false;
            }
        } else if (u.cloaked) {
            u.cloaked = false;   // toggled off (or no longer a cloaker): decloak now
        }

        if (u.underConstruction) continue;   // silent until finished
        // Retail 51d9d4 updates weapon timers independently of transport
        // missions; 52ae90 decrements before checking whether a weapon can aim.
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
        if (u.embarked()) {                  // riding a transport
            Unit* t = unit(u.inTransport);
            if (t && t->alive()) {
                // 4dad30's attached branch commits the host attach point as the
                // cargo's own +0x68 every mover tick, height included; the sight
                // batch then reads it (see updateNavigationExploration).
                const Fixed hostY=t->type->canFly?t->flightY:t->groundY;
                const auto point=retailAttachedPosition(
                    retailAttachPoint({t->x.v,hostY.v,t->z.v},{},{},-1,0),
                    u.type->floater,uint8_t(u.type->waterline),uint8_t(seaLevel_));
                u.x=Fixed::raw(point[0]);u.groundY=Fixed::raw(point[1]);u.z=Fixed::raw(point[2]);
                updateBodyIndex(u);
            }
            else { if (mission_ || scenario_) justDied_.push_back(u.id); retireAirOccupant(u); u.deadFor = 0; noteOrders(u.id); }  // transport lost with all hands
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
        // (A MobileBuild waiting out mobile units on its site retries above.)
        if (!u.orders.empty() && u.orders.front().buildType && !u.orders.front().buildRectangle && u.buildSiteId == 0 &&
            !u.orders.front().placementFailures)
            buildDue_.push_back(u.id);
        tickReclaimArea(u);
        // Reclaim needs no deferral: consuming a feature spawns nothing, so
        // nothing can reallocate units_ underneath us. Its stage 0 runs as
        // soon as the leg is current, before the mover (406250, 4d4e80).
        if (!u.orders.empty()) {
            const auto& leg=u.orders[currentLeg(u.orders)];
            if (leg.reclaimFeat && u.reclaimId == 0) u.reclaimId = leg.reclaimFeat;
            // (A body still dying waits here until its corpse feature exists.)
            if (leg.reclaimFeat && leg.reclaimFeat == u.reclaimId && leg.mission.stage == 0)
                prepareReclaimApproach(u);
        }
        if (!u.orders.empty() && u.repairId == 0) {
            const auto& leg=u.orders[currentLeg(u.orders)];
            // A ground repair's leg is the job, route corners ahead of it or not.
            u.repairId=leg.patrolRepair || !u.type->canFly ? leg.repairTarget : u.orders.front().repairTarget;
        }
        // HelpBuild's and RepairUnit's stage 0 run as soon as the leg is
        // current, before the mover (405d70, 407289; 4d4e80 for both).
        prepareHelpApproach(u);
        prepareRepairApproach(u);

        // 52ae90's auto slots: fire at swept targets whatever the order.
        tickSlotWeapons(u);

        bool groundMovementHandled=false;
        const size_t currentOrder=u.orders.empty()?0:currentLeg(u.orders);
        const bool routedSurfaceUnload=!u.orders.empty() && currentOrder<u.orders.size() &&
            u.orders[currentOrder].unload && u.orders[currentOrder].transportUnloadApproach;
        if (!u.orders.empty() && (u.orders.front().load || u.orders.front().unload || routedSurfaceUnload))
            groundMovementHandled=tickTransport(u, dt);
        else if (g_phase) { PhaseSpan span(g_tcomb,probe::kCombat); tickCombat(u, dt, groundMovementHandled); }
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
                // building's edge gets dragged back into moving) -- and, as there,
                // only when the reach weapon is melee (Weapon::centreReach).
                const int reachSlot = approachSlot(u);
                const bool centreReach =
                    reachSlot >= 0 && u.type->weapons[size_t(reachSlot)].centreReach();
                float pad = t->type->maxVel <= Fixed() && !centreReach
                    ? 8.0f * float(std::max(t->type->footX, t->type->footZ)) + 24.0f : 0.0f;
                // Weapon switchers approach using the selected weapon, just as
                // tickCombat does. A longer secondary spell must not stop a
                // dragon outside its selected breath weapon's range.
                const bool allWeapons=!u.type->weaponSwitching && u.type->weapons.size()>1;
                const float range=allWeapons ? u.type->maxRange() : (sel ? float(sel->range) : u.type->maxRange());
                if (std::sqrt(dx * dx + dz * dz) > (range + pad) * 0.95f) return false;
                // Don't sit still with no shot: a ranged unit whose line to the
                // target is blocked keeps moving to get around the wall.
                bool needLoS = u.type->canMove && approachNeedsClearLine(u,*t);
                return !needLoS ||
                       combatLineOfSight(u,*t);
            }();
        if (combatHold) {
            // The hold skips the flight body, so a flyer stops dead where it
            // came into range. Its velocity has to stop with it: retail's lead
            // (51aa50) reads the target mover's velocity, so a stale one made
            // every leading shooter aim ahead of a flyer hanging still -- a
            // Lokken's Fire Ball missed an attacking Gryphon 47 times of 47.
            // Speed is |velocity| (tickFlightBody), so it stops too; the next
            // flight step reads it for its vertical limit.
            if (u.type->canFly) holdFlight(u);
            continue;
        }

        Fixed target = u.baseSpeed;
        // Formation pacing uses the slowest member, except for a member
        // catching up from behind the group's centre.
        if (u.squad < 0 && !u.orders.empty() && u.orders.front().targetId == 0) {
            if (FormAgg* f = formOf(u); f && f->n > 1) {
                const float cx = f->cx(), cz = f->cz();
                // "Behind the centre" is measured against the leg the group is
                // walking NOW -- against the last QUEUED leg a straggler check
                // would compare everyone to a point nobody is heading for yet.
                const Order& legEnd = u.orders[currentLeg(u.orders)];
                float gx = legEnd.x.toFloat(), gz = legEnd.z.toFloat();
                float uToGoal = detmath::len(u.x.toFloat() - gx, u.z.toFloat() - gz);
                float cToGoal = detmath::len(cx - gx, cz - gz);
                if (uToGoal <= cToGoal + kFormBehind) target = fxMin(target, f->slowest);
            }
        }
        if (pathfindingMode_==PathfindingMode::Retail) retailGroupMaximum_=retailGroupLimit(u);
        if (g_phase) {
            PhaseSpan span(g_moveMs,probe::kMovement);
            tickNavigationMovement(u,target);
        } else tickNavigationMovement(u,target);
        retailGroupMaximum_=Fixed();

    }

    // Retail updates missions/movers in 51d3e0 before 4f6c70 runs 416430
    // (5263aa, then 526411). Install completed routes only after movement,
    // and admit new searches from the position and occupancy after that step.
    if (pathService_) {
      SearchGradeBatch searchBatch(*this);
      paths_.tick(
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
    }

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
    if (!retailAllocation_) {
        // The fire-at-will sweep's per-player slot ranges: each seat's units
        // in id order (units_ is id-ordered).
        sweepIds_.resize(players_.size());
        for (auto& ids:sweepIds_) ids.clear();
        for (const auto& member:units_)
            if (member.alive() && member.player>=0 && size_t(member.player)<sweepIds_.size())
                sweepIds_[size_t(member.player)].push_back(member.id);
    }
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
        if (scenario_ && !player.retailResources)
            player.mana = std::clamp(player.mana,0.0,double(player.manaCapacity()));
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
                    TAK_PASS();
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
        // 40fe00 -> 40f330: the fire-at-will sweep, after the AI scheduler and
        // before the bookkeeping clock. Every seat runs it; neutral does not.
        if (const int seat=int(&player-players_.data()); !isNeutralPlayer(seat)) tickFireSweep(seat);
        if (player.buildCache && !player.retailResources) player.buildCache->resources.advance();
        // 411a90 -> 411700: the visibility lists are rebuilt on the bookkeeping
        // clock, before its rand(30) draw. A player without that clock uses
        // the same 7-tick rule on its own stamp.
        if (const int seat=int(&player-players_.data()); !isNeutralPlayer(seat)) {
            uint32_t& last=player.cacheClock.enabled ? player.cacheClock.lastRefresh : player.visibleRefresh;
            if (tickCounter_>=last+7u) {
                rebuildVisibleLists(seat);
                if (!player.cacheClock.enabled) player.visibleRefresh=tickCounter_;
            }
        }
        if (retailTickPlayerCache(player.cacheClock,tickCounter_,[&](int n){return pathRand(n);}) && player.buildCache) {
            auto& cache=*player.buildCache;
            const int owner=int(&player-players_.data());
            unsigned population=0;
            for (auto& entry:cache.entries) entry.inputs.count=entry.inputs.completed=0;
            TAK_PASS();
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
            const float capacity=player.manaCapacity(); // current World economy host
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
        if (pulse && cur.w) applyHit(*cur.w, cur.x.toFloat(), cur.z.toFloat(), cur.player, cur.fromId, nullptr,
                                     {}, std::array<int32_t,3>{cur.x.v, cur.y.v, cur.z.v});
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
            applyHit(*hit.w,hit.x.toFloat(),hit.z.toFloat(),hit.player,hit.fromId,nullptr,
                {},std::array<int32_t,3>{hit.x.v,hit.y.v,hit.z.v});
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
        if (b.w) applyHit(*b.w, Fixed::raw(b.position[0]).toFloat(), Fixed::raw(b.position[2]).toFloat(),
                          b.player, b.fromId, nullptr, {}, b.position);
    }
    deathBlasts_.clear();

    if (g_phase) _sep0 = std::chrono::steady_clock::now();
#ifndef NDEBUG
    if (g_phase && probe::kPmu) _pmuSep = probe::pmuRead();
#endif
    // Separation: push overlapping mobile units apart. The spatial hash limits
    // each unit to its ~3x3 neighbourhood, so this is O(n) not O(n^2). Each pair
    // is handled once (by the lower index), so the result matches the old loop.
    {
        std::optional<PhaseSpan> _grid;
        if (g_phase) _grid.emplace(g_gridMs, probe::kGrid);
        rebuildGrid();   // units moved this tick; rebuild for accurate neighbours
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
    {
        std::optional<PhaseSpan> _explore;
        if (g_phase) _explore.emplace(g_exploreMs, probe::kExplore);
        updateNavigationExploration();
    }
    updateOutcome();
    // The tick total stops here (as it always has); the compaction below is timed on its own.
    double phaseSep = 0, phaseTotal = 0;
    if (g_phase) {
        auto _end = std::chrono::steady_clock::now();
        phaseSep = std::chrono::duration<double,std::milli>(_end-_sep0).count();
        phaseTotal = std::chrono::duration<double,std::milli>(_end-_tk0).count();
#ifndef NDEBUG
        if (probe::kPmu) { probe::pmuAdd(probe::kSep, _pmuSep); probe::pmuAdd(probe::kTotal, _pmuTick); }
#endif
    }
    // Campaign mission runner: feed this tick's build/death events into the "god"
    // script, then advance it (VM + triggers + win/lose). Runs on every peer (build
    // & death events queue VM threads; the spawns/orders happen in step()), so the
    // mission stays in lockstep with no relayed actions.
    if (mission_) {
        TAK_PASS();
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
        updateOutcome(); // Rules can change results and unit counts this tick.
    }
    // Unit COBs ran before the movers above, so GET 33 on the next tick reads
    // this tick's actual wrapped heading delta, not the mover's unclamped request.
    auto turnBam = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            Unit& u = units_[i];
            u.animationTurnBam=std::bit_cast<int16_t>(
                uint16_t(uint16_t(u.heading.v)-u.tickStartHeadingBam));
        }
    };
    if (units_.size() >= kPostMinParallel && simPool().workers())   // own state only (WE E4.3)
        simPool().forChunks(units_.size(), SimPool::kUnitChunk, 0,
                            [&](size_t, size_t begin, size_t end, unsigned) { turnBam(begin, end); });
    else {
        TAK_PASS();
        turnBam(0, units_.size());
    }
    {
        std::optional<PhaseSpan> _compact;
        if (g_phase) _compact.emplace(g_compactMs, probe::kCompact);
        compactRetiredUnits();
    }
    if (g_phase) {
        const double ttot = phaseTotal, tsep = phaseSep;
        static double thr = tak::devEnv("TAK_PHASE_MS") ? atof(tak::devEnv("TAK_PHASE_MS")) : 15.0;
        if (ttot > thr) {   // only report a stall (threshold tunable via TAK_PHASE_MS)
            int alive = 0; for (auto& u : units_) if (u.alive()) ++alive;
            // flow= and path= used to sit here; the flow fields and the inline
            // A* are both gone, so the counters were always zero. prologue= is a part
            // of other=; compact= is outside tick= (it runs after the total is taken).
            std::fprintf(stderr, "SIMPHASE tick=%.2fms combat=%.2f sep=%.2f explore=%.2f vis=%.2f burn=%.2f grid=%.2f scripts=%.2f movement=%.2f nav=%.2f other=%.2f units=%d prologue=%.2f compact=%.2f\n",
                         ttot, g_tcomb, tsep, g_exploreMs, g_visMs, g_burnMs, g_gridMs, g_scriptMs, g_moveMs, g_navMs,
                         ttot - g_tcomb - tsep - g_visMs - g_burnMs - g_gridMs - g_scriptMs - g_moveMs - g_navMs, alive,
                         g_prologueMs, g_compactMs);
#ifndef NDEBUG
            if (probe::kPmu) {
                // Per phase: cycles, instructions, cycles stalled on L1-miss loads, LLC misses
                // (user mode, this thread). "other" is tick minus every named bucket; unlike
                // SIMPHASE's other= it does not subtract grid a second time (grid is inside sep).
                probe::PmuSample other = probe::tlPmu[probe::kTotal];
                for (int ph : {probe::kCombat, probe::kSep, probe::kVis, probe::kBurn, probe::kScripts,
                               probe::kMovement, probe::kNav})
                    for (int i = 0; i < 4; ++i) other.v[i] -= probe::tlPmu[ph].v[i];
                std::string line = "SIMPMU t=" + std::to_string(tickCounter_) + " units=" + std::to_string(alive);
                auto add = [&](const char* name, const probe::PmuSample& v) {
                    char b[160];
                    std::snprintf(b, sizeof b, " %s=%llu/%llu/%llu/%llu", name, (unsigned long long)v.v[0],
                                  (unsigned long long)v.v[1], (unsigned long long)v.v[2], (unsigned long long)v.v[3]);
                    line += b;
                };
                for (int ph = 0; ph < probe::kPhases; ++ph) add(probe::kPhaseNames[ph], probe::tlPmu[ph]);
                add("other", other);
                std::fprintf(stderr, "%s\n", line.c_str());
            }
#endif
        }
    }
#ifndef NDEBUG
    if (probe::kStats) {
        const auto& c = probe::tl;
        int alive = 0; for (auto& u : units_) if (u.alive()) ++alive;
        std::fprintf(stderr, "SIMSTATS tick=%u units=%d vm_ticks=%llu vm_skippable=%llu vm_empty=%llu vm_debt_flushes=%llu"
                     " near_scans=%llu near_cells=%llu near_cells_masked=%llu near_blocks_skipped=%llu"
                     " near_cells_block_skippable=%llu near_cells_outside_disk=%llu near_cells_tested=%llu"
                     " near_cells_tested_outside_disk=%llu acq_scans=%llu los_calls=%llu"
                     " refresh_rects=%llu refresh_grade_evals=%llu refresh_raw_grades=%llu refresh_cells_rated=%llu compact_moved=%llu passes=%llu"
                     " acq_gate_walks=%llu acq_gate_walks_before=%llu body_rects=%llu body_rects_heap=%llu"
                     "%s\n",
                     tickCounter_, alive, (unsigned long long)c.vmTicks, (unsigned long long)c.vmSkippable,
                     (unsigned long long)c.vmEmpty, (unsigned long long)c.vmDebtFlushes, (unsigned long long)c.nearScans,
                     (unsigned long long)c.nearCells, (unsigned long long)c.nearCellsMasked,
                     (unsigned long long)c.nearBlocksSkipped, (unsigned long long)c.nearCellsBlockSkippable,
                     (unsigned long long)c.nearCellsOutsideDisk, (unsigned long long)c.nearCellsTested,
                     (unsigned long long)c.nearCellsTestedOutsideDisk, (unsigned long long)c.acqScans,
                     (unsigned long long)c.losCalls, (unsigned long long)c.refreshRects,
                     (unsigned long long)c.refreshGradeEvals, (unsigned long long)c.refreshRawGrades,
                     (unsigned long long)c.refreshCellsRated,
                     (unsigned long long)c.compactMoved, (unsigned long long)c.passes,
                     (unsigned long long)c.acqGateWalks, (unsigned long long)c.acqGateWalksBefore,
                     (unsigned long long)c.bodyRects, (unsigned long long)c.bodyRectsHeap,
                     probe::allocFields().c_str());
        probe::tl = {};   // work between ticks (commands, the state hash) counts toward the next line
    }
#endif
}

void World::resumeClocks(uint32_t tick, uint32_t gameRng) {
    // Spawn stamped the mover clocks with the counter as it stood; move them to the new one with it.
    const uint32_t was = tickCounter_;
    for (auto& u : units_) {
        if (u.groundGradeTick == was) u.groundGradeTick = tick;
        if (u.groundMoveTick == was) u.groundMoveTick = tick;
    }
    tickCounter_ = tick;
    gameRng_ = gameRng;
}

uint64_t World::posDigest() const {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xFF; h *= 1099511628211ULL; }
    };
    for (const auto& u : units_) {
        mix(uint64_t(uint32_t(u.id)));
        uint64_t t = 1469598103934665603ULL;   // the type NAME, not its pointer or registry index
        if (u.type) for (unsigned char c : u.type->name) { t ^= c; t *= 1099511628211ULL; }
        mix(t);
        mix(uint64_t(uint32_t(u.player)));
        mix(uint64_t(uint32_t(u.x.v)));
        mix(uint64_t(uint32_t(u.z.v)));
        mix(uint64_t(uint32_t(u.hp.v)));
        if (u.orders.empty()) { mix(0); continue; }
        const Order& o = u.orders.front();
        mix(1 + (o.load ? 2u : 0u) + (o.unload ? 4u : 0u) + (o.attackMove ? 8u : 0u) + (o.patrol ? 16u : 0u) +
            (o.guard ? 32u : 0u) + (o.waitAttack ? 64u : 0u) + (o.buildType ? 128u : 0u) + (o.wait > 0 ? 256u : 0u));
        mix(uint64_t(uint32_t(o.targetId)));
        mix(uint64_t(uint32_t(o.x.v)));
        mix(uint64_t(uint32_t(o.z.v)));
    }
    return h;
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
    // Fold fixed-point positions and angles as raw integers below. Mana is a
    // double, so preserve all eight bytes rather than narrowing it to a float.
    auto bits64 = [](double d) { uint64_t b; std::memcpy(&b, &d, 8); return b; };
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
        if (u.scenarioArmor != 1.0f || u.scenarioWeapon != 1.0f) {
            hUnitMisc = fnv(hUnitMisc, std::bit_cast<uint32_t>(u.scenarioArmor));
            hUnitMisc = fnv(hUnitMisc, std::bit_cast<uint32_t>(u.scenarioWeapon));
        }
        hUnitMisc = fnv(hUnitMisc, uint32_t(u.baseSpeed.v));
        if (u.animationTurnBam) hUnitMisc = fnv(hUnitMisc,uint16_t(u.animationTurnBam));
        hUnitMisc=fnv(hUnitMisc,uint32_t(u.scriptAimTarget));
        for (int32_t t : u.slotTarget) hUnitMisc=fnv(hUnitMisc,uint32_t(t));
        hUnitMisc=fnv(hUnitMisc,uint32_t(u.slotAuto)|uint32_t(u.slotManual)<<8);
        hUnitMisc=fnv(hUnitMisc,uint32_t(u.faceRequest));
        for (int32_t rl : u.reloads) hUnitMisc = fnv(hUnitMisc, uint64_t(uint32_t(rl)));
        for(const auto& aim:u.weaponAim) {
            hUnitMisc=fnv(hUnitMisc,aim.heading);hUnitMisc=fnv(hUnitMisc,aim.pitch);hUnitMisc=fnv(hUnitMisc,aim.flags);
        }
        hUnitMisc = fnv(hUnitMisc, uint64_t(uint32_t(u.stance)));
        hUnitMisc = fnv(hUnitMisc, uint64_t(u.moveState) * 3 + uint64_t(u.fireState));
        hUnitMisc = fnv(hUnitMisc, u.standingOrder);
        hUnitMisc = fnv(hUnitMisc, uint64_t((u.cloakOn ? 1u : 0u) | (u.active ? 2u : 0u)));
        hUnitMisc = fnv(hUnitMisc, uint64_t(uint32_t(u.repairId)));
        hUnitMisc = fnv(hUnitMisc, u.workScriptWorking);
        hUnitMisc = fnv(hUnitMisc, uint64_t(uint32_t(int32_t(u.squad))));
    }
    uint64_t hProj = fnv(seed, projectiles_.size());
    for (const auto& p : projectiles_) {
        hProj = fnv(fnv(fnv(hProj, uint32_t(p.fromPlayer)), uint64_t(uint32_t(p.x.v))), uint64_t(uint32_t(p.z.v)));
        if(p.guided3d || p.ballistic3d) {
            hProj=fnv(hProj,p.guided3d ? 0x47554944u : 0x42414c4cu);
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
        if (f.alive) { ++fAlive; fWork ^= (uint64_t(f.fadeLeft) << 1) ^ uint64_t(uint32_t(f.id)); }
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
    // A 64-bit fold of the quantities that must agree between lockstep peers.
    uint64_t h = 1469598103934665603ULL;
    WordMix words(h);   // sim/statefold.h
    // Optional checkpoints include the newer script/navigation state omitted
    // from the older motion-oriented hashTrace diagnostic.
    auto checkpoint = [&](const char* section) {
#ifndef NDEBUG
        static const char* limit = std::getenv("TAK_HASHDETAIL");
        if (limit && tickCounter_ <= std::strtoul(limit,nullptr,10)) {
            std::fprintf(stderr,"HASHDETAIL t=%u section=%s hash=%016llx\n",
                         tickCounter_,section,static_cast<unsigned long long>(words.digest()));
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
    auto mix = [&words](uint64_t v) { words(v); };
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
    if (buildRestrictionsEnabled_) {
        mix(0x5553454f4e4c59ull);
        mix(buildRestrictions_.size());
        for (const auto& type : buildRestrictions_) {
            mix(type.size());
            for (unsigned char c : type) mix(c);
        }
    }
    if (scenarioParticipantCount_ >= 0) { mix(0x4e45555452414cull); mix(scenarioParticipantCount_); }
    if (!scenarioTypeStats_.empty()) {
        mix(0x4352545459504553ull); mix(scenarioTypeStats_.size());
        for (const auto& [name, stats] : scenarioTypeStats_) {
            mix(name.size()); for (unsigned char c : name) mix(c);
            for (int value : stats) mix(uint32_t(value));
        }
    }
    if(isLegionPathfinding(pathfindingMode_)) {mix(0x4c4547494f4e0000ull);if(legion_)mix(legion_->checksum());}
    if(!convoys_.empty()) {mix(0x434f4e564f590000ull);mix(convoys_.checksum());}
    if (doubleSight_) mix(0x44424c5349474854ull);
    if (roomMapRevealed_ || roomLosOff_) { mix(0x524f4f4d564953ull); mix(roomMapRevealed_); mix(roomLosOff_); }
    if (powerCodes_) {   // only in a POWER CODES match, so every other hash is unchanged
        mix(0x504f574552434f44ull);
        mix(uint8_t(damageMode_));mix(powerLosOff_);mix(powerMappingOff_);
    }
    if (patrolRepairs_) mix(0x5054524c52455052ull);
    mix(nextMovementController_);
    mix(navigationExplored_.size());
    mix(uint32_t(restoredNavigationViewer_));
    // The pool when this world runs one (a query never builds one).
    SimPool* const pool=!serialThreads_ && pool_ && pool_->workers() ? pool_.get() : nullptr;
    auto plane16=[&](const std::vector<uint16_t>& cells) {
        return foldPlane(pool,cells.size(),[&](size_t begin,size_t end) {
            return foldCells16(end-begin,[&](size_t i){return cells[begin+i];});
        });
    };
    mix(plane16(navigationExplored_));
    checkpoint("exploration");
    // Current-view counts (derivable from the hashed footprints and the static
    // exploration heights, folded anyway as a cheap consistency check: they
    // decide what each player sees, and so its targets).
    mix(viewCounts_.size());
    mix(plane16(viewCounts_));
    mix(deathSights_.size());
    for (const auto& d:deathSights_) {
        mix(uint16_t(d.sight.x));mix(uint16_t(d.sight.z));mix(uint32_t(d.sight.eyeHeight));
        mix(uint16_t(d.sight.distance));mix(d.sight.sightHeight);mix(d.sight.active);
        mix(uint8_t(d.player));mix(d.expiry);
    }
    checkpoint("vision");
    // Each script folds on its own and the digests in id order, so up to four
    // threads read the scripts' megabytes.
    std::vector<const decltype(unitScripts_)::value_type*> scripts;
    scripts.reserve(unitScripts_.size());
    for (const auto& entry:unitScripts_) scripts.push_back(&entry);
    std::vector<uint64_t> scriptDigests(scripts.size());
    auto foldScripts=[&](size_t begin,size_t end) {
        for (size_t s=begin;s<end;++s) {
            const auto& [id,factory]=*scripts[s];
            WordMix script(0x434f42564dull);
            script(uint32_t(id));
            script(factory.activated); script(factory.ready); script(factory.yardOpen); script(factory.buggerOff);
            if (factory.dying) { script(0x44454144u);script(factory.deathStopValue); }
            if (factory.movementRate) { script(0x4d4f5645u);script(factory.movementRate); }
            if (factory.turnDirection) { script(0x5455524eu);script(uint32_t(int32_t(factory.turnDirection))); }
            // A skipped VM's unpaid debt is folded in: each sleeper hashes the count it
            // would hold had the skipped ticks run (UnitScript::pay).
            const auto& state=factory.peek();
            const uint32_t debt=factory.debt();
            script(state.vm.active); script(uint32_t(state.vm.ticksPerSecond));
            // The VM and piece words are 32-bit: two to a fold, the odd last one
            // zero-padded.
            const auto& statics=state.vm.statics;
            for (size_t n=0;n<statics.size();n+=2)
                script(statics[n]|(n+1<statics.size() ? uint64_t(statics[n+1])<<32 : 0));
            for (const auto& thread:state.vm.threads) {
                const bool owes=debt && (thread.words[0]&0xfff00000u)==0x2400000u;
                auto word=[&](size_t n)->uint64_t {return owes && n==3 ? thread.words[n]-debt : thread.words[n];};
                for (size_t n=0;n<thread.words.size();n+=2)
                    script(word(n)|(n+1<thread.words.size() ? word(n+1)<<32 : 0));
            }
            for (const auto& piece:state.pieces) {
                script(uint32_t(piece.active)|uint32_t(piece.visible)<<1|uint32_t(piece.cached)<<2|
                       uint32_t(piece.shaded)<<3|uint32_t(piece.rendered)<<4);
                std::array<uint32_t,24> values;
                size_t k=0;
                for (const auto* axes:{&piece.moveTarget,&piece.moveSpeed,&piece.turnTarget,&piece.turnSpeed,
                                      &piece.spinTarget,&piece.spinAcceleration,&piece.move,&piece.turn})
                    for (auto value:*axes) values[k++]=uint32_t(value);
                for (k=0;k<values.size();k+=2) script(values[k]|uint64_t(values[k+1])<<32);
            }
            scriptDigests[s]=script.digest();
        }
    };
    if (pool) pool->forChunks(scripts.size(),64,128,[&](size_t,size_t begin,size_t end,unsigned) {foldScripts(begin,end);});
    else foldScripts(0,scripts.size());
    mix(scripts.size());
    for (uint64_t digest:scriptDigests) mix(digest);
    checkpoint("scripts");
    mix(searchGrades_.size()); mix(uint32_t(activeSearchGrade_));
    for (const auto& plane:searchGrades_) {
        mix(plane.footX); mix(plane.footZ); mix(plane.navVersion); mix(plane.checksum);
        mix(plane.preparation.recent); mix(plane.preparation.stale); mix(plane.preparation.requestSlot);
    }
    checkpoint("grades");
    if(!retiredOwners_.empty()) {mix(0x52455449524544ull);mix(retiredHash_);mix(uint32_t(nextId_));}
    TAK_PASS();
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
        if (u.healCarry) { mix(0x4845414cu); mix(u.healCarry); }   // self-repair remainder
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
        // A producer's rally plan is handed to every output, so it is gameplay
        // state. Folded only when set: a unit with none hashes as before.
        if (!u.rally.empty()) {
            mix(0x52414c59u); mix(uint64_t(u.rally.size()));
            for (const Order& r : u.rally) {
                mix(uint32_t(r.x.v)); mix(uint32_t(r.z.v)); mix(uint32_t(r.targetId));
                mix(uint32_t(r.attackMove) | uint32_t(r.patrol) << 1 | uint32_t(r.patrolOpen) << 2);
            }
        }
        mix(u.missionEvents);
        mix(u.standbyAllowed); mix(u.standbyActive);
        if (u.legionLift) { mix(0x4c494654u); mix(u.legionLiftUntil); mix(uint32_t(u.legionLiftX.v)); mix(uint32_t(u.legionLiftZ.v)); mix(u.legionLiftSince); }
        if (u.legionLiftRest) { mix(0x52455354u); mix(u.legionLiftRest); }
        if (u.legionWindow || u.legionStill || u.legionShuffle) {
            mix(0x5354494cu); mix(u.legionWindow); mix(u.legionStill); mix(u.legionShuffle);
            const auto& w=legionWindow_[size_t(u.id)];
            if (u.legionWindow) {mix(uint32_t(w.x)); mix(uint32_t(w.z)); mix(uint32_t(w.turn)); mix(uint32_t(w.path)); mix(uint32_t(w.turned));}
            if (u.legionShuffle) {mix(uint32_t(w.lockX)); mix(uint32_t(w.lockZ)); mix(uint32_t(w.lockTicks));}
        }
        if (size_t(u.id)<legionBodyWindow_.size() && legionBodyWindow_[size_t(u.id)].ticks) {
            const auto& b=legionBodyWindow_[size_t(u.id)];
            mix(0x424f4459u); mix(b.ticks); mix(uint32_t(b.w.x)); mix(uint32_t(b.w.z)); mix(uint32_t(b.w.turn)); mix(uint32_t(b.w.path)); mix(uint32_t(b.w.turned));
        }
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
            if (order.reclaimArea) {
                mix(0x41524541u);
                const auto& a=*order.reclaimArea;
                mix(uint32_t(a.minX));mix(uint32_t(a.minZ));mix(uint32_t(a.maxX));mix(uint32_t(a.maxZ));
                mix(a.stage);mix(uint32_t(a.lastX));mix(uint32_t(a.lastZ));
            }
            if (order.reclaimAreaApproach) mix(0x52434c4d41505052ull);
            if (order.helpSite) { mix(0x48454c50u); mix(uint32_t(order.helpSite)); }
            if (order.guardRepair) { mix(0x47524550u); mix(uint32_t(order.repairTarget)); }
            // A ground HelpBuild's or RepairUnit's own stages (tickGroundHelp,
            // tickGroundRepair), once its leg has started.
            if ((order.helpSite || order.repairTarget) && !(u.type && u.type->canFly) &&
                (order.mission.stage || order.mission.pending || order.repairLooks)) {
                mix(0x574f524b4c4547ull);
                mix(order.mission.stage);mix(order.mission.waitMask);mix(order.mission.deadline);
                mix(order.mission.pending);mix(order.repairLooks);mix(order.controller);
            }
            if (order.reclaimFeat && (order.mission.stage || order.mission.pending)) {
                mix(0x52434c4d50454e44ull);mix(order.mission.pending);
                mix(order.mission.stage);mix(order.mission.waitMask);mix(order.mission.deadline);
                mix(uint32_t(order.reclaimCountdown));mix(order.reclaimHeading);mix(order.controller);
            }
            if (order.patrolOpen) mix(0x5041544f50454eull);
            if (order.patrolRepair) {
                mix(0x5054524c52455052ull);
                mix(uint32_t(order.repairTarget));mix(uint32_t(order.clickX.v));mix(uint32_t(order.clickZ.v));
            }
            if (order.park) {
                mix(0x5041524bu);const auto& p=*order.park;
                mix(p.target);mix(p.padding);mix(p.attempts);mix(uint32_t(p.permanent));mix(p.ring.has_value());
                if (p.ring) {mix(p.ring->x);mix(p.ring->z);mix(p.ring->innerRadius);
                    mix(p.ring->outerTolerance);mix(p.ring->outerSquared);}
                if (p.spread) {
                    mix(0x5350524541444eull);mix(uint32_t(p.door));mix(p.spot.has_value());
                    if (p.spot) {mix(uint32_t(p.spot->first));mix(uint32_t(p.spot->second));}
                }
            }
            mix(order.transportUnloadApproach);
            if(order.load || order.unload || order.transportUnloadApproach) {
            mix(order.transportProductionAhead);
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
            if (order.manaBuildArea) {
                const auto& area=*order.manaBuildArea;
                mix(0x4d414e4141524541ull);
                mix(uint32_t(area.minX.v));mix(uint32_t(area.maxX.v));
                mix(uint32_t(area.minZ.v));mix(uint32_t(area.maxZ.v));
                mix(area.nextSpot);mix(area.exploring);
                mix(area.visited.size());for (uint32_t i:area.visited) mix(i);
                mix(area.type->id.size());
                for (unsigned char c:area.type->id) mix(c);
            }
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
                // VTOL_Follow (tickFlyingGuard) runs on a guard order's mission.
                if (order.guard && (order.mission.stage || order.mission.waitMask)) {
                    mix(0x564f5242u); mix(order.guardOrbitAngle); mix(order.mission.deadline);
                    mix(order.mission.stage); mix(order.mission.waitMask); mix(order.mission.pending);
                }
                // AirToGround (tickAirToGround) runs on an attack order's mission,
                // and so does AirToAir (tickAirToAir).
                else if (order.targetId && (order.mission.stage || order.mission.waitMask || order.airRunCount)) {
                    mix(0x41495252554eull);
                    mix(order.mission.stage); mix(order.mission.waitMask); mix(order.mission.deadline);
                    mix(order.mission.pending); mix(order.mission.flags); mix(uint32_t(order.airRunCount));
                }
                if (order.mixedSlots) mix(0x4d49584544ull);
                if (order.airToAir) {
                    const auto& a=*order.airToAir;
                    mix(0x4149523241495200ull); mix(uint32_t(a.count)); mix(a.evadeController);
                    mix(a.intercept.has_value());
                    if (a.intercept) {
                        const auto& g=*a.intercept;
                        mix(uint32_t(g.point.x)); mix(uint32_t(g.point.y)); mix(uint32_t(g.point.z));
                        mix(uint32_t(g.velocity.x)); mix(uint32_t(g.velocity.y)); mix(uint32_t(g.velocity.z));
                        mix(g.heading); mix(g.turns);
                    }
                    mix(a.evade.has_value());
                    if (a.evade) {
                        const auto& e=*a.evade;
                        mix(e.mission.stage); mix(e.mission.waitMask); mix(e.mission.deadline);
                        mix(e.mission.pending); mix(e.mission.flags);
                        mix(uint32_t(e.maneuver)); mix(uint32_t(e.budget));
                    }
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
            if (order.convoyTick != ConvoyTable::kNone) {mix(0x434f4e5654494bull);mix(order.convoyTick);}
            if (order.orderSerial) {mix(0x4f52444552534e00ull);mix(order.orderSerial);}
            if (order.placementFailures) {mix(0x504c4143454dull);mix(order.placementFailures);mix(order.placementRetryTick);}
            if (order.attackChased || order.attackAimTick != Order::kAttackUnaimed) {
                mix(0x415454414b41494dull);mix(order.attackChased);mix(order.attackAimTick);
            }
            if (order.chaseLeashed) {
                mix(0x434841534555ull);mix(order.chaseLeash);
                mix(uint16_t(order.chaseOrigin.x));mix(uint16_t(order.chaseOrigin.z));
            }
            if (order.stationFree || order.stationRefTick) {
                mix(0x53544e52454cull);mix(order.stationFree);
                mix(uint32_t(order.stationRefX));mix(uint32_t(order.stationRefZ));mix(order.stationRefTick);
            }
            if (!order.groundMission && !(u.type && u.type->canFly && (order.patrol || order.flightMoveMission))) continue;
            mix(order.controller);
            if(isLegionPathfinding(pathfindingMode_))mix(order.issuedTick);
            mix(order.missionRadius);
            if(order.productionExit) {
                mix(0x50524f4445584954ull);
                mix(uint32_t(order.productionExit->first.v));mix(uint32_t(order.productionExit->second.v));
            }
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
            if (order.formationLevel) mix(0x464f524d00000000ull|uint8_t(order.formationLevel));
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
        mix(uint16_t(u.viewSight.x));mix(uint16_t(u.viewSight.z));
        mix(uint32_t(u.viewSight.eyeHeight));mix(uint16_t(u.viewSight.distance));
        mix(u.viewSight.sightHeight);mix(u.viewSight.active);mix(uint8_t(u.viewPlayer));
        mix(u.groundGradeTick);
        mix(uint64_t(u.veteran));
        if (u.scenarioArmor != 1.0f || u.scenarioWeapon != 1.0f) {
            mix(0x4352545354415453ull);
            mix(std::bit_cast<uint32_t>(u.scenarioArmor));
            mix(std::bit_cast<uint32_t>(u.scenarioWeapon));
        }
        // All three reload timers, not just the primary: they now decide WHICH
        // weapon the auto-selector fires, so a drift in any of them would change
        // behaviour.
        for (int32_t rl : u.reloads) mix(uint64_t(uint32_t(rl)));   // ticks
        for(const auto& aim:u.weaponAim) {mix(aim.heading);mix(aim.pitch);mix(aim.flags);}
        mix(uint32_t(u.scriptAimTarget));
        for (int32_t t : u.slotTarget) mix(uint32_t(t));
        mix(uint32_t(u.slotAuto)|uint32_t(u.slotManual)<<8);
        mix(uint32_t(u.faceRequest));
        if (u.blockedTarget) {
            mix(0x424c4b44u);mix(uint32_t(u.blockedTarget));
            mix(uint32_t(u.blockedShots)|uint32_t(u.repositions)<<8|uint32_t(u.searchFailed)<<16);
            mix(uint32_t(uint16_t(u.blockedX))|uint32_t(uint16_t(u.blockedZ))<<16);
        }
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
        if (u.cloakBlockedUntil) { mix(0x434c4f4bu); mix(u.cloakBlockedUntil); }
        if (u.guardHelpTick) { mix(0x47554152u); mix(u.guardHelpTick); }
        if (u.guardHeard) { mix(0x47484541u); mix(u.guardHeard); }
        if (u.reviveTarget) {
            mix(0x52455649u);mix(uint32_t(u.reviveTarget));mix(uint32_t(u.reviveWork));mix(uint8_t(u.reviveMode));
        }
        if (u.conjureHoverTarget) {
            mix(0x484f5645u);mix(uint32_t(u.conjureHoverTarget));
            if (u.conjureHoverGoal) {
                const auto& g=*u.conjureHoverGoal;
                mix(uint32_t(g.point.x));mix(uint32_t(g.point.y));mix(uint32_t(g.point.z));
                mix(g.flags);mix(g.heading);mix(uint16_t(g.radius));
            }
        }
        mix(uint32_t(u.productionSiteId)); mix(uint32_t(u.buildProgress));
        if (u.buildFraction) {mix(0x46524143u);mix(u.buildFraction);}
        if (u.productionRetryTick) {mix(0x52455452u);mix(u.productionRetryTick);}
        mix(u.underConstruction); mix(u.buildBegun); mix(uint32_t(u.conjureRate.v));
        if (u.lodestoneReplacement) {
            const auto& old=*u.lodestoneReplacement;
            mix(0x4c4f4445u);for (char c:old.type->id) mix(uint8_t(c));
            mix(uint32_t(old.x.v));mix(uint32_t(old.z.v));mix(old.heading.v);mix(std::bit_cast<uint64_t>(old.completedWork));
        }
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
        mix(u.workScriptWorking);
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
        if(p.guided3d || p.ballistic3d) {
            mix(p.guided3d ? 0x47554944u : 0x42414c4cu);
            mix(p.fromId);mix(p.slot);mix(p.targetId);mix(p.start);mix(p.end);mix(p.substeps);
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
            mix(flame.owner);mix(flame.fromId);mix(flame.targetId);mix(flame.slot);mix(flame.start);mix(flame.end);
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
        mix(uint64_t(uint32_t(e.y.v)));  // the 3D area pass reads it
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
        if (t.manaShareMask != 0xff) { mix(0x53484152454d414eull); mix(t.manaShareMask); }
        { uint64_t b; std::memcpy(&b, &t.mana, 8); mix(b); }   // double: fold all 8 bytes
        if (t.constructionDemand != 0) { mix(0x434f4e5344454d44ull); mix(std::bit_cast<uint64_t>(t.constructionDemand)); }
        if (t.scenarioResourceLimit != 0) { mix(0x4352544c494d4954ull); mixf(t.scenarioResourceLimit); }
        mix(t.cacheClock.enabled); mix(t.cacheClock.lastRefresh);
        mix(uint32_t(t.sweepCursor));
        mix(t.aiThreatUntil);
        mix(t.visibleRefresh);
        mix(t.visibleEnemies.size());for (int id:t.visibleEnemies) mix(uint32_t(id));
        mix(t.visibleAllies.size());for (int id:t.visibleAllies) mix(uint32_t(id));
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
            // Raw ints. The reclaim state is the fade countdown (the work is
            // on the reclaiming builder's order).
            const uint32_t w = f.fadeLeft;
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
        mix(uint32_t(id));mix(uint32_t(footprint.x));mix(uint32_t(footprint.z));mix(uint32_t(footprint.type));mix(uint32_t(footprint.damage));
    }
    TAK_PASS();
    for (const auto& corpse:units_) if (!corpse.alive() &&
        (corpse.deadFor<kRetiredTicks || corpseFootprints_.contains(corpse.id))) {
        mix(uint32_t(corpse.id));mix(uint32_t(corpse.deadFor));mix(uint32_t(corpse.corpseUntil));
        mix(uint32_t(corpse.corpseStatue));mix(corpse.corpseFading);mix(corpse.corpseBlocks);
        mix(uint32_t(corpse.deathAnimationTicks));mix(corpse.deathHasCorpse);
    }
    // Every map cell's feature (millions of cells on a big map), empty terrain
    // included, in the same pass as the few footprint-tail and non-world-owned
    // cells' extras. A chunk sums those as scrambled (index, extras) words, so
    // the order foldCells16 reads its cells in does not matter.
    mix(foldPlane(pool,mapPlacementCells_.size(),[&](size_t begin,size_t end) {
        uint64_t extras=0;
        const uint64_t cells=foldCells16(end-begin,[&](size_t j) {
            const size_t i=begin+j;
            const auto& cell=mapPlacementCells_[i];
            const bool tail=cell.feature==0xfffe;
            if (tail || cell.owner!=kRetailWorldFeatureOwner) {
                uint64_t x=uint64_t(i)<<25|uint64_t(tail)<<24|uint64_t(cell.owner);
                if (tail) x|=uint64_t(cell.backX)<<16|uint64_t(cell.backZ)<<8;
                x^=x>>30;x*=0xbf58476d1ce4e5b9ull;x^=x>>27;x*=0x94d049bb133111ebull;
                extras+=x^(x>>31);
            }
            return cell.feature;
        });
        return cells*0x9e3779b97f4a7c15ull^extras;
    }));
    mix(uint64_t(gameRng_));   // shared RNG stream position must agree
    // Retail's CRT seed is per-process and not a lockstep input. World uses its
    // match-seeded CRT stream for damage spread; once combat consumes it, include
    // that future-affecting state in the peer hash.
    if (damageCrtUsed_) { mix(0x444d47524354ull); mix(windRng_); }
    mix(windEnabled_);
    if (!retailAllocation_) {
        // Pool slots order path admission and every allocation draws CRT.
        mix(0x504f4f4cull);mix(windRng_);
        for (const auto& pool:entitySlots_.unitAt) mix(pool.size());
        for (const auto& u:units_) mix(uint32_t(entitySlots_.slot(u.id)));
    }
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
    if (mission_) {
        // GET40 makes scores and future attribution authoritative in campaigns.
        mix(scoreAutomaticDisabled_);
        for(const auto& p:players_) mix(uint32_t(p.score));
        TAK_PASS();
        for(const auto& u:units_) mix(uint32_t(u.lastHitPlayer));
    }
    h=words.digest();
    if (mission_) mission_->foldHash(h);
    if (scenario_) scenario_->foldHash(h); // scenario flags/timers/outcome are lockstep state
    for (uint8_t d : forcedDefeat_) { h ^= (d + 1u); h *= 1099511628211ULL; }
    return h;
}

// Mission runner ownership -- ctor/dtor defined here where MissionScript is a complete
// type, so no other TU instantiates the unique_ptr<MissionScript> deleter.
World::World() {
    paths_.setEntitySlots(&entitySlots_);
    paths_.setGradeHost({
        [this](int id,bool last) { prepareSearchGrade(id,last); },
        [this](int id,int x,int z,int retry,PathCell start) { return searchGrade(id,x,z,retry,start); },
        [this](int id) { finishSearchGrade(id); }
    });
}
void World::setPathfindingMode(PathfindingMode mode) {
    if(!validPathfindingMode(uint8_t(mode)))
        throw std::invalid_argument("invalid pathfinding mode");
    if(tickCounter_ && mode!=pathfindingMode_)throw std::logic_error("pathfinding mode is fixed for the match");
    if(mode==pathfindingMode_)return;
    paths_.clear();legion_.reset();convoys_.clear();pathfindingMode_=mode;
}
World::~World() = default;
void World::setMission(std::unique_ptr<MissionScript> m) {
    mission_ = std::move(m);
    if (mission_) mission_->start(*this);   // queue the Start script (runs on the first tick)
}
int World::missionOutcome() const { return mission_ ? mission_->outcome() : 0; }

void World::applyScenarioResourceAction(int player, int opcode, int32_t value) {
    if (player < 0 || player >= numPlayers()) return;
    auto& p=players_[size_t(player)];
    // Native 4cb593..4cb68a writes the override separately from capacity.
    // Pool writes round to float; subtraction may remain negative until tick-end.
    switch (opcode) {
        case 16: case 20:
            p.scenarioResourceLimit=opcode==16 ? float(value) : 0.f;
            if (p.retailResources) p.retailResources->capacityOverride=p.scenarioResourceLimit;
            return;
        case 17: p.mana=float(value); break;
        case 18:
            p.mana=float(double(float(p.mana))+double(value));
            if (p.retailResources) p.retailResources->totalProduced+=double(value);
            break;
        case 19: p.mana=float(double(float(p.mana))-double(value)); break;
        default:return;
    }
    if (p.retailResources) p.retailResources->stored=float(p.mana);
}

void World::clearScenarioState() {
    for (auto& p:players_) {
        if (p.scenarioResourceLimit != 0 && p.retailResources) p.retailResources->capacityOverride=0;
        p.scenarioResourceLimit=0;
    }
    scenario_.reset();
    forcedDefeat_.clear();
    hadMonarch_.clear();
    winningTeam_ = -1;
}
void World::setScenario(std::unique_ptr<ScenarioScript> s) {
    scenario_ = std::move(s);
    if (scenario_) scenario_->start(*this);
}
void World::forceDefeat(int player) {
    if (player < 0 || player >= numPlayers()) return;
    if (int(forcedDefeat_.size()) <= player) forcedDefeat_.resize(size_t(player) + 1, 0);
    forcedDefeat_[size_t(player)] = 1;
    if (scenario_ && player < numPlayers()) {
        auto& p = players_[size_t(player)];
        if (!p.defeated) p.defeatedAt = int32_t(tickCounter_);
        p.defeated = true;
        removeScenarioUnits(player);
    }
}

void World::removeScenarioUnits(int player) {
    // Retail CRT defeat (4f6be0 -> 5131b0) sends death type 10, then retires
    // every remaining live owner. Zero severity skips Killed/Dying, corpse and
    // death weapons; this is immediate removal, not timed self destruction.
    for (auto& u : units_) {
        if (!u.alive() || !u.type || u.player != player) continue;
        stop(u.id);
        u.hp = Fixed();
        u.lastHitBy = 0; u.lastHitPlayer = -1;
        u.deathType = 10; u.severity = 0; u.corpseStatue = -1;
        beginUnitDeath(u);
        if (u.type->isStructure())
            blockFoot(*u.type,u.x.toFloat(),u.z.toFloat(),false);
        retireCorpse(u);
        u.speed = Fixed(); u.selfDestructT = -1;
        updateBodyIndex(u);
        for (auto& plane : searchGrades_)
            refreshSearchRect(plane,footprintOrigin(u.x,u.type->footX),
                footprintOrigin(u.z,u.type->footZ),u.type->footX,u.type->footZ);
        ++players_[size_t(player)].losses;
        // Feed rule counters directly: initial evaluation and end-of-tick rules
        // can both reach here outside the ordinary justDied_ collection window.
        if (scenario_) scenario_->unitDied(*this,u.id);
    }
    players_[size_t(player)].unitCount = 0;
}

bool World::hasScenarioOutcomes() const { return bool(scenario_); }
int World::scenarioOutcome(int player) const {
    if (!scenario_) return 0;
    if (player >= 0) {
        if (!scenario_->participates(player)) return 0;
        if (int result = scenario_->outcome(player)) return result;
        return player < int(forcedDefeat_.size()) && forcedDefeat_[size_t(player)] ? -1 : 0;
    }
    bool any = false, won = false;
    for (int p = 0; p < numPlayers(); ++p) if (scenario_->participates(p)) {
        const int result = scenarioOutcome(p);
        if (!result) return 0;
        any = true; won |= result > 0;
    }
    return any ? (won ? 1 : -1) : 0;
}

int World::updateOutcome() {
    // A player is defeated when it has no living units. Compute per-player
    // alive counts, then per-team. This runs on every sim (referee included),
    // so all peers conclude win/defeat on the same tick.
    std::vector<int> aliveByPlayer(players_.size(), 0);
    // Living Monarch (commander unit) per player, for the monarch-loss rule.
    std::vector<int> monarchByPlayer(players_.size(), 0);
    TAK_PASS();
    for (const auto& u : units_)
        if (u.alive() && u.type &&
            u.player >= 0 && u.player < numPlayers()) {
            ++aliveByPlayer[size_t(u.player)];
            if (u.type->commander) ++monarchByPlayer[size_t(u.player)];
        }
    if (hadMonarch_.size() != players_.size()) hadMonarch_.assign(players_.size(), 0);
    for (int p = 0; p < numPlayers(); ++p) {
        if (monarchByPlayer[size_t(p)] > 0) hadMonarch_[size_t(p)] = 1;
        // No living units OR -- when the Monarch is NOT expendable -- a player who
        // once fielded a Monarch has now lost it. Both are deterministic and computed
        // identically on every peer + the referee, so win/defeat agree in lockstep.
        bool monarchDead = !monarchExpendable_ && hadMonarch_[size_t(p)] &&
                           monarchByPlayer[size_t(p)] == 0;
        bool forced = p < int(forcedDefeat_.size()) && forcedDefeat_[size_t(p)];
        bool wasDefeated = players_[size_t(p)].defeated;
        // Retail CRT scenario virtual+0x20 bypasses stock outcome checks.
        // Empty armies can be awaiting reinforcements or custom objectives.
        players_[size_t(p)].defeated = scenario_
            ? (!scenario_->participates(p) || scenarioOutcome(p) < 0 || forced)
            : ((aliveByPlayer[size_t(p)] == 0) || monarchDead || forced);
        // Stamp the moment of elimination once, for the end-of-game "Time" column.
        if (!wasDefeated && players_[size_t(p)].defeated) players_[size_t(p)].defeatedAt = int32_t(tickCounter_);
        players_[size_t(p)].unitCount = aliveByPlayer[size_t(p)];   // re-sync the cap count
    }

    if (scenario_) {
        winningTeam_ = -1;
        if (scenarioOutcome(-1) > 0) {
            for (int p = 0; p < numPlayers(); ++p) if (scenarioOutcome(p) > 0) {
                const int team = players_[size_t(p)].team;
                if (winningTeam_ >= 0 && winningTeam_ != team) { winningTeam_ = -1; break; }
                winningTeam_ = team;
            }
        }
        return winningTeam_;
    }

    // Count DISTINCT teams that still have a living unit (robust to any team id,
    // not just 0..n-1): a surviving player counts its team once -- the first time
    // that team appears among survivors. If exactly one team remains it wins.
    // A player counts as surviving only if NOT defeated -- so a monarch-loss
    // elimination (units still alive but the Monarch is dead) removes them from the
    // running just like being wiped out.
    int survivingTeam = -1, survivingCount = 0;
    for (int p = 0; p < numPlayers(); ++p) {
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
