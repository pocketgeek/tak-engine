#include "sim/retailreclaimarea.h"
#include "sim/sim.h"
#include "net/protocol.h"
#include <cstdio>
#include <iostream>
#include <vector>
#include <string>
using namespace tak;
int main(int argc,char** argv) {
    if (argc>1 && std::string(argv[1])=="--selector") {
        sim::RetailReclaimArea a;int32_t x,z;uint32_t mask;
        while(std::cin>>a.minX>>a.minZ>>a.maxX>>a.maxZ>>x>>z>>mask) {
            int index=0;
            auto result=sim::retailReclaimAreaTarget(a,x,z,[&](int32_t,int32_t){return (mask>>(index++))&1;});
            if(result)std::cout<<1<<' '<<result->first<<' '<<result->second<<'\n';
            else std::cout<<"0 0 0\n";
        }
        return 0;
    }
    int failed=0;
    auto check=[&](bool ok,const char* text){std::printf("%s %s\n",ok?"PASS":"FAIL",text);failed+=!ok;};
    net::Command command;command.kind=net::Cmd::ReclaimArea;command.x=17.5f;command.z=32;
    command.x2=320;command.z2=144.25f;command.queue=1;
    net::Writer wire;wire.cmd(command);net::Reader reader(wire.b.data(),wire.b.size());
    const auto decoded=reader.cmd();
    check(reader.ok && decoded.kind==command.kind && decoded.x==command.x && decoded.z==command.z &&
          decoded.x2==command.x2 && decoded.z2==command.z2 && decoded.queue==1,"area command wire roundtrip");
    net::Reader truncated(wire.b.data(),wire.b.size()-1);
    (void)truncated.cmd();
    check(!truncated.ok,"truncated area endpoint is rejected");
    net::Command ordinary;ordinary.kind=net::Cmd::Move;
    net::Writer ordinaryWire;ordinaryWire.cmd(ordinary);
    check(ordinaryWire.b.size()==35,"ordinary command wire layout is unchanged");
    net::Writer mixed;mixed.cmd(ordinary);mixed.cmd(command);mixed.cmd(ordinary);
    net::Reader mixedReader(mixed.b.data(),mixed.b.size());
    const auto first=mixedReader.cmd(),middle=mixedReader.cmd(),last=mixedReader.cmd();
    check(mixedReader.ok && first.kind==net::Cmd::Move && middle.x2==command.x2 &&
          middle.z2==command.z2 && last.kind==net::Cmd::Move,
          "mixed area and ordinary replay commands retain framing");
    sim::World world;world.setVisPlayer(-1);
    std::vector<uint8_t> heights(32*32,100);
    world.setTerrain(heights,32,32,20);
    world.setMapPlacementFeatures(std::vector<uint16_t>(32*32,0xffff),{});
    sim::UnitType builder;builder.id="area-builder";builder.maxHp=100;
    builder.isBuilder=builder.canMove=builder.canReclaim=true;
    builder.maxVel=sim::Fixed::fromInt(1);builder.buildDist=500;builder.sight=500;
    int id=world.spawn(&builder,152,160,0,0);
    world.addFeature(11,184,168,1,4,1,1,false);
    world.addFeature(12,136,168,1,4,1,1,false);
    world.addFeature(13,200,168,1,4,1,1,false);
    world.reclaimArea(id,128,160,208,176,false);
    check(world.unit(id)->orders.size()==1 && world.unit(id)->orders.front().reclaimArea.has_value(),
          "area remains one persistent command rather than a precomputed queue");
    world.tick(1.0f/30);
    check(world.unit(id)->reclaimId==12 && world.feature(11)->alive,
          "native sampled-cell distance selects the left feature first");
    // Simulate a new builder position while its next child is pending: selection
    // must use this position, not the original click's ordering.
    world.unit(id)->x=sim::Fixed::fromInt(208);
    world.updateNavigationExploration();
    world.tick(1.0f/30);
    check(!world.feature(12)->alive && world.unit(id)->reclaimId==13 && world.feature(11)->alive,
          "next target is selected from current builder position");
    world.tick(1.0f/30);
    world.tick(1.0f/30);
    world.tick(1.0f/30);
    check(!world.feature(11)->alive && world.unit(id)->orders.empty(),"area completes after targets are exhausted");
    world.addFeature(14,168,168,1,4,1,1,false);
    world.order(id,160,160,false);
    world.reclaimArea(id,128,160,208,176,true);
    check(world.unit(id)->orders.size()>=2 && world.unit(id)->orders.back().reclaimArea.has_value(),
          "shift area reclaim queues behind an existing move");
    world.stop(id);
    check(world.unit(id)->orders.empty(),"stop clears persistent area work");
    world.reclaimArea(id,128,160,208,176,false);
    world.cancelBuilds(id);world.order(id,320,160,false);
    check(world.unit(id)->orders.back().groundMission && !world.unit(id)->orders.back().reclaimArea,
          "replacement move cancels the persistent area");

    sim::World hidden;hidden.setVisPlayer(-1);hidden.setTerrain(heights,32,32,20);
    builder.sight=1;
    const int hb=hidden.spawn(&builder,152,160,0,0);
    hidden.addFeature(20,408,408,1,4,1,1,false);
    hidden.reclaimArea(hb,128,128,416,416,false);hidden.tick(1.0f/30);
    check(hidden.unit(hb)->orders.empty() && hidden.feature(20)->alive,
          "headless authoritative area selection excludes unseen features");
    sim::UnitType ally=builder;ally.id="area-observer";
    hidden.setTeam(1,0);
    hidden.spawn(&ally,400,400,1,0);
    hidden.reclaimArea(hb,128,128,416,416,false);hidden.tick(1.0f/30);
    check(hidden.unit(hb)->reclaimId==20,"allied sight admits visible area features");
    const int other=hidden.spawn(&builder,400,400,0,0);
    hidden.reclaim(other,20,false);
    hidden.tick(1.0f/30);hidden.tick(1.0f/30);hidden.tick(1.0f/30);
    check(!hidden.feature(20)->alive && hidden.unit(hb)->orders.empty(),
          "a target removed while a child is active lets the area complete");
    sim::World footprint;footprint.setVisPlayer(-1);footprint.setTerrain(heights,32,32,20);
    footprint.setMapPlacementFeatures(std::vector<uint16_t>(32*32,0xffff),{});
    const int fb=footprint.spawn(&builder,184,184,0,0);
    footprint.addFeature(30,176,176,1,4,2,2,false);
    footprint.reclaimArea(fb,184,184,184,184,false);footprint.tick(1.0f/30);
    check(footprint.unit(fb)->reclaimId==30,
          "area touching only a footprint tail selects the anchored feature");
    return failed?1:0;
}
