// Hard's and Absurd's economy and army make-up, as decisions of a real
// ai::Controller on constructed bases: what its producers are ordered to build,
// and where, in one think.
#include "ai/ai.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>
using namespace tak;
static int fails=0;
static void check(bool ok,const char* label) {
    std::printf("[%s] %s\n",ok ? "PASS" : "FAIL",label);if (!ok) ++fails;
}
// A flat 4096 px map under the room's default FOG OF WAR, EXPLORED: the whole
// map's ground (its deposits too) is known from the start, units only by sight.
static void terrain(sim::World& w) {
    w.setTerrain(std::vector<uint8_t>(256*256,100),256,256,20);
    w.setRoomVision(true,false);
}
static std::vector<net::Command> think(ai::Controller& ai,sim::World& w,uint32_t tick=0) {
    std::vector<net::Command> out;ai.tick(w,tick,[&](const auto& c){out.push_back(c);});
    return out;
}
static int orders(const std::vector<net::Command>& cs,net::Cmd kind,const char* type) {
    return int(std::count_if(cs.begin(),cs.end(),[&](const auto& c){return c.kind==kind && std::string(c.type)==type;}));
}
// A producer that already has work, which the AI leaves alone: a factory with
// a unit queued, a mobile builder with a build order.
static void busy(sim::World& w,int id,const sim::UnitType* making) {
    auto& u=*w.unit(id);
    if (u.type->isStructure()) { u.buildQueue.push_back(making); return; }
    sim::Order o;o.buildType=making;u.orders.push_back(o);
}
// Short of mana as retail's trigger reads it: over the last second more mana
// was asked for than came in (Player::displayResources; per tick: earned, asked).
static void shortOfMana(sim::World& w,int player,float mana) {
    auto& me=w.player(player);
    me.mana=mana;
    for (auto& sample:me.displayResources.samples) sample={me.income/30,me.income/30+10};
}

// The roster: ARA has a factory building (keep), a constructor (mason) and a
// miner whose whole menu is the lodestone; ZON has no factory building, so its
// handlers are its factories and its lodestone builders at once.
struct Roster {
    std::shared_ptr<hpi::Vfs::Files> files=std::make_shared<hpi::Vfs::Files>();
    hpi::Vfs vfs;
    sim::TypeRegistry reg;
    ai::Profile profile;
    void file(const std::string& path,const std::string& text) {
        std::string lines;
        for (char ch : text) { lines += ch; if (ch == ';' || ch == '{' || ch == '}') lines += '\n'; }
        (*files)[path]={lines.begin(),lines.end()};
    }
    void type(const std::string& id,const std::string& fields) {
        file("units/"+id+".fbi","[UNITINFO]{unitname="+id+";objectname="+id+
            ";maxdamage=100;footprintx=2;footprintz=2;buildtime=30;buildcost=10;sightdistance=600;"+fields+"}");
    }
    const sim::UnitType* operator()(const char* id) const {return reg.find(id);}
    Roster() {
        const std::string mobile="canmove=1;maxvelocity=1;";
        type("arakeep","side=ARA;builder=1;workertime=30;");
        type("aramason","side=ARA;builder=1;workertime=30;"+mobile);
        type("araminer","side=ARA;builder=1;workertime=30;"+mobile);
        type("aralode","side=ARA;yardmap=SSSS;mogriumincome=10;mogriumstorage=1000;");
        type("arasword","side=ARA;"+mobile);
        type("arabow","side=ARA;"+mobile);
        type("arahawk","side=ARA;"+mobile);
        file("canbuild/arakeep/arasword.tdf","[CANBUILD]{sortbias=0;}");
        file("canbuild/aramason/arakeep.tdf","[CANBUILD]{sortbias=0;}");
        file("canbuild/araminer/aralode.tdf","[CANBUILD]{sortbias=0;}");
        // A second keep that trains only scouts, and one that trains only archers.
        type("araeyrie","side=ARA;builder=1;workertime=30;");
        type("ararange","side=ARA;builder=1;workertime=30;");
        file("canbuild/araeyrie/arahawk.tdf","[CANBUILD]{sortbias=0;}");
        file("canbuild/ararange/arabow.tdf","[CANBUILD]{sortbias=0;}");
        type("zonking","side=ZON;builder=1;commander=1;workertime=30;"+mobile);
        file("canbuild/zonking/zonhand.tdf","[CANBUILD]{sortbias=0;}");
        type("zonhand","side=ZON;builder=1;workertime=30;"+mobile);
        type("zonlode","side=ZON;yardmap=SSSS;mogriumincome=10;mogriumstorage=1000;");
        type("zongob","side=ZON;"+mobile);
        type("zontroll","side=ZON;"+mobile);
        for (const char* made : {"zonlode","zongob","zontroll"})
            file(std::string("canbuild/zonhand/")+made+".tdf","[CANBUILD]{sortbias=0;}");
        vfs.setMapFiles(files);
        reg.loadDir(vfs,"units");reg.loadBuildTree(vfs,"canbuild");
        // Fighters: a primary weapon that does damage (the wave pool's test).
        for (const char* id : {"arasword","arabow","zongob","zontroll"}) {
            auto* t=const_cast<sim::UnitType*>(reg.find(id));
            t->weapon.damage=10;t->weapon.range=120;t->weapons={t->weapon};
        }
        for (const char* id : {"arakeep","araeyrie","ararange","aramason","araminer","aralode","arasword","arabow",
                               "zonhand","zonlode","zongob","zontroll"}) profile.weight[id]=10;
        // The scout and the archer as ai/default.txt has them: a limit of 4 each.
        profile.weight["arahawk"]=1;profile.limit["arahawk"]=4;profile.limit["arabow"]=4;
    }
};

// One factory per 60 income at Hard and Absurd: a working factory spends 30 to
// 120 mana a second, so the old target of one per 25 kept them broke. A store
// filling up says production is not spending the income: one more factory.
static void factoryTarget(const Roster& r) {
    auto buildsKeep=[&](ai::Difficulty difficulty,float income,int keeps,float mana=6000) {
        sim::World w;terrain(w);w.buildNavClasses(r.reg);
        w.player(0).storage=20000;w.player(0).mana=mana;w.player(0).income=income;
        for (int i=0;i<keeps;++i) busy(w,w.spawn(r("arakeep"),400+float(i%8)*200,400+float(i/8)*200,0,0),r("arasword"));
        w.spawn(r("aramason"),1100,1300,0,0);
        ai::Controller controller(0,r.reg,r.profile,1,difficulty);
        return orders(think(controller,w),net::Cmd::Build,"arakeep")==1;
    };
    for (auto level : {ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
        check(buildsKeep(level,300,5),"Hard/Absurd add a factory below one per 60 income");
        check(!buildsKeep(level,300,6),"Hard/Absurd stop at one factory per 60 income (and one)");
        check(!buildsKeep(level,300,12),"Hard/Absurd do not build a thirteenth factory on 300 income");
        check(!buildsKeep(level,300,8) && buildsKeep(level,300,8,15000),
              "with the store over 60% full Hard/Absurd build one more factory than they have");
    }
    // The other levels keep their own targets: Normal one per 40 (eight at most),
    // the defensive levels one per 25 without a ceiling.
    check(buildsKeep(ai::Difficulty::Normal,300,7) && !buildsKeep(ai::Difficulty::Normal,300,8),
          "Normal keeps one factory per 40 income, eight at most");
    check(buildsKeep(ai::Difficulty::Defensive,300,12) && !buildsKeep(ai::Difficulty::Defensive,300,13),
          "Defensive keeps one factory per 25 income");
}

// Broke, with work in hand, Hard and Absurd start nothing new -- but the one
// lodestone retail's AI builds when it runs short of mana, as the other levels do.
static void shortOfManaLodestone(const Roster& r) {
    auto lodes=[&](ai::Difficulty difficulty,float mana,bool underWay) {
        sim::World w;terrain(w);w.buildNavClasses(r.reg);
        w.setManaSpots({{1216,816},{1616,816}});
        w.player(0).storage=5000;w.player(0).income=100;
        shortOfMana(w,0,mana);
        w.spawn(r("arakeep"),800,800,0,0);
        const int site=w.spawn(r("arakeep"),800,1200,0,0);w.unit(site)->underConstruction=true;
        w.spawn(r("araminer"),1000,1000,0,0);
        w.spawn(r("araminer"),1000,1100,0,0);
        if (underWay) {
            const int other=w.spawn(r("araminer"),1600,1000,0,0);
            sim::Order o;o.buildType=r("aralode");o.buildX=sim::Fixed::fromInt(1616);o.buildZ=sim::Fixed::fromInt(816);
            w.unit(other)->orders.push_back(o);
        }
        ai::Controller controller(0,r.reg,r.profile,1,difficulty);
        return orders(think(controller,w),net::Cmd::Build,"aralode");
    };
    for (auto level : {ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
        check(lodes(level,10,false)==1,"Hard/Absurd, broke with work in hand, start one lodestone");
        check(lodes(level,10,true)==0,"... one at a time: none while a builder is on one");
    }
    check(lodes(ai::Difficulty::Normal,10,false)==1,"Normal's short-of-mana lodestone is unchanged");
}

// A full store: more income buys nothing. Hard and Absurd then build no
// lodestone; the builder is free for the next think.
static void fullStore(const Roster& r) {
    auto buildsLode=[&](ai::Difficulty difficulty,float mana) {
        sim::World w;terrain(w);w.buildNavClasses(r.reg);
        w.setManaSpots({{1216,816}});
        w.player(0).storage=10000;w.player(0).mana=mana;w.player(0).income=300;
        for (int i=0;i<6;++i) busy(w,w.spawn(r("arakeep"),400+float(i)*200,400,0,0),r("arasword"));
        w.spawn(r("araminer"),1000,1000,0,0);
        ai::Controller controller(0,r.reg,r.profile,1,difficulty);
        return orders(think(controller,w),net::Cmd::Build,"aralode")==1;
    };
    for (auto level : {ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
        check(buildsLode(level,5000),"Hard/Absurd keep adding lodestones while the store has room");
        check(!buildsLode(level,9500),"Hard/Absurd build no lodestone beside a full store");
    }
}

// With a unit cap set, lodestones take a quarter of it at most, none is started
// in the last twentieth of it, and an order given but not yet a unit counts.
static void unitCap(const Roster& r) {
    // `lodes` lodestones standing, `army` swordsmen, then an idle miner beside a
    // free deposit and an idle keep.
    struct Result { int lodes,swords; };
    auto room=[&](ai::Difficulty difficulty,int cap,int lodes,int army,bool queued) {
        sim::World w;terrain(w);w.buildNavClasses(r.reg);
        std::vector<std::pair<float,float>> spots{{1216,1408}};
        for (int i=0;i<lodes;++i) spots.emplace_back(400+float(i%10)*128,3008+float(i/10)*128);
        w.setManaSpots(spots);
        w.setUnitCap(cap);
        w.player(0).storage=40000;w.player(0).mana=8000;w.player(0).income=300;
        w.spawn(r("araminer"),1000,1400,0,0);
        w.spawn(r("arakeep"),800,800,0,0);
        if (queued) {
            // A second keep whose queued swordsman has not begun: no unit yet.
            const int keep=w.spawn(r("arakeep"),1400,800,0,0);
            w.unit(keep)->buildQueue.push_back(r("arasword"));
        }
        for (int i=0;i<lodes;++i) w.spawn(r("aralode"),spots[size_t(i)+1].first,spots[size_t(i)+1].second,0,0);
        for (int i=0;i<army;++i) w.spawn(r("arasword"),2000+float(i%20)*40,2000+float(i/20)*40,0,0);
        ai::Controller controller(0,r.reg,r.profile,1,difficulty);
        const auto cs=think(controller,w);
        return Result{orders(cs,net::Cmd::Build,"aralode"),orders(cs,net::Cmd::Train,"arasword")};
    };
    for (auto level : {ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
        check(room(level,40,9,5,false).lodes==1,"below a quarter of the cap a lodestone is still built");
        check(room(level,40,10,5,false).lodes==0,"lodestones stop at a quarter of the unit cap");
        check(room(level,0,10,5,false).lodes==1,"with no unit cap the lodestones have no share to keep to");
        // 38 of 40: two slots left. The miner comes first in unit order.
        const auto last=room(level,40,2,34,false);
        check(last.lodes==0 && last.swords==1,"the last slots under the cap go to fighters, not lodestones");
        check(room(level,40,2,33,false).lodes==1,"before the last twentieth of the cap a lodestone is still built");
    }
    // 39 of 40, and one more swordsman queued: the cap is spoken for.
    for (auto level : {ai::Difficulty::Normal,ai::Difficulty::Hard,ai::Difficulty::Absurd,ai::Difficulty::Defensive}) {
        const auto full=room(level,40,0,36,true);
        check(full.swords==0 && full.lodes==0,"an order not yet begun counts against the unit cap");
        check(room(level,40,0,35,true).swords==1,"... and one slot below it a fighter is still trained");
    }
}

// Zhon's handlers are its factories, and conjure all the time they are not away
// at a lodestone: Hard and Absurd keep as many as their income feeds (one per 60)
// and a quarter more for the ones away, not another side's whole builder
// allowance on top.
static void zhonAllowance(const Roster& r) {
    auto conjuresHandler=[&](ai::Difficulty difficulty,float income,int handlers) {
        sim::World w;terrain(w);w.buildNavClasses(r.reg);
        w.player(0).storage=20000;w.player(0).mana=6000;w.player(0).income=income;
        w.spawn(r("zonking"),800,800,0,0);
        for (int i=0;i<handlers;++i) busy(w,w.spawn(r("zonhand"),1400+float(i%6)*60,1400+float(i/6)*60,0,0),r("zongob"));
        for (int i=0;i<handlers*4;++i) w.spawn(r("zongob"),2000+float(i%16)*40,400+float(i/16)*40,0,0);
        ai::Controller controller(0,r.reg,r.profile,1,difficulty);
        return orders(think(controller,w),net::Cmd::Build,"zonhand")==1;
    };
    for (auto level : {ai::Difficulty::Hard,ai::Difficulty::Absurd}) {
        // Income 300: six factories' worth, and two away at lodestones.
        check(conjuresHandler(level,300,7),"Hard/Absurd Zhon adds producers up to one per 60 income and a quarter more");
        check(!conjuresHandler(level,300,8),"Hard/Absurd Zhon keeps no more producers than its income feeds");
    }
}

int main() {
    const Roster roster;
    factoryTarget(roster);
    shortOfManaLodestone(roster);
    fullStore(roster);
    unitCap(roster);
    zhonAllowance(roster);
    std::printf("%s (%d failed)\n",fails ? "FAILED" : "OK",fails);
    return fails ? 1 : 0;
}
