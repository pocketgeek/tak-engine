#include "sim/sim.h"
#include <cstdio>
#include <stdexcept>

using namespace tak::sim;
static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static void advance(World& w) { for (int i=0;i<20;++i) w.tick(1.f/30); }
int main() {
    UnitType eyes; eyes.id=eyes.name="eyes";eyes.maxHp=100;eyes.sight=128;eyes.sightHeight=40;
    eyes.footX=eyes.footZ=1;
    const auto setup=[](World& w) {
        w.setSerialThreads(true);w.setPlayerCount(2);
        w.setTerrain(std::vector<uint8_t>(128*128,0),128,128,0);
    };
    World w;setup(w);
    const int id=w.spawn(&eyes,512,512,{},0);
    advance(w);
    check(w.cellVisible(512,512),"unit reveals its position");
    check(!w.cellVisible(720,512),"ordinary sight does not reach distant point");
    w.unit(id)->x=Fixed::fromInt(1408);
    advance(w);
    check(!w.cellVisible(512,512),"lost line of sight hides units");
    check(w.visibility()[32*w.visW()+32]==1,"visited terrain stays explored after moving away");
    check(w.visibility()[110*w.visW()+110]==0,"unvisited terrain stays unexplored");
    const auto hash=w.stateHash();
    w.revealTerrain();
    check(w.visibility()[110*w.visW()+110]==1 && !w.cellVisible(1760,1760),
          "mapped terrain starts foggy, not visible");
    check(w.stateHash()==hash,"terrain display mode does not alter simulation");

    for (bool radar:{false,true}) {
        World normal,doubled;setup(normal);setup(doubled);doubled.setDoubleSight(true);
        UnitType type=eyes;
        if (radar) {type.sight=16;type.radar=128;}
        normal.spawn(&type,512,512,{},0);doubled.spawn(&type,512,512,{},0);
        advance(normal);advance(doubled);
        check(!normal.cellVisible(720,512) && doubled.cellVisible(720,512),
              radar?"double vision doubles radar":"double vision doubles sight");
        check(!doubled.cellVisible(832,512),"double vision remains bounded");
        check(normal.stateHash()!=doubled.stateHash(),"vision rule mismatch is detectable");
        if (!radar) {
            const auto& a=normal.navigationExploration();const auto& b=doubled.navigationExploration();
            size_t na=0,nb=0;for(auto v:a)na+=bool(v&1);for(auto v:b)nb+=bool(v&1);
            check(nb>na,"authoritative exploration uses doubled sight too");
        }
    }
    std::puts("PASS: persistent fog memory, mapped terrain, doubled sight/radar, authoritative exploration");
}
