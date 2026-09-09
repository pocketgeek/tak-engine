#include "client/replayfile.h"

#include "net/protocol.h"      // tak::net::Reader / Event / Command

#include <cstdio>
#include <utility>

bool loadReplayFile(const std::string& path, ReplayFile& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d(size_t(n < 0 ? 0 : n));
    if (!d.empty() && std::fread(d.data(), 1, d.size(), f) != d.size()) { std::fclose(f); return false; }
    std::fclose(f);
    if (d.size() < 4 || d[0] != 'T' || d[1] != 'A' || d[2] != 'K' || d[3] != 'R') return false;
    tak::net::Reader r(d.data() + 4, d.size() - 4);
    uint32_t fmt = r.u32();        // format version
    r.u32();                       // protocol version
    out.mapId = r.str();
    uint8_t crusades = r.u8(); uint8_t gods = r.u8(); r.u8();
    if (fmt >= 2) out.overridePolicy = r.u8();   // override tier the game ran under
    out.cfg.unitCap = 0;                          // fmt<3 replays ran without a unit cap
    if (fmt >= 3) out.cfg.unitCap = uint16_t(r.u32());
    out.cfg.monarchExpendable = true;             // fmt<4 replays ran monarch-expendable
    if (fmt >= 4) out.cfg.monarchExpendable = r.u8() != 0;
    if (fmt >= 5) out.cfg.stressTest = r.u8() != 0;   // fmt<5 had no stress test
    r.u32();                       // seed (setupMatch derives its own timing)
    uint8_t nslots = r.u8();
    out.crusades = crusades != 0;
    out.cfg.gods = gods != 0;
    out.cfg.slots.resize(nslots);
    int maxUsed = 0;
    for (int i = 0; i < nslots; ++i) {
        uint8_t type = r.u8(), faction = r.u8(); r.u8(); uint8_t team = r.u8();
        bool used = (type == 1 || type == 2);
        out.cfg.slots[size_t(i)] = {used, faction % 5, team};
        if (used) maxUsed = i;
    }
    // The game used setPlayerCount(maxUsedSlot+1); match it exactly (empty trailing
    // players would otherwise enter the state hash and diverge from the recording).
    out.cfg.slots.resize(size_t(maxUsed + 1));
    uint32_t nticks = r.u32();
    for (uint32_t t = 0; t < nticks && r.ok; ++t) {
        uint32_t len = r.u32();
        if (!r.avail(len)) return false;
        tak::net::Reader br(r.p, len);
        r.p += len;
        br.u32();                  // tick index (implicit = t)
        tak::net::Bundle bd;
        uint32_t nc = br.u32();
        for (uint32_t i = 0; i < nc && br.ok; ++i) bd.cmds.push_back(br.cmd());
        uint32_t ne = br.u32();
        for (uint32_t i = 0; i < ne && br.ok; ++i) {
            tak::net::Event e; e.kind = tak::net::Event::Kind(br.u8()); e.player = br.u8();
            bd.events.push_back(e);
        }
        out.bundles.push_back(std::move(bd));
    }
    return true;
}
