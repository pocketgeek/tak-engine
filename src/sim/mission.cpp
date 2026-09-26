#include <cstdlib>
#include "sim/mission.h"

#include "sim/sim.h"
#include "sim/footprint.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tak::sim {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
bool ieq(const std::string& a, const char* b) {
    size_t n = 0;
    for (; a.size() > n && b[n]; ++n)
        if (std::tolower((unsigned char)a[n]) != std::tolower((unsigned char)b[n])) return false;
    return n == a.size() && b[n] == '\0';
}
// Deterministic per-type numeric id for GetUtype / GET_UNIT_VALUE(7) comparisons
// (FNV-1a of the lowercased type id, so both peers agree without an assignment order).
int32_t typeHash(const UnitType* t) {
    if (!t) return 0;
    uint32_t h = 2166136261u;
    for (unsigned char c : t->id) { h ^= std::tolower(c); h *= 16777619u; }
    return int32_t(h & 0x7fffffff) | 1;   // never 0
}
// Split "Verb rest of the string" into the leading verb and the remainder.
std::pair<std::string, std::string> splitVerb(const std::string& cmd) {
    size_t sp = cmd.find(' ');
    if (sp == std::string::npos) return {cmd, {}};
    return {cmd.substr(0, sp), cmd.substr(sp + 1)};
}

}  // namespace

MissionScript::MissionScript(std::vector<uint8_t> cobBytes, const tak::tdf::Node& header,
                             const TypeRegistry& reg, int humanPlayer, std::string origin,
                             std::vector<int> playerMap)
    : reg_(reg), human_(humanPlayer), origin_(std::move(origin)), playerMap_(std::move(playerMap)) {
    if (cobBytes.empty()) {
        // Data-only mission (11 of the 74 shipped ones ship no .cob): no god script.
        // The .ota conditions + the placed units' InitialMission queues drive it.
        parseConditions(header);
        return;
    }
    try {
        cob_ = cob::load(cobBytes, origin_);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mission: cob load failed (%s): %s\n", origin_.c_str(), e.what());
        parseConditions(header);   // still honour the .ota win/lose rules
        return;
    }
    vm_ = std::make_unique<cob::Vm>(cob_, /*deterministicRand=*/true);   // hashed: mission RAND must be lockstep-identical
    vm_->onMapCommand = [this](int nameIdx, const std::vector<int32_t>& a) {
        return mapCommand(*world_, nameIdx, a);
    };
    vm_->onGet = [this](int32_t valId, const std::vector<int32_t>& a) {
        return getValue(*world_, valId, a);
    };
    vm_->onSetUnitValue = [this](int32_t valId, int32_t v) { setUnitValue(valId, v); };
    // Scripted story VO. Eight of the shipped mission cobs play a line at a beat
    // (an alchemist's death, a dragon's arrival, Monsara's cry) and we dropped all
    // of them on the floor -- the mission VM had no sound hook at all. The sim
    // cannot play audio, so record the request and let the viewer act on it.
    vm_->onPlaySound = [this](int32_t nameIdx,int32_t flags) {
        if (!world_) return;
        const std::string& n = cob_.name(size_t(nameIdx));
        if (!n.empty()) world_->requestSound(n,flags);
    };
    parseConditions(header);
}

const UnitType* MissionScript::findType(const std::string& name) const {
    return reg_.find(lower(name));
}

// ---- lifecycle -------------------------------------------------------------

void MissionScript::start(World& w) {
    if (started_) return;
    started_ = true;
    world_ = &w;
    // The placed units' own order queues run first, so the NPC choreography is in
    // place before the god script's Start fires. A data-only mission (no cob) has
    // no VM -- these queues and the .ota conditions ARE the whole mission.
    // Two passes: register every name first, because a bodyguard's `g NAME` clause
    // routinely refers to a unit whose own `i NAME` clause comes later in the file.
    for (const auto& [id, orders] : initialOrders_) {
        size_t p = 0;
        while ((p = orders.find('i', p)) != std::string::npos) {
            bool start = p == 0 || orders[p - 1] == ',' || orders[p - 1] == ' ';
            if (start && p + 1 < orders.size() && orders[p + 1] == ' ') {
                size_t a = orders.find_first_not_of(' ', p + 1);
                size_t b = orders.find_first_of(", ", a);
                if (a != std::string::npos) {
                    std::string nm = orders.substr(a, b == std::string::npos ? b : b - a);
                    if (!nm.empty()) idents_.emplace(lower(nm), id);
                }
            }
            ++p;
        }
    }
    for (const auto& [id, orders] : initialOrders_) applyOrders(w, id, orders);
    initialOrders_.clear();
    if (vm_) vm_->start("Start");
}

void MissionScript::step(World& w, float dt) {
    world_ = &w;
    if (vm_) vm_->tick(dt);
    ++clock_;
    // Timed reinforcements (SetMission "b TYPE H X Y") that have come due.
    for (size_t k = 0; k < pendingSpawns_.size();) {
        if (clock_ >= pendingSpawns_[k].at) {
            const PendingSpawn& s = pendingSpawns_[k];
            if (s.type) w.spawn(s.type, s.x, s.z, 0.0f, s.player);
            pendingSpawns_.erase(pendingSpawns_.begin() + std::ptrdiff_t(k));
        } else {
            ++k;
        }
    }
    sweepTriggers(w);
    if (outcome_ == 0) evalConditions(w, dt);
}

void MissionScript::unitBuilt(World& w, int id) {
    if (!vm_ || cob_.scriptIndex("UnitCreated") < 0) return;
    world_ = &w; getUnitContext_ = id;
    vm_->start("UnitCreated", {id, 0});
}

int MissionScript::conditionOwner(int player) const {
    const int enemy=playerMap_.size()>2 ? playerMap_[2] : 1;
    return player==human_ ? 0 : player==enemy ? 1 : 2;
}

void MissionScript::unitCaptured(World& w,int id) {
    const Unit* u=w.unit(id);
    if (!u || conditionOwner(u->player)!=1) return;
    for (auto& c:conds_)
        if(c.kind==Cond::CaptureUnitType && u->type==c.type) {
            c.met=true;emitConditionSound(w,c);
        }
}

void MissionScript::unitDied(World& w, int id) {
    if (const Unit* victim=w.unit(id)) {
        const int owner=conditionOwner(victim->player);
        for(auto& c:conds_) {
            bool survivor=false;
            if(c.kind==Cond::KillAllMobileUnits || c.kind==Cond::KillAllOfType || c.kind==Cond::AllUnitsKilledOfType) {
                for(const auto& u:w.units()) {
                    if(u.id==id || !u.alive() || u.hp<=Fixed() || !u.type)continue;
                    const int side=conditionOwner(u.player);
                    if(c.kind==Cond::AllUnitsKilledOfType ? side>1 : side!=1)continue;
                    if(c.kind==Cond::KillAllMobileUnits ? u.type->isStructure() : u.type!=c.type)continue;
                    survivor=true;break;
                }
            }
            retailCampaignDeath(c.kind,c,owner,victim->type==c.type,
                victim->player>=0 && size_t(victim->player)<playerCommanders_.size() &&
                    victim->type && victim->type==playerCommanders_[size_t(victim->player)],
                victim->type && !victim->type->isStructure(),survivor);
            emitConditionSound(w,c);
        }
    }
    if (!vm_ || cob_.scriptIndex("UnitDestroyed") < 0) return;
    world_ = &w; getUnitContext_ = id;
    vm_->start("UnitDestroyed", {id});
}

// ---- MAP_COMMAND dispatch (on the resolved verb string) --------------------

int32_t MissionScript::mapCommand(World& w, int nameIdx, const std::vector<int32_t>& a) {
    auto [verb, rest] = splitVerb(cob_.name(uint32_t(nameIdx)));
    if (ieq(verb, "create"))        return doCreate(w, rest, a);
    if (ieq(verb, "setmission"))  { if (!a.empty()) applyOrders(w, a[0], rest); return 0; }
    if (ieq(verb, "settrigger")) {
        if (!a.empty() && a[0] >= 0 && a[0] < 16) {
            Region& r = regions_[size_t(a[0])];
            r.armed = true;
            if (a.size() >= 5) { r.rect = true;  r.x = cellToWorld(float(a[1])); r.z = cellToWorld(float(a[2]));
                                                 r.x2 = cellToWorld(float(a[3])); r.z2 = cellToWorld(float(a[4])); }
            else if (a.size() >= 4) { r.rect = false; r.x = cellToWorld(float(a[1])); r.z = cellToWorld(float(a[2]));
                                                       r.r = float(a[3]) * 16.0f; }
            inside_[size_t(a[0])].clear();
        }
        return 0;
    }
    if (ieq(verb, "removetrigger")) { if (!a.empty() && a[0] >= 0 && a[0] < 16) regions_[size_t(a[0])].armed = false; return 0; }
    if (ieq(verb, "getutype"))      return typeHash(findType(rest));
    if (ieq(verb, "setattribute")) { if (a.size() >= 2) doSetAttribute(w, rest, a[0], a[1]); return 0; }
    if (ieq(verb, "writevalue"))   { if (!a.empty()) vars_[lower(rest)] = a[0]; return 0; }
    if (ieq(verb, "readvalue"))    { auto it = vars_.find(lower(rest)); return it != vars_.end() ? it->second : 0; }
    if (ieq(verb, "capture")) {
        if (!a.empty()) if (Unit* u=w.unit(a[0])) {
            const int next=a.size()>=2 ? a[1] : human_;
            if(u->player!=next) { unitCaptured(w,u->id);u->player=next; }
        }
        return 0;
    }
    if (ieq(verb, "kill"))         { if (!a.empty()) if (Unit* u = w.unit(a[0])) u->hp = Fixed(); return 0; }
    if (ieq(verb, "screenshake")) {
        // Dramatic beats -- quakes, collapses -- asked for a shake and got nothing.
        // Purely cosmetic, but it is the script's only way to punctuate a scripted
        // moment. Args (when given) are magnitude and duration.
        float mag = !a.empty() ? float(a[0]) : 3.0f;
        float dur = a.size() > 1 ? float(a[1]) / 30.0f : 1.5f;
        w.requestShake(mag > 0 ? mag : 3.0f, dur > 0 ? dur : 1.5f);
        return 0;
    }
    return 0;
}

int32_t MissionScript::doCreate(World& w, const std::string& type, const std::vector<int32_t>& a) {
    const UnitType* t = findType(type);
    if (!t) { std::fprintf(stderr, "mission %s: create unknown type '%s'\n", origin_.c_str(), type.c_str()); return 0; }
    int player, cx, cy;
    if (a.size() >= 3)      { player = a[0]; cx = a[1]; cy = a[2]; }   // (player, x, y), player 0-based
    else if (a.size() >= 2) { player = 0;    cx = a[0]; cy = a[1]; }   // default -> the human
    else return 0;
    // The script's 0-based player is a .ota id (0 == Player1); map it to the World slot
    // the placements used, so scripted units land on the right side.
    int otaN = player + 1;
    if (otaN >= 1 && otaN < int(playerMap_.size()) && playerMap_[size_t(otaN)] >= 0)
        player = playerMap_[size_t(otaN)];
    player = std::clamp(player, 0, w.numPlayers() - 1);
    int id = w.spawn(t, cellToWorld(float(cx)), cellToWorld(float(cy)), 0.0f, player);
    if (id >= 0) getUnitContext_ = id;
    return id >= 0 ? id : 0;
}

// SetMission order-queue mini-language (see docs/campaign-design.md). Implements
// m/ma/a/p/s/d/w/o/v (move, move-attack, attack, patrol-loop, give, destroy, timed
// wait, stance, scalar); the timed reinforcement drop (b) and wait-for-attack (wa)
// event hold are still TODO.
void MissionScript::applyOrders(World& w, int unitId, const std::string& orders) {
    Unit* u = w.unit(unitId);
    if (!u) return;
    bool queue = false;
    size_t i = 0;
    auto skipsp = [&] { while (i < orders.size() && (orders[i] == ' ' || orders[i] == ',')) ++i; };
    auto num = [&]() -> float {
        skipsp(); size_t j = i;
        while (i < orders.size() && orders[i] != ' ' && orders[i] != ',') ++i;
        return orders.size() > j ? std::strtof(orders.substr(j, i - j).c_str(), nullptr) : 0.0f;
    };
    auto word = [&]() -> std::string {
        skipsp(); size_t j = i;
        while (i < orders.size() && orders[i] != ' ' && orders[i] != ',') ++i;
        return orders.substr(j, i - j);
    };
    while (i < orders.size()) {
        skipsp();
        if (i >= orders.size()) break;
        char c = char(std::tolower((unsigned char)orders[i]));
        char c2 = i + 1 < orders.size() ? char(std::tolower((unsigned char)orders[i + 1])) : 0;
        ++i;
        if (c == 'm') {                                   // m X Y  (move) / ma X Y (move-attack)
            bool atk = (c2 == 'a'); if (atk) ++i;
            float x = num(), y = num();
            if (atk) w.attackMove(unitId, cellToWorld(x), cellToWorld(y), queue);
            else     w.order(unitId, cellToWorld(x), cellToWorld(y), queue);
            queue = true;
        } else if (c == 'a') {                            // a TYPE (attack type) / a X Y (attack ground)
            skipsp();
            if (i < orders.size() && (std::isalpha((unsigned char)orders[i]) || orders[i] == '_')) {
                std::string ty = word();
                if (const UnitType* t = findType(ty)) {   // nearest enemy of that type
                    int best = -1; float bd = 1e18f;
                    for (const auto& e : w.units())
                        if (e.alive() && e.type == t && !w.allied(e.player, u->player)) {
                            float dx = e.x.toFloat() - u->x.toFloat(), dz = e.z.toFloat() - u->z.toFloat(), d = dx * dx + dz * dz;
                            if (d < bd) { bd = d; best = e.id; }
                        }
                    if (best >= 0) { w.attack(unitId, best, queue); queue = true; }
                }
            } else {
                float x = num(), y = num();
                w.attackMove(unitId, cellToWorld(x), cellToWorld(y), queue);
                queue = true;
            }
        } else if (c == 'p') {                            // p X Y (patrol waypoint: loops)
            float x = num(), y = num();
            w.patrolTo(unitId, cellToWorld(x), cellToWorld(y), queue); queue = true;
        } else if (c == 's') {                            // hand the unit to the human player
            if(u->player!=human_) { unitCaptured(w,unitId);u->player=human_; }
        } else if (c == 'd') {                            // self-destruct / remove
            u->hp = Fixed();
        } else if (c == 'w') {                            // w N (wait N s) / wa (wait-for-attack)
            if (c2 == 'a') {
                ++i;
                w.orderWaitAttack(unitId, queue); queue = true;   // ambush: hold until threatened
            } else {
                float n = num();
                skipsp();
                if (i < orders.size() && std::isdigit((unsigned char)orders[i])) num();  // optional 2nd arg
                w.orderWait(unitId, n, queue); queue = true;
            }
        } else if (c == 'b') {                            // b TYPE H X Y: timed reinforcement drop
            std::string ty = word();
            float hh = num(), bx = num(), by = num();
            if (const UnitType* t = findType(ty))
                pendingSpawns_.push_back({t, u->player, cellToWorld(bx), cellToWorld(by),
                                      clock_ + int32_t(hh * 30.0f)});
        } else if (c == 'o') {                            // o A [B]: combat stance
            float a = num();
            skipsp();
            if (i < orders.size() && std::isdigit((unsigned char)orders[i])) num();
            w.setStance(unitId, int(a));
        } else if (c == 'v') {                            // v F: movement scalar (not modelled)
            num();
        } else if (c == 'i') {                            // i NAME: name this unit
            // The third most common verb (679 uses). It registers the unit under a
            // name that other units' `g` clauses -- and the .ota's own Ident= field
            // -- refer to, which is how a mission wires up bodyguards.
            std::string nm = word();
            if (!nm.empty()) idents_[lower(nm)] = unitId;
        } else if (c == 'g') {                            // g NAME: guard that unit
            std::string nm = word();
            auto it = idents_.find(lower(nm));
            if (it != idents_.end() && it->second != unitId) {
                w.guard(unitId, it->second, queue);
                queue = true;
            }
        } else if (c == 'c') {                            // c: cloak (no operand)
            w.setCloak(unitId, true);
        } else if (c == 'u') {                            // u X Y: unload cargo there
            float x = num(), y = num();
            w.unloadAt(unitId, cellToWorld(x), cellToWorld(y));
        } else {
            word();                                       // skip unrecognised token's operands
        }
    }
}

void MissionScript::doSetAttribute(World& w, const std::string& sub, int unitId, int pct) {
    Unit* u = w.unit(unitId);
    if (!u || !u->type) return;
    float f = float(pct) / 100.0f;
    if (ieq(sub, "healthpercentage"))     u->hp = Fixed::fromFloat(u->type->maxHp * f);
    else if (ieq(sub, "manapercentage"))  u->mana = u->type->maxMana * f;
    else if (ieq(sub, "armorpercentage")) u->armBuff = f;      // live armour multiplier
    else if (ieq(sub, "attackpercentage")) u->atkBuff = f;
}

// ---- GET / SET -------------------------------------------------------------

int32_t MissionScript::getValue(World& w, int32_t valId, const std::vector<int32_t>& a) {
    // The mission GET table. Scripts call GET(valId, a1..a4) 135 times across the
    // shipped missions and we answered only id 7, returning 0 for everything else --
    // which silently mis-resolved real missions: takmission04 read a player's unit
    // count as 0 and declared victory the instant anything died, takmission09 never
    // recognised Heket's death, and takmission22's POW counter never advanced.
    int32_t a1 = a.empty() ? 0 : a[0];
    switch (valId) {
        case 5: {
            // Living units belonging to an .ota player (the script uses .ota player
            // numbers, so map them into our compacted slots the way placements do).
            int player = a1;
            if (a1 >= 1 && a1 < int(playerMap_.size()) && playerMap_[size_t(a1)] >= 0)
                player = playerMap_[size_t(a1)];
            int n = 0;
            for (const auto& u : w.units())
                if (u.alive() && u.type && u.player == player) ++n;
            return n;
        }
        case 7:    // the context unit's type (GET_UNIT_VALUE form)
            if (Unit* u = w.unit(getUnitContext_)) return typeHash(u->type);
            return 0;
        case 30:   // a named unit's TYPE -- compared against the getUtype command,
                   // so it must hash identically
            if (Unit* u = w.unit(a1)) return typeHash(u->type);
            return 0;
        case 35:   // a unit's X cell (paired with 36 and compared to cell constants)
            if (Unit* u = w.unit(a1)) return int32_t(u->x.toFloat()) / 16;
            return 0;
        case 36:   // a unit's Z cell
            if (Unit* u = w.unit(a1)) return int32_t(u->z.toFloat()) / 16;
            return 0;
        case 40:   // native 4d42f3: score of the requested zero-based player slot
            return a1>=0 && a1<w.numPlayers() ? w.player(a1).score : 0;
        default:
            // id 31 (one use, semantics unclear) and anything else: 0.
            return 0;
    }
}

void MissionScript::setUnitValue(int32_t valId, int32_t value) {
    if (valId == 2) outcome_ = value ? 1 : -1;   // scripted force victory / defeat
    if (valId == 40 && world_) world_->setScriptScore(value); // native 4d3ffe/4d4011
}

// ---- triggers --------------------------------------------------------------

void MissionScript::sweepTriggers(World& w) {
    if (cob_.scriptIndex("TriggerHit") < 0) return;
    for (size_t rid = 0; rid < regions_.size(); ++rid) {
        const Region& r = regions_[rid];
        if (!r.armed) continue;
        auto& occ = inside_[rid];
        std::unordered_set<int> now;
        for (const auto& u : w.units()) {
            if (!u.alive() || u.embarked()) continue;
            const float ux = u.x.toFloat(), uz = u.z.toFloat();
            bool in = r.rect ? (ux >= std::min(r.x, r.x2) && ux <= std::max(r.x, r.x2) &&
                                uz >= std::min(r.z, r.z2) && uz <= std::max(r.z, r.z2))
                             : ((ux - r.x) * (ux - r.x) + (uz - r.z) * (uz - r.z) <= r.r * r.r);
            if (!in) continue;
            now.insert(u.id);
            if (!occ.count(u.id)) {   // edge: newly entered -> fire the script once
                getUnitContext_ = u.id;
                vm_->start("TriggerHit", {int32_t(rid), u.id, u.player});
                if (outcome_ != 0) return;
            }
        }
        occ = std::move(now);
    }
}

// ---- data-driven win/lose conditions --------------------------------------

void MissionScript::parseConditions(const tak::tdf::Node& h) {
    auto text=[&](const char* key){return h.valueOr(key,"");};
    auto integer=[](const std::string& value) {
        const long long n=std::strtoll(value.c_str(),nullptr,0);
        return int32_t(std::clamp(n,-2147483648LL,2147483647LL));
    };
    auto add=[&](Cond::Kind kind,bool victory,const char* key,bool typed,int numbers,bool positive=false) {
        const std::string value=text(key);
        if(value.empty())return;
        Cond c;c.kind=kind;c.victory=victory;
        size_t begin=0;
        if(typed) {
            const auto comma=value.find(',');
            std::string name=value.substr(0,comma);
            const auto first=name.find_first_not_of(" \t"),last=name.find_last_not_of(" \t");
            name=first==std::string::npos?"":name.substr(first,last-first+1);
            c.type=findType(name);
            c.wildcard=ieq(name,"ANYTYPE") && (kind==Cond::MoveUnitToRadius || kind==Cond::UnitTypePassesX || kind==Cond::UnitTypePassesZ);
            begin=comma==std::string::npos?value.size():comma+1;
        }
        int32_t* fields[]={&c.a,&c.b,&c.c};
        for(int i=0;i<numbers && i<3;++i) {
            const auto comma=value.find(',',begin);
            *fields[i]=integer(value.substr(begin,comma==std::string::npos?comma:comma-begin));
            begin=comma==std::string::npos?value.size():comma+1;
        }
        if(positive && ((kind==Cond::AnyUnitPassesX || kind==Cond::AnyUnitPassesZ) ? c.a<0 : c.a<=0))return;
        if(!typed && !numbers && integer(value)==0)return;
        c.remaining=c.a;
        conds_.push_back(c);
    };
    // Match native parser order. Victory is the conjunction of every rule;
    // defeat is the disjunction. Boolean zero and nonpositive timers disable.
    add(Cond::KillEnemyCommander,true,"KillEnemyCommander",false,0);
    add(Cond::DestroyAllUnits,true,"DestroyAllUnits",false,0);
    add(Cond::KillAllMobileUnits,true,"KillAllMobileUnits",false,0);
    add(Cond::BuildUnitType,true,"BuildUnitType",true,0);
    add(Cond::CaptureUnitType,true,"CaptureUnitType",true,0);
    add(Cond::KillAllOfType,true,"KillAllOfType",true,0);
    add(Cond::KillUnitType,true,"KillUnitType",true,1);
    add(Cond::MoveUnitToRadius,true,"MoveUnitToRadius",true,3);
    add(Cond::UnitTypePassesX,true,"UnitTypePassesX",true,1);
    add(Cond::UnitTypePassesZ,true,"UnitTypePassesZ",true,1);
    add(Cond::VictoryTimerRunsOut,true,"VictoryTimerRunsOut",false,1,true);
    add(Cond::CommanderKilled,false,"CommanderKilled",false,0);
    add(Cond::AllUnitsKilled,false,"AllUnitsKilled",false,0);
    add(Cond::AllUnitsKilledOfType,false,"AllUnitsKilledOfType",true,0);
    add(Cond::UnitTypeKilled,false,"UnitTypeKilled",true,1);
    add(Cond::DeathTimerRunsOut,false,"DeathTimerRunsOut",false,1,true);
    add(Cond::AnyUnitPassesX,false,"AnyUnitPassesX",false,1,true);
    add(Cond::AnyUnitPassesZ,false,"AnyUnitPassesZ",false,1,true);
    // 522f48 skips the default DestroyAllUnits in campaign mode (1).
    // Empty victory lists stay pending for a scripted win, e.g. takx13_mt.
    // The default defeat AllUnitsKilled at522f78 is unconditional.
    if(std::none_of(conds_.begin(),conds_.end(),[](const Cond& c){return !c.victory;})) {
        Cond c;c.kind=Cond::AllUnitsKilled;conds_.push_back(c);
    }
}

void MissionScript::emitConditionSound(World& w, Cond& condition) {
    // Native non-timer victory classes retain an independent soundplayed +8
    // flag. DestroyAllUnits can become false again, so completion alone cannot
    // replace this once-only cosmetic latch. Defeat/timer rules have no cue.
    if(!condition.victory || condition.kind==Cond::VictoryTimerRunsOut ||
       !condition.met || condition.soundPlayed)return;
    condition.soundPlayed=true;
    w.requestSound("Victory Condition",7);
}

void MissionScript::evalConditions(World& w, float) {
    auto ready=[](const Unit& u){return u.alive() && u.hp>Fixed() && u.type && !u.underConstruction && !u.embarked();};
    bool victory=std::any_of(conds_.begin(),conds_.end(),[](const Cond& c){return c.victory;});
    bool defeat=false;
    for(Cond& c:conds_) {
        switch(c.kind) {
        case Cond::DestroyAllUnits:
            c.met=std::none_of(w.units().begin(),w.units().end(),[&](const Unit& u){return u.alive() && u.hp>Fixed() && conditionOwner(u.player)==1;});
            break;
        case Cond::AllUnitsKilled:
            c.met=std::none_of(w.units().begin(),w.units().end(),[&](const Unit& u){return conditionOwner(u.player)==0 && ready(u);});
            break;
        case Cond::VictoryTimerRunsOut:case Cond::DeathTimerRunsOut:
            c.met=uint32_t(clock_)>=uint32_t(c.a)*30u;break;
        case Cond::BuildUnitType:
            for(const auto& u:w.units())if(u.alive() && u.hp>Fixed() && conditionOwner(u.player)==0 && u.type==c.type && !u.underConstruction)c.met=true;
            break;
        case Cond::MoveUnitToRadius:
            for(const auto& u:w.units()) {
                if(conditionOwner(u.player)!=0 || !ready(u) || (!c.wildcard && u.type!=c.type))continue;
                if(retailCampaignRadius(u.x.v,u.z.v,int32_t(uint32_t(c.a)<<20),int32_t(uint32_t(c.b)<<20),int32_t(uint32_t(c.c)<<20)))c.met=true;
            }
            break;
        case Cond::UnitTypePassesX:case Cond::UnitTypePassesZ:
        case Cond::AnyUnitPassesX:case Cond::AnyUnitPassesZ: {
            const bool enemy=c.kind==Cond::AnyUnitPassesX || c.kind==Cond::AnyUnitPassesZ;
            const bool x=c.kind==Cond::UnitTypePassesX || c.kind==Cond::AnyUnitPassesX;
            for(const auto& u:w.units()) {
                if(!u.alive() || u.hp<=Fixed() || !u.type || conditionOwner(u.player)!=(enemy?1:0))continue;
                if(!enemy && !c.wildcard && u.type!=c.type)continue;
                const int coordinate=x?footprintOrigin(u.x,u.type->footX):footprintOrigin(u.z,u.type->footZ);
                if(retailCampaignAxis(coordinate,c.a))c.met=true;
            }
            break;
        }
        default:break; // Event conditions retain their native completion latch.
        }
        emitConditionSound(w,c);
        if(c.victory)victory=bool(victory && c.met);
        else defeat=bool(defeat || c.met);
    }
    if(victory)outcome_=1;
    else if(defeat)outcome_=-1;
}

// ---- lockstep hash ---------------------------------------------------------

void MissionScript::foldHash(uint64_t& h) const {
    auto mix = [&](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix(uint64_t(int64_t(outcome_)));
    for (const Region& r : regions_) mix(r.armed ? 1u : 0u);
    mix(uint32_t(clock_));
    for(const UnitType* type:playerCommanders_) {
        if(type)for(unsigned char ch:type->id)mix(ch);
        mix(0);
    }
    // Event counters and completion latches must survive lockstep checks.
    for (const Cond& c : conds_) {
        mix(c.met ? 1u : 0u);
        mix(uint32_t(c.remaining));
    }
    mix(uint64_t(pendingSpawns_.size()));   // timed reinforcements still pending
    for (const auto& s : pendingSpawns_) {
        uint32_t at; std::memcpy(&at, &s.at, 4); mix(at);
        mix(uint64_t(uint32_t(s.player)));
    }
    if (vm_) for (size_t i = 0; i < cob_.numStatics; ++i) mix(uint64_t(uint32_t(vm_->getStatic(i))));
    std::vector<std::pair<std::string,int32_t>> variables(vars_.begin(),vars_.end());
    std::sort(variables.begin(),variables.end());
    for (const auto& [k, v] : variables) { for (char ch : k) mix(uint64_t((unsigned char)ch)); mix(uint64_t(uint32_t(v))); }
}

}  // namespace tak::sim
