// World::clone / World::restoreFrom: a copy must be exact and separate. Exact means it
// runs on as the original would, hash for hash, on every tick; separate means neither
// can reach the other afterwards. Replay seeking stands on both (client/replaybar.h).
#include "navigation_test_common.h"
#include "hpi/hpi.h"
#include "sim/legion.h"
#include "sim/matchsetup.h"
#include "sim/mission.h"
#include <cstdlib>
#include <memory>
#include <random>
#include <string>

using namespace navigation_test;

namespace {
void lockstep(World& a,World& b,int ticks,const char* what) {
    for(int t=0;t<ticks;++t) {
        a.tick(1.f/30);b.tick(1.f/30);
        if(a.stateHash()!=b.stateHash())
            throw std::runtime_error(std::string(what)+": diverged "+std::to_string(t+1)+" ticks after the copy");
    }
}
// Orders that keep routes in play: fields, chains, shared goals and regrouping.
void shuffleOrders(World& world,const std::vector<int>& ids,std::mt19937& rng,int cells) {
    const int groups=4;
    for(int g=0;g<groups;++g) {
        const float x=float(16+rng()%uint32_t(cells-32))*16,z=float(16+rng()%uint32_t(cells-32))*16;
        for(size_t i=size_t(g);i<ids.size();i+=size_t(groups))world.order(ids[i],x,z,false);
    }
}
struct Army {
    UnitType type=mover();
    std::vector<int> ids;
    std::mt19937 rng{1234};
};
void build(World& world,Army& army,PathfindingMode mode,int cells,int rows,bool walls) {
    world.setGameSeed(7);
    setup(world,mode,true,false,cells);
    world.setPlayerCount(2);world.setTeam(0,0);world.setTeam(1,1);
    if(walls)for(int w=1;w<4;++w) {   // three long walls with a gap each, so routes go round
        const int x=w*cells/4;
        world.blockCells(x,0,2,cells-20,true);
    }
    for(int z=0;z<rows;++z)for(int x=0;x<rows;++x)
        army.ids.push_back(world.spawn(&army.type,float((8+x*3)*16),float((8+z*3)*16),std::nullopt,(x+z)%2));
    known(world);
}

void exactAtEveryStage(PathfindingMode mode,const char* what,int cells,int rows,bool wantChains=false) {
    World world;Army army;build(world,army,mode,cells,rows,true);
    world.tick(1.f/30);
    int copies=0;
    bool chains=false;
    for(int round=0;round<14;++round) {
        if(round%3==0)shuffleOrders(world,army.ids,army.rng,cells);
        for(int t=0;t<23;++t)world.tick(1.f/30);
        if(mode==PathfindingMode::Legion)chains|=world.legionNavigator()->accounting().bytes.tiles>0;
        auto copy=world.clone();
        require(copy->stateHash()==world.stateHash(),"a copy hashes differently from its original");
        lockstep(world,*copy,40,what);
        ++copies;
    }
    require(!wantChains||chains,"the fixture never held a tile chain, so it proves nothing about them");
    std::printf("  %s: %d copies stayed in step\n",what,copies);
}

void separate(PathfindingMode mode) {
    World world;Army army;build(world,army,mode,128,4,false);
    shuffleOrders(world,army.ids,army.rng,128);
    for(int t=0;t<60;++t)world.tick(1.f/30);
    // A world that never saw what is about to happen to the original.
    World twin;Army twinArmy;build(twin,twinArmy,mode,128,4,false);
    shuffleOrders(twin,twinArmy.ids,twinArmy.rng,128);
    for(int t=0;t<60;++t)twin.tick(1.f/30);
    auto copy=world.clone();
    const uint64_t kept=copy->stateHash();
    // The original goes its own way, hard: orders, a death, a walled map, ticks.
    shuffleOrders(world,army.ids,army.rng,128);
    world.unit(army.ids.front())->hp=Fixed();
    world.blockCells(20,20,88,88,true);
    require(!world.navFor(&army.type).walkable(40,40),"the wall went up");
    require(copy->navFor(&army.type).walkable(40,40),"a copy's grid reads its original's obstacles");
    UnitType keep=mover();
    keep.canMove=false;keep.maxVel=Fixed();keep.footX=keep.footZ=24;   // stamps the shared obstacle overlay
    require(world.spawn(&keep,float(32*16),float(32*16),std::nullopt,0)>0,"the fixture's building was refused");
    for(int t=0;t<120;++t)world.tick(1.f/30);
    require(copy->stateHash()==kept,"the original reached into its copy");
    const uint64_t farAhead=world.stateHash();
    // Routes asked for now read the maps: the copy's own, as in the twin, not the original's.
    std::mt19937 copyOrders{5},twinOrders{5};
    shuffleOrders(*copy,army.ids,copyOrders,128);
    shuffleOrders(twin,twinArmy.ids,twinOrders,128);
    lockstep(*copy,twin,150,"a copy after its original changed");
    require(world.stateHash()==farAhead,"the copy reached into its original");
    // And the original is unhurt: it still runs in step with a copy of itself.
    auto again=world.clone();
    lockstep(world,*again,60,"after the copies separated");
}

// A copy of a copy, its source then freed and the memory reused: nothing of the source
// may still be read. (World holds no such pointer; a Legion route's tiles did, once.)
void outlivesItsSource() {
    World world;Army army;build(world,army,PathfindingMode::Legion,1024,5,true);
    world.tick(1.f/30);
    bool chains=false;
    for(int round=0;round<8;++round) {
        if(round%3==0)shuffleOrders(world,army.ids,army.rng,1024);
        for(int t=0;t<23;++t)world.tick(1.f/30);
        chains|=world.legionNavigator()->accounting().bytes.tiles>0;
        auto source=world.clone();
        auto copy=source->clone();
        source.reset();
        std::vector<std::vector<uint32_t>> rubble;
        for(size_t size:{size_t(1024),size_t(4096),size_t(16384),size_t(65536)})
            for(int i=0;i<256;++i)rubble.emplace_back(size/sizeof(uint32_t),0xABABABABu);
        lockstep(world,*copy,30,"after its source was freed");
    }
    require(chains,"the fixture never held a tile chain");
}

void restoreInPlace(PathfindingMode mode) {
    World world;Army army;build(world,army,mode,128,4,true);
    shuffleOrders(world,army.ids,army.rng,128);
    for(int t=0;t<50;++t)world.tick(1.f/30);
    auto keyframe=world.clone();
    const uint64_t at=world.stateHash();
    std::mt19937 rng{99};
    shuffleOrders(world,army.ids,rng,128);
    std::vector<uint64_t> straight;
    for(int t=0;t<150;++t) {world.tick(1.f/30);straight.push_back(world.stateHash());}
    // Twice from the one keyframe: a restore must leave it reusable.
    for(int pass=0;pass<2;++pass) {
        const World* address=&world;
        world.restoreFrom(*keyframe);
        require(address==&world&&world.stateHash()==at,"a restore did not land on the keyframe's state");
        rng.seed(99);
        shuffleOrders(world,army.ids,rng,128);
        for(int t=0;t<150;++t) {
            world.tick(1.f/30);
            if(world.stateHash()!=straight[size_t(t)])
                throw std::runtime_error("restored run diverged from the straight run at +"+std::to_string(t));
        }
    }
    require(keyframe->stateHash()==at,"a restore changed its keyframe");
}

void poolAndObserversStay() {
    World world;Army army;
    build(world,army,PathfindingMode::Retail,64,2,false);
    int observed=0;
    world.setRngObserver([&](const World::RngObservation&){++observed;});
    for(int t=0;t<3;++t)world.tick(1.f/30);
    require(observed>0,"the fixture draws no random numbers, so it proves nothing about observers");
    auto copy=world.clone();
    observed=0;
    for(int t=0;t<30;++t)copy->tick(1.f/30);
    require(observed==0,"a copy carried its original's observer");
    world.setRngObserver(nullptr);
    for(int t=0;t<30;++t)world.tick(1.f/30);
    copy->setSerialThreads(false);world.setSerialThreads(false);
    lockstep(world,*copy,30,"each on a thread pool of its own");
}

void missionData(const std::string& data) {
    auto vfs=tak::hpi::mountPlayVfs(data,{},{},true,nullptr);
    TypeRegistry registry;setupRegistry(registry,vfs,false);
    const char* stem="takmission22_ph";
    World world,other;int human=0,otherHuman=0;
    require(setupMission(world,registry,vfs,stem,human)&&setupMission(other,registry,vfs,stem,otherHuman),
            "no campaign mission in the test data");
    require(world.mission()&&world.mission()->ok(),"this mission has no script to copy");
    world.setSerialThreads(true);other.setSerialThreads(true);
    lockstep(world,other,300,"two loads of the mission");
    auto copy=world.clone();
    for(int t=0;t<600;++t)other.tick(1.f/30);
    lockstep(world,*copy,600,"campaign mission");
    require(copy->stateHash()==other.stateHash(),"a copy parted from a straight run of the mission");
    // With real types the walkers read the per-class grids: wall off the original and send
    // everyone across the map. The copy must walk as a world that never saw the wall does.
    world.blockCells(0,0,1<<14,1<<14,true);
    float goalX=0,goalZ=0;
    for(const auto& u:copy->units())if(u.alive()&&u.type&&u.player==human&&u.type->canMove) {
        goalX=u.x.toFloat()+900;goalZ=u.z.toFloat()+500;
        const int cx=int(u.x.toFloat())/16,cz=int(u.z.toFloat())/16;
        require(!world.navFor(u.type).walkable(cx,cz),"the wall went up");
        require(copy->navFor(u.type).walkable(cx,cz)==other.navFor(u.type).walkable(cx,cz),
                "a copy's grids read its original's obstacles");
        break;
    }
    for(World* w:{copy.get(),&other})
        for(const auto& u:w->units())
            if(u.alive()&&u.type&&u.type->canMove&&u.player==human)w->order(u.id,goalX,goalZ,false);
    lockstep(*copy,other,300,"campaign mission, after its original was walled off");
    std::printf("  mission %s stayed in step\n",stem);
}
}

int main(int argc,char** argv) {try {
    exactAtEveryStage(PathfindingMode::Retail,"retail",128,5);
    exactAtEveryStage(PathfindingMode::Legion,"legion",256,8);
    exactAtEveryStage(PathfindingMode::Legion,"legion over tile chains",1024,5,true);
    separate(PathfindingMode::Retail);
    separate(PathfindingMode::Legion);
    outlivesItsSource();
    restoreInPlace(PathfindingMode::Retail);
    restoreInPlace(PathfindingMode::Legion);
    poolAndObserversStay();
    if(argc>2&&std::string(argv[1])=="--data")missionData(argv[2]);
    std::puts("world clone passed");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"world clone: %s\n",error.what());return 1;}}
