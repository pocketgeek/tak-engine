// The information bar's status line (client/unitstatus.h). Every type
// without activatewhenbuilt is created with its activation bit clear (4c033e,
// default 0; the parser's default since protocol 312), so the archer is as
// "off" as a new gate. Only a unit that can be switched off reads INACTIVE;
// the line used to say it for every idle unit of such a type.
#include "client/unitstatus.h"
#include "sim/sim.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using namespace tak::sim;
static void check(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }

static UnitR snapshot(const Unit& u) {
    UnitR r;
    r.id = u.id; r.type = u.type; r.player = u.player;
    r.active = u.active; r.cloaked = u.cloaked; r.underConstruction = u.underConstruction;
    r.selfDestructT = u.selfDestructT < 0 ? -1.0f : float(u.selfDestructT) / 30.0f;
    r.repairId = u.repairId; r.reclaimId = u.reclaimId; r.buildSiteId = u.buildSiteId;
    r.captureOrders(u);
    return r;
}

int main() try {
    World w;
    w.setPlayerCount(2);
    w.setTerrain(std::vector<uint8_t>(64 * 64, 50), 64, 64, 0);
    // As the parser makes them: no activatewhenbuilt key, so not activated.
    UnitType archer; archer.id = archer.name = "archer"; archer.maxHp = 100;
    archer.maxVel = Fixed::fromInt(2); archer.activateWhenBuilt = false;
    UnitType gate; gate.id = gate.name = "gate"; gate.maxHp = 500;
    gate.onOffable = true; gate.activateWhenBuilt = false;
    const int a = w.spawn(&archer, 300, 300, 0, 0), g = w.spawn(&gate, 600, 600, 0, 0);
    check(!w.unit(a)->active && !w.unit(g)->active, "a type without activatewhenbuilt starts active");
    const std::string idle = tak::client::unitStatusLabel(snapshot(*w.unit(a)));
    check(idle == "STANDBY", "an idle archer reads " + idle + ", not STANDBY");
    w.order(a, 400, 300, false);
    const std::string moving = tak::client::unitStatusLabel(snapshot(*w.unit(a)));
    check(moving == "MOVING", "a moving archer reads " + moving + ", not MOVING");
    const std::string off = tak::client::unitStatusLabel(snapshot(*w.unit(g)));
    check(off == "INACTIVE", "a gate switched off reads " + off + ", not INACTIVE");
    UnitR on = snapshot(*w.unit(g)); on.active = true;
    const std::string onText = tak::client::unitStatusLabel(on);
    check(onText == "STANDBY", "a gate switched on reads " + onText + ", not STANDBY");
    std::puts("unit status: PASS");
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
