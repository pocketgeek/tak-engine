// legion_group_motion -- visible-motion quality of group move orders with REAL
// unit types (retail turn rates, speeds and footprints), any path mode.
//
//   legion_group_motion DATA MODE SCENARIO UNITS [TICKS]
//     MODE      retail | retail-plus | flowfield | cooperative | legion
//     SCENARIO  open (straight across open ground) | wall (around a wall)
//               | cross (two groups ordered through each other)
//
// Per moving unit and tick it measures what a player sees: heading changes
// while moving, motion sideways (>45 deg) or backward (>90 deg) to the facing,
// stop-go (moving -> stopped -> moving before arrival), order-to-first-motion
// latency, and heading-turn reversals (zig-zag / wobble). Prints one JSON line.
#include "crowdbench_matrix.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"

#include <cstdio>
#include <string>

using namespace tak;
using namespace tak::sim;

int main(int argc,char** argv) {
    if(argc<5) {std::fprintf(stderr,"usage: legion_group_motion DATA MODE open|wall|cross UNITS [TICKS]\n");return 2;}
    const std::string mode=argv[2],scenario=argv[3];
    const int count=std::atoi(argv[4]),ticks=argc>5?std::atoi(argv[5]):2400;
    auto vfs=hpi::mountRetailRoot(argv[1],hpi::OverridePolicy::None);
    TypeRegistry registry;setupRegistry(registry,vfs,true);
    // A deterministic spread of ordinary ground units (name order, every
    // fifth eligible type), weapons stripped so nobody fights.
    std::vector<UnitType> types;int eligible=0;
    for(const auto& [name,t]:registry.types()) {
        if(t.isStructure()||t.canFly||t.floater||t.canHover||t.minWaterDepth>0||t.footX>3||t.footZ>3||!t.canMove)continue;
        if(eligible++%5)continue;
        auto copy=t;copy.weapon.damage=0;copy.weapons.clear();types.push_back(copy);
        if(types.size()==12)break;
    }
    const int W=256,H=256;
    World world;world.setGameSeed(1);world.setVisPlayer(-1);world.setSerialThreads(true);world.setPathService(true);
    world.setPathfindingMode(PathfindingMode(mode=="retail"?0:mode=="flowfield"?1:mode=="cooperative"?2:mode=="retail-plus"?3:4));
    world.setPlayerCount(1);world.setTeam(0,0);
    world.setTerrain(std::vector<uint8_t>(size_t(W)*H,100),W,H,64);
    std::vector<crowdbench_matrix::Rect> walls;
    if(scenario=="wall")walls.push_back({124,84,6,88});
    crowdbench_matrix::barriers(world,W,H,{},walls);
    struct Track {int id=0;int gx=0,gz=0;int firstMove=-1;bool moving=false,wasMoving=false;int32_t lastTurn=0;
                  uint16_t lastHeading=0;int32_t lastX=0,lastZ=0;};
    std::vector<Track> tracks;
    const bool cross=scenario=="cross";
    for(int k=0;k<count;++k) {
        const bool second=cross&&k%2;
        const int slot=cross?k/2:k;
        const int perRow=8,col=slot/perRow,row=slot%perRow;
        const auto& type=types[size_t(k)%types.size()];
        const int cx=second?200+col*4:56-col*4,cz=112+row*4;
        Track t;t.id=world.spawn(&type,float(cx*16),float(cz*16),std::nullopt,0);
        if(t.id<=0) {std::fprintf(stderr,"spawn failed\n");return 1;}
        t.gx=second?40*16:216*16;t.gz=128*16;tracks.push_back(t);
    }
    world.tick(1.f/30);crowdbench_matrix::known(world);
    for(auto& t:tracks) {
        world.order(t.id,float(t.gx),float(t.gz),false);
        const auto& u=*world.unit(t.id);t.lastHeading=uint16_t(u.heading.v);t.lastX=u.x.v;t.lastZ=u.z.v;
    }
    uint64_t movingTicks=0,headingTicks=0,turnSum=0,sideways=0,backward=0,stopGo=0,reversals=0,firstMoveSum=0,
             neverMoved=0,arrived=0,bigTurn=0;
    for(int tick=1;tick<=ticks;++tick) {
        world.tick(1.f/30);
        for(auto& t:tracks) {
            const Unit* u=world.unit(t.id);if(!u)continue;
            const int64_t dx=int64_t(u->x.v)-t.lastX,dz=int64_t(u->z.v)-t.lastZ;
            const bool moved=dx||dz;
            const int32_t dh=int32_t(int16_t(uint16_t(u->heading.v)-t.lastHeading));
            if(moved) {
                ++movingTicks;
                if(t.firstMove<0) {t.firstMove=tick;firstMoveSum+=uint64_t(tick);}
                if(t.wasMoving==false&&t.moving)++stopGo;
                t.moving=true;
                // Motion direction against the facing, in port BAM.
                const auto motion=retailHeadingToPort(uint16_t(retailDirection(Fixed::raw(int32_t(-dx)),Fixed::raw(int32_t(-dz))).v));
                const int32_t off=std::abs(retailTurnRequest(motion,u->heading));
                // Ignore sub-pixel nudges: only real travel counts.
                if(dx*dx+dz*dz>int64_t(16384)*16384) {
                    if(off>8192)++sideways;
                    if(off>16384)++backward;
                }
            }
            if(dh) {
                ++headingTicks;turnSum+=uint64_t(std::abs(dh));
                if(std::abs(dh)>2048)++bigTurn;
                if(t.lastTurn&&((t.lastTurn>0)!=(dh>0)))++reversals;
                t.lastTurn=dh;
            }
            t.wasMoving=moved;
            t.lastHeading=uint16_t(u->heading.v);t.lastX=u->x.v;t.lastZ=u->z.v;
        }
    }
    for(auto& t:tracks) {
        const Unit* u=world.unit(t.id);
        if(t.firstMove<0)++neverMoved;
        if(u&&std::abs(u->x.toFloat()-float(t.gx))<160&&std::abs(u->z.toFloat()-float(t.gz))<160&&u->orders.empty())++arrived;
    }
    std::printf("{\"mode\":\"%s\",\"scenario\":\"%s\",\"units\":%d,\"ticks\":%d,\"types\":%zu,\"arrived\":%llu,"
                "\"never_moved\":%llu,\"first_move_mean\":%.1f,\"moving_ticks\":%llu,\"heading_change_ticks\":%llu,"
                "\"turn_bam_total\":%llu,\"big_turn_ticks\":%llu,\"turn_reversals\":%llu,\"sideways_ticks\":%llu,"
                "\"backward_ticks\":%llu,\"stop_go\":%llu}\n",
                mode.c_str(),scenario.c_str(),count,ticks,types.size(),(unsigned long long)arrived,
                (unsigned long long)neverMoved,count>int(neverMoved)?double(firstMoveSum)/(count-int(neverMoved)):0.0,
                (unsigned long long)movingTicks,(unsigned long long)headingTicks,(unsigned long long)turnSum,
                (unsigned long long)bigTurn,(unsigned long long)reversals,(unsigned long long)sideways,
                (unsigned long long)backward,(unsigned long long)stopGo);
    if(std::getenv("TAK_MOTION_TYPES"))for(const auto& t:types)
        std::fprintf(stderr,"%s foot %dx%d maxVel %d turn %d inplace %d\n",t.id.c_str(),t.footX,t.footZ,t.maxVel.v,t.turnRate,t.turnInPlaceRate);
    return 0;
}
