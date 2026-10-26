// replay_test -- the .takrep header round-trips, and the loader refuses what it
// cannot faithfully replay.
//
// Reviewed complaint, all of it true of the old format: the loader "accepts
// unsupported format versions, ignores recorded tick indices, and can return success
// after malformed inner bundles or an incomplete header", and the header itself was
// missing inputs the match started from -- random starts, the per-slot AI level, the
// benchmark intensity, the mission -- while the seed and the protocol version were
// recorded and then thrown away.
//
// Every one of those is a SILENT failure: playback simulates a different game and
// says nothing, which is exactly the property that makes a replay useless for
// diagnosing a desync. So the tests below care less about the happy path than about
// each way a bad file used to be accepted.

#include "net/replayhdr.h"

#include <cstdio>
#include <string>

using namespace tak::net;

static int g_fail = 0;
static void check(bool cond, const char* what, const std::string& detail = {}) {
    std::printf("  %-62s %s%s%s\n", what, cond ? "ok" : "FAIL",
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!cond) ++g_fail;
}

// A header with every field set to something distinctive, so a field that is not
// carried across shows up as a mismatch rather than coincidentally matching a zero.
static ReplayHeader sample() {
    ReplayHeader h;
    h.mapId = "Inner Circle";
    h.mapDigest = std::string(64, 'a');
    h.modDigest = std::string(64, 'b');
    h.mission = "camp03";
    h.engineVersion = "9.9.9";
    h.crusades = 1;
    h.forfeitSelfDestruct = 1;
    h.gameMods = 1;
    h.unitCap = 1234;
    h.monarchExpendable = 1;
    h.stressTest = 1;
    h.randomStarts = 2;   // BY SLOT, the newest value (a u8 since format 6)
    h.doubleSight = 1;
    h.benchmark = 4;
    h.seed = 0xabcdef01u;
    h.dataHash = 0x0123456789abcdefull;
    h.mapScenario = 1;
    h.powerCodes = 1;
    h.fogExplored = 2;
    for (int i = 0; i < kMaxSlots; ++i) {
        h.slotType[i] = uint8_t(i % 3);
        h.slotFaction[i] = uint8_t(i % 5);
        h.slotColor[i] = uint8_t(i);
        h.slotTeam[i] = uint8_t(i % 4);
        h.slotAiLevel[i] = uint8_t(i % 5);
    }
    return h;
}

int main() {
    std::printf("replay_test\n");

    std::printf("header round-trip:\n");
    {
        const ReplayHeader in = sample();
        Writer w;
        writeReplayHeader(w, in);
        // Skip the 4-byte magic, as the loader does.
        Reader r(w.b.data() + 4, w.b.size() - 4);
        ReplayHeader out;
        uint32_t fmt = 0, proto = 0;
        check(readReplayHeader(r, out, fmt, proto), "a written header reads back");
        check(fmt == kReplayFormat, "format version survives", std::to_string(fmt));
        check(proto == kNetVersion, "protocol version survives", std::to_string(proto));
        check(out.mapId == in.mapId, "mapId");
        check(out.mapDigest == in.mapDigest, "verified map copy survives recording");
        check(out.modDigest == in.modDigest, "verified game mod copy survives recording");
        check(out.mission == in.mission, "mission (a campaign replay is not a skirmish)");
        check(out.engineVersion == in.engineVersion, "engine version");
        check(out.seed == in.seed, "seed (was recorded, then discarded by the loader)");
        check(out.randomStarts == in.randomStarts, "randomStarts (start locations, BY SLOT included)");
        check(out.doubleSight == in.doubleSight, "double sight survives recording");
        check(out.benchmark == in.benchmark, "benchmark intensity (the staged spawns)");
        check(out.dataHash == in.dataHash, "gameplay data hash");
        check(out.unitCap == in.unitCap, "unitCap");
        check(out.monarchExpendable == in.monarchExpendable, "monarchExpendable");
        check(out.stressTest == in.stressTest, "stressTest");
        check(out.gameMods == in.gameMods, "gameMods");
        check(out.mapScenario == in.mapScenario, "MAP SCENARIO (format 13)");
        check(out.powerCodes == in.powerCodes, "POWER CODES (format 14)");
        check(out.fogExplored == in.fogExplored, "FOG OF WAR (format 15)");
        bool slotsOk = true;
        for (int i = 0; i < kMaxSlots; ++i)
            slotsOk &= out.slotType[i] == in.slotType[i] &&
                       out.slotFaction[i] == in.slotFaction[i] &&
                       out.slotColor[i] == in.slotColor[i] &&
                       out.slotTeam[i] == in.slotTeam[i] &&
                       out.slotAiLevel[i] == in.slotAiLevel[i];
        check(slotsOk, "every slot, INCLUDING its AI level");
        // The AI level is the one that silently changed the simulation: the income
        // multiplier is derived from it at setup and the recorded commands do not
        // carry it, so an Absurd AI replayed on normal income.
        check(out.slotAiLevel[4] == in.slotAiLevel[4], "AI level specifically",
              std::to_string(out.slotAiLevel[4]));
    }

    std::printf("format 14 (before FOG OF WAR):\n");
    {
        // A format-14 file is a format-15 header without its trailing option byte.
        // Its room fog survives only in the POWER CODES byte (bit 1, power codes on).
        for (uint8_t codes : {uint8_t(0), uint8_t(1), uint8_t(3)}) {
            auto in = sample(); in.powerCodes = codes;
            Writer w;
            writeReplayHeader(w, in);
            w.b.pop_back();
            w.b[4] = 14;
            Reader r(w.b.data() + 4, w.b.size() - 4);
            ReplayHeader out; out.fogExplored = 7;
            uint32_t fmt = 0, proto = 0;
            check(readReplayHeader(r, out, fmt, proto) && fmt == 14 && r.p == r.end &&
                  out.fogExplored == (codes == 1 ? 0 : 1),
                  "a format-14 header reads exactly, its fog from the POWER CODES byte");
        }
    }

    std::printf("format 13 (before POWER CODES):\n");
    {
        // A format-13 file is a format-15 header without its two trailing option bytes.
        Writer w;
        writeReplayHeader(w, sample());   // powerCodes = 1
        w.b.pop_back(); w.b.pop_back();
        w.b[4] = 13;
        Reader r(w.b.data() + 4, w.b.size() - 4);
        ReplayHeader out; out.powerCodes = 7;
        uint32_t fmt = 0, proto = 0;
        check(readReplayHeader(r, out, fmt, proto) && fmt == 13 && out.mapScenario == 1 && out.powerCodes == 0 &&
              r.p == r.end, "a format-13 header reads exactly, with POWER CODES off");
    }

    std::printf("format 12 (before MAP SCENARIO):\n");
    {
        // A format-12 file is a format-15 header without its three trailing option bytes.
        Writer w;
        writeReplayHeader(w, sample());   // mapScenario = 1
        w.b.pop_back(); w.b.pop_back(); w.b.pop_back();
        w.b[4] = 12;
        Reader r(w.b.data() + 4, w.b.size() - 4);
        ReplayHeader out; out.mapScenario = 7;
        uint32_t fmt = 0, proto = 0;
        check(readReplayHeader(r, out, fmt, proto) && fmt == 12 && out.mapScenario == 0 && r.p == r.end,
              "a format-12 header reads exactly, with MAP SCENARIO off");
    }

    std::printf("pathfinding mode and incompatible recordings:\n");
    for (auto mode : {tak::sim::PathfindingMode::Retail, tak::sim::PathfindingMode::Legion}) {
        auto in = sample(); in.mission.clear(); in.pathfindingMode = mode;
        Writer w; writeReplayHeader(w, in);
        const size_t modeAt = w.b.size() - size_t(kMaxSlots) * 5 - 5;   // ... mode, count, slots, mapScenario, powerCodes, fogExplored
        ReplayHeader out; uint32_t fmt=0, proto=0;
        Reader r(w.b.data()+4,w.b.size()-4);
        check(readReplayHeader(r,out,fmt,proto) && out.pathfindingMode==mode,
              "explicit skirmish pathfinder survives recording");
        // 1-3 were the removed Flowfield, Cooperative and Retail+ identities.
        for (uint8_t unknown : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(5), uint8_t(255)}) {
            auto bad = w.b; bad[modeAt] = unknown;
            Reader invalid(bad.data()+4,bad.size()-4);
            check(!readReplayHeader(invalid,out,fmt,proto), "unknown pathfinder is refused");
        }
        w.b.erase(w.b.begin()+modeAt); w.b[4]=10; w.b[8]=219;
        Reader old(w.b.data()+4,w.b.size()-4);
        out.pathfindingMode=tak::sim::PathfindingMode::Legion;
        check(readReplayHeader(old,out,fmt,proto) && out.pathfindingMode==tak::sim::PathfindingMode::Retail &&
              !supportedReplayProtocol(fmt,proto), "old header is readable but incompatible playback is refused");
    }
    for(auto mode:{tak::sim::PathfindingMode::Legion}) {
        auto in=sample(); in.pathfindingMode=mode;
        Writer w; writeReplayHeader(w,in);
        Reader r(w.b.data()+4,w.b.size()-4); ReplayHeader out; uint32_t fmt=0,proto=0;
        check(readReplayHeader(r,out,fmt,proto) && out.pathfindingMode==tak::sim::PathfindingMode::Retail,
              "campaign playback always uses Retail");
    }

    std::printf("the loader refuses what it cannot replay:\n");
    check(supportedReplayProtocol(9, kNetVersion), "current simulation protocol remains replayable");
    check(!supportedReplayProtocol(10, 219), "previous Retail simulation protocol is refused");
    check(!supportedReplayProtocol(11, 220), "pre-area Retail and Flowfield replays are refused");
    check(!supportedReplayProtocol(11, 221), "previous crowded-arrival protocol is refused");
    check(!supportedReplayProtocol(11, 223), "previous Flowfield avoidance protocol is refused");
    check(!supportedReplayProtocol(11, 227), "pre-Legion protocol is refused");
    check(supportedReplayProtocol(11, kNetVersion) && kNetVersion == 299, "replay format 11 is kept under protocol 299");
    check(supportedReplayProtocol(14, kNetVersion), "replay format 14 is kept under protocol 299");
    check(supportedReplayProtocol(15, kNetVersion) && kReplayFormat == 15, "replay format 15 is written under protocol 299");
    // 299 stops hits on a unit already at 0 HP in the tick it dies and saturates HP damage (hashed state).
    check(!supportedReplayProtocol(15, 298), "previous protocol where a dying unit kept taking hits is refused");
    check(!supportedReplayProtocol(16, kNetVersion), "a newer replay format is refused");
    // 298 removes the Crusades meta-game messages and loads the retail Crusades roster and menus (hashed state).
    check(!supportedReplayProtocol(15, 297), "previous protocol with the Crusades meta game and canbuildcb menus is refused");
    // 297 fires plain Ballistic shells without the 2D line test (hashed state).
    check(!supportedReplayProtocol(15, 296), "previous protocol with line-tested arrows and trebuchets is refused");
    // 296 counts blocked flames and judges ballistic range by retail's arc solve (hashed state).
    check(!supportedReplayProtocol(15, 295), "previous protocol with flat out-of-arc lobs is refused");
    // 295 aims at the drawn SweetSpot (posed piece bounds; hashed state).
    check(!supportedReplayProtocol(15, 294), "previous protocol with local-vertex SweetSpots is refused");
    // 294 clears areas as retail (overlapped map features removed at load, explored world features clearable
    // under fog, the area's approach and child exits; hashed state) and records the room's fog (format 15).
    check(!supportedReplayProtocol(14, 293), "previous protocol with ghost features and stalled area clears is refused");
    // 293 reads one ai/takengine.txt for all our AI levels (map packages carry it instead of ai/d.txt ... ai/ad.txt,
    // so their digests can differ). The sim is 292's.
    check(!supportedReplayProtocol(14, 292), "previous protocol with per-level AI files in map packages is refused");
    // 292 closes the loop of an armed or builder flyer's Shift-queued patrol (hashed state; 291 hovered at the point).
    check(!supportedReplayProtocol(14, 291), "previous protocol with hovering queued flyer patrols is refused");
    // 291 takes a map package's AI files from the map's own archive (a colliding installed map or a game mod's copy no
    // longer changes the package digest). The sim is 290's.
    check(!supportedReplayProtocol(14, 290), "previous protocol with the mount's AI files in map packages is refused");
    // 290 adds AI level 6, Classic (SlotInfo.aiLevel); a 289 peer would clamp it to Normal. The sim is 289's.
    check(!supportedReplayProtocol(14, 289), "previous protocol without the Classic AI level is refused");
    // 289 seats the campaign human at Player1 / world slot 0 whatever its keywords (takx22_mt/takx24_mt hashed state),
    // scales the target score's turn penalty by the road/water multiplier (412a77; hashed state), and adds the
    // disconnect wait and Event::AiTakeover (Waiting/Reseated, Rejoin fromTick).
    check(!supportedReplayProtocol(14, 288), "previous protocol (old campaign seat, unscaled turn penalty, no AI takeover) is refused");
    // 288 adds power codes (GameOptions powerCodes byte, Cmd::PowerCode 28, replay format 14 POWER CODES byte); the sim is 287's with them off.
    check(!supportedReplayProtocol(13, 287), "previous protocol without power codes is refused");
    check(!supportedReplayProtocol(14, 287), "a format-14 file claiming protocol 287 is refused");
    // 287 replaces overrides with mods (GameOptions/replay byte gameMods, Mod* messages); the sim is 286's.
    check(!supportedReplayProtocol(13, 286), "previous protocol without mods is refused");
    // 286 changes Legion movement at ~100 bodies, round 6 (gatehouse corridors: walk on past the shot, deep rings fill back to front; hashed state).
    check(!supportedReplayProtocol(13, 285), "previous protocol without the Legion round 6 rules is refused");
    // 285 changes Legion movement at ~100 bodies, round 5 (rings press into a wall's shadow, late rings for fight columns, the reach edge in reach; hashed state).
    check(!supportedReplayProtocol(13, 284), "previous protocol without the Legion round 5 rules is refused");
    // 284 adds the lobby seat move (Msg::MoveSeat); the sim is 283's.
    check(!supportedReplayProtocol(13, 283), "previous protocol without the lobby seat move is refused");
    // 283 changes Legion movement at ~100 bodies, round 4 (lane order through turns, rings along walls, fight/guard round still targets, room at a merge; hashed state).
    check(!supportedReplayProtocol(13, 282), "previous protocol without the Legion round 4 rules is refused");
    // 282 changes Legion movement at ~100 bodies, round 3 (rings round still targets, passage greens and small-group queueing, corridor-mouth funnel claims; hashed state).
    check(!supportedReplayProtocol(13, 281), "previous protocol without the Legion round 3 rules is refused");
    // 281 makes attackers reposition when shots keep stopping on indestructible terrain or features (hashed state).
    check(!supportedReplayProtocol(13, 280), "previous protocol without blocked-shot repositioning is refused");
    // 280 changes Legion movement at ~100 bodies, round 2 (sub-cell presses, ring bends, wide-gap lanes, flow-around speed; hashed state).
    check(!supportedReplayProtocol(13, 279), "previous protocol with the round 1 Legion ring and flow-around rules is refused");
    // 279 changes Legion movement at ~100 bodies (passage admission, departures, attack rings; hashed state).
    check(!supportedReplayProtocol(13, 278), "previous protocol with the older Legion passage and ring rules is refused");
    // 278 adds random-map recipe v9 (scattered neutral mana); a 277 peer would refuse a v9 map id.
    check(!supportedReplayProtocol(13, 277), "previous protocol without scattered mana recipes is refused");
    // 277 keeps Legion formation flyers on station while their ground waits for its registration (hashed state).
    check(!supportedReplayProtocol(13, 276), "previous protocol releasing formation flyers on a waiting ground is refused");
    // 276 adds voice chat messages (never in a replay); a 275 peer cannot speak them.
    check(!supportedReplayProtocol(13, 275), "previous protocol without voice chat is refused");
    // 275 adds AI level 5 (Absurdly Defensive), whose double income every peer derives from the level.
    check(!supportedReplayProtocol(13, 274), "previous protocol without Absurdly Defensive is refused");
    // 274 finishes a lodestone upgrade under a short share (scale the tick of work, then clamp).
    check(!supportedReplayProtocol(13, 273), "previous stalling lodestone-upgrade protocol is refused");
    // 273 shares short mana across concurrent construction (retail allocation) instead of first-come.
    check(!supportedReplayProtocol(13, 272), "previous all-or-nothing construction protocol is refused");
    // 272 adds Legion's stage-8 navigation (arrival headway on the own slot, chokepoints, subgroups).
    check(!supportedReplayProtocol(13, 271), "previous static/traffic protocol is refused");
    // 271 separates Legion's static navigation from traffic (closures vs improvements, chain repair, jams as traffic).
    check(!supportedReplayProtocol(13, 270), "previous hierarchical-route protocol is refused");
    // 270 changes Legion's long routes (hierarchy, corridors, tile chains over shared tiles).
    check(!supportedReplayProtocol(13, 269), "previous route-query protocol is refused");
    // 269 changes Legion's route consumers (stated regions on paused builds, metric-versioned progress).
    check(!supportedReplayProtocol(13, 268), "previous scheduling protocol is refused");
    // 268 changes Legion's navigation schedule (budgeted registration, incremental plane builds, fair field service).
    check(!supportedReplayProtocol(13, 267), "previous explicit-order protocol is refused");
    // 267 adds explicit selection orders (Cmd::Order in tick bundles; Legion clicks apply whole in one tick).
    check(!supportedReplayProtocol(13, 266), "previous per-unit-click protocol is refused");
    // 266 sizes entity pools from retail's maxunits and grows them as needed (path admission scan costs).
    check(!supportedReplayProtocol(13, 265), "previous entity-pool protocol is refused");
    // 265 changes path search (production entity pools, ground-word ownership, pending re-requests).
    check(!supportedReplayProtocol(13, 264), "previous path-search protocol is refused");
    // 264 changes Legion movement (the 2026-10-14 review: stall clock, slots by arrival, passage lanes, slot budget).
    check(!supportedReplayProtocol(13, 263), "previous Legion-review protocol is refused");
    // 263 changes air combat (hashed flyer holds, 3D area damage, led facing, attack runs).
    check(!supportedReplayProtocol(13, 262), "previous air-combat protocol is refused");
    // 262 stops a held flyer's velocity (hashed).
    check(!supportedReplayProtocol(13, 261), "previous held-flyer protocol is refused");
    // 261 changed the simulation (retail healtime self-repair), which ends the 257-260
    // allowance the lobby/setup-only 258-260 kept.
    check(!supportedReplayProtocol(13, 260) && !supportedReplayProtocol(13, 259) && !supportedReplayProtocol(12, 258),
          "previous lobby-only protocols are refused");
    check(!supportedReplayProtocol(12, 257) && !supportedReplayProtocol(11, 257), "0.8.2's protocol 257 is refused");
    check(!supportedReplayProtocol(12, 256), "previous air-transport protocol is refused");
    check(!supportedReplayProtocol(11, 250), "previous blocked-build-site protocol is refused");
    check(!supportedReplayProtocol(11, 247), "previous Legion W8 passage-gate protocol is refused");
    check(!supportedReplayProtocol(11, 246), "previous Legion W7 crossing-group protocol is refused");
    check(!supportedReplayProtocol(11, 244), "previous Legion C1 paused-build protocol is refused");
    check(!supportedReplayProtocol(11, 243), "previous Legion W6 flyer-pairing protocol is refused");
    check(!supportedReplayProtocol(11, 242), "previous Legion W4 protocol is refused");
    check(!supportedReplayProtocol(11, 241), "previous Legion record-walk protocol is refused");
    check(!supportedReplayProtocol(11, 240), "previous Legion settle/convoy protocol is refused");
    check(!supportedReplayProtocol(11, 239), "previous Legion formation/approach protocol is refused");
    check(!supportedReplayProtocol(11, 224), "previous two-pathfinder simulation protocol is refused");
    check(!supportedReplayProtocol(11, 219), "old protocol cannot claim a flow-capable format");
    check(!supportedReplayProtocol(9, 212), "older simulation protocol is refused");
    check(!supportedReplayProtocol(9, 210), "protocol 210 predates the naval production correction");
    check(!supportedReplayProtocol(9, 211), "protocol 211 predates the naval production correction");
    check(!supportedReplayProtocol(9, 209), "older simulation protocol is refused");
    check(!supportedReplayProtocol(9, kNetVersion+1), "future protocol is refused");
    check(!supportedReplayProtocol(8, 210), "compatibility exception requires format 9");
    check(!supportedReplayProtocol(kReplayFormat+1, kNetVersion), "unsupported format is refused even with current protocol");
    {
        // A format from the future. The old loader read it anyway, interpreting
        // whatever bytes happened to follow.
        Writer w;
        writeReplayHeader(w, sample());
        w.b[4] = uint8_t(kReplayFormat + 7);   // format is the u32 right after the magic
        Reader r(w.b.data() + 4, w.b.size() - 4);
        ReplayHeader out;
        uint32_t fmt = 0, proto = 0;
        check(!readReplayHeader(r, out, fmt, proto), "a NEWER format version is refused");
    }
    {
        Writer w;
        writeReplayHeader(w, sample());
        w.b[4] = 0;                            // format 0 is not a thing
        Reader r(w.b.data() + 4, w.b.size() - 4);
        ReplayHeader out;
        uint32_t fmt = 0, proto = 0;
        check(!readReplayHeader(r, out, fmt, proto), "format 0 is refused");
    }
    {
        // Truncated header. The old reader ran off the end and returned success.
        Writer w;
        writeReplayHeader(w, sample());
        w.b.resize(w.b.size() / 2);
        Reader r(w.b.data() + 4, w.b.size() - 4);
        ReplayHeader out;
        uint32_t fmt = 0, proto = 0;
        check(!readReplayHeader(r, out, fmt, proto), "a truncated header is refused");
    }
    {
        // A slot count larger than the table it fills.
        Writer w;
        writeReplayHeader(w, sample());
        // The slot count is the last byte before the slot block: find it by rebuilding
        // a header and noting where the block starts.
        Writer probe;
        ReplayHeader h = sample();
        writeReplayHeader(probe, h);
        size_t countAt = probe.b.size() - size_t(kMaxSlots) * 5 - 4;   // ... count, slots, mapScenario, powerCodes, fogExplored
        w.b[countAt] = uint8_t(kMaxSlots + 9);
        Reader r(w.b.data() + 4, w.b.size() - 4);
        ReplayHeader out;
        uint32_t fmt = 0, proto = 0;
        check(!readReplayHeader(r, out, fmt, proto),
              "an impossible slot count is refused (would overrun the table)");
    }

    std::printf(g_fail ? "replay_test: %d FAILURE(S)\n" : "replay_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
