#include "sim/scenario.h"

#include "sim/sim.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <charconv>

namespace tak::sim {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
int toInt(const std::string& s) { return std::atoi(s.c_str()); }
// cell coord -> world pixel centre (matches the placed-unit spawn convention).
float cellToWorld(float cell) { return cell * 16.0f + 8.0f; }
}  // namespace

ScenarioScript::ScenarioScript(const tak::crt::Scenario& scen, const TypeRegistry& reg,
                               int viewPlayer, int maxPlayer, int mapWCells, int mapHCells,
                               uint32_t participants)
    : reg_(reg), view_(viewPlayer), maxPlayer_(std::max(1, maxPlayer)),
      participants_(participants & ((1u << std::min(8, maxPlayer_)) - 1)),
      outcomes_(size_t(maxPlayer_), 0),
      mapW_(mapWCells), mapH_(mapHCells), regions_(scen.regions) {
    // Never transfer an absent player's rules to another owner. Production match
    // setup rejects unseated authored players before constructing the runner.
    players_.assign(size_t(maxPlayer_), {});
    // CRT bank zero is All Players; banks 1..8 belong to owners 0..7.
    // Native loader 4ccb20 copies global rules first into each seated player's
    // own rule list, so disabling a global rule affects only that player.
    for (size_t p = 0; p < players_.size(); ++p) if (participates(int(p))) {
        if (!scen.players.empty()) players_[p] = scen.players[0];
        if (p + 1 < scen.players.size())
            players_[p].insert(players_[p].end(), scen.players[p+1].begin(), scen.players[p+1].end());
    }
    disabled_.resize(players_.size());
    state_.resize(players_.size());
    for (size_t p = 0; p < players_.size(); ++p) {
        disabled_[p].assign(players_[p].size(), 0);
    }
}

const UnitType* ScenarioScript::findType(const std::string& name) const {
    return reg_.find(lower(name));
}

const tak::crt::Region* ScenarioScript::region(const std::string& name) const {
    std::string want = lower(name);
    if (want.empty() || want == "anywhere") return nullptr;   // whole map
    for (const auto& r : regions_)
        if (lower(r.name) == want) return &r;
    return nullptr;
}

bool ScenarioScript::inRegion(World&, float x, float z, const std::string& loc) const {
    const tak::crt::Region* r = region(loc);
    int cx = int(x / 16.0f), cz = int(z / 16.0f);
    if (!r) return cx >= 0 && cz >= 0 && cx < mapW_ && cz < mapH_;   // whole map
    int lox = std::min(r->x1, r->x2), hix = std::max(r->x1, r->x2);
    int loz = std::min(r->z1, r->z2), hiz = std::max(r->z1, r->z2);
    return cx >= lox && cx <= hix && cz >= loz && cz <= hiz;
}

void ScenarioScript::regionCenter(const std::string& loc, float& x, float& z) const {
    const tak::crt::Region* r = region(loc);
    if (!r) { x = cellToWorld(float(mapW_) * 0.5f); z = cellToWorld(float(mapH_) * 0.5f); return; }
    x = cellToWorld(float(r->x1 + r->x2) * 0.5f);
    z = cellToWorld(float(r->z1 + r->z2) * 0.5f);
}

int ScenarioScript::parsePlayer(const std::string& s) const {
    std::string lo = lower(s);
    if (lo.rfind("all", 0) == 0) return -1;             // All Players
    // Trailing number: "Player 3" -> slot 2. Invalid recipients never alias
    // the final real player (nor broadcast to everybody).
    const auto begin=s.find_last_not_of("0123456789");
    const size_t start=begin==std::string::npos?0:begin+1;
    int value=0;
    const auto result=std::from_chars(s.data()+start,s.data()+s.size(),value);
    return result.ec==std::errc() && value>0 && value<=maxPlayer_ ? value-1 : maxPlayer_;

}

int ScenarioScript::countControl(World& w, int player, const std::string& typeName,
                                 const std::string& loc) const {
    const bool any = lower(typeName) == "any unit";
    const UnitType* t = findType(typeName);
    if (!any && !t) return 0;
    int n = 0;
    for (const auto& u : w.units())
        if (u.alive() && u.player == player && (any || u.type == t) &&
            inRegion(w, u.x.toFloat(), u.z.toFloat(), loc)) ++n;
    return n;
}

void ScenarioScript::applyOutcome(World& w, int player, int result, int recipients) {
    // Retail 4cad50 routes self/allies/opponents separately; 4f6b90/4f6be0
    // latch the first result. A victory never fabricates another player's defeat.
    for (int q = 0; q < maxPlayer_; ++q) {
        const bool selected = recipients == 0 ? q == player :
            recipients == 1 ? w.allied(player, q) : !w.allied(player, q);
        if (participates(q) && selected) {
            if (!outcomes_[size_t(q)]) outcomes_[size_t(q)] = int8_t(result);
            // Native defeat marks gameplay state even after a displayed win.
            if (result < 0) w.forceDefeat(q);
        }
    }
}

// -------- conditions (opcode space per the RE table) --------
bool ScenarioScript::evalCond(World& w, int player, const tak::crt::Rule& c, bool initial) {
    const auto& s = c.slot;
    PState& ps = state_[size_t(player)];
    switch (c.opcode) {
        case 0:  return initial;                         // Start of game
        case 1:  return int64_t(clock_) > int64_t(toInt(s[0])) * 30;            // Gametime > v
        case 2:  return int64_t(clock_) < int64_t(toInt(s[0])) * 30;            // Gametime < v
        case 3: { auto it = ps.timers.find(toInt(s[0]));   // Timer > v (unset => false)
                  return it != ps.timers.end() && it->second.value > float(toInt(s[1])); }
        case 4: { auto it = ps.timers.find(toInt(s[0]));   // Timer < v (unset => false)
                  return it != ps.timers.end() && it->second.value < float(toInt(s[1])); }
        case 5:  return ps.killed[lower(s[1])] > toInt(s[0]);   // killed more than N X
        case 6:  return ps.killed[lower(s[1])] < toInt(s[0]);   // killed less than N X
        case 9:  return ps.lost[lower(s[1])] > toInt(s[0]);     // lost more than N X
        case 10: return ps.lost[lower(s[1])] < toInt(s[0]);     // lost less than N X
        case 7: case 8: case 11: case 12: {                    // killed/lost most/least X
            const UnitType* t = findType(s[0]);
            std::map<std::string, int32_t>& mine = (c.opcode <= 8) ? ps.killed : ps.lost;
            bool most = (c.opcode == 7 || c.opcode == 11);
            int32_t v = mine[lower(s[0])];
            (void)t;
            bool win = true;
            for (size_t q = 0; q < state_.size(); ++q) {
                if (int(q) == player) continue;
                int32_t o = ((c.opcode <= 8) ? state_[q].killed : state_[q].lost)[lower(s[0])];
                if (most ? (o >= v) : (o <= v)) win = false;
            }
            return win && v > 0;
        }
        case 13: case 14: {                                    // control the most/least X at L
            int mine = countControl(w, player, s[0], s[1]);
            bool most = (c.opcode == 13);
            bool win = mine > 0 || !most;
            for (int q = 0; q < maxPlayer_; ++q) {
                if (q == player) continue;
                int o = countControl(w, q, s[0], s[1]);
                if (most ? (o >= mine) : (o < mine)) win = false;
            }
            return win;
        }
        case 15: return countControl(w, player, s[1], s[2]) > toInt(s[0]);   // control > N X at L
        case 16: return countControl(w, player, s[1], s[2]) < toInt(s[0]);   // control < N X at L
        case 17: return ps.flags[s[0]] > toInt(s[1]);          // Flag f > v
        case 18: return ps.flags[s[0]] < toInt(s[1]);          // Flag f < v
        case 19: return true;                                  // Always
        case 20: return false;                                 // Never
        case 21: case 22: {                                    // opponents left </> N
            int opp = 0;
            for (int q = 0; q < w.numPlayers(); ++q)
                if (participates(q) && q != player && !w.allied(player, q) && !w.player(q).defeated) ++opp;
            return c.opcode == 21 ? opp < toInt(s[0]) : opp > toInt(s[0]);
        }
        case 23: {                                             // Random: N percent true
            rng_ = rng_ * 6364136223846793005ULL + 1442695040888963407ULL;
            uint32_t roll = uint32_t((rng_ >> 33) % 100);
            return int(roll) < toInt(s[0]);
        }
        case 24: return w.player(player).mana < float(toInt(s[0]));   // Resources < v
        case 25: return w.player(player).mana > float(toInt(s[0]));   // Resources > v
        default: return false;
    }
}

// -------- actions (independent opcode space) --------
void ScenarioScript::runAction(World& w, int player, int group, const tak::crt::Rule& a) {
    const auto& s = a.slot;
    PState& ps = state_[size_t(player)];
    switch (a.opcode) {
        case 0: ps.timers[toInt(s[0])] = {float(toInt(s[1])), false}; break;   // countdown t=v
        case 1: ps.timers[toInt(s[0])] = {float(toInt(s[1])), true};  break;   // countup t=v
        case 2: ps.flags[s[0]] = toInt(s[1]); break;                           // set flag f=v
        case 3: ps.flags[s[1]] += toInt(s[0]); break;                          // add v to flag f
        case 4: ps.flags[s[1]] -= toInt(s[0]); break;                          // sub v from flag f
        case 5: applyOutcome(w, player, 1, 0); break;                         // Victory for me
        case 6: applyOutcome(w, player, -1, 0); break;                        // Defeat for me
        case 7: {                                                              // Create X at L
            if (const UnitType* t = findType(s[0])) {
                float x, z; regionCenter(s[1], x, z);
                w.spawn(t, x, z, 0.0f, player);
            }
            break;
        }
        case 8:                                                                // Destroy X at L
            if (const UnitType* t = findType(s[0]))
                for (auto& u : w.units())
                    if (u.alive() && u.player == player && u.type == t && inRegion(w, u.x.toFloat(), u.z.toFloat(), s[1]))
                        u.hp = Fixed();
            break;
        case 9:                                                                // I own all X at L
            if (const UnitType* t = findType(s[0]))
                for (auto& u : w.units())
                    if (u.alive() && u.type == t && inRegion(w, u.x.toFloat(), u.z.toFloat(), s[1])) u.player = player;
            break;
        case 10:                                                               // Heal X by v at L
            if (const UnitType* t = findType(s[0]))
                for (auto& u : w.units())
                    if (u.alive() && u.player == player && u.type == t && inRegion(w, u.x.toFloat(), u.z.toFloat(), s[2]))
                        u.hp = fxMin(Fixed::fromFloat(u.type->maxHp),
                                     u.hp + Fixed::fromFloat(u.type->maxHp * float(toInt(s[1])) / 100.0f));
            break;
        case 11:                                                               // Damage X by v at L
            if (const UnitType* t = findType(s[0]))
                for (auto& u : w.units())
                    if (u.alive() && u.player == player && u.type == t && inRegion(w, u.x.toFloat(), u.z.toFloat(), s[2]))
                        u.hp = fxMax(Fixed(),
                                     u.hp - Fixed::fromFloat(u.type->maxHp * float(toInt(s[1])) / 100.0f));
            break;
        case 12: showClock_ = true; break;                                     // Display gameclock
        case 13: pending_.push_back({clock_, parsePlayer(s[0]), s[1]}); break;  // Display P text
        case 14: disabled_[size_t(player)][size_t(group)] = 1; break;          // Disable rule
        case 15:                                                               // Move all X at L1 to L2
            if (const UnitType* t = findType(s[0])) {
                float dx, dz; regionCenter(s[2], dx, dz);
                for (auto& u : w.units())
                    if (u.alive() && u.player == player && u.type == t && inRegion(w, u.x.toFloat(), u.z.toFloat(), s[1]))
                        w.order(u.id, dx, dz, false);
            }
            break;
        case 16: w.player(player).storage = float(toInt(s[0])); break;         // resource limit
        case 17: w.player(player).mana = float(toInt(s[0])); break;            // resources = v
        case 18: w.player(player).mana += float(toInt(s[0])); break;           // add v
        case 19: w.player(player).mana = std::max(0.0, w.player(player).mana - double(toInt(s[0]))); break;
        case 20: break;                                                        // resources normal (no-op)
        case 21: applyOutcome(w, player, 1, 1); break;                        // Victory me + teammates
        case 22: applyOutcome(w, player, -1, 1); break;                       // Defeat me + teammates
        case 23: applyOutcome(w, player, 1, 2); break;                        // Victory for opponents
        case 24: applyOutcome(w, player, -1, 2); break;                       // Defeat for opponents
        case 25: pending_.push_back({clock_, parsePlayer(s[0]),
                                    s[1] + std::to_string(ps.flags[s[2]]) + s[3]}); break; // Display P text flag text
        default: break;
    }
}

void ScenarioScript::start(World& w) {
    if (started_) return;
    started_ = true;
    evaluate(w, true);
}

void ScenarioScript::trace(int player,int group,int action,const crt::Rule* rule) noexcept {
    if(!trace_)return;
    try {trace_(clock_,player,group,action,rule);}
    catch(...) {trace_={};} // A failed diagnostic sink cannot interrupt lockstep.
}

void ScenarioScript::step(World& w, float) {
    if (!started_) start(w);
    ++clock_;
    ++ticks_;
    if (clock_ % 30 == 0) evaluate(w, false);
}

void ScenarioScript::evaluate(World& w, bool initial) {
    // Native 4ccf90: initial evaluation, then integer game-second boundaries.
    // Timers advance by one before rules; countdowns may become negative.
    for (auto& ps : state_)
        for (auto& [id, t] : ps.timers) {
            (void)id;
            t.value += t.countUp ? 1.0f : -1.0f;
        }
    for (int p = 0; p < int(players_.size()); ++p) {
        if (!participates(p) || w.player(p).defeated) continue;
        for (int g = 0; g < int(players_[size_t(p)].size()); ++g) {
            if (disabled_[size_t(p)][size_t(g)]) continue;
            const auto& grp = players_[size_t(p)][size_t(g)];
            bool all = true;
            for (const auto& c : grp.conditions)
                if (!evalCond(w, p, c, initial)) { all = false; break; }
            if (all) {
                trace(p,g,-1,nullptr);
                for(size_t i=0;i<grp.actions.size();++i) {
                    const auto& a=grp.actions[i];trace(p,g,int(i),&a);
                    runAction(w,p,g,a);
                }
            }
        }
    }
}

void ScenarioScript::unitDied(World& w, int id) {
    const Unit* u = w.unit(id);
    if (!u || !u->type) return;
    std::string ty = u->type->id; // CRT operands identify FBI types, not localized display names
    if (u->player >= 0 && u->player < int(state_.size())) state_[size_t(u->player)].lost[ty]++;
    // Kill credit to the last attacker's owner.
    if (const Unit* k = w.unit(u->lastHitBy))
        if (k->player >= 0 && k->player < int(state_.size())) state_[size_t(k->player)].killed[ty]++;
}

std::vector<ScenarioScript::Msg> ScenarioScript::drainMessages() {
    std::vector<Msg> out;
    out.swap(pending_);
    // Filter to the viewing player (or All Players); -1 view sees everything.
    if (view_ >= 0)
        out.erase(std::remove_if(out.begin(), out.end(),
                   [&](const Msg& m) { return m.player >= 0 && m.player != view_; }), out.end());
    return out;
}

void ScenarioScript::foldHash(uint64_t& h) const {
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ULL; };
    mix(uint64_t(ticks_));
    mix(rng_);
    mix(participants_);
    for (auto outcome : outcomes_) mix(uint8_t(outcome));
    for (size_t p = 0; p < state_.size(); ++p) {
        const PState& ps = state_[p];
        for (const auto& [k, v] : ps.flags) {
            for (char c : k) mix(uint64_t(uint8_t(c)));
            mix(uint64_t(int64_t(v)));
        }
        for (const auto& [id, t] : ps.timers) {
            uint32_t bits; std::memcpy(&bits, &t.value, 4);
            mix((uint64_t(uint32_t(id)) << 1) ^ (uint64_t(bits) << 8) ^ uint64_t(t.countUp));
        }
        for (const auto& [k, v] : ps.killed) { mix(0x11); for (char c : k) mix(uint8_t(c)); mix(uint64_t(int64_t(v))); }
        for (const auto& [k, v] : ps.lost)   { mix(0x22); for (char c : k) mix(uint8_t(c)); mix(uint64_t(int64_t(v))); }
        for (uint8_t d : disabled_[p]) mix(d);
    }
}

}  // namespace tak::sim
