#include "sim/sim.h"
#include <algorithm>
#include <cstdio>

// Compare the permanent-reveal shortcut against the full retail traversal,
// including cached footprint state, over uneven terrain and changing viewers.
static bool checkRevealShortcut() {
    using namespace tak::sim;
    constexpr int width=48,height=40;
    std::vector<uint16_t> full(width*height),skip(width*height);
    RetailSightFootprint a,b;
    a.distance=b.distance=160;
    a.sightHeight=b.sightHeight=24;
    size_t fullReads=0,skipReads=0;
    for (int step=0;step<400;++step) {
        const auto x=Fixed::fromInt(((step*7)%52-2)*32);
        const auto z=Fixed::fromInt(((step*11)%44-2)*32);
        const auto y=Fixed::fromInt((step*13)%180);
        const uint16_t viewers=uint16_t(1u<<(step%4)) | (step<200 ? 3 : 12);
        auto heights=[](int cx,int cz) {
            return RetailExplorationHeight{uint8_t((cx*7+cz*3)%180),
                                           uint8_t((cx*3+cz*11)%150)};
        };
        retailUpdateSight(a,x,y,z,30,true,width,height,0,
            [&](int cx,int cz){++fullReads;return heights(cx,cz);},
            [&](int cx,int cz,int delta,uint16_t){if(delta>0) full[cz*width+cx]|=viewers;});
        retailUpdateSight(b,x,y,z,30,true,width,height,0,
            [&](int cx,int cz){++skipReads;return heights(cx,cz);},
            [&](int cx,int cz,int delta,uint16_t){if(delta>0) skip[cz*width+cx]|=viewers;},
            [&](int cx,int cz,bool active){return active && (skip[cz*width+cx]&viewers)!=viewers;});
        if (full!=skip || a.x!=b.x || a.z!=b.z || a.eyeHeight!=b.eyeHeight ||
            a.active!=b.active) return false;
    }
    return skipReads<fullReads;
}

int main() {
    if (!checkRevealShortcut()) {
        std::fputs("exploration shortcut differs from full traversal\n",stderr);return 1;
    }
    using namespace tak::sim;
    UnitType type{};
    type.name=type.id="sight-probe";type.maxHp=100;
    type.maxVel=Fixed::fromInt(1);type.canMove=true;
    type.footX=type.footZ=1;type.sight=160;type.sightHeight=24;
    World serial,parallel;
    serial.setSerialThreads(true);
    std::vector<uint8_t> terrain(128*128);
    for (int z=0;z<128;++z) for (int x=0;x<128;++x)
        terrain[size_t(z)*128+x]=uint8_t((x*7+z*3)%180);
    for (auto* world:{&serial,&parallel}) {
        world->setVisPlayer(-1);world->setTerrain(terrain,128,128,30);
        for (int i=0;i<4096;++i) world->spawn(&type,float((i%64)*16),float((i/64)*16),0,i%4);
    }
    for (int step=0;step<16;++step) {
        for (auto* world:{&serial,&parallel}) {
            world->setTeam(1,step<8 ? 0 : 1);
            for (size_t i=0;i<world->units().size();++i) {
                auto& u=world->units()[i];
                // Overlapping revealers, map edges, changing eye heights,
                // construction exclusions, and an alliance change.
                u.x=Fixed::fromInt(int((i*13+step*7)%132)*16-32);
                u.z=Fixed::fromInt(int((i*7+step*11)%132)*16-32);
                u.groundY=Fixed::fromInt(int((i+step*9)%200));
                u.underConstruction=(i+size_t(step))%17==0;
            }
            world->tick(1.f/30);
        }
        if (serial.navigationExploration()!=parallel.navigationExploration() ||
            serial.stateHash()!=parallel.stateHash()) {
            std::fprintf(stderr,"parallel exploration differs at step %d\n",step);return 1;
        }
    }
    if (std::none_of(parallel.navigationExploration().begin(),parallel.navigationExploration().end(),
                     [](uint16_t value){return value!=0;})) return 1;
    std::puts("parallel exploration matches serial masks and world hashes");
}
