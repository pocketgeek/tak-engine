// conjure_test -- the Zhon monarch's (zonhunt) conjure animation, driven through the
// real COB VM the way the client does. The conjure gesture (RestoreWatcher looping
// `build`) and the airborne body pose (FlightControl -> `attack`) both gate on the
// ACTIVE static (6), which the client sets via setSFXoccupy(5) while the flyer works.
// This pins that the arms actually move -- and that they FREEZE if the flyer is not
// kept active, which is the failure the engine's altitude `busy` test guards against
// (traced with tools/re/emuphase.py + cobtool; verified live driving the Vm).
//
//   conjure_test <retail-install-dir>
#include "hpi/hpi.h"
#include "cob/cob.h"
#include "cob/vm.h"
#include "sim/matchsetup.h"
#include <cstdio>
#include <algorithm>
#include <memory>
#include <set>
#include <string>

using namespace tak;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

static void checkStopHover(sim::World& world, const sim::TypeRegistry& registry, int id) {
    net::Command command{};
    command.kind=net::Cmd::Stop;command.player=0;command.unitId=id;
    sim::applyCommand(world,registry,command);
    for (int tick=0;tick<30;++tick) world.tick(1.0f/30.0f);
    const auto* builder=world.unit(id);
    check(builder && !builder->buildSiteId && !builder->productionSiteId &&
          !builder->conjureHoverTarget && !builder->conjureHoverGoal,
          "Stop detaches the conjurer from its site and clears the hover controller");
    // A stopped flyer may install an ordinary landing order.
    check(builder && !builder->repeatType && builder->buildQueue.empty() &&
          std::none_of(builder->orders.begin(),builder->orders.end(),
                       [](const sim::Order& order) { return order.buildType!=nullptr; }),
          "stopped conjurer does not restart the canceled job");
}

// Does any arm/hand piece animate over `frames` ticks?
static bool armMoves(cob::Vm& vm, int frames) {
    const cob::File& f = vm.file();
    static const std::set<std::string> arms =
        {"armup_L", "armup_R", "armlow_L", "armlow_R", "hand_R", "hand_L"};
    for (int i = 0; i < frames; ++i) {
        vm.tick(1.0f / 30.0f);
        for (size_t p = 0; p < vm.pieces().size() && p < f.pieces.size(); ++p) {
            if (!arms.count(f.pieces[p])) continue;
            const auto& ps = vm.pieces()[p];
            for (int a = 0; a < 3; ++a)
                if (ps.moving[a] || ps.turning[a] || ps.spin[a] != 0.0f) return true;
        }
    }
    return false;
}

static std::shared_ptr<const cob::File> load(const hpi::Vfs& vfs) {
    return std::make_shared<const cob::File>(
        cob::load(vfs.read("scripts/zonhunt.cob"), "zonhunt.cob"));
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: conjure_test <install>\n"); return 2; }
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    auto f = load(vfs);
    for (bool crusades : {false,true}) {
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,crusades);
        for (const auto& [name,type]:registry.types()) {
            const auto& menu=registry.buildable(name);
            if (type.isStructure() || menu.empty()) continue;
            const auto* output=registry.find(menu.front());
            if (!output) continue;
            sim::World world;world.setVisPlayer(-1);
            world.setTerrain(std::vector<uint8_t>(128*128,100),128,128,20);
            const int id=world.spawn(&type,800,800,0,0);
            const int enemy=world.spawn(registry.find("zonter"),1200,1200,0,1);
            world.order(id,1500,1500,false);
            net::Command c{};c.unitId=id;c.player=0;c.kind=net::Cmd::RepeatTrain;
            std::snprintf(c.type,sizeof(c.type),"%s",output->id.c_str());
            sim::applyCommand(world,registry,c);
            check(world.unit(id)->repeatType==output,"mobile infinite production starts through command path");
            check(world.unit(id)->orders.empty(),"infinite production retires previous movement orders");
            std::printf("infinite producer=%s balance=%d\n",name.c_str(),int(crusades));
            for (bool emptyQueue : {false,true}) {
                if (emptyQueue) world.unit(id)->buildQueue.clear();
                // Resource/output stalls and between-item gaps must remain locked.
                world.player(0).mana=0;
                const auto before=world.stateHash();
                world.unit(id)->repeatType=nullptr;
                check(world.stateHash()!=before,"infinite production lock participates in lockstep hash");
                world.unit(id)->repeatType=output;
                for (int kind=0;kind<=int(net::Cmd::SetSquad);++kind) {
                    c.kind=net::Cmd(kind);if (c.kind==net::Cmd::Stop) continue;
                    c.targetId=enemy;c.x=1500;c.z=1500;
                    for (int queued : {0,1}) {
                        c.queue=uint8_t(queued);sim::applyCommand(world,registry,c);
                        check(world.stateHash()==before && world.unit(id)->repeatType==output,
                              "only Stop is accepted during infinite mobile production");
                    }
                }
            }
            c.kind=net::Cmd::Stop;c.player=1;sim::applyCommand(world,registry,c);
            check(world.unit(id)->repeatType==output,"another player cannot unlock the producer");
            c.player=0;sim::applyCommand(world,registry,c);
            check(!world.unit(id)->repeatType && world.unit(id)->buildQueue.empty(),
                  "Stop clears infinite production");
            // Same command batch: do not require a later tick/render snapshot to unlock.
            c.kind=net::Cmd::Move;c.queue=0;sim::applyCommand(world,registry,c);
            check(!world.unit(id)->orders.empty(),"Move immediately after Stop works normally");
            c.kind=net::Cmd::Stop;sim::applyCommand(world,registry,c);
            c.kind=net::Cmd::Train;c.targetId=1;sim::applyCommand(world,registry,c);
            c.kind=net::Cmd::Move;sim::applyCommand(world,registry,c);
            check(!world.unit(id)->orders.empty() && !world.unit(id)->repeatType,
                  "finite production does not lock mobile orders");
        }
        sim::World world;world.setVisPlayer(-1);
        const int id=world.spawn(registry.find("arakeep"),800,800,0,0);
        world.setRepeat(id,registry.find("arasword"));
        net::Command c{};c.unitId=id;c.kind=net::Cmd::Move;c.x=1200;c.z=1200;
        sim::applyCommand(world,registry,c);
        check(!world.unit(id)->rally.empty(),"stationary infinite factories retain rally commands");
    }
    for (bool crusades : {false,true}) {
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,crusades);
        const auto* monarch=registry.find("zonhunt");
        const auto* hunter=registry.find("zonter");
        if (!monarch || !hunter) return 2;
        sim::UnitType output=*hunter;output.buildTime=10000;
        for (int heading : {0,16384,32768,49152}) {
            sim::World world;world.setVisPlayer(-1);
            world.setTerrain(std::vector<uint8_t>(128*128,100),128,128,20);
            world.buildNavClasses(registry);
            const int id=world.spawn(monarch,1000,1000,sim::radiansFromBam(sim::Bam(heading)),0);
            world.train(id,&output,1);
            if (heading==0) world.setRepeat(id,&output);
            for (int tick=0;tick<90;++tick) world.tick(1.0f/30.0f);
            if (heading==0) {
                const auto before=world.stateHash();
                net::Command c{};c.unitId=id;c.x=1500;c.z=1500;
                c.kind=net::Cmd::Move;sim::applyCommand(world,registry,c);
                c.kind=net::Cmd::Build;std::snprintf(c.type,sizeof(c.type),"zonlode");
                sim::applyCommand(world,registry,c);
                check(world.stateHash()==before,"rejected orders preserve active conjure site and hover state");
            }
            const auto* builder=world.unit(id);
            const auto* site=world.unit(builder->productionSiteId);
            check(site && site->underConstruction,"queued Hunter remains under construction");
            if (site) {
                const auto wanted=sim::retailHeadingToPort(uint16_t(sim::retailDirection(builder->x-site->x,builder->z-site->z).v));
                check(std::abs(sim::bamDiff(wanted,builder->heading))<8192,
                      crusades ? "Crusades Zhon monarch faces her conjured unit" : "standard Zhon monarch faces her conjured unit");
                check(builder->conjureHoverTarget==site->id,
                      "queued conjurer uses the active site's hover controller");
            }
            checkStopHover(world,registry,id);
        }
    }
    for (bool crusades : {false,true}) {
        sim::TypeRegistry registry;sim::setupRegistry(registry,vfs,crusades);
        const auto* monarch=registry.find("zonhunt");
        sim::UnitType output=*registry.find("zonter");output.buildTime=10000;
        for (const auto [dx,dz] : {std::pair{128,0},std::pair{0,128},std::pair{-128,0},std::pair{0,-128},
                                  std::pair{128,128},std::pair{-128,128},std::pair{-128,-128},std::pair{128,-128},
                                  std::pair{64,0},std::pair{0,64}}) {
            sim::World world;world.setVisPlayer(-1);
            world.setTerrain(std::vector<uint8_t>(128*128,100),128,128,20);
            world.buildNavClasses(registry);
            const int id=world.spawn(monarch,1000,1000,0,0);
            world.queueBuild(id,&output,float(1000+dx),float(1000+dz),false);
            check(!world.unit(id)->orders.empty(),"placed conjure accepts the selected site");
            std::set<std::pair<int,int>> hoverPositions;
            for (int tick=0;tick<300;++tick) {
                world.tick(1.0f/30.0f);
                const auto* b=world.unit(id);
                if (tick>90 && b->buildSiteId)
                    hoverPositions.emplace(b->x.floorInt(),b->z.floorInt());
            }
            check(hoverPositions.size()>8,"placed conjurer moves around the site while working");
            const auto* builder=world.unit(id);
            const auto* site=world.unit(builder->buildSiteId);
            check(site && site->underConstruction,"placed Hunter remains under construction");
            if (site) {
                const auto wanted=sim::retailHeadingToPort(uint16_t(sim::retailDirection(builder->x-site->x,builder->z-site->z).v));
                const float distance=sim::fxLen(builder->x-site->x,builder->z-site->z).toFloat();
                check(std::abs(distance-float(monarch->buildDist))<24.0f,
                      "placed flying conjurer holds its build distance from the site");
                check(std::abs(sim::bamDiff(wanted,builder->heading))<8192,"placed conjure faces the actual site");
                check(builder->conjureHoverTarget==site->id,"hover controller stays attached to the placed site");
            }
            checkStopHover(world,registry,id);
        }
    }
    check(f->scriptIndex("RestoreWatcher") >= 0 && f->scriptIndex("FlightControl") >= 0 &&
              f->scriptIndex("build") >= 0,
          "zonhunt carries the conjure threads (RestoreWatcher/FlightControl/build)");

    std::printf("[conjure while ACTIVE -- the client's normal case]\n");
    {
        cob::Vm vm(f);
        vm.start("Create");
        for (int i = 0; i < 30; ++i) vm.tick(1.0f / 30.0f);
        vm.start("setSFXoccupy", {5});   // active (static 6) -- client sets this while busy
        vm.start("BeginFlight");
        for (int i = 0; i < 30; ++i) vm.tick(1.0f / 30.0f);
        vm.start("StartBuilding");
        check(armMoves(vm, 120), "the conjure arm gesture plays (build loops via RestoreWatcher)");
        std::set<int> wingPoses, armPoses;
        for (int i=0;i<180;++i) {
            vm.tick(1.0f/30.0f);
            wingPoses.insert(int(vm.pieces()[26].rot[2]*10000));
            armPoses.insert(int(vm.pieces()[7].rot[1]*10000));
        }
        check(wingPoses.size()>8,"conjuring repeats the airborne wing flap");
        check(armPoses.size()>8,"conjuring repeats the arm gesture throughout the job");
    }

    std::printf("[conjure while NOT active -- documents the freeze the busy test prevents]\n");
    {
        cob::Vm vm(f);
        vm.start("Create");
        for (int i = 0; i < 30; ++i) vm.tick(1.0f / 30.0f);
        vm.start("setSFXoccupy", {0});   // NOT active (static 6 clear)
        for (int i = 0; i < 30; ++i) vm.tick(1.0f / 30.0f);
        vm.start("StartBuilding");
        check(!armMoves(vm, 120),
              "with the flyer inactive the arms FREEZE (why the engine keeps it airborne)");
    }

    std::printf("conjure_test: %s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
