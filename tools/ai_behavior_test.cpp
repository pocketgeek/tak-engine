// Command-level AI policy tests; movement continues through the normal World API.
#include "ai/ai.h"
#include "sim/matchsetup.h"
#include "hpi/hpi.h"
#include <cmath>
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
// Archers that outrange both our sight and the defended radius must still be
// answered: whoever is hitting a unit inside the territory is a threat.
static void defensiveShooters(const sim::TypeRegistry& registry,const ai::Profile& profile,
                              const sim::UnitType& soldier) {
    auto base=soldier;base.id="base";base.maxVel={};base.sight=300;
    base.weapon.damage=0;base.weapons.clear();
    auto archer=soldier;archer.id="archer";archer.weapon.range=450;archer.weapons={archer.weapon};
    auto blind=soldier;blind.sight=100;
    auto melee=blind;melee.id="melee";melee.weapon.range=30;melee.weapons={melee.weapon};
    for (const auto* defenderType:{&blind,&melee}) {
        const bool isMelee=defenderType==&melee;
        sim::World w;terrain(w);w.player(0).defensiveAi=true;
        w.spawn(&base,400,800,0,0);
        const int defender=w.spawn(defenderType,1000,800,0,0);   // inside the 900 home radius
        const int shooter=w.spawn(&archer,1420,800,0,1);           // 1020 from home, unseen
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive,{{3400,800}});
        check(attacks(think(defensive,w))==0,"an unseen archer outside the territory is no threat until it hits something");
        w.unit(defender)->lastHitBy=shooter;
        auto cs=think(defensive,w,30);
        check(attacks(cs)==1 && std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
            return c.unitId==defender && (isMelee ? c.kind==net::Cmd::Attack && c.targetId==shooter
                                                  : c.kind==net::Cmd::AttackMove);
        }),isMelee ? "a melee defender attacks the archer hitting it directly (chases past the Defensive stance)"
                   : "a ranged defender attack-moves on the unseen archer hitting it");
        for (const auto& c:cs) sim::applyCommand(w,registry,c);
        check(attacks(think(defensive,w,60))==0,"the response to an attacker is not re-issued every think");
        w.unit(shooter)->x=sim::Fixed::fromInt(2100);w.unit(defender)->x=sim::Fixed::fromInt(1800);
        cs=think(defensive,w,90);
        check(attacks(cs)==0 && std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
            return c.unitId==defender && c.kind==net::Cmd::Move && c.x==1000 && c.z==800;
        }),"defenders give up an attacker beyond the pursuit radius and return to their posts");
    }
    {
        sim::World w;terrain(w);w.spawn(&base,400,800,0,0);
        const int defender=w.spawn(&blind,1000,800,0,0);
        const int shooter=w.spawn(&archer,1700,800,0,1);   // out of its own reach of the victim
        w.unit(defender)->lastHitBy=shooter;
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive,{{3400,800}});
        check(attacks(think(defensive,w))==0,"a stale attacker no longer within reach of its victim is ignored");
    }
}

// The Monarch fights back instead of standing on a build/move order under fire,
// and the Defensive AI does not site buildings inside a spotted fight.
static void monarchFightsBack(const sim::TypeRegistry& registry,const ai::Profile& profile,
                              const sim::UnitType& soldier) {
    auto base=soldier;base.id="base";base.maxVel={};base.sight=750;
    base.weapon.damage=0;base.weapons.clear();
    auto monarch=soldier;monarch.id="monarch";monarch.commander=true;monarch.isBuilder=true;monarch.sight=300;
    {
        sim::World w;terrain(w);w.spawn(&base,400,800,0,0);
        const int m=w.spawn(&monarch,600,800,0,0);
        const int enemy=w.spawn(&soldier,680,800,0,1);
        net::Command mv;mv.kind=net::Cmd::Move;mv.player=0;mv.unitId=m;mv.x=900;mv.z=800;
        sim::applyCommand(w,registry,mv);
        ai::Controller ai(0,registry,profile,1,ai::Difficulty::Normal,{{3400,800}});
        auto cs=think(ai,w);
        check(std::any_of(cs.begin(),cs.end(),[&](const auto& c){
            return c.kind==net::Cmd::Attack && c.unitId==m && c.targetId==enemy;
        }),"a moving Monarch attacks an enemy within its reach");
        w.unit(enemy)->x=sim::Fixed::fromInt(2000);
        cs=think(ai,w,30);
        check(std::none_of(cs.begin(),cs.end(),[&](const auto& c){return c.kind==net::Cmd::Attack && c.unitId==m;}),
              "a Monarch with no enemy near is left to its work");
        auto blindMonarch=monarch;blindMonarch.sight=60;
        sim::World w2;terrain(w2);w2.spawn(&base,400,800,0,0);
        const int m2=w2.spawn(&blindMonarch,600,800,0,0);
        const int shooter=w2.spawn(&soldier,780,800,0,1);
        w2.unit(m2)->lastHitBy=shooter;
        ai::Controller ai2(0,registry,profile,1,ai::Difficulty::Normal,{{3400,800}});
        cs=think(ai2,w2);
        check(std::any_of(cs.begin(),cs.end(),[&](const auto& c){
            return c.kind==net::Cmd::Attack && c.unitId==m2 && c.targetId==shooter;
        }),"an idle Monarch hits back at an attacker it cannot see");
    }
    {
        sim::World w;terrain(w);w.spawn(&base,800,800,0,0);
        const int builderId=w.spawn(&monarch,800,800,0,0);
        sim::UnitType building;building.id="site-test";building.maxHp=100;building.maxVel=sim::Fixed();
        building.footX=building.footZ=2;
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive,{{3400,800}});
        float x=0,z=0;
        check(ai::PlacementProbe::place(defensive,w,&building,*w.unit(builderId),x,z),
              "Defensive placement baseline finds a site");
        const float fx=x+40,fz=z;
        w.spawn(&soldier,fx,fz,0,1);
        float x2=0,z2=0;
        const bool found=ai::PlacementProbe::place(defensive,w,&building,*w.unit(builderId),x2,z2);
        check(!found || (x2-fx)*(x2-fx)+(z2-fz)*(z2-fz)>=400*400,
              "the Defensive AI does not site a building inside a spotted fight");
    }
}

// Retail limits each builder type, not the total (ai/default.txt: arabuild,
// arafly and arapries 4 each), and has no difficulty levels. Normal grows from the
// Monarch plus one to the Monarch plus four as income allows; Easy and Defensive
// stay at the Monarch plus one.
static void builderCaps() {
    auto files=std::make_shared<hpi::Vfs::Files>();
    auto file=[&](const std::string& path,const std::string& text) {
        std::string lines;
        for (char ch : text) { lines += ch; if (ch == ';' || ch == '{' || ch == '}') lines += '\n'; }
        (*files)[path]={lines.begin(),lines.end()};
    };
    auto type=[&](const char* id,const char* fields) {
        file(std::string("units/")+id+".fbi",std::string("[UNITINFO]{unitname=")+id+
            ";objectname="+id+";maxdamage=100;footprintx=2;footprintz=2;buildtime=30;buildcost=10;"+fields+"}");
    };
    type("keep","builder=1;workertime=30;");
    type("king","builder=1;commander=1;workertime=30;canmove=1;maxvelocity=1;");
    type("mason","builder=1;workertime=30;canmove=1;maxvelocity=1;");
    file("canbuild/keep/mason.tdf","[CANBUILD]{sortbias=0;}");
    hpi::Vfs vfs;vfs.setMapFiles(files);
    sim::TypeRegistry reg;reg.loadDir(vfs,"units");reg.loadBuildTree(vfs,"canbuild");
    auto trainsMason=[&](ai::Difficulty difficulty,float income,int masons) {
        sim::World w;terrain(w);w.buildNavClasses(reg);
        w.player(0).mana=10000;w.player(0).income=income;
        w.spawn(reg.find("keep"),400,800,0,0);
        w.spawn(reg.find("king"),800,800,0,0);
        for (int i=0;i<masons;++i) w.spawn(reg.find("mason"),800+float(i)*40,1200,0,0);
        ai::Profile p;p.weight["mason"]=100;p.limit["mason"]=4;
        ai::Controller controller(0,reg,p,1,difficulty);
        const auto cs=think(controller,w,difficulty==ai::Difficulty::Easy ? 90 : 0);
        return std::any_of(cs.begin(),cs.end(),[](const auto& c) {
            return c.kind==net::Cmd::Train && std::string(c.type)=="mason";
        });
    };
    check(!trainsMason(ai::Difficulty::Normal,20,1),"Normal opens with the Monarch and one builder");
    check(trainsMason(ai::Difficulty::Normal,100,1),"Normal adds builders as its income grows");
    check(trainsMason(ai::Difficulty::Normal,150,3),"Normal grows to the Monarch and four builders");
    check(!trainsMason(ai::Difficulty::Normal,1000,4),"Normal stops at the Monarch and four builders");
    for (auto difficulty : {ai::Difficulty::Passive,ai::Difficulty::Easy})
        check(!trainsMason(difficulty,150,1),"Easy and Defensive keep the Monarch and one builder");
}

static void defensiveResponses(const sim::TypeRegistry& registry,const ai::Profile& profile,
                               const sim::UnitType& soldier) {
    auto base=soldier;base.id="base";base.maxVel={};base.sight=750;
    base.weapon.damage=0;base.weapons.clear();
    auto builder=soldier;builder.isBuilder=true;builder.commander=true;
    for (auto mode:{sim::PathfindingMode::Retail,sim::PathfindingMode::Legion}) {
        sim::World w;terrain(w);w.setPathfindingMode(mode);w.player(0).defensiveAi=true;
        w.spawn(&base,400,800,0,0);
        std::vector<int> army;
        for (int i=0;i<6;++i) army.push_back(w.spawn(&soldier,400,960+float(i)*40,0,0));
        const int monarch=w.spawn(&builder,450,800,0,0);
        const int unfinished=w.spawn(&soldier,500,1200,0,0);w.unit(unfinished)->underConstruction=true;
        const int cargo=w.spawn(&soldier,500,1240,0,0);w.unit(cargo)->inTransport=monarch;
        const int intruder=w.spawn(&soldier,1100,800,0,1);
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive,{{3400,800}});
        auto cs=think(defensive,w);
        check(attacks(cs)==6 && std::all_of(cs.begin(),cs.end(),[&](const auto& c) {
            return c.kind!=net::Cmd::AttackMove || (c.x==1100 && c.z==800 &&
                std::find(army.begin(),army.end(),c.unitId)!=army.end());
        }),"Defensive mobilizes its whole ready army to meet a spotted intruder in both path modes");
        for (const auto& c:cs) sim::applyCommand(w,registry,c);
        check(attacks(think(defensive,w,30))==0,"Defensive does not restart an unchanged interception every think");
        for (int tick=0;tick<90;++tick) w.tick(1.f/30.f);
        check(std::all_of(army.begin(),army.end(),[&](int id){return w.unit(id)->x.toFloat()>430;}),
              "Defensive interception commands actually move the army through normal navigation");
        // A converted enemy must cease being a threat, including while a
        // defender has an automatic combat order ahead of its movement goal.
        w.unit(intruder)->player=0; // observe the ownership change after conversion
        cs=think(defensive,w,120);
        check(attacks(cs)==0 && std::count_if(cs.begin(),cs.end(),[&](const auto& c) {
            return c.kind==net::Cmd::Move && c.x==400 &&
                std::find(army.begin(),army.end(),c.unitId)!=army.end();
        })==6,"Defensive recalls responders to their posts when a threat is converted");
        for (const auto& c:cs) sim::applyCommand(w,registry,c);
        check(think(defensive,w,150).empty(),"Defensive does not repeatedly reset the return march");
    }
    {
        sim::World w;terrain(w);w.spawn(&base,400,800,0,0);
        const int defender=w.spawn(&soldier,400,1000,0,0);
        const int enemy=w.spawn(&soldier,1100,800,0,1);
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive,{{3400,800}});
        auto cs=think(defensive,w);for(const auto& c:cs)sim::applyCommand(w,registry,c);
        w.unit(enemy)->x=sim::Fixed::fromInt(3000);
        cs=think(defensive,w,30);
        check(attacks(cs)==0 && std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
            return c.unitId==defender && c.kind==net::Cmd::Move && c.x==400 && c.z==1000;
        }),"Defensive recalls its army instead of following a retreating enemy to its base");
        for(const auto& c:cs)sim::applyCommand(w,registry,c);
        w.unit(defender)->orders.clear();w.unit(defender)->x=sim::Fixed::fromInt(500);
        check(think(defensive,w,60).empty(),"Defensive respects return-order completion, including relaxed crowd arrival");
    }
    {
        sim::World w;terrain(w);auto blindBase=base;blindBase.sight=80;
        w.spawn(&blindBase,400,800,0,0);w.spawn(&soldier,400,1000,0,0);
        const int enemy=w.spawn(&soldier,1000,800,0,1);
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive,{{3400,800}});
        check(attacks(think(defensive,w))==0,"Defensive ignores nearby threats hidden by fog");
        blindBase.radar=700;
        auto cs=think(defensive,w,30);
        check(attacks(cs)==1,"Defensive responds to threats spotted by radar");
        w.unit(enemy)->cloaked=true;
        check(attacks(think(defensive,w,60))==0,"Defensive does not target a cloaked enemy");
    }
    {
        sim::World w;terrain(w);w.spawn(&base,400,800,0,0);
        w.spawn(&soldier,400,1000,0,0);
        auto scout=soldier;scout.weapon.damage=0;scout.weapons.clear();scout.sight=500;
        w.spawn(&scout,2500,800,0,0);w.spawn(&soldier,2700,800,0,1);
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive,{{2700,800}});
        check(attacks(think(defensive,w))==0,"Defensive ignores distant visible enemies and known enemy starts");
        w.player(2).team=w.player(0).team;w.spawn(&soldier,1050,800,0,2);
        auto unarmed=scout;w.spawn(&unarmed,1000,800,0,1);
        check(attacks(think(defensive,w,30))==0,"Allies and harmless scouts do not mobilize the defensive army");
        w.spawn(&base,2500,1200,0,0);w.spawn(&soldier,2700,1200,0,1);
        check(attacks(think(defensive,w,60))==1,"Defensive also protects owned outlying buildings");
    }
    {
        sim::World w;terrain(w,true);w.spawn(&base,1300,800,0,0);
        const int ground=w.spawn(&soldier,1300,1000,0,0);
        auto flyer=soldier;flyer.canFly=true;
        const int air=w.spawn(&flyer,1300,1200,0,0);
        w.spawn(&soldier,2624,800,0,1);
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive);
        // An outlying building reveals the threat across disconnected land.
        w.spawn(&base,2624,1200,0,0);
        const auto cs=think(defensive,w);
        check(attacks(cs)==1 && std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
            return c.kind==net::Cmd::AttackMove && c.unitId==air;
        }) && std::none_of(cs.begin(),cs.end(),[&](const auto& c) {
            return c.kind==net::Cmd::AttackMove && c.unitId==ground;
        }),"Defensive sends reachable flyers while keeping stranded ground units home");
    }
    {
        sim::World w;terrain(w,true);w.spawn(&base,1480,800,0,0);
        auto boat=soldier;boat.domain=sim::UnitType::Domain::Water;boat.weapon.range=240;
        const int ship=w.spawn(&boat,1800,800,0,0);w.spawn(&soldier,1480,1000,0,1);
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive);
        const auto cs=think(defensive,w);
        check(attacks(cs)==1 && std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
            return c.kind==net::Cmd::AttackMove && c.unitId==ship && c.x>=1536;
        }),"Defensive ships use a reachable water approach to a threat on shore");
    }
    {
        sim::World w;terrain(w);w.spawn(&base,400,800,0,0);
        for(int i=0;i<300;++i)w.spawn(&soldier,400+float(i%10)*32,1000+float(i/10)*32,0,0);
        w.spawn(&soldier,1100,800,0,1);
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive);
        auto cs=think(defensive,w);
        check(attacks(cs)==256,"Defensive limits interception commands per think for large armies");
        for(const auto& c:cs)sim::applyCommand(w,registry,c);
        check(attacks(think(defensive,w,30))==44,"Defensive sends remaining troops without restarting the first wave");
    }
    {
        sim::World w;terrain(w);w.spawn(&base,400,800,0,0);
        const int defender=w.spawn(&soldier,400,1000,0,0);
        const int enemy=w.spawn(&soldier,1100,800,0,1);
        w.attack(defender,enemy,false);
        ai::Controller defensive(0,registry,profile,1,ai::Difficulty::Passive);
        check(attacks(think(defensive,w))==0,"Defensive preserves an existing explicit attack order");
    }
}
// Zhon has no factory buildings: its mobile builders train the army and build
// the lodestones. Their allowance is another side's factories plus its builders
// past the Monarch, not the factories alone.
static void mobileProducers() {
    auto files=std::make_shared<hpi::Vfs::Files>();
    auto file=[&](const std::string& path,const std::string& text) {
        std::string lines;
        for (char ch : text) { lines += ch; if (ch == ';' || ch == '{' || ch == '}') lines += '\n'; }
        (*files)[path]={lines.begin(),lines.end()};
    };
    auto type=[&](const char* id,const char* fields) {
        file(std::string("units/")+id+".fbi",std::string("[UNITINFO]{unitname=")+id+
            ";objectname="+id+";maxdamage=100;footprintx=2;footprintz=2;buildtime=30;buildcost=10;"+fields+"}");
    };
    // ZON: the Monarch conjures handlers, which conjure creatures. TAR: the same
    // menus, plus a factory building.
    for (const char* side : {"zon","tar"}) {
        const std::string s=side,upper=s=="zon" ? "ZON" : "TAR";
        type((s+"king").c_str(),("side="+upper+";builder=1;commander=1;workertime=30;canmove=1;maxvelocity=1;").c_str());
        type((s+"hand").c_str(),("side="+upper+";builder=1;workertime=30;canmove=1;maxvelocity=1;").c_str());
        type((s+"gob").c_str(),("side="+upper+";canmove=1;maxvelocity=1;").c_str());
        type((s+"troll").c_str(),("side="+upper+";canmove=1;maxvelocity=1;").c_str());
        file("canbuild/"+s+"king/"+s+"hand.tdf","[CANBUILD]{sortbias=0;}");
        file("canbuild/"+s+"hand/"+s+"gob.tdf","[CANBUILD]{sortbias=0;}");
        file("canbuild/"+s+"hand/"+s+"troll.tdf","[CANBUILD]{sortbias=0;}");
    }
    type("tarhell","side=TAR;builder=1;workertime=30;");
    file("canbuild/tarhell/targob.tdf","[CANBUILD]{sortbias=0;}");
    hpi::Vfs vfs;vfs.setMapFiles(files);
    sim::TypeRegistry reg;reg.loadDir(vfs,"units");reg.loadBuildTree(vfs,"canbuild");
    auto conjuresHandler=[&](const std::string& side,ai::Difficulty difficulty,float income,int handlers) {
        sim::World w;terrain(w);w.buildNavClasses(reg);
        w.player(0).mana=100000;w.player(0).income=income;
        w.spawn(reg.find(side+"king"),800,800,0,0);
        // Busy handlers: each is already conjuring, so none of them acts this think.
        for (int i=0;i<handlers;++i) {
            const int id=w.spawn(reg.find(side+"hand"),1400+float(i%6)*60,1400+float(i/6)*60,0,0);
            sim::Order o;o.buildType=reg.find(side+"gob");
            w.unit(id)->orders.push_back(o);
        }
        for (int i=0;i<12;++i) w.spawn(reg.find(side+"troll"),2000+float(i)*40,800,0,0);
        ai::Profile p;
        for (const char* id : {"hand","gob","troll"}) {p.weight[side+id]=10;p.limit[side+id]=16;}
        ai::Controller controller(0,reg,p,1,difficulty);
        const auto cs=think(controller,w,difficulty==ai::Difficulty::Easy ? 90 : 0);
        return std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
            return c.kind==net::Cmd::Build && std::string(c.type)==side+"hand";
        });
    };
    // Income 100: three factories (100/40 + 1) and four builders, the Monarch included.
    check(conjuresHandler("zon",ai::Difficulty::Normal,100,3),
          "Zhon's mobile producers count as builders as well as factories");
    check(conjuresHandler("zon",ai::Difficulty::Normal,100,5),
          "Zhon's producers grow to its factories plus its builders past the Monarch");
    check(!conjuresHandler("zon",ai::Difficulty::Normal,100,6),
          "Zhon's producers stop at its factories plus its builders past the Monarch");
    check(!conjuresHandler("tar",ai::Difficulty::Normal,100,3),
          "a side with factory buildings keeps its mobile producers to the factory count");
    check(conjuresHandler("zon",ai::Difficulty::Easy,100,2),
          "Easy Zhon keeps the Monarch's one builder on top of its two factories");
    check(!conjuresHandler("zon",ai::Difficulty::Easy,100,3),
          "Easy Zhon stops at two factories and one builder");
}

// Every builder=1 mobile unit counts against the difficulty's cap, the Monarch,
// the unfinished and the queued included; no builder type passes its own limit.
static void builderTotals() {
    auto files=std::make_shared<hpi::Vfs::Files>();
    auto file=[&](const std::string& path,const std::string& text) {
        std::string lines;
        for (char ch : text) { lines += ch; if (ch == ';' || ch == '{' || ch == '}') lines += '\n'; }
        (*files)[path]={lines.begin(),lines.end()};
    };
    auto type=[&](const char* id,const char* fields) {
        file(std::string("units/")+id+".fbi",std::string("[UNITINFO]{unitname=")+id+
            ";objectname="+id+";maxdamage=100;footprintx=2;footprintz=2;buildtime=30;buildcost=10;"+fields+"}");
    };
    // ARA: a keep trains masons and priests; a priest's menu is troops, so it is
    // a factory by category but a builder all the same. ZON: no factory building.
    type("arakeep","side=ARA;builder=1;workertime=30;");
    type("araking","side=ARA;builder=1;commander=1;workertime=30;canmove=1;maxvelocity=1;");
    type("aramason","side=ARA;builder=1;workertime=30;canmove=1;maxvelocity=1;");
    type("arapries","side=ARA;builder=1;workertime=30;canmove=1;maxvelocity=1;");
    type("aramonk","side=ARA;canmove=1;maxvelocity=1;");
    file("canbuild/arakeep/aramason.tdf","[CANBUILD]{sortbias=0;}");
    file("canbuild/arakeep/arapries.tdf","[CANBUILD]{sortbias=0;}");
    file("canbuild/arapries/aramonk.tdf","[CANBUILD]{sortbias=0;}");
    type("zonking","side=ZON;builder=1;commander=1;workertime=30;canmove=1;maxvelocity=1;");
    type("zonhand","side=ZON;builder=1;workertime=30;canmove=1;maxvelocity=1;");
    type("zongob","side=ZON;canmove=1;maxvelocity=1;");
    file("canbuild/zonking/zonhand.tdf","[CANBUILD]{sortbias=0;}");
    file("canbuild/zonhand/zonhand.tdf","[CANBUILD]{sortbias=0;}");
    file("canbuild/zonhand/zongob.tdf","[CANBUILD]{sortbias=0;}");
    hpi::Vfs vfs;vfs.setMapFiles(files);
    sim::TypeRegistry reg;reg.loadDir(vfs,"units");reg.loadBuildTree(vfs,"canbuild");
    auto builders=[](const std::vector<net::Command>& cs,std::initializer_list<const char*> ids) {
        return int(std::count_if(cs.begin(),cs.end(),[&](const auto& c) {
            return (c.kind==net::Cmd::Train || c.kind==net::Cmd::Build) &&
                std::any_of(ids.begin(),ids.end(),[&](const char* id){return std::string(c.type)==id;});
        }));
    };
    // keeps idle keeps; masons/priests already standing; queued masons in one
    // more, busy keep that has not begun them.
    auto aramon=[&](ai::Difficulty difficulty,float income,int keeps,int masons,int priests,int queued,
                    int masonLimit=4) {
        sim::World w;terrain(w);w.buildNavClasses(reg);
        w.player(0).mana=100000;w.player(0).income=income;
        w.spawn(reg.find("araking"),800,800,0,0);
        for (int i=0;i<keeps;++i) w.spawn(reg.find("arakeep"),300+float(i)*100,300,0,0);
        if (queued) {
            const int id=w.spawn(reg.find("arakeep"),300,1600,0,0);
            for (int i=0;i<queued;++i) w.unit(id)->buildQueue.push_back(reg.find("aramason"));
        }
        for (int i=0;i<masons;++i) w.spawn(reg.find("aramason"),1400+float(i)*60,1400,0,0);
        for (int i=0;i<priests;++i) {
            // Busy priests: each is already training, so none of them acts this think.
            const int id=w.spawn(reg.find("arapries"),1400+float(i)*60,1600,0,0);
            sim::Order o;o.buildType=reg.find("aramonk");w.unit(id)->orders.push_back(o);
        }
        for (int i=0;i<12;++i) w.spawn(reg.find("aramonk"),2000+float(i)*40,800,0,0);
        ai::Profile p;
        p.weight["aramason"]=10;p.limit["aramason"]=masonLimit;
        p.weight["arapries"]=10;p.limit["arapries"]=4;p.weight["aramonk"]=10;p.limit["aramonk"]=40;
        ai::Controller controller(0,reg,p,1,difficulty);
        return builders(think(controller,w,difficulty==ai::Difficulty::Easy ? 90 : 0),{"aramason","arapries"});
    };
    // Income 1000: Normal's cap is five, the Monarch included, and eight factories.
    check(aramon(ai::Difficulty::Normal,1000,1,0,4,0)==0,
          "priests count against the builder cap though their menus make them factories");
    check(aramon(ai::Difficulty::Normal,1000,1,2,1,0)==1,
          "below the cap a factory race still adds a builder");
    check(aramon(ai::Difficulty::Normal,1000,1,3,0,1)==0,
          "a builder queued in a factory counts against the cap before it begins");
    check(aramon(ai::Difficulty::Normal,1000,3,3,0,0)==1,
          "several producers in one think stop at the cap");
    check(aramon(ai::Difficulty::Hard,100000,1,4,4,0)==0,
          "Hard holds a builder type to its own ai/default.txt limit");
    auto zhon=[&](ai::Difficulty difficulty,float income,int handlers,int conjuring) {
        sim::World w;terrain(w);w.buildNavClasses(reg);
        w.player(0).mana=100000;w.player(0).income=income;
        w.spawn(reg.find("zonking"),800,800,0,0);
        for (int i=0;i<handlers;++i) {
            // Busy handlers: the first `conjuring` walk to conjure a handler.
            const int id=w.spawn(reg.find("zonhand"),1400+float(i%6)*60,1400+float(i/6)*60,0,0);
            sim::Order o;o.buildType=reg.find(i<conjuring ? "zonhand" : "zongob");
            w.unit(id)->orders.push_back(o);
        }
        for (int i=0;i<12;++i) w.spawn(reg.find("zongob"),2000+float(i)*40,800,0,0);
        ai::Profile p;p.weight["zonhand"]=10;p.limit["zonhand"]=3;p.weight["zongob"]=10;p.limit["zongob"]=40;
        if (difficulty==ai::Difficulty::Normal) p.limit["zonhand"]=16;
        ai::Controller controller(0,reg,p,1,difficulty);
        return builders(think(controller,w,difficulty==ai::Difficulty::Easy ? 90 : 0),{"zonhand"});
    };
    // Income 100: three factories and four builders, so the Monarch and six handlers.
    check(zhon(ai::Difficulty::Normal,100,5,0)==1,"Zhon's producers grow to its own allowance");
    check(zhon(ai::Difficulty::Normal,100,5,1)==0,
          "a handler ordered but not yet conjured counts against Zhon's allowance");
    // Hard at income 100000: 32 factories and a cap of 12, so 44 with the Monarch.
    check(zhon(ai::Difficulty::Hard,100000,3,0)==1,
          "Hard Zhon's producers take the factory raise past their ai/default.txt limit");
    check(zhon(ai::Difficulty::Hard,100000,43,0)==0,
          "Hard Zhon's producers stop at its factories plus its builder cap");
}

// The income target met, an idle builder still builds income while the store
// is full (retail's builders draw from the whole menu, the lodestone among it).
static void fullStore() {
    auto files=std::make_shared<hpi::Vfs::Files>();
    auto file=[&](const std::string& path,const std::string& text) {
        std::string lines;
        for (char ch : text) { lines += ch; if (ch == ';' || ch == '{' || ch == '}') lines += '\n'; }
        (*files)[path]={lines.begin(),lines.end()};
    };
    auto type=[&](const char* id,const char* fields) {
        file(std::string("units/")+id+".fbi",std::string("[UNITINFO]{unitname=")+id+
            ";objectname="+id+";maxdamage=100;footprintx=2;footprintz=2;buildtime=30;buildcost=10;"+fields+"}");
    };
    type("keep","builder=1;workertime=30;");
    type("mason","builder=1;workertime=30;canmove=1;maxvelocity=1;");
    type("mine","mogriumincome=10;");
    file("canbuild/keep/mason.tdf","[CANBUILD]{sortbias=0;}");
    file("canbuild/mason/mine.tdf","[CANBUILD]{sortbias=0;}");
    hpi::Vfs vfs;vfs.setMapFiles(files);
    sim::TypeRegistry reg;reg.loadDir(vfs,"units");reg.loadBuildTree(vfs,"canbuild");
    auto buildsMine=[&](ai::Difficulty difficulty,float mana) {
        sim::World w;terrain(w);w.buildNavClasses(reg);
        w.player(0).storage=5000;w.player(0).mana=mana;w.player(0).income=100;
        w.spawn(reg.find("keep"),400,800,0,0);
        for (int i=0;i<3;++i) w.spawn(reg.find("mason"),800+float(i)*60,1200,0,0);
        ai::Profile p;p.weight["mason"]=10;p.limit["mason"]=3;p.weight["mine"]=10;
        ai::Controller controller(0,reg,p,1,difficulty);
        const auto cs=think(controller,w,difficulty==ai::Difficulty::Easy ? 90 : 0);
        return std::any_of(cs.begin(),cs.end(),[](const auto& c) {
            return c.kind==net::Cmd::Build && std::string(c.type)=="mine";
        });
    };
    check(!buildsMine(ai::Difficulty::Normal,3000),"Normal's builders rest once income meets its target");
    check(buildsMine(ai::Difficulty::Normal,4800),"Normal's idle builders build income while the store is full");
    check(buildsMine(ai::Difficulty::Passive,4800),"Defensive's idle builders build income while the store is full");
    check(!buildsMine(ai::Difficulty::Easy,4800),"Easy's builders rest beside a full store");
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
    builderCaps();
    mobileProducers();
    builderTotals();
    fullStore();
    defensiveResponses(empty,profile,soldier);
    defensiveShooters(empty,profile,soldier);
    monarchFightsBack(empty,profile,soldier);
    {
        sim::World w;terrain(w);w.player(0).income=100;
        for(int i=0;i<35;++i)w.spawn(&soldier,400+float(i%7)*40,800+float(i/7)*40,0,0);
        ai::Controller controller(0,empty,profile,1,ai::Difficulty::Easy,{{3400,800}});
        check(attacks(think(controller,w,3*60*30))==0,"Easy leaves four minutes before its first attack");
        check(attacks(think(controller,w,4*60*30))==8,"Easy attacks with at most eight units");
        check(attacks(think(controller,w,5*60*30))==0,"Easy waits between attack waves");
        check(attacks(think(controller,w,6*60*30))==8,"Easy resumes attacking after two minutes");
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
        sim::World w;terrain(w,true);auto boat=soldier;boat.domain=sim::UnitType::Domain::Water;
        const int ship=w.spawn(&boat,2000,800,0,0);
        ai::Controller controller(0,empty,profile,1,ai::Difficulty::Normal,{{3400,800}});
        const auto cs=think(controller,w);
        check(attacks(cs)==1 && cs[0].unitId==ship && cs[0].x<2560,
              "ships approach an enemy coast even when the starting position lies beyond weapon range");
    }
    {
        sim::World w;std::vector<uint8_t> heights(256*256,100);
        for(int z=0;z<256;++z)for(int x=0;x<256;++x)
            if((x>=48 && x<80) || (x>=144 && x<176))heights[z*256+x]=0;
        w.setTerrain(std::move(heights),256,256,20);w.setVisPlayer(-1);
        auto boat=soldier;boat.domain=sim::UnitType::Domain::Water;
        w.spawn(&boat,1000,800,0,0);
        ai::Controller controller(0,empty,profile,1,ai::Difficulty::Normal,{{3000,800}});
        check(attacks(think(controller,w))==0,"naval coastal projection rejects a disconnected body of water");
    }
    for (auto difficulty:{ai::Difficulty::Passive,ai::Difficulty::Easy,ai::Difficulty::Normal}) {
        sim::World w;terrain(w,true);w.player(0).income=200;
        auto boat=soldier;boat.domain=sim::UnitType::Domain::Water;
        const int count=difficulty==ai::Difficulty::Easy ? 2 : 3;
        for (int i=0;i<count;++i) w.spawn(&boat,2000,800+float(i)*80,0,0);
        ai::Controller controller(0,empty,profile,1,difficulty,{{3400,800}});
        const auto cs=think(controller,w,4*60*30);
        check(attacks(cs)==(difficulty==ai::Difficulty::Passive ? 0 : count),
              "small fleets deploy despite a large land-army budget; Defensive never attacks");
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
    {
        // Self-contained production tree: profile caps must not stop a defensive
        // or aggressive AI while mana and the actual game cap allow more troops.
        auto files=std::make_shared<hpi::Vfs::Files>();
        auto file=[&](const std::string& path,const std::string& text) {
            std::string lines;
            for (char ch : text) { lines += ch; if (ch == ';' || ch == '{' || ch == '}') lines += '\n'; }
            (*files)[path]={lines.begin(),lines.end()};
        };
        auto type=[&](const char* id,const char* fields) {
            file(std::string("units/")+id+".fbi",std::string("[UNITINFO]{unitname=")+id+
                ";objectname="+id+";maxdamage=100;footprintx=2;footprintz=2;buildtime=30;buildcost=10;"+fields+"}");
        };
        type("soldier","canmove=1;maxvelocity=1;");
        type("scout","canmove=1;maxvelocity=1;");
        type("unique","canmove=1;maxvelocity=1;totalallowed=1;");
        type("factory","builder=1;workertime=30;");
        type("handler","builder=1;workertime=30;canmove=1;maxvelocity=1;");
        type("lode","mogriumincome=20;");
        type("worker","builder=1;workertime=30;canmove=1;maxvelocity=1;");
        file("canbuild/worker/lode.tdf","[CANBUILD]{sortbias=0;}");
        for (const char* parent : {"factory","handler"})
            for (const char* child : {"soldier","scout","unique"})
                file(std::string("canbuild/")+parent+"/"+child+".tdf","[CANBUILD]{sortbias=0;}");
        file("canbuild/handler/lode.tdf","[CANBUILD]{sortbias=0;}");
        hpi::Vfs vfs;vfs.setMapFiles(files);
        sim::TypeRegistry reg;reg.loadDir(vfs,"units");reg.loadBuildTree(vfs,"canbuild");
        for (auto difficulty : {ai::Difficulty::Passive,ai::Difficulty::Normal,ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
            sim::World w;terrain(w);w.buildNavClasses(reg);w.setUnitCap(250);
            w.player(0).mana=10000;w.player(0).income=200;
            w.spawn(reg.find("factory"),400,800,0,0);
            for (int i=0;i<190;++i)w.spawn(reg.find("soldier"),1600+float(i%20)*40,1600+float(i/20)*40,0,0);
            ai::Profile p;p.weight["soldier"]=100;p.limit["soldier"]=1;
            ai::Controller controller(0,reg,p,1,difficulty);
            auto trains=[](const auto& cs){return std::count_if(cs.begin(),cs.end(),[](const auto& c){return c.kind==net::Cmd::Train;});};
            check(trains(think(controller,w))==(difficulty==ai::Difficulty::Normal ? 0 : 1),
                  "sustained modes keep producing past old army/profile ceilings; Normal retains its target");
            w.setUnitCap(191);
            check(trains(think(controller,w))==0,"continued production respects the actual player cap");
            w.setUnitCap(250);p.limit["soldier"]=0;
            check(trains(think(controller,w))==0,"disabled profile entries stay disabled");
            p.weight["soldier"]=0;p.weight["unique"]=100;
            w.spawn(reg.find("unique"),3000,2000,0,0);
            check(trains(think(controller,w))==0,"actual per-type gameplay caps remain enforced");
        }
        {
            sim::World w;terrain(w);w.buildNavClasses(reg);
            w.player(0).mana=10000;w.player(0).income=25;
            const int handler=w.spawn(reg.find("handler"),800,800,0,0);
            ai::Profile p;p.weight["soldier"]=100;p.weight["lode"]=100;
            ai::Controller controller(0,reg,p,1,ai::Difficulty::Normal);
            auto cs=think(controller,w);
            check(std::any_of(cs.begin(),cs.end(),[&](const auto& c){return c.unitId==handler && c.kind==net::Cmd::Build && std::string(c.type)=="soldier";}),
                  "mobile production creature makes an opening army before more economy");
            const int defender=w.spawn(reg.find("soldier"),880,800,0,0);
            ai::Profile none;ai::Controller passive(0,reg,none,1,ai::Difficulty::Passive);
            cs=think(passive,w);
            check(attacks(cs)==0 && std::any_of(cs.begin(),cs.end(),[&](const auto& c){return c.unitId==defender && c.kind==net::Cmd::Move;}),
                  "defensive troops clear mobile production sites without an attack order");
            for (int i=0;i<3;++i)w.spawn(reg.find("soldier"),1200+float(i)*40,800,0,0);
            int worker=w.spawn(reg.find("worker"),1600,800,0,0);
            sim::Order approach;approach.x=sim::Fixed::fromInt(2000);approach.z=sim::Fixed::fromInt(800);
            sim::Order build=approach;build.buildType=reg.find("lode");
            w.unit(worker)->orders={approach,build};
            cs=think(controller,w);
            check(std::any_of(cs.begin(),cs.end(),[&](const auto& c){return c.unitId==handler && c.kind==net::Cmd::Build && std::string(c.type)=="soldier";}),
                  "economy construction behind approach waypoints leaves mobile production making troops");
        }
        // A deposit that a builder is walking to is reserved by the order's SITE.
        // A real MobileBuild order keeps its approach point beside the site in x/z
        // (World::makeBuildOrder); the reservation compared that point, 45 px from
        // the deposit here, against a 32 px match and so never held. In a replayed
        // 4v4 Absurd game two allies sent a twelfth builder to a deposit eleven of
        // theirs were already walking to, and raced each other for home deposits.
        enum class Owner { Self, Ally, Enemy };
        for (auto owner : {Owner::Self,Owner::Ally,Owner::Enemy})
            for (auto difficulty : {ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
                sim::World w;terrain(w);w.buildNavClasses(reg);
                w.setManaSpots({{2000,800},{2000,1600}});
                const int player=owner==Owner::Self ? 0 : 1;
                const int walker=w.spawn(reg.find("worker"),1200,400,0,player);
                const int idle=w.spawn(reg.find("worker"),1800,800,0,0);   // nearer the walker's deposit
                if (player==1) w.setTeam(1,owner==Owner::Ally ? 0 : 1);
                w.player(0).mana=10000;
                net::Command order;order.kind=net::Cmd::Build;order.player=uint8_t(player);order.unitId=walker;
                order.x=2000;order.z=800;std::snprintf(order.type,sizeof order.type,"lode");
                sim::applyCommand(w,reg,order);
                const auto& queued=w.unit(walker)->orders;
                const bool real=!queued.empty() && queued.back().buildType==reg.find("lode") &&
                    std::hypot(queued.back().x.toFloat()-2000,queued.back().z.toFloat()-800)>=32;
                check(real,"a queued lodestone's order point is its approach, away from the deposit");
                ai::Profile p;p.weight["lode"]=100;
                ai::Controller controller(0,reg,p,1,difficulty);
                const auto cs=think(controller,w);
                auto builds=[&](float z) {
                    return std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
                        return c.unitId==idle && c.kind==net::Cmd::Build && c.x==2000 && c.z==z;
                    });
                };
                if (owner==Owner::Enemy)
                    check(builds(800),"an enemy builder's destination does not reserve a deposit");
                else
                    check(builds(1600) && !builds(800),owner==Owner::Self
                          ? "expansion leaves a deposit its own walking builder will take"
                          : "expansion leaves a deposit an ally's walking builder will take");
            }
        // Map-wide expansion leaves a free deposit in a living ally's home area to the
        // ally only while the ally is claiming it: a builder bound for it (never
        // raced), or one of the ally's that can reach it while the deposit has sat
        // unclaimed for less than two minutes of game time. Retail's base-radius
        // search never builds there at all, but leaving a deposit to an ally that
        // never takes it cost a mixed team 20-35% of its income. An enemy's, a
        // defeated ally's and a builderless ally's home deposits are open.
        constexpr uint32_t kClaimTicks=2*60*30;
        enum class Ally { Idle, Bound, Lapsed, NoBuilder, Defeated, Enemy };
        for (auto difficulty : {ai::Difficulty::Hard,ai::Difficulty::Absurd})
            for (auto ally : {Ally::Idle,Ally::Bound,Ally::Lapsed,Ally::NoBuilder,Ally::Defeated,Ally::Enemy}) {
                sim::World w;terrain(w);w.buildNavClasses(reg);
                w.setManaSpots({{2560,800},{1600,1792}});   // the ally's home deposit, a free one
                w.spawn(reg.find("factory"),400,800,0,0);    // our base
                auto monarch=*reg.find("worker");monarch.commander=true;
                const int allyBuilder=ally==Ally::NoBuilder ? 0 : w.spawn(&monarch,3000,800,0,1);   // the ally's start
                if (ally==Ally::NoBuilder) w.spawn(reg.find("factory"),3000,800,0,1);   // its Monarch lost, a building left
                const int idle=w.spawn(reg.find("worker"),2000,800,0,0);   // nearer the ally's deposit
                w.setTeam(1,ally==Ally::Enemy ? 1 : 0);
                if (ally==Ally::Defeated) w.player(1).defeated=true;
                w.player(0).mana=10000;
                if (ally==Ally::Bound || ally==Ally::Lapsed) {
                    net::Command order;order.kind=net::Cmd::Build;order.player=1;order.unitId=allyBuilder;
                    order.x=2560;order.z=800;std::snprintf(order.type,sizeof order.type,"lode");
                    sim::applyCommand(w,reg,order);
                }
                ai::Profile p;p.weight["lode"]=100;
                ai::Controller controller(0,reg,p,1,difficulty);
                // Where the idle builder expands at `tick`: 'A' the ally's deposit, 'F' the free one.
                auto expands=[&](uint32_t tick) {
                    for (const auto& c:think(controller,w,tick))
                        if (c.unitId==idle && c.kind==net::Cmd::Build) return c.x==2560 && c.z==800 ? 'A' : c.x==1600 && c.z==1792 ? 'F' : '?';
                    return '-';
                };
                switch (ally) {
                    case Ally::Idle: {
                        const bool held=expands(0)=='F' && expands(kClaimTicks-20)=='F';
                        check(held,"an ally's home deposit stays its own for two minutes while its idle builder can reach it");
                        check(expands(kClaimTicks)=='A',"an ally's home deposit left unclaimed for two minutes opens to expansion");
                        break;
                    }
                    case Ally::Bound: {
                        // It walks: 100 px nearer by each think (see the stalled builders below).
                        bool held=expands(0)=='F';
                        for (uint32_t tick : {kClaimTicks,2*kClaimTicks}) {
                            w.unit(allyBuilder)->x-=sim::Fixed::fromInt(100);
                            held=held && expands(tick)=='F';
                        }
                        check(held,"expansion never races an ally's builder bound for its home deposit, however long it walks");
                        break;
                    }
                    case Ally::Lapsed: {
                        bool held=expands(0)=='F';
                        w.unit(allyBuilder)->x-=sim::Fixed::fromInt(100);   // still walking at tick 2000
                        held=held && expands(2000)=='F';
                        w.unit(allyBuilder)->orders.clear();   // the claim lapses at tick 2000
                        held=held && expands(kClaimTicks)=='F' && expands(2000+kClaimTicks-20)=='F';
                        check(held,"an ally's home deposit stays its own for two minutes after its last claim lapsed");
                        check(expands(2000+kClaimTicks)=='A',"an ally's home deposit opens two minutes after its last claim lapsed");
                        break;
                    }
                    case Ally::NoBuilder:
                        check(expands(0)=='A',"an ally with no builder left (Monarch Expendable) holds no deposit");
                        break;
                    case Ally::Defeated:
                        check(expands(0)=='A',"expansion claims a defeated ally's home deposit");
                        break;
                    case Ally::Enemy:
                        check(expands(0)=='A',"expansion claims an enemy's home deposit");
                        break;
                }
            }
        // Only a builder that can reach the deposit holds it for the ally.
        for (bool reaches : {true,false}) {
            sim::World w;w.setVisPlayer(-1);
            std::vector<uint8_t> h(256*256,100);
            for (int z=0;z<256;++z) for (int x=96;x<160;++x) h[z*256+x]=0;
            w.setTerrain(std::move(h),256,256,60);w.buildNavClasses(reg);   // too deep to wade from x=1536 to 2560
            w.setManaSpots({{2640,800},{3600,2600}});   // the ally's home deposit, a free one
            w.spawn(reg.find("factory"),3800,3200,0,0);  // our base
            w.spawn(reg.find("factory"),2960,800,0,1);   // the ally's base
            w.spawn(reg.find("worker"),reaches ? 3200 : 1000,800,0,1);   // its only builder, across the water or not
            const int idle=w.spawn(reg.find("worker"),3400,1400,0,0);   // nearer the ally's deposit
            w.setTeam(1,0);w.player(0).mana=10000;
            ai::Profile p;p.weight["lode"]=100;
            ai::Controller controller(0,reg,p,1,ai::Difficulty::Absurd);
            const auto cs=think(controller,w);
            const bool claims=std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
                return c.unitId==idle && c.kind==net::Cmd::Build && c.x==2640 && c.z==800;
            });
            check(claims!=reaches,reaches ? "an ally builder that can reach its home deposit holds it"
                                          : "an ally builder that cannot reach its home deposit does not hold it");
        }
        // Once the ally whose home it is no longer holds a deposit -- unclaimed for
        // two minutes, or no builder of its own that can reach it -- the deposit
        // goes to the nearest of the other allies, not to one that would walk across
        // the map and hold it all the way. Every difficulty that reaches it alike.
        for (auto difficulty : {ai::Difficulty::Easy,ai::Difficulty::Normal,ai::Difficulty::Hard,ai::Difficulty::Absurd})
            for (bool ownerBuilds : {true,false})
                for (bool nearer : {true,false}) {
                    sim::World w;terrain(w);w.buildNavClasses(reg);
                    w.setManaSpots({{2560,800},{1600,1792}});   // the owner's home deposit, a free one
                    w.spawn(reg.find("factory"),1200,800,0,0);   // our base, 1360 from the owner's deposit
                    // The owner's start: an idle builder, or a building and no builder left.
                    w.spawn(reg.find(ownerBuilds ? "worker" : "factory"),3000,800,0,1);
                    // A third ally's base, 1092 from the owner's deposit or 3083.
                    w.spawn(reg.find("factory"),nearer ? 3000 : 400,nearer ? 1800 : 3000,0,2);
                    const int idle=w.spawn(reg.find("worker"),2000,800,0,0);
                    w.setTeam(1,0);w.setTeam(2,0);w.player(0).mana=10000;
                    ai::Profile p;p.weight["lode"]=100;
                    ai::Controller controller(0,reg,p,1,difficulty);
                    think(controller,w);
                    const auto cs=think(controller,w,ownerBuilds ? kClaimTicks : 0);
                    const bool claims=std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
                        return c.unitId==idle && c.kind==net::Cmd::Build && c.x==2560 && c.z==800;
                    });
                    check(claims!=nearer,nearer ? (ownerBuilds ? "an ally home deposit open after two minutes is left to a nearer ally"
                                                               : "a builderless ally's home deposit is left to a nearer ally")
                                                : (ownerBuilds ? "an ally home deposit open after two minutes goes to its nearest ally"
                                                               : "a builderless ally's home deposit goes to its nearest ally"));
                }
        // Normal, Easy and Defensive reserve deposits the same way: a site an own or
        // allied builder is walking to, and one planned earlier in the same think.
        // Without it two Normal allies, or two builders of one Normal AI, raced
        // for the nearest free deposit and the later one walked there for nothing.
        for (auto owner : {Owner::Self,Owner::Ally,Owner::Enemy})
            for (auto difficulty : {ai::Difficulty::Passive,ai::Difficulty::Easy,ai::Difficulty::Normal}) {
                sim::World w;terrain(w);w.buildNavClasses(reg);
                w.setManaSpots({{2000,800},{2000,1200}});
                w.spawn(reg.find("factory"),1800,1000,0,0);   // home, within Defensive's 600
                const int player=owner==Owner::Self ? 0 : 1;
                const int walker=w.spawn(reg.find("worker"),1200,400,0,player);
                const int idle=w.spawn(reg.find("worker"),1900,800,0,0);   // nearer the walker's deposit
                if (player==1) w.setTeam(1,owner==Owner::Ally ? 0 : 1);
                w.player(0).mana=10000;
                net::Command order;order.kind=net::Cmd::Build;order.player=uint8_t(player);order.unitId=walker;
                order.x=2000;order.z=800;std::snprintf(order.type,sizeof order.type,"lode");
                sim::applyCommand(w,reg,order);
                ai::Profile p;p.weight["lode"]=100;
                ai::Controller controller(0,reg,p,1,difficulty);
                const auto cs=think(controller,w);
                auto builds=[&](float z) {
                    return std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
                        return c.unitId==idle && c.kind==net::Cmd::Build && c.x==2000 && c.z==z;
                    });
                };
                if (owner==Owner::Enemy)
                    check(builds(800),"Normal, Easy and Defensive ignore an enemy builder's destination");
                else
                    check(builds(1200) && !builds(800),owner==Owner::Self
                          ? "Normal, Easy and Defensive leave a deposit their own walking builder will take"
                          : "Normal, Easy and Defensive leave a deposit an ally's walking builder will take");
            }
        for (auto difficulty : {ai::Difficulty::Passive,ai::Difficulty::Normal}) {
            sim::World w;terrain(w);w.buildNavClasses(reg);
            w.setManaSpots({{2000,800},{2000,1200}});
            w.spawn(reg.find("factory"),1800,1000,0,0);
            w.spawn(reg.find("worker"),1900,800,0,0);
            w.spawn(reg.find("worker"),1900,820,0,0);   // both nearest the same deposit
            w.player(0).mana=10000;
            ai::Profile p;p.weight["lode"]=100;
            ai::Controller controller(0,reg,p,1,difficulty);
            std::vector<float> sites;
            for (const auto& c:think(controller,w)) if (c.kind==net::Cmd::Build) sites.push_back(c.z);
            check(sites.size()==2 && sites[0]!=sites[1],"Normal and Defensive builders take different deposits in one think");
        }
        // ...and leave a living ally's home deposit to it while the ally is claiming
        // it. Normal's and Easy's builders reach 1600 from home, far enough to take a
        // close ally's deposits before the ally gets to them.
        for (auto difficulty : {ai::Difficulty::Easy,ai::Difficulty::Normal})
            for (auto ally : {Ally::Idle,Ally::Bound,Ally::Enemy}) {
                sim::World w;terrain(w);w.buildNavClasses(reg);
                w.setManaSpots({{2560,800},{1600,1792}});   // the ally's home deposit, a free one
                w.spawn(reg.find("factory"),1200,800,0,0);   // our base, 1360 from the ally's deposit
                auto monarch=*reg.find("worker");monarch.commander=true;
                const int allyBuilder=w.spawn(&monarch,3000,800,0,1);   // the ally's start
                const int idle=w.spawn(reg.find("worker"),2000,800,0,0);   // nearer the ally's deposit
                w.setTeam(1,ally==Ally::Enemy ? 1 : 0);
                w.player(0).mana=10000;
                if (ally==Ally::Bound) {
                    net::Command order;order.kind=net::Cmd::Build;order.player=1;order.unitId=allyBuilder;
                    order.x=2560;order.z=800;std::snprintf(order.type,sizeof order.type,"lode");
                    sim::applyCommand(w,reg,order);
                }
                ai::Profile p;p.weight["lode"]=100;
                ai::Controller controller(0,reg,p,1,difficulty);
                const uint32_t lastHeld=kClaimTicks-uint32_t(ai::paramsFor(difficulty).thinkPeriod);
                auto expands=[&](uint32_t tick) {
                    for (const auto& c:think(controller,w,tick))
                        if (c.unitId==idle && c.kind==net::Cmd::Build) return c.x==2560 && c.z==800 ? 'A' : c.x==1600 && c.z==1792 ? 'F' : '?';
                    return '-';
                };
                switch (ally) {
                    case Ally::Idle:
                        check(expands(0)=='F' && expands(lastHeld)=='F' && expands(kClaimTicks)=='A',
                              "Normal and Easy leave a close ally's home deposit to it for two minutes, then take it");
                        break;
                    case Ally::Bound: {
                        bool held=expands(0)=='F';
                        for (uint32_t tick : {kClaimTicks,2*kClaimTicks}) {
                            w.unit(allyBuilder)->x-=sim::Fixed::fromInt(100);   // walking
                            held=held && expands(tick)=='F';
                        }
                        check(held,"Normal and Easy never race an ally's builder bound for its home deposit");
                        break;
                    }
                    default:
                        check(expands(0)=='A',"Normal and Easy take an enemy's home deposit");
                        break;
                }
            }
        // A builder holds its deposit only while it is getting there: one that has
        // come no nearer by 64 px for a minute of game time no longer reserves it,
        // for its own AI or an ally, and its own AI stops it and never sends it
        // back there. In a 4v4 a Zhon handler bound for a human ally's deposit
        // moved 109 px in 440 s and kept every ally off it for the rest of the game.
        constexpr uint32_t kStallTicks=60*30;
        for (auto owner : {Owner::Self,Owner::Ally})
            for (auto difficulty : {ai::Difficulty::Normal,ai::Difficulty::Absurd})
                for (bool walks : {true,false}) {
                    sim::World w;terrain(w);w.buildNavClasses(reg);
                    w.setManaSpots({{2000,800},{2000,1600}});
                    w.spawn(reg.find("factory"),1800,1000,0,0);
                    const int player=owner==Owner::Self ? 0 : 1;
                    const int walker=w.spawn(reg.find("worker"),1000,800,0,player);
                    const int idle=w.spawn(reg.find("worker"),1800,800,0,0);   // nearer the walker's deposit
                    if (player==1) w.setTeam(1,0);
                    w.player(0).mana=10000;
                    net::Command order;order.kind=net::Cmd::Build;order.player=uint8_t(player);order.unitId=walker;
                    order.x=2000;order.z=800;std::snprintf(order.type,sizeof order.type,"lode");
                    sim::applyCommand(w,reg,order);
                    ai::Profile p;p.weight["lode"]=100;
                    ai::Controller controller(0,reg,p,1,difficulty);
                    // A think every 20 s, the walker 100 px nearer by each when it walks.
                    auto takes=[&](const std::vector<net::Command>& out) {
                        return std::any_of(out.begin(),out.end(),[&](const auto& c) {
                            return c.unitId==idle && c.kind==net::Cmd::Build && c.x==2000 && c.z==800;
                        });
                    };
                    bool heldBefore=true,stopped=false;
                    std::vector<net::Command> cs;
                    for (uint32_t tick=0;tick<=kStallTicks;tick+=600) {
                        if (walks && tick) w.unit(walker)->x+=sim::Fixed::fromInt(100);
                        cs=think(controller,w,tick);
                        if (tick<kStallTicks && takes(cs)) heldBefore=false;
                        stopped=stopped || std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
                            return c.unitId==walker && c.kind==net::Cmd::Stop;
                        });
                    }
                    if (walks) {
                        check(heldBefore && !takes(cs) && !stopped,"a builder still gaining on its deposit keeps it reserved");
                        continue;
                    }
                    check(heldBefore && takes(cs),owner==Owner::Self
                          ? "a deposit its own builder has stalled short of for a minute goes to another builder"
                          : "a deposit an ally's builder has stalled short of for a minute goes to another builder");
                    check(stopped==(owner==Owner::Self),owner==Owner::Self ? "the AI stops its own stalled builder"
                                                                           : "the AI leaves an ally's stalled builder alone");
                    if (owner!=Owner::Self) continue;
                    net::Command stop;stop.kind=net::Cmd::Stop;stop.player=0;stop.unitId=walker;
                    sim::applyCommand(w,reg,stop);
                    char walkerSite='-';
                    for (const auto& c:think(controller,w,kStallTicks+60))
                        if (c.unitId==walker && c.kind==net::Cmd::Build) walkerSite=c.z==800 ? 'S' : 'F';
                    check(walkerSite=='F',"a stopped builder is sent to another deposit, not back to the one it stalled on");
                }
        // ...but one at work on an earlier order -- here a repair within reach, its
        // lodestone queued behind -- holds it however long the work takes. The
        // sim assigns the repair before the builder gets there, so one assigned it
        // but getting no nearer is stalled all the same.
        for (auto difficulty : {ai::Difficulty::Normal,ai::Difficulty::Absurd})
            for (bool atWork : {true,false}) {
                sim::World w;terrain(w);w.buildNavClasses(reg);
                w.setManaSpots({{2000,800},{2000,1600}});
                w.spawn(reg.find("factory"),1800,1000,0,0);
                const int repairer=w.spawn(reg.find("worker"),1000,800,0,1);
                const int hurt=w.spawn(reg.find("soldier"),atWork ? 1040 : 400,atWork ? 800 : 1600,0,1);
                const int idle=w.spawn(reg.find("worker"),1800,800,0,0);
                w.setTeam(1,0);w.player(0).mana=10000;
                w.unit(hurt)->hp=sim::Fixed::fromInt(50);
                w.repair(repairer,hurt,false);
                w.queueBuild(repairer,reg.find("lode"),2000,800,true);
                w.unit(repairer)->repairId=hurt;
                w.unit(repairer)->workScriptWorking=atWork;   // in reach and repairing, or not there yet
                ai::Profile p;p.weight["lode"]=100;
                ai::Controller controller(0,reg,p,1,difficulty);
                bool heldBefore=w.unit(repairer)->orders.size()==2,takenAtStall=false;
                for (uint32_t tick=0;tick<=kStallTicks;tick+=600) {
                    const auto cs=think(controller,w,tick);
                    const bool takes=std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
                        return c.unitId==idle && c.kind==net::Cmd::Build && c.x==2000 && c.z==800;
                    });
                    if (tick<kStallTicks) heldBefore=heldBefore && !takes; else takenAtStall=takes;
                }
                check(heldBefore && takenAtStall!=atWork,atWork
                      ? "an ally's builder repairing before its queued lodestone keeps the deposit reserved"
                      : "an ally's builder sent to repair that gets no nearer gives up its queued deposit");
            }
        // Retail builds a lodestone whenever it runs short of mana -- store under
        // 30% full, spending outpacing income -- with none unfinished and no
        // builder already on one; that lodestone then replaces the planner's
        // ordinary pick (icd 0x410810, 0x40c890, 0x40c830). Normal's income target
        // alone held it to one lodestone for a whole game on "4x4 team", where a
        // deposit pays 100 a second, however short of mana it ran.
        enum class Store { Short, Starved, Full, Covered, Walking };
        for (auto difficulty : {ai::Difficulty::Passive,ai::Difficulty::Easy,ai::Difficulty::Normal})
            for (auto store : {Store::Short,Store::Starved,Store::Full,Store::Covered,Store::Walking}) {
                sim::World w;terrain(w);w.buildNavClasses(reg);
                w.setManaSpots({{1200,800},{1200,1200},{1600,800}});
                w.spawn(reg.find("factory"),800,800,0,0);
                w.spawn(reg.find("lode"),1200,800,0,0);   // the first lodestone, already paying
                const int worker=w.spawn(reg.find("worker"),1000,1000,0,0);
                auto& me=w.player(0);
                me.income=110;me.storage=6000;   // income far past 25 + 20 per factory
                me.mana=store==Store::Full ? 3000 : store==Store::Starved ? 10 : 100;
                if (store==Store::Starved)   // broke with construction under way: no ordinary build
                    w.unit(w.spawn(reg.find("soldier"),800,1200,0,0))->underConstruction=true;
                for (auto& sample:me.displayResources.samples)   // per tick: earned, spent
                    sample={110.f/30,store==Store::Covered ? 110.f/30 : 10.f};
                if (store==Store::Walking) {
                    const int other=w.spawn(reg.find("worker"),1400,800,0,0);
                    net::Command order;order.kind=net::Cmd::Build;order.player=0;order.unitId=other;
                    order.x=1600;order.z=800;std::snprintf(order.type,sizeof order.type,"lode");
                    sim::applyCommand(w,reg,order);
                }
                ai::Profile p;p.weight["lode"]=100;
                ai::Controller controller(0,reg,p,1,difficulty);
                // Easy acts with one producer a think, in turn; tick 90 is the worker's.
                const auto cs=think(controller,w,difficulty==ai::Difficulty::Easy ? 90 : 0);
                const bool builds=std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
                    return c.unitId==worker && c.kind==net::Cmd::Build && std::string(c.type)=="lode";
                });
                switch (store) {
                    case Store::Short:
                        check(builds,"Normal, Easy and Defensive build a lodestone when short of mana, whatever their income");
                        break;
                    case Store::Starved:
                        check(builds,"a starved AI still starts that lodestone, though nothing else");
                        break;
                    case Store::Full:
                        check(!builds,"no lodestone while the store is 30% full or more");
                        break;
                    case Store::Covered:
                        check(!builds,"no lodestone while income covers spending");
                        break;
                    case Store::Walking:
                        check(!builds,"no second lodestone while a builder is on its way to one");
                        break;
                }
            }
        // ...within retail's base radius of home (0x40cf30): 800 px for a builder,
        // 560 for the Monarch, while the base is small. Searching Normal's own 1600
        // instead sent a builder 1250 px into an enemy base on Inner Circle for a
        // freed home deposit, where it died with the lodestone.
        for (auto difficulty : {ai::Difficulty::Passive,ai::Difficulty::Normal})
            for (bool monarch : {false,true})
                for (bool inside : {true,false}) {
                    sim::World w;terrain(w);w.buildNavClasses(reg);
                    w.spawn(reg.find("factory"),800,800,0,0);
                    w.spawn(reg.find("lode"),1200,800,0,0);   // home is the buildings' centre, (1000,800)
                    const float d=monarch ? (inside ? 500.f : 700.f) : (inside ? 700.f : 1100.f);
                    w.setManaSpots({{1200,800},{1000,800+d}});
                    auto king=*reg.find("worker");king.commander=monarch;
                    const int worker=w.spawn(&king,1000,1000,0,0);
                    auto& me=w.player(0);
                    me.income=110;me.storage=6000;me.mana=100;
                    for (auto& sample:me.displayResources.samples) sample={110.f/30,10.f};
                    ai::Profile p;p.weight["lode"]=100;
                    ai::Controller controller(0,reg,p,1,difficulty);
                    const auto cs=think(controller,w);
                    const bool builds=std::any_of(cs.begin(),cs.end(),[&](const auto& c) {
                        return c.unitId==worker && c.kind==net::Cmd::Build && std::string(c.type)=="lode";
                    });
                    // Defensive's own 600 px leash still holds its builders closer.
                    const bool expected=inside && !(difficulty==ai::Difficulty::Passive && !monarch);
                    check(builds==expected,monarch
                          ? (inside ? "the Monarch builds the short-of-mana lodestone within 560 px of home"
                                    : "the Monarch leaves a deposit beyond 560 px of home to the ordinary ladder")
                          : (inside ? "a builder builds the short-of-mana lodestone within 800 px of home"
                                    : "a builder leaves a deposit beyond 800 px of home to the ordinary ladder"));
                }
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
            for (auto difficulty : {ai::Difficulty::Passive,ai::Difficulty::Normal,ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
                sim::World w;terrain(w);w.buildNavClasses(reg);w.player(0).mana=10000;
                w.setManaSpots({{2800,800}});
                auto king=*reg.find("araking");king.commander=false; // expansion builder using the same menu
                w.spawn(&king,400,800,0,0);
                ai::Profile p;p.weight["aralode"]=100;
                ai::Controller controller(0,reg,p,1,difficulty,{{3400,800}});
                auto cs=think(controller,w);
                bool build=std::any_of(cs.begin(),cs.end(),[](const auto& c){return c.kind==net::Cmd::Build;});
                check(build==(difficulty==ai::Difficulty::Hard || difficulty==ai::Difficulty::Absurd),"Hard and Absurd expand beyond the normal and defensive home limits");
            }
            for(auto difficulty:{ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
                sim::World w;terrain(w);w.buildNavClasses(reg);w.player(0).mana=10000;
                w.setManaSpots({{2800,800},{2800,1200}});
                auto builder=*reg.find("araking");builder.commander=false;
                const int first=w.spawn(&builder,400,800,0,0);
                const int second=w.spawn(&builder,400,1200,0,0);
                ai::Profile p;p.weight["aralode"]=100;
                ai::Controller controller(0,reg,p,1,difficulty);
                const auto cs=think(controller,w);
                std::vector<net::Command> sites;
                for(const auto& c:cs)if(c.kind==net::Cmd::Build)sites.push_back(c);
                check(sites.size()==2 && (sites[0].x!=sites[1].x || sites[0].z!=sites[1].z),
                      "expansion builders reserve different mana spots before lockstep applies orders");
                // The real order list: a move, then the build queued behind it.
                net::Command move;move.kind=net::Cmd::Move;move.player=0;move.unitId=first;move.x=1600;move.z=800;
                sim::applyCommand(w,reg,move);
                net::Command build=move;build.kind=net::Cmd::Build;build.x=2800;build.queue=1;
                std::snprintf(build.type,sizeof build.type,"aralode");
                sim::applyCommand(w,reg,build);
                check(w.unit(first)->orders.size()==2 && w.unit(first)->orders.back().buildType==reg.find("aralode"),
                      "the expansion route holds the build behind its move");
                // Idle beside the reserved deposit, which is nearer than the free one.
                w.unit(second)->x=sim::Fixed::fromInt(2400);w.unit(second)->z=sim::Fixed::fromInt(800);
                const auto later=think(controller,w,uint32_t(ai::paramsFor(difficulty).thinkPeriod));
                check(std::any_of(later.begin(),later.end(),[](const auto& c){return c.kind==net::Cmd::Build && c.z==1200;}),
                      "expansion respects a mana site reserved behind movement waypoints");
            }
            {
                sim::World w;terrain(w);w.buildNavClasses(reg);w.player(0).mana=5000;
                w.spawn(reg.find("araking"),400,800,0,0);
                ai::Profile p;p.weight["aralode"]=100;
                ai::Controller controller(0,reg,p,1,ai::Difficulty::Hard);
                check(think(controller,w).empty(),"AI does not buy nonproducing lodestones on a map without mana spots");
                w.setManaSpots({{800,800}});
                const auto cs=think(controller,w,20);
                check(std::any_of(cs.begin(),cs.end(),[](const auto& c){return c.kind==net::Cmd::Build;}),
                      "AI still builds a lodestone where a mana deposit exists");
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
                // Start from the actual inland Veruna spawn, with the ordinary
                // land-factory budget already filled. The AI must find a coast,
                // construct the Sea Fort and complete a ship through normal orders.
                sim::World naval;naval.setVisPlayer(-1);
                sim::MatchConfig config;config.vfs=&vfs;
                config.mapPath=hpi::findMap(vfs,"Varro Passage");
                config.slots={{true,2,0,1.0f},{true,0,1,1.0f}};
                const auto starts=sim::setupMatch(naval,reg,config);
                naval.spawn(reg.find("verkeep"),256,2800,0,0);
                naval.spawn(reg.find("vercastl"),256,2100,0,0);
                ai::Profile p;p.weight["verasy"]=100;p.weight["verscout"]=100;
                p.weight["verflag"]=10000; // tempting larger ship whose launch footprint is blocked
                ai::Controller controller(0,reg,p,1,ai::Difficulty::Normal,{starts.back()});
                naval.player(0).mana=100000;naval.player(0).income=40;
                const auto cs=think(controller,naval);
                check(std::any_of(cs.begin(),cs.end(),[](const auto& c) {
                    return c.kind==net::Cmd::Build && std::string(c.type)=="verasy";
                }),"Veruna establishes a shipyard beyond the old search radius despite full land production");
                for(const auto& c:cs)sim::applyCommand(naval,reg,c);
                bool ship=false;
                for(uint32_t tick=1;tick<=18000 && !ship;++tick) {
                    naval.player(0).mana=100000;naval.player(0).income=40;
                    controller.tick(naval,tick,[&](const auto& c){sim::applyCommand(naval,reg,c);});
                    naval.tick(1.f/30.f);
                    ship=std::any_of(naval.units().begin(),naval.units().end(),[](const auto& u) {
                        return u.alive() && !u.underConstruction && u.type && u.type->id=="verscout";
                    });
                }
                check(ship,"AI completes a Sea Fort and launches a Scout on Varro Passage");
            }
            {
                sim::World naval;naval.setVisPlayer(-1);
                sim::MatchConfig config;config.vfs=&vfs;
                config.mapPath=hpi::findMap(vfs,"Varro Passage");
                config.slots={{true,2,0,1.0f},{true,0,1,1.0f}};config.startSeed=1;
                const auto starts=sim::setupMatch(naval,reg,config);
                const auto p=ai::loadProfile(vfs);
                ai::Controller controller(0,reg,p,1,ai::Difficulty::Normal,{starts.back()});
                bool ship=false;
                for(uint32_t tick=0;tick<12000 && !ship;++tick) {
                    controller.tick(naval,tick,[&](const auto& c){sim::applyCommand(naval,reg,c);});
                    naval.tick(1.f/30.f);
                    ship=std::any_of(naval.units().begin(),naval.units().end(),[](const auto& u) {
                        return u.player==0 && u.alive() && !u.underConstruction && u.type &&
                            u.type->domain==sim::UnitType::Domain::Water;
                    });
                }
                check(ship,"Normal AI produces boats from the actual opening economy and retail build profile");
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
