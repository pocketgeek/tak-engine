// pathblock_test -- a unit must get PAST other units standing in its way.
//
// Reported as "they don't go around larger obstacles, they try their damndest to go
// over or through them first". Terrain was only half of it. The other half was other
// units, and it had two causes that had to be fixed together -- fixing either alone
// leaves the unit stuck, which is why this test exists rather than a comment.
//
// ONE: the SEARCH routed through them. cellScore grades a parked body kCellOccupied,
// which EQUALS kCellThreshold, so it is passable-but-costly -- retail's model, where
// every icd call site compares against 4. Our mover has no local avoidance and bodies
// are solid, so a route through a standing crowd is a route into a wall.
//
// TWO: the route SHORTCUT straightened it back through them. Once the search goes
// around, a shortcut that only checks terrain undoes the detour.
//
// Measured on the wall below before the fix: every thickness from 1 to 8 cells STUCK,
// and it stayed stuck with the shortcut disabled entirely -- which is how the first
// diagnosis (blame the shortcut) was shown to be wrong. After: every thickness passes,
// travelling 1262-1329 against a 1100 straight line.

#include "sim/sim.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace tak::sim;

static int g_fail = 0;
static void check(bool cond, const std::string& what, const std::string& detail = {}) {
    std::printf("  %-56s %s%s%s\n", what.c_str(), cond ? "ok" : "FAIL",
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!cond) ++g_fail;
}

static UnitType soldier() {
    UnitType t{};
    t.name = "sol"; t.id = "sol";
    t.maxVel = tak::sim::Fixed::fromFloat(70.0f / 30.0f); t.turnRate = 10000;
    t.maxHp = 100; t.canMove = true;
    t.footX = 2; t.footZ = 2;
    return t;
}

// A wall of PARKED bodies across the direct line, `thick` rows deep, with open ground
// round both ends. Returns the distance travelled, or -1 if the unit never arrived.
static float runWall(int thick) {
    const int W = 160, H = 160;
    World w;
    w.setVisPlayer(-1);
    w.setTerrain(std::vector<uint8_t>(size_t(W) * size_t(H), 100), W, H, /*seaLevel=*/20);
    w.setPathService(true);
    UnitType s = soldier();
    // Player 1's units, left standing still -- a neutral crowd, not a fight.
    for (int t = 0; t < thick; ++t)
        for (int k = 0; k < 40; ++k)
            w.spawn(&s, 600.0f + float(k) * 32.0f, 1000.0f + float(t) * 32.0f, 0, 1);
    for (auto& u : w.units()) u.speed = tak::sim::Fixed();
    const int id = w.spawn(&s, 900, 500, 0, 0);
    w.order(id, 900, 1600, /*queue=*/false);
    float px = w.unit(id)->x.toFloat(), pz = w.unit(id)->z.toFloat(), travelled = 0;
    for (int i = 0; i < 30 * 200; ++i) {
        w.tick(1.0f / 30.0f);
        const Unit* q = w.unit(id);
        travelled += std::sqrt((q->x.toFloat() - px) * (q->x.toFloat() - px) + (q->z.toFloat() - pz) * (q->z.toFloat() - pz));
        px = q->x.toFloat(); pz = q->z.toFloat();
        const float dx = q->x.toFloat() - 900.0f, dz = q->z.toFloat() - 1600.0f;
        if (std::sqrt(dx * dx + dz * dz) < 60.0f) return travelled;
    }
    return -1.0f;
}


int main() {
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(128*128,100),128,128,20);
        w.setPathService(true);
        auto s=soldier();
        const int id=w.spawn(&s,320,640,0,0);
        w.patrol(id,960,640);
        bool outbound=false,returned=false,secondOutbound=false;
        for (int tick=0;tick<1500;++tick) {
            w.tick(1.f/30);
            const float x=w.unit(id)->x.toFloat();
            if (x>880) { if (returned) secondOutbound=true; outbound=true; }
            if (outbound && x<400) returned=true;
        }
        check(outbound && returned && secondOutbound,
              "ground patrol travels out, returns, and repeats");
    }
    std::printf("pathblock_test\n");
    // The first blocking body by ID wins, even when spatial cell order is
    // reversed. A mobile blocker returns 2 while a structure returns 0.
    for (bool structureFirst : {false,true}) {
        World w;w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(32*32,100),32,32,20);
        UnitType mover=soldier(),mobile=soldier(),structure=soldier();
        mobile.footX=mobile.footZ=structure.footX=structure.footZ=1;
        structure.maxVel=Fixed();
        w.spawn(structureFirst ? &structure : &mobile,168,168);
        w.spawn(structureFirst ? &mobile : &structure,152,152);
        const int self=w.spawn(&mover,64,64);
        check(w.cellScore(&mover,10,10,self)==(structureFirst ? 0 : 2),
              "mixed blockers retain ascending-ID precedence over cell order");
    }
    // Stress flyers exposed this through a landed body below the map. Aging
    // its clipped footprint read unrelated heap bytes into the cached checksum.
    for (auto [bx,bz] : {std::pair{-5,32},{69,32},{32,-5},{32,69},
                        {-1,32},{64,32},{32,-1},{32,64}}) {
        World w;w.setVisPlayer(-1);w.setPathService(true);
        w.setTerrain(std::vector<uint8_t>(64*64,100),64,64,20);
        UnitType body=soldier(),mover=soldier();mover.footX=mover.footZ=4;
        w.spawn(&body,float(bx*16+16),float(bz*16+16),0,0);
        const int id=w.spawn(&mover,160,160,0,0);
        for (int tick=0;tick<14;++tick) w.tick(1.f/30.f);
        w.order(id,640,640,false);
        bool consistent=true;
        for (int tick=0;tick<60;++tick) {
            w.tick(1.f/30.f);
            consistent &= w.searchGradeChecksumsValid();
        }
        check(w.pathStats().completions()+w.pathStats().failures()>0,
              "off-map body fixture exercises the search cache");
        check(consistent,"off-map footprint aging preserves the grade checksum invariant");
    }
    // Native move-command validation/submission preserves blocked destinations.
    // A nearest-walkable-cell scan changes both the active and queued missions.
    for (bool boat : {false,true}) for (auto [fx,fz] : {std::pair{1,1},{2,2},{2,6},{6,2}}) {
        World w;w.setPathService(true);
        w.setTerrain(std::vector<uint8_t>(32*32,0),32,32,boat?32:0);
        UnitType type=soldier();type.footX=fx;type.footZ=fz;
        type.floater=boat;type.minWaterDepth=boat?13:-10000;type.maxWaterDepth=boat?10000:20;
        type.domain=boat?UnitType::Domain::Water:UnitType::Domain::Ground;
        const int id=w.spawn(&type,6*16+fx*8,6*16+fz*8);
        w.blockCells(12,12,12,12,true);
        w.order(id,270.25f,340.5f,false);
        w.order(id,290.5f,330.25f,true);
        const auto& orders=w.unit(id)->orders;
        check(orders.size()==2 && orders[0].missionTarget==std::pair{Fixed::fromFloat(270.25f),Fixed::fromFloat(340.5f)} &&
              orders[1].x==Fixed::fromFloat(290.5f) &&
              orders[1].z==Fixed::fromFloat(330.25f) && w.pathStats().pending(id),
              "blocked active/queued destinations retained",
              std::string(boat?"boat ":"land ")+std::to_string(fx)+"x"+std::to_string(fz));
        check(orders[0].x==footprintWaypoint(footprintCell(270.25f,fx),fx) &&
              orders[0].z==footprintWaypoint(footprintCell(340.5f,fz),fz),
              "initial steering endpoint uses independent footprint axes");
    }
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64*64,100),64,64,20);
        w.setPathService(true);
        UnitType builder=soldier(), site=soldier();
        builder.isBuilder=true;
        builder.turnInPlaceRate=1000;
        site.maxVel=Fixed(); site.footX=site.footZ=2;
        site.buildTime=10000;
        for (int tick=0;tick<14;++tick) w.tick(1.0f/30.0f);
        const int id=w.spawn(&builder,128,128);
        w.queueBuild(id,&site,31*16,9*16,false);
        const Order approach=w.unit(id)->orders.front();
        check(approach.buildRectangle==RetailRectGoal{28,32,6,10},
              "normal build commands use the verified rectangular approach bounds");
        w.tick(1.0f/30.0f);
        check(w.units().size()==1 && !w.unit(id)->buildSiteId,
              "rectangular builder approach does not create a site before arrival");
        check(w.pathStats().completions()>0 && w.unit(id)->orders.back().buildType==&site &&
              w.unit(id)->orders.back().buildRectangle==approach.buildRectangle,
              "route delivery retains the owning builder order and rectangular goal");
        auto* u=w.unit(id);
        u->x=Fixed::fromInt(29*16); u->z=Fixed::fromInt(8*16);
        u->heading=Bam(0);
        const auto x=u->x,z=u->z;
        w.tick(1.0f/30.0f);
        check(w.units().size()==1 && u->x==x && u->z==z && (u->orders.back().mission.pending&0x100),
              "rectangle arrival holds position and signals its builder mission");
        w.tick(1.0f/30.0f);
        check(w.unit(id)->x==x && w.unit(id)->z==z,
              "deferred builder placement cannot add a movement step into the site");
        check(w.unit(id)->buildSiteId && w.unit(id)->heading==Bam(1000),
              "builder begins its pivot on the placement dispatch");
        w.tick(1.0f/30.0f);
        check(w.unit(id)->heading==Bam(2000) && w.unit(id)->x==x && w.unit(id)->z==z,
              "working builder turns exactly once and skips the ordinary mover");
    }
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64*64,100),64,64,20);
        w.setPathService(true);
        UnitType builder=soldier(),site=soldier();
        builder.isBuilder=true; builder.workerTime=1000;
        site.maxVel=Fixed(); site.buildTime=1; site.buildCost=0;
        const int id=w.spawn(&builder,128,128);
        w.queueBuild(id,&site,320,128,false);
        w.queueBuild(id,&site,128,320,true);
        w.tick(1.0f/30.0f);
        check(w.units().size()==1 && w.unit(id)->orders.back().mission.stage==0,
              "queued build remains inactive while the builder approaches its predecessor");
        for (int t=0;t<1200 && (w.units().size()<3 || w.unit(id)->buildSiteId);++t)
            w.tick(1.0f/30.0f);
        check(w.units().size()==3 && !w.unit(id)->buildSiteId && w.unit(id)->orders.empty(),
              "builder completes successive rectangular approaches in queue order");
    }
    // Replacing a movement order must retire its in-flight search. Otherwise
    // delivery rewrites the replacement command as a route to the old goal.
    for (bool ambush : {false, true}) {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
        w.setPathService(true);
        UnitType s = soldier();
        const int id = w.spawn(&s, 128, 128);
        w.order(id, 800, 128, false);
        check(w.pathStats().pending(id), "move has an outstanding search before replacement");
        if (ambush) w.orderWaitAttack(id, true);
        else w.orderWait(id, 10, true);
        check(w.pathStats().pending(id), "queued wait preserves current movement search");
        if (ambush) w.orderWaitAttack(id, false);
        else w.orderWait(id, 10, false);
        // A later queued destination must not be absorbed by stale delivery.
        w.order(id, 128, 800, true);
        check(!w.pathStats().pending(id),
              ambush ? "ambush replacement cancels old movement search"
                     : "wait replacement cancels old movement search");
        for (int tick = 0; tick < 30; ++tick) w.tick(1.0f / 30.0f);
        const Unit& u = *w.unit(id);
        check(u.x == Fixed::fromInt(128) && u.z == Fixed::fromInt(128) &&
                  u.orders.size() == 2 &&
                  (ambush ? u.orders.front().waitAttack : u.orders.front().wait == 270) &&
                  u.orders.back().x == Fixed::fromInt(128) &&
                  u.orders.back().z == Fixed::fromInt(800),
              ambush ? "stale delivery cannot erase ambush and queued move"
                     : "stale delivery cannot erase wait and queued move");
    }
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
        w.setPathService(true);
        w.setPathBudget(1); // A blocked goal must survive an unfinished search.
        w.blockCells(16, 0, 2, 64, true);
        UnitType s = soldier();
        const int id = w.spawn(&s, 128, 128);
        w.order(id, 800, 128, false);
        w.order(id, 128, 800, true);
        w.unit(id)->speed = Fixed();
        w.unit(id)->stuckFor = 30;
        w.unit(id)->stuckX = w.unit(id)->x;
        w.unit(id)->stuckZ = w.unit(id)->z;
        w.tick(1.0f / 30.0f);
        const Unit& u = *w.unit(id);
        check(u.orders.size() == 2 && u.orders.front().x == Fixed::fromInt(800) &&
                  u.orders.front().z == Fixed::fromInt(128) &&
                  u.orders.back().x == Fixed::fromInt(128) &&
                  u.orders.back().z == Fixed::fromInt(800),
              "blocked goal and queued move survive unfinished search");
        check(w.pathStats().pending(id), "blocked goal retains ownership of pending search");
        w.tick(1.0f / 30.0f);
        check(w.pathStats().pending(id) && u.orders.size() == 2 &&
                  u.orders.front().x == Fixed::fromInt(800),
              "blocked movement does not silently advance to queued command");
    }
    // Captured stage-2 failure must cancel before the search can deliver,
    // reanchoring the direct navigator segment and retaining the queued goal.
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
        w.setPathService(true);
        UnitType s = soldier();
        const int id = w.spawn(&s, 128, 128);
        w.order(id, 800, 128, false);
        w.order(id, 128, 800, true);
        auto& u = *w.unit(id);
        auto& goal = u.orders.front();
        goal.mission.stage = 2;
        goal.mission.waitMask = 0x2701;
        goal.mission.pending = 0x2000;
        goal.mission.deadline = 100;
        u.routeStamp = 0;
        const auto controller = goal.controller;
        goal.segmentX=Fixed::fromInt(32);
        const auto anchor = Fixed::fromInt(u.x.floorInt());
        w.tick(1.0f / 30.0f);
        check(w.pathStats().pending(id) && w.pathStats().completions() == 0,
              "ground failure replaces the search while retaining the admission delay");
        check(u.orders.size() == 2 && u.orders.front().segmentX == anchor &&
              u.orders.front().controller != controller &&
              u.orders.front().missionRadius == 64 && u.orders.front().mission.stage == 2 &&
              u.orders.front().mission.waitMask == 0x2701 &&
              u.orders.front().mission.deadline >= 6 && u.orders.front().mission.deadline <= 10,
              "ground reset reanchors direct segment, preserves queue and schedules polling");
        const auto hash = w.stateHash();
        ++u.orders.front().mission.deadline;
        check(hash != w.stateHash(), "ground dispatcher state participates in lockstep hash");
    }
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
        w.setPathService(true);
        UnitType s = soldier();
        const int id = w.spawn(&s, 128, 128);
        w.order(id, 128, 128, false);
        w.order(id, 800, 128, true);
        w.unit(id)->speed=s.brake*Fixed::fromInt(2);
        w.tick(1.0f / 30.0f);
        check(w.unit(id)->orders.front().mission.pending & 0x100,
              "ground arrival reports an event to its owning mission");
        check(w.unit(id)->speed==s.brake && w.unit(id)->x==Fixed::fromInt(128),
              "ground arrival applies inactive braking without another movement step");
        w.tick(1.0f / 30.0f);
        const auto& u = *w.unit(id);
        check(!u.orders.empty() && u.orders.back().x == Fixed::fromInt(800) &&
              u.orders.back().mission.stage == 2,
              "arrival dispatch advances and initializes the queued ground goal");
    }
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(20 * 20, 100), 20, 20, 20);
        w.setPathService(true);
        w.setPathBudget(1);
        w.blockCells(10, 0, 1, 20, true);
        UnitType s = soldier();
        // Failure notification requires knowing that the wall cuts off the goal.
        s.sight = 1024; s.upright = true;
        s.footX = s.footZ = 1;
        s.maxVel = Fixed::fromFloat(0.001f);
        const int id = w.spawn(&s, 24, 168);
        w.order(id, 296, 168, false);
        auto& u = *w.unit(id);
        u.orders.front().mission = {2, 0x2701, 0xffffffffu, 0, 0};
        u.routeStamp = 0;
        // Keep the separate terrain watchdog out of this dispatcher fixture.
        u.stuckFor = -10000;
        u.stuckX = u.x; u.stuckZ = u.z;
        const auto controller = u.orders.front().controller;
        bool notified = false;
        for (int tick = 0; tick < 1000 && !u.orders.empty(); ++tick) {
            w.tick(1.0f / 30.0f);
            if (!u.orders.empty() && (u.orders.back().mission.pending & 0x2000)) {
                notified = true;
                break;
            }
        }
        check(notified && w.pathStats().pending(id) && w.pathStats().failures() == 0,
              "World receives early failure while cost search remains unfinished");
        w.tick(1.0f / 30.0f);
        check(!u.orders.empty() && u.orders.back().controller != controller &&
              u.orders.back().missionRadius == 32 && w.pathStats().pending(id) &&
              w.pathStats().failures() == 0,
              "next World dispatch replaces early-notifying search before stale delivery");
    }
    // A newly completed route becomes visible after this tick's movement.
    // Compare with the same mover whose background search is disabled.
    {
        World searched, direct;
        UnitType s = soldier();
        for (World* w : {&searched, &direct}) {
            w->setVisPlayer(-1);
            w->setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
            w->blockCells(20, 0, 2, 32, true);
            for (int tick=0;tick<14;++tick) w->tick(1.0f/30.0f);
            const int id = w->spawn(&s, 135.5f, 128);
            w->setPathService(w == &searched);
            w->order(id, 640, 384, false);
            w->unit(id)->heading = Bam(8192);
            w->unit(id)->speed = s.maxVel;
            w->tick(1.0f / 30.0f);
        }
        const auto& a = searched.units().front();
        const auto& b = direct.units().front();
        check(searched.pathStats().completions() + searched.pathStats().failures() > 0,
              "timing fixture completes its search on the first eligible tick");
        check(a.groundScanTick==0 && b.groundScanTick>0,
              "route delivery invalidates the next local obstacle scan");
        check(a.x == b.x && a.z == b.z && a.heading == b.heading && a.speed == b.speed,
              "route completion does not change movement earlier in the same tick");
        check(footprintCell(a.x, 2) != footprintCell(Fixed::fromFloat(135.5f), 2) &&
              !a.orders.empty() &&
              a.orders.front().segmentX == footprintWaypoint(footprintCell(a.x, 2), 2) &&
              a.orders.front().segmentZ == footprintWaypoint(footprintCell(a.z, 2), 2),
              "new search anchors its route in the cell reached by this tick's move");
    }
    // Repeated destination clicks while a 2x2 group passes terrain. Check every
    // committed footprint, including moments when an in-flight route is replaced.
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(80 * 80, 100), 80, 80, 20);
        w.setPathService(true);
        UnitType s = soldier();
        w.blockCells(24, 12, 5, 24, true);
        std::vector<int> group;
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i)
                group.push_back(w.spawn(&s, 128 + 48 * i, 272 + 48 * j));
        bool overlap = false, terrain = false;
        std::vector<uint32_t> radii(group.size(), 0);
        for (int tick = 0; tick < 3600; ++tick) {
            if (tick == 0 || tick == 300 || tick == 600)
                for (size_t i = 0; i < group.size(); ++i)
                    w.order(group[i], 800 + 48 * int(i % 4),
                            (tick == 300 ? 720 : 320) + 48 * int(i / 4), false);
            w.tick(1.0f / 30.0f);
            for (size_t a = 0; a < group.size(); ++a) {
                const Unit& u = *w.unit(group[a]);
                for (const auto& goal : u.orders)
                    if (goal.groundMission) { radii[a] = goal.missionRadius; break; }
                const int x = footprintOrigin(u.x, 2), z = footprintOrigin(u.z, 2);
                terrain |= !w.nav().fits(x + 1, z + 1, 2);
                for (size_t b = a + 1; b < group.size(); ++b) {
                    const Unit& v = *w.unit(group[b]);
                    overlap |= std::abs(x - footprintOrigin(v.x, 2)) < 2 &&
                               std::abs(z - footprintOrigin(v.z, 2)) < 2;
                }
            }
        }
        int arrived = 0;
        for (size_t i = 0; i < group.size(); ++i) {
            const Unit& u = *w.unit(group[i]);
            float dx = u.x.toFloat() - (800 + 48 * int(i % 4));
            float dz = u.z.toFloat() - (320 + 48 * int(i / 4));
            // Mission failure grows the accepted cell circle. Include one
            // cell for the difference between cell origin and subcell position.
            const float accepted = float(radii[i] + 4) + 23.0f;
            arrived += u.orders.empty() && dx * dx + dz * dz <= accepted * accepted;
            if (!u.orders.empty() || dx * dx + dz * dz > accepted * accepted) {
                std::printf("    unfinished id=%d pos=(%.3f,%.3f) speed=%.3f mode=%u orders=%zu\n",
                            u.id, u.x.toFloat(), u.z.toFloat(), u.speed.toFloat(),
                            unsigned(u.groundMovementMode), u.orders.size());
                for (const auto& o : u.orders)
                    std::printf("      point=(%.3f,%.3f) segment=(%.3f,%.3f) goal=%d\n",
                                o.x.toFloat(), o.z.toFloat(), o.segmentX.toFloat(), o.segmentZ.toFloat(), int(o.goal));
            }
        }
        check(!overlap && !terrain, "repeated group orders never overlap bodies or terrain");
        check(arrived == 16, "all 16 units complete within their mission radius after repeated clicks",
              std::to_string(arrived) + "/16");
    }
    // A nearby goal still needs a route when the intervening cell is blocked.
    // The old <3-cell request shortcut cancelled every retry, forever.
    for (bool body : {false, true}) {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
        w.setPathService(true);
        UnitType s = soldier();
        s.footX = s.footZ = 1;
        if (body) w.spawn(&s, 168, 168, 0, 1);
        else w.blockCells(10, 10, 1, 1, true);
        const int id = w.spawn(&s, 152, 168, 0, 0);
        w.order(id, 184, 168, false);
        for (int i = 0; i < 30 * 30; ++i) w.tick(1.0f / 30.0f);
        const Unit* u = w.unit(id);
        const float dx = u->x.toFloat() - 184, dz = u->z.toFloat() - 168;
        check(dx * dx + dz * dz < 16 * 16 && u->orders.empty(),
              body ? "short move detours around a parked unit"
                   : "short move detours around terrain",
              "x=" + std::to_string(u->x.toFloat()));
    }

    // The leader is pressing into a wall at less than 75% of cruising speed.
    // Being slightly faster than its follower does not make it passable traffic.
    // Exercise the actual mover as well as the route scorer.
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
        UnitType s = soldier();
        s.footX = s.footZ = 1;
        s.maxVel = Fixed::fromInt(2);
        w.blockCells(11, 10, 1, 1, true);
        int leader = w.spawn(&s, 175.5f, 168, 0, 0);
        int follower = w.spawn(&s, 159.5f, 168, 0, 0);
        w.order(leader, 400, 168, false);
        w.order(follower, 400, 168, false);
        w.unit(leader)->heading = w.unit(follower)->heading = Bam(16384);
        w.unit(leader)->speed = Fixed::fromFloat(0.9f);
        w.unit(follower)->speed = Fixed::fromFloat(0.8f);
        w.tick(1.0f / 30.0f);
        check(w.unit(follower)->x < Fixed::fromInt(160),
              "a slowed follower cannot step into a stalled leader");
        check(w.cellScore(&s, 10, 10, follower) == kCellUnitBlocked,
              "the route scorer also refuses stalled same-way traffic");
        w.unit(leader)->speed = w.unit(follower)->baseSpeed * Fixed::raw(0xc000);
        check(w.cellScore(&s, 10, 10, follower) == kCellGround,
              "traffic at the retail 75% speed floor preserves the terrain grade");
        w.unit(leader)->heading = retailHeadingToPort(65535);
        w.unit(follower)->heading = retailHeadingToPort(0);
        check(w.cellScore(&s, 10, 10, follower) == kCellUnitBlocked,
              "traffic heading comparison preserves retail's unsigned wrap boundary");
    }

    // Two movers attempt to claim the same empty cell during one tick. A
    // snapshot-only occupancy grid lets both succeed even with hard collision.
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
        UnitType s = soldier();
        s.footX = s.footZ = 1;
        s.maxVel = Fixed::fromInt(2);
        const int a = w.spawn(&s, 159, 168, 0, 0);
        const int b = w.spawn(&s, 177, 168, 0, 0);
        w.order(a, 400, 168, false);
        w.order(b, 100, 168, false);
        w.unit(a)->heading = Bam(16384);
        w.unit(b)->heading = Bam(49152);
        w.unit(a)->speed = w.unit(b)->speed = s.maxVel;
        w.tick(1.0f / 30.0f);
        check(w.unit(a)->x.floorInt() / 16 != w.unit(b)->x.floorInt() / 16,
              "two movers cannot claim the same cell in one tick");
    }

    // A prospective step must reserve the entire body, even when the centre
    // itself is clear. The route scorer must see that same blocked footprint.
    {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
        UnitType s = soldier();
        s.maxVel = Fixed::fromInt(2);
        w.blockCells(11, 9, 1, 1, true);
        const int id = w.spawn(&s, 167, 160, 0, 0);
        w.order(id, 400, 160, false);
        w.unit(id)->heading = Bam(16384);
        w.unit(id)->speed = s.maxVel;
        w.tick(1.0f / 30.0f);
        check(w.unit(id)->x >= Fixed::fromInt(167) && w.unit(id)->x < Fixed::fromInt(168),
              "terrain under the footprint edge blocks the step");
        check(w.cellScore(&s, 11, 10, id) < kCellThreshold,
              "the route scorer also sees terrain under the footprint edge");
        w.tick(1.0f / 30.0f);
        check(w.unit(id)->bodyBlockStreak == 2 &&
                  w.unit(id)->speed <= Fixed::fromFloat(w.unit(id)->baseSpeed.toFloat() * 0.2f),
              "terrain refusals advance the same slowdown state as bodies");
    }

    for (bool guard : {false, true}) {
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(64 * 64, 100), 64, 64, 20);
        w.setPathService(true);
        UnitType s = soldier();
        s.footX = s.footZ = 1;
        s.weapon.damage = 1;
        s.weapon.range = 24;
        s.weapons = {s.weapon};
        UnitType victim = s;
        victim.weapon.damage = 0;
        victim.weapons.clear();
        victim.maxHp = 10000;
        w.blockCells(10, 5, 1, 11, true);
        const int target = w.spawn(&victim, 408, 168, 0, guard ? 0 : 1);
        const int id = w.spawn(&s, 104, 168, 0, 0);
        if (guard) w.guard(id, target, false);
        else w.attack(id, target, false);
        w.order(id, 408, 408, true);
        for (int i = 0; i < 30 * 45; ++i) w.tick(1.0f / 30.0f);
        const Unit* u = w.unit(id);
        const float dx = u->x.toFloat() - 408, dz = u->z.toFloat() - 168;
        check(dx * dx + dz * dz < 70 * 70,
              guard ? "guard routes around terrain to its target"
                    : "attack routes around terrain to its target");
        check(!u->orders.empty() && u->orders.front().targetId == target &&
                  u->orders.back().z == Fixed::fromInt(408),
              "routing preserves the target and queued move");
        w.unit(target)->hp = Fixed();
        for (int i = 0; i < 30 * 30; ++i) w.tick(1.0f / 30.0f);
        u = w.unit(id);
        const float qx = u->x.toFloat() - 408, qz = u->z.toFloat() - 408;
        check(qx * qx + qz * qz < 16 * 16 && u->orders.empty(),
              "losing the target retires the route and resumes the queued move");
    }

    std::printf("a unit routes around a wall of parked bodies:\n");
    for (int thick = 1; thick <= 8; ++thick) {
        const float travelled = runWall(thick);
        check(travelled > 0,
              "wall " + std::to_string(thick) + " rows deep is passed",
              travelled > 0 ? std::to_string(int(travelled)) + "px travelled"
                            : "STUCK against it");
        // ...and it goes AROUND rather than wandering: the detour is bounded. The
        // straight line is 1100px; a sane way round the end of the wall is well under
        // three times that. Before the fix a stuck unit racked up 2515-3636px going
        // nowhere, so this also catches "arrives eventually, by accident".
        if (travelled > 0)
            check(travelled < 3300.0f,
                  "  and does not wander to get there",
                  std::to_string(int(travelled)) + "px for a 1100px straight line");
    }

    // The goal itself being occupied must still work: ordering a unit to where
    // something already stands is ordinary, and the search's goal exemption is what
    // keeps that from failing to route at all.
    std::printf("the goal can be occupied:\n");
    {
        const int W = 120, H = 120;
        World w;
        w.setVisPlayer(-1);
        w.setTerrain(std::vector<uint8_t>(size_t(W) * size_t(H), 100), W, H, 20);
        w.setPathService(true);
        UnitType s = soldier();
        for (int k = 0; k < 6; ++k) w.spawn(&s, 1200.0f + float(k) * 24.0f, 1200.0f, 0, 1);
        for (auto& u : w.units()) u.speed = tak::sim::Fixed();
        const int id = w.spawn(&s, 400, 400, 0, 0);
        w.order(id, 1200, 1200, false);
        bool close = false;
        for (int i = 0; i < 30 * 120 && !close; ++i) {
            w.tick(1.0f / 30.0f);
            const Unit* q = w.unit(id);
            const float dx = q->x.toFloat() - 1200.0f, dz = q->z.toFloat() - 1200.0f;
            close = std::sqrt(dx * dx + dz * dz) < 140.0f;
        }
        check(close, "a unit still reaches a destination others are standing on");
    }

    std::printf(g_fail ? "pathblock_test: %d FAILURE(S)\n" : "pathblock_test: all passed\n",
                g_fail);
    return g_fail ? 1 : 0;
}
