// Text driver for tools/re/check_retail_group.py: runs the engine's Retail
// group helpers (src/sim/retailgroup.h, retailGroundFormation) on states the
// checker also loads into the original routines. Built by the checker.
#include "sim/retailgroup.h"
#include "sim/retailmission.h"
#include <cstdio>
#include <iostream>
#include <string>

using namespace tak::sim;

static RetailGroupRecord readRecord(std::istream& in,bool& members) {
    RetailGroupRecord r;int m,a,c;
    in>>m>>a>>c>>r.groundSpeed>>r.boatSpeed;members=m;r.active=a;r.changed=c;
    for (int k=0;k<3;++k) for (auto* g:{&r.all[size_t(k)],&r.moving[size_t(k)]})
        in>>g->count>>g->area>>g->centre.x>>g->centre.y>>g->centre.z;
    return r;
}
static void writeRecord(const RetailGroupRecord& r) {
    std::printf("%d %d %d %d",int(r.active),int(r.changed),r.groundSpeed,r.boatSpeed);
    for (int k=0;k<3;++k) for (const auto* g:{&r.all[size_t(k)],&r.moving[size_t(k)]})
        std::printf(" %d %d %d %d %d",g->count,g->area,g->centre.x,g->centre.y,g->centre.z);
    std::printf("\n");
}
static RetailGroupMember readMember(std::istream& in) {
    RetailGroupMember m;int kind,mover;
    in>>kind>>mover>>m.position.x>>m.position.y>>m.position.z>>m.area;
    m.kind=RetailGroupClass(kind);m.mover=mover;return m;
}

int main() {
    std::string op;
    while (std::cin>>op) {
        if (op=="tick") {
            int notify,n;std::cin>>notify;
            std::array<RetailGroupRecord,100> records{};std::array<bool,100> members{};
            for (int g=1;g<100;++g) {bool m;records[size_t(g)]=readRecord(std::cin,m);members[size_t(g)]=m;}
            std::cin>>n;std::vector<RetailGroupUnit> units(static_cast<size_t>(n));
            for (auto& u:units) {int e;std::cin>>u.group>>e;u.eligible=e;u.member=readMember(std::cin);
                std::cin>>u.missionFlags>>u.typeMaximum;}
            std::vector<uint8_t> paced;
            retailGroupTick(units,members,records,paced);
            std::vector<size_t> notified;
            if (notify) notified=retailGroupNotify(units,records);
            for (int g=1;g<100;++g) writeRecord(records[size_t(g)]);
            for (auto p:paced) std::printf("%d ",int(p));
            std::printf("\n");
            for (auto i:notified) std::printf("%zu ",i);
            std::printf("\n");
        } else if (op=="query") {
            bool m;auto r=readRecord(std::cin,m);auto u=readMember(std::cin);int a,b,level,all;
            std::cin>>a>>b>>level>>all;
            const auto c=retailGroupCentre(r,u,a,b);
            std::printf("%d %d %d %d %d\n",c.x,c.y,c.z,retailGroupRadius(r,u,a,b),
                        int(retailGroupOutOfSlot(r,u,b,level,all)));
        } else if (op=="check") {
            bool m;auto r=readRecord(std::cin,m);auto u=readMember(std::cin);uint32_t flags;
            RetailGroupPoint16 p,g;int draw;
            std::cin>>flags>>p.x>>p.y>>p.z>>g.x>>g.y>>g.z>>draw;
            int draws=0;
            const int result=retailGroundGroupCheck(r,u,flags,p,g,[&](int){++draws;return draw;});
            if (result==1) retailGroupLeave(r,u);
            std::printf("%d %u %d\n",result,flags,draws);writeRecord(r);
        } else if (op=="formation") {
            bool m;auto r=readRecord(std::cin,m);auto u=readMember(std::cin);
            int stage,level,attached,mode,draw;uint32_t flags;
            std::cin>>stage>>level>>attached>>mode>>flags>>draw;
            RetailMissionState s;s.stage=uint8_t(stage);s.flags=flags;
            std::string calls;int gx=0,gz=0;
            const bool all=(flags&0x4000000u)!=0;
            const int result=retailGroundFormation(s,1000,level,attached,mode,
                [&]{return level>0 ? retailGroupOutOfSlot(r,u,false,level,all) : retailGroupOutOfSlot(r,u,true,-level,all);},
                [&]{calls+="stop,";},
                [&]{const auto c=retailGroupCentre(r,u,all,level<=0);gx=c.x;gz=c.z;calls+="centre,";},
                [&]{calls+="move,";},[&]{calls+="scan,";return false;},[&](int){calls+="rand,";return draw;});
            if (s.waitMask&1) calls+="sleep"+std::to_string(s.deadline-1000)+",";
            std::printf("%d %d %d %d %s\n",result,int(s.stage),gx,gz,calls.empty()?"-":calls.c_str());
        }
        std::fflush(stdout);
    }
}
