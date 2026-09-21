// Command-level AI policy tests; movement continues through the normal World API.
#include "ai/ai.h"
#include "sim/matchsetup.h"
#include "hpi/hpi.h"
#include <cstdio>
#include <algorithm>
namespace tak::ai {
struct PlacementProbe {
    static bool place(const Controller& c,const sim::World& w,const sim::UnitType* t,
                      const sim::Unit& builder,float& x,float& z) {
        return c.placeSite(w,t,builder,800,800,x,z);
    }
};
}
using namespace tak;
static int fails=0;
static void check(bool ok,const char* label) {
    std::printf("[%s] %s\n",ok ? "PASS" : "FAIL",label);if (!ok) ++fails;
}
static sim::UnitType fighter() {
    sim::UnitType t;t.id="fighter";t.maxHp=100;t.maxVel=sim::Fixed::fromInt(2);
    t.canMove=true;t.footX=t.footZ=2;t.turnRate=t.turnInPlaceRate=1000;
    t.weapon.damage=10;t.weapon.range=120;t.weapons={t.weapon};
    t.defaultMove=1;t.defaultFire=2;t.buildTime=1;return t;
}
static void terrain(sim::World& w,bool islands=false) {
    std::vector<uint8_t> h(256*256,100);
    if (islands) for (int z=0;z<256;++z) for (int x=96;x<160;++x) h[z*256+x]=0;
    w.setVisPlayer(-1);w.setTerrain(std::move(h),256,256,20);
}
static std::vector<net::Command> think(ai::Controller& ai,sim::World& w,uint32_t tick=0) {
    std::vector<net::Command> out;ai.tick(w,tick,[&](const auto& c){out.push_back(c);});return out;
}
static int attacks(const std::vector<net::Command>& cs) {
    return int(std::count_if(cs.begin(),cs.end(),[](const auto& c){return c.kind==net::Cmd::AttackMove || c.kind==net::Cmd::Attack;}));
}
int main(int argc,char** argv) {
    sim::TypeRegistry empty;ai::Profile profile;auto soldier=fighter();
    for (auto difficulty : {ai::Difficulty::Passive,ai::Difficulty::Normal,ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
        sim::World w;terrain(w);w.player(0).income=100;
        for (int i=0;i<35;++i) w.spawn(&soldier,400+float(i%7)*40,800+float(i/7)*40,0,0);
        ai::Controller controller(0,empty,profile,1,difficulty,{{3400,800}});
        auto cs=think(controller,w);
        check(attacks(cs)==(difficulty==ai::Difficulty::Passive ? 0 : 35),"difficulty permits heavy attacks only for attacking AIs");
        if (difficulty==ai::Difficulty::Passive) {
            check(std::count_if(cs.begin(),cs.end(),[](const auto& c){return c.kind==net::Cmd::Stance && c.targetId==1;})==35,
                  "Passive AI orders defensive rather than hold-fire stances");
        }
    }
    for (float income : {20.f,200.f}) {
        sim::World w;terrain(w);w.player(0).income=income;
        for (int i=0;i<25;++i) w.spawn(&soldier,400+float(i)*32,800,0,0);
        ai::Controller controller(0,empty,profile,1,ai::Difficulty::Normal,{{3400,800}});
        check(attacks(think(controller,w))==(income==20 ? 25 : 1),"actual income scales the force saved for a heavy attack");
    }
    {
        sim::World w;terrain(w);w.player(0).income=100;
        for (int i=0;i<14;++i) w.spawn(&soldier,400+float(i)*32,800,0,0);
        ai::Controller controller(0,empty,profile,1,ai::Difficulty::Normal,{{3400,800}});
        int largest=0,probes=0;
        for (int wave=0;wave<40 && largest<30;++wave) {
            auto cs=think(controller,w,uint32_t(wave*750));int n=attacks(cs);
            largest=std::max(largest,n);if (n>0 && n<30) probes+=n;
            for (const auto& c:cs) sim::applyCommand(w,empty,c);
            w.spawn(&soldier,400,800+float(wave)*32,0,0);
        }
        check(probes>0 && probes<=10,"probing raids preserve the accumulating home force");
        check(largest>=30,"repeated probing still allows a heavy attack to form");
    }
    {
        sim::World w;terrain(w,true);auto air=soldier;air.canFly=true;air.id="air";
        int ground=w.spawn(&soldier,400,800,0,0);int flyer=w.spawn(&air,400,1000,0,0);
        int unfinished=w.spawn(&air,400,1200,0,0);w.unit(unfinished)->underConstruction=true;
        ai::Controller controller(0,empty,profile,1,ai::Difficulty::Normal,{{3400,800}});
        auto cs=think(controller,w);
        check(attacks(cs)==1 && cs[0].unitId==flyer,"island probe uses a reachable flyer, not stranded or unfinished units");
        check(w.unit(ground)->orders.empty(),"AI terrain assessment does not mutate navigation");
    }
    {
        sim::World w;terrain(w,true);auto boat=soldier;boat.domain=sim::UnitType::Domain::Water;
        boat.weapon.range=240;
        int ship=w.spawn(&boat,2000,800,0,0);
        ai::Controller controller(0,empty,profile,1,ai::Difficulty::Normal,{{2624,800}});
        const auto cs=think(controller,w);
        check(attacks(cs)==1 && cs[0].unitId==ship && cs[0].x<2560,
              "naval attacks choose reachable water within range of the enemy shore");
    }
    {
        sim::World w;terrain(w);
        int a=w.spawn(&soldier,400,800,0,0),b=w.spawn(&soldier,450,800,0,1);
        w.attack(a,b,false);w.unit(a)->orders.front().autoTarget=true;
        w.setStance(a,2);
        check(w.unit(a)->orders.empty() && w.unit(a)->fireState==0,"Passive cancels an existing automatic engagement");
        w.attack(a,b,false);w.setStance(a,2);
        check(!w.unit(a)->orders.empty() && w.unit(a)->orders.front().targetId==b,"Passive retains an explicit player attack");
        w.player(0).defensiveAi=true;int d=w.spawn(&soldier,800,800,0,0);
        check(w.unit(d)->moveState==0 && w.unit(d)->fireState==2,"Passive AI units defend from their first tick");
        for (auto defaults : {std::pair{0,0},std::pair{0,2},std::pair{1,2}}) for (bool placed : {false,true}) {
            sim::World prod;terrain(prod);auto output=soldier;
            output.defaultMove=defaults.first;output.defaultFire=defaults.second;
            auto factory=soldier;if (!placed) factory.maxVel={};
            factory.isBuilder=true;factory.workerTime=1000;factory.buildDist=100;
            int f=prod.spawn(&factory,800,800,0,0);
            if (placed) prod.startBuild(f,&output,880,800,sim::World::Approach::None);
            else prod.train(f,&output,1);
            for (int t=0;t<60;++t) prod.tick(1.f/30.f);
            const sim::Unit* built=nullptr;for (const auto& u:prod.units()) if (u.type==&output) built=&u;
            check(built && !built->underConstruction && built->moveState==defaults.first && built->fireState==defaults.second,
                  "completed factory production and placed conjuring retain type standing orders");
        }
    }
    for (int source=0;source<4;++source) {
        double earned[2]{};
        for (int bonus=0;bonus<2;++bonus) {
            sim::World w;terrain(w);auto builder=soldier;
            builder.isBuilder=true;builder.canReclaim=true;builder.storage=10000;
            builder.weapons.clear();builder.weapon.damage=0;
            builder.income=source==0 ? 10 : 0;
            int id=w.spawn(&builder,800,800,0,0);
            w.player(0).mana=0;w.player(0).manaMult=bonus ? 2 : 1;
            if (source==1) {
                w.addFeature(101,840,800,60,60,1,1,false);
                w.reclaim(id,101,false);
            }
            if (source>=2) {
                sim::FeatType corpse;corpse.energy=60;corpse.reclaimable=true;
                w.setFeatureTypes({corpse});w.mapCorpse(&soldier,0);
                int dead=w.spawn(&soldier,840,800,0,1);
                auto* u=w.unit(dead);u->hp={};u->deadFor=120;u->corpseUntil=2000;
                u->corpseWork=sim::Fixed::fromInt(60);
                if (source==2) w.reclaim(id,-dead,false);
            }
            for (int tick=0;tick<240;++tick) w.tick(1.f/30.f);
            earned[bonus]=w.player(0).mana;
        }
        check(earned[0]>0 && std::abs(earned[1]-earned[0]*2)<0.001,
              "Absurd doubles recurring, feature, ordered-corpse and automatic-corpse income");
    }
    if (argc>1) {
        auto vfs=hpi::mountRetailRoot(argv[1]);
        for (bool crusades : {false,true}) {
            sim::TypeRegistry reg;sim::setupRegistry(reg,vfs,crusades);
            for (const char* monarch : sim::kMonarchs) {
                auto builderType=*reg.find(monarch);builderType.canFly=true;
                for (auto shape : {std::pair{2,2},std::pair{7,12},std::pair{12,7}})
                for (float offset : {0.f,32.f,64.f}) {
                    sim::World w;terrain(w);w.buildNavClasses(reg);
                    const int builderId=w.spawn(&builderType,800,800,0,0);
                    sim::UnitType building;building.id=shape.first==7 ? "arakeep" : "reserved-site-test";
                    building.side=builderType.side;building.maxHp=100;building.maxVel=sim::Fixed();
                    building.footX=shape.first;building.footZ=shape.second;
                    ai::Controller controller(0,reg,profile,1,ai::Difficulty::Normal);
                    float x=0,z=0;
                    check(ai::PlacementProbe::place(controller,w,&building,*w.unit(builderId),x,z),
                          "AI placement baseline exists without mana spots");
                    // The candidate would cover this deposit; the planner must
                    // choose another site, preserving every faction's upgrade size.
                    const float mx=x+offset,mz=z;
                    w.setManaSpots({{mx,mz}});
                    check(ai::PlacementProbe::place(controller,w,&building,*w.unit(builderId),x,z),
                          "AI finds another site instead of occupying a mana spot");
                    w.spawn(&building,x,z,0,0);
                    w.unit(builderId)->deadFor=0; // remove temporary mobile occupancy
                    for (const auto& [id,lode]:reg.types())
                        if (lode.onMana && lode.isStructure() && lode.side==builderType.side)
                            check(w.canPlace(&lode,mx,mz),
                                  "AI structure preserves space for lodestones and upgrades");
                }
            }
            {
                // Absurd starts with enough income to pick a Keep before a
                // lodestone. Its first short-side approach used to walk the
                // Monarch into the placement exclusion and retry forever.
                sim::World w;w.setVisPlayer(-1);
                sim::MatchConfig config;config.vfs=&vfs;
                config.mapPath=hpi::findMap(vfs,"ulasem arena");
                config.slots={{true,0,0,1.0f},{true,0,1,2.0f,true,false}};
                const auto starts=sim::setupMatch(w,reg,config);
                const auto openingProfile=ai::loadProfile(vfs);
                ai::Controller controller(1,reg,openingProfile,0x1234,
                                          ai::Difficulty::Absurd,{starts.front()});
                bool started=false;
                for (uint32_t tick=0;tick<900 && !started;++tick) {
                    controller.tick(w,tick,[&](const auto& command){sim::applyCommand(w,reg,command);});
                    w.tick(1.f/30.f);
                    started=std::any_of(w.units().begin(),w.units().end(),[](const auto& unit) {
                        return unit.alive() && unit.player==1 && unit.type && unit.type->id=="arakeep";
                    });
                }
                check(started,"Absurd Aramon starts its first Keep without repeating rejected approaches");
            }
            for (auto difficulty : {ai::Difficulty::Passive,ai::Difficulty::Normal,ai::Difficulty::Hard}) {
                sim::World w;terrain(w);w.buildNavClasses(reg);w.player(0).mana=10000;
                w.setManaSpots({{2800,800}});
                auto king=*reg.find("araking");king.commander=false; // expansion builder using the same menu
                w.spawn(&king,400,800,0,0);
                ai::Profile p;p.weight["aralode"]=100;
                ai::Controller controller(0,reg,p,1,difficulty,{{3400,800}});
                auto cs=think(controller,w);
                bool build=std::any_of(cs.begin(),cs.end(),[](const auto& c){return c.kind==net::Cmd::Build;});
                check(build==(difficulty==ai::Difficulty::Hard),"Hard expands beyond the normal and defensive home limits");
            }
            {
                sim::World w;terrain(w);w.buildNavClasses(reg);
                w.spawn(reg.find("arakeep"),800,800,0,0);
                const int guard=w.spawn(reg.find("araarch"),800,880,0,0);
                ai::Profile p;
                ai::Controller controller(0,reg,p,1,ai::Difficulty::Passive,{{3400,800}});
                const auto cs=think(controller,w);
                check(std::any_of(cs.begin(),cs.end(),[&](const auto& c){
                    return c.kind==net::Cmd::Move && c.unitId==guard &&
                        (c.x-800)*(c.x-800)+(c.z-800)*(c.z-800)<=600*600;
                }) && attacks(cs)==0,"defensive AI clears factory exits with local moves, never attacks");
            }
            for (const auto& fixture : {std::array{"cresage","cresmit","creauto"},
                                       std::array{"araking","arakeep","araarch"},
                                       std::array{"tarnecro","tardung","tararch"},
                                       std::array{"vermage","verkeep","versword"}}) {
                const auto* builderType=reg.find(fixture[0]);
                const auto* factoryType=reg.find(fixture[1]);
                const auto* outputType=reg.find(fixture[2]);
                if (!builderType || !factoryType || !outputType) {
                    check(false,"factory orientation fixture types exist");continue;
                }
                sim::World w;terrain(w);w.buildNavClasses(reg);w.player(0).mana=100000;
                const float reach=float(std::max(factoryType->footX,factoryType->footZ))*8+24;
                const int builder=w.spawn(builderType,1000,800+reach,0,0);
                const int factory=w.startBuild(builder,factoryType,1000,800,sim::World::Approach::None);
                check(factory!=0,"factory construction accepts a reachable site");
                if (factory) {
                    for (int tick=0;tick<9000 && w.unit(factory)->underConstruction;++tick) {
                        w.player(0).mana=100000;w.tick(1.f/30.f);
                    }
                    check(!w.unit(factory)->underConstruction,"factory finishes construction");
                    w.order(builder,700,800,false);
                    for (int tick=0;tick<180;++tick) w.tick(1.f/30.f);
                    w.train(factory,outputType,1);
                    for (int tick=0;tick<600;++tick) { w.player(0).mana=100000;w.tick(1.f/30.f); }
                    bool produced=false;
                    for (const auto& u:w.units()) if (u.type==outputType) produced=true;
                    std::printf("  factory %s: ",fixture[1]);
                    check(produced,"new construction produces through its script-oriented yard");
                }
            }
            {
                const sim::UnitType* factory=nullptr;const sim::UnitType* ship=nullptr;
                for (const auto& [id,type]:reg.types()) {
                    if (!type.isStructure()) continue;
                    for (const auto& child:reg.buildable(id)) {
                        const auto* t=reg.find(child);
                        if (t && !t->isStructure() && !t->isBuilder && t->domain==sim::UnitType::Domain::Water && t->weapon.damage>0) {
                            factory=&type;ship=t;break;
                        }
                    }
                    if (factory) break;
                }
                check(factory && ship,"retail registry supplies a naval production fixture");
                if (factory && ship) for (bool water : {false,true}) {
                    sim::World naval;terrain(naval,water);naval.buildNavClasses(reg);
                    naval.player(0).mana=100000;naval.player(0).income=100;
                    naval.spawn(factory,2000,800,0,0);
                    ai::Profile p;p.weight[ship->id]=100;
                    ai::Controller controller(0,reg,p,1,ai::Difficulty::Normal,{{2624,800}});
                    const auto cs=think(controller,naval);
                    bool train=std::any_of(cs.begin(),cs.end(),[](const auto& c){return c.kind==net::Cmd::Train;});
                    check(train==water,"naval production requires usable water and an accessible enemy shore");
                }
            }
            sim::World w;terrain(w);w.buildNavClasses(reg);
            bool defaults=true;
            for (const auto& [id,type]:reg.types()) {
                int uid=w.spawn(&type,800,800,0,0);const auto* u=w.unit(uid);
                defaults &= u->moveState==type.defaultMove && u->fireState==type.defaultFire;
            }
            check(defaults,"all shipped units receive their retail stance defaults in this balance");
        }
    }
    std::printf("ai_behavior_test: %d failures\n",fails);return fails ? 1 : 0;
}
