// The Classic AI level's building blocks (src/ai/classic*): the retail AI profile
// baker, the per-type facts and the bookkeeping clock with its dynamic build
// priorities on a fixed World, the squad layer (constructor rolls, assignment),
// and (with a data directory) the shipped profile through the real VFS and a
// Classic seat's squads and its factories' production in games on Abnar's Terrace.
//
//   classic_ai_test                 unit tests
//   classic_ai_test <retail-root>   + the shipped data checks
//   classic_ai_test --bases <retail-root>   the P4b games: a Classic seat of each
//       faction builds its base on Abnar's Terrace for 10 game-minutes
//   classic_ai_test --raid <retail-root>    the P5 game: a scripted raid into a
//       Classic base (defenders sent at the raiders, the Monarch's retreat)
//   classic_ai_test --profile       oracle mode for tools/re/check_ai_profile.py:
//       stdin  "types <n>" then n lines "<name> <category words...>", then any
//              number of "case <hex script>" lines
//       stdout per case, for each catalogue index 1..n: "pref limit prefLock limitLock"
//   classic_ai_test --printweights <retail-root>
//       oracle mode for tools/re/check_ai_printweights.py: the shipped types and
//       Abnar's Terrace's profile, as a Classic seat sees them. stdin lines:
//         facts <name>   -> "<found> <flags> <secondary> <minwaterdepth> <cost bits> <pref> <limit>"
//         state <population> <stored> <capacity> <n> (<name> <count> <completed>)*n
//               <30 x (income usage)>   -> no output; sets the table (retail's order)
//         creation <0|1> -> 1: the manager's creation pass, before the profile
//                           file is read (every limit still -1)
//         seed <s>       -> one priority pass over that table from seed s:
//                           "<priority>*n <final seed> <draws>"
//   classic_ai_test --squad-ctor    oracle mode for tools/re/check_ai_squad_ctor.py
//   classic_ai_test --assignment    oracle mode for tools/re/check_ai_assignment.py
//   classic_ai_test --factory-think oracle mode for tools/re/check_ai_factory_think.py
//   classic_ai_test --placement     oracle mode for tools/re/check_ai_placement.py
//   classic_ai_test --constructor   oracle mode for tools/re/check_ai_constructor.py
//   classic_ai_test --static-grade  oracle mode for tools/re/check_ai_static_grade.py
//   classic_ai_test --threat        oracle mode for tools/re/check_ai_threat.py
//   classic_ai_test --defence       oracle mode for tools/re/check_ai_defence.py
//   classic_ai_test --retreat       oracle mode for tools/re/check_ai_retreat.py
//       (line layouts in those scripts and in the oracle functions below)
#include "ai/ai.h"
#include "ai/classicbrain.h"
#include "ai/classicdefence.h"
#include "ai/classicprofile.h"
#include "ai/classicplacement.h"
#include "ai/classicproduction.h"
#include "ai/classictypes.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/retailconstruction.h"
#include "sim/sim.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace tak;
static int fails=0;
static void check(bool ok,const char* label) {
    std::printf("[%s] %s\n",ok ? "PASS" : "FAIL",label);if (!ok) ++fails;
}

static int profileOracle() {
    std::string line,word;
    ai::ClassicCatalogue catalogue;
    size_t n=0;
    if (!std::getline(std::cin,line)) return 2;
    {std::istringstream in(line);in>>word>>n;if (word!="types") return 2;}
    std::vector<std::pair<std::string,std::vector<std::string>>> entries;
    for (size_t i=0;i<n && std::getline(std::cin,line);++i) {
        std::istringstream in(line);std::string name;in>>name;
        std::vector<std::string> cats;while (in>>word) cats.push_back(word);
        entries.push_back({name,cats});
    }
    // The oracle hands the names in retail's table order already.
    for (const auto& [name,cats] : entries) {
        std::string words;for (const auto& c : cats) words+=c+" ";
        catalogue.add(nullptr,name,words);
    }
    while (std::getline(std::cin,line)) {
        if (line.rfind("case ",0)!=0) continue;
        std::string script;
        for (size_t i=5;i+1<line.size();i+=2) script.push_back(char(std::stoi(line.substr(i,2),nullptr,16)));
        ai::ClassicProfile p(catalogue.size());p.execute(script,catalogue);
        for (size_t t=1;t<catalogue.size();++t)
            std::printf("%s%u %d %u %u",t>1 ? " " : "",unsigned(p.pref[t]),p.limit[t],
                        unsigned(p.prefLocked[t]),unsigned(p.limitLocked[t]));
        std::printf("\n");
    }
    return 0;
}

static ai::ClassicCatalogue sample() {
    return ai::ClassicCatalogue::synthetic({
        {"araarch",{"ARA"}},{"arabuild",{"ARA","builder"}},{"aracastl",{"ARA"}},{"aradrag",{"ARA"}},
        {"arakeep",{"ARA"}},{"aralode",{"ARA"}},{"arasiege",{"ARA"}},{"tararch",{"TAR"}},
    });
}

static void profileRules() {
    const auto c=sample();
    auto at=[&](const ai::ClassicProfile&,const char* name){return size_t(c.byName.at(name));};
    check(c.size()==9 && c.names[0].empty() && c.names[1]=="araarch" && c.names[8]=="tararch",
          "the catalogue is name-ordered behind retail's empty slot 0");
    {
        ai::ClassicProfile p(c.size());
        bool fresh=true;
        for (size_t t=0;t<c.size();++t) fresh=fresh && p.pref[t]==100 && p.limit[t]==-1 && !p.prefLocked[t] && !p.limitLocked[t];
        check(fresh,"every type starts at preference 100, unlimited, unlocked");
    }
    {
        // The weight store's emulated values (0x410c80 on a fresh 100).
        ai::ClassicProfile p(c.size());
        p.execute("weight araarch 0.5\nweight arabuild 0.999\nweight aracastl 0.019\nweight aradrag 0.0099\n"
                  "weight arakeep 2\nweight aralode -1\nweight arasiege 25\nweight tararch 0\n",c);
        check(p.pref[at(p,"araarch")]==50 && p.pref[at(p,"arabuild")]==99 && p.pref[at(p,"aracastl")]==1 &&
              p.pref[at(p,"aradrag")]==0 && p.pref[at(p,"arakeep")]==100 && p.pref[at(p,"aralode")]==0 &&
              p.pref[at(p,"arasiege")]==100 && p.pref[at(p,"tararch")]==0,
              "weight = clamp(trunc(100 * w), 0, 100): 0.5->50 0.999->99 0.019->1 0.0099->0 2->100 -1->0 25->100 0->0");
        p.execute("weight araarch 2\nweight tararch 5\n",c);
        check(p.pref[at(p,"araarch")]==50 && p.pref[at(p,"tararch")]==0 && p.prefLocked[at(p,"tararch")],
              "an exact unit line locks its type: the first writer wins, a zero weight included");
    }
    {
        ai::ClassicProfile p(c.size());
        p.execute("weight ALL 0.5\nweight all 0.5\nweight araarch 0.5\nweight ara 2\n",c);
        check(p.pref[at(p,"arabuild")]==50 && p.pref[at(p,"araarch")]==12 && p.pref[at(p,"tararch")]==25 &&
              !p.prefLocked[at(p,"arabuild")] && p.prefLocked[at(p,"araarch")],
              "category lines (ALL, the FBI category words) compound without locking; an exact line scales the result");
    }
    {
        ai::ClassicProfile p(c.size());
        p.execute("// Kingdoms AI Profile\r\n\r\nWEIGHT ARAARCH 0.25\r\n  limit\tarabuild 3\r\nlimit arabuild 9\r\n"
                  "bogus foo 1\r\nweight arakeep 0.5 extra tokens\r\nlimit aradrag\r\nweight araseige 0\r\n"
                  "weight aracastl 0.5 # 0.1\r\n# weight aralode 0\r\nlimit #aralode 1\r\n",c);
        check(p.pref[at(p,"araarch")]==25 && p.limit[at(p,"arabuild")]==3 && p.pref[at(p,"arakeep")]==50 &&
              p.limit[at(p,"aradrag")]==0 && p.limitLocked[at(p,"aradrag")] && p.pref[at(p,"aracastl")]==50 &&
              p.pref[at(p,"aralode")]==100 && p.limit[at(p,"aralode")]==-1,
              "case-insensitive commands and names, CRLF and tabs, // and # comments, extra tokens ignored, a missing value is 0");
        check(p.pref[at(p,"arasiege")]==100 && p.limit[at(p,"arasiege")]==-1,
              "a misspelt name (araseige) does nothing: the Rolling Tower stays 100 and unlimited");
        check(p.allows(at(p,"arabuild"),2) && !p.allows(at(p,"arabuild"),3) && !p.allows(at(p,"aradrag"),0) &&
              p.allows(at(p,"arasiege"),100000),"the limit check: -1 unlimited, else count < limit");
    }
    check(ai::classicAtof("1.5x")==1.5f && ai::classicAtof("  -2")==-2.0f && ai::classicAtof("1e2")==100.0f &&
          ai::classicAtof("1d2")==100.0f && ai::classicAtof(".5")==0.5f && ai::classicAtof("abc")==0.0f &&
          ai::classicAtof("1e")==1.0f,"atof: prefix parse, Fortran d exponent, junk is 0");
    check(ai::classicAtoi("12abc")==12 && ai::classicAtoi(" -7")==-7 && ai::classicAtoi("4294967297")==1 &&
          ai::classicAtoi("x")==0,"atoi: prefix parse with the C library's wrap-around");
    check(ai::classicScaledPref(64,67108864.0f)==0 && ai::classicScaledPref(65,67108864.0f)==100 &&
          ai::classicScaledPref(100,1e30f)==0 && ai::classicScaledPref(0,1e38f*10.0f)==0,
          "the store keeps _ftol's low 32 bits: huge weights wrap or give 0");
    {
        // The Park-Miller fixed point: a seed that is a multiple of 2^31-1.
        const int player=1;
        const uint32_t room=0x7fffffffu^0x66e29572u^(0x9e3779b9u*uint32_t(player+1));
        check(sim::retailSeed(room^(0x9e3779b9u*uint32_t(player+1)))==0x7fffffffu,
              "this room seed would give retail's seeding the fixed point");
        ai::ClassicBrain b(player,room);
        const uint32_t s0=b.seed();b.rand(1000);
        check(s0!=0x7fffffffu && b.seed()!=s0,"seedFor steps off the fixed point, so the stream moves");
    }
}

static void typeFacts() {
    bool site=false,lode=false;
    auto flags=[&](const char* ym,int fx,int fz){ai::classicYardMapFlags(ym,fx,fz,site,lode);return std::pair{site,lode};};
    check(flags("S",3,3)==std::pair{true,true} && flags(" S",2,2)==std::pair{true,true} &&
          flags("oooS",2,2)==std::pair{true,false} && flags("ooo ooo ooS",3,3)==std::pair{true,false} &&
          flags("oS",1,1)==std::pair{false,false} && flags("o",3,3)==std::pair{false,false} &&
          flags("xSo",1,1)==std::pair{true,true} && flags("",2,2)==std::pair{false,false},
          "yardmap walk: S anywhere in the cells = mana site, S first = lodestone class; the last char repeats, unknown chars are skipped");
}

// A self-contained registry (the same synthetic-VFS trick as ai_behavior_test).
static sim::TypeRegistry syntheticRegistry() {
    auto files=std::make_shared<hpi::Vfs::Files>();
    auto file=[&](const std::string& path,const std::string& text) {
        std::string lines;
        for (char ch : text) { lines += ch; if (ch == ';' || ch == '{' || ch == '}') lines += '\n'; }
        (*files)[path]={lines.begin(),lines.end()};
    };
    auto type=[&](const char* id,const char* fields,const char* extra="") {
        file(std::string("units/")+id+".fbi",std::string("[UNITINFO]{unitname=")+id+";objectname="+id+
             ";side=ARA;maxdamage=100;footprintx=2;footprintz=2;buildtime=30;buildcost=10;"+fields+"}"+extra);
    };
    type("soldier","canmove=1;maxvelocity=1;category=ARA LAND;","[WEAPON1]{damage=5;range=100;}");
    type("tower","yardmap=oooo;","[WEAPON2]{range=200;}");            // a weapon block, no damage
    type("lode","buildcost=0;buildtime=92;yardmap=S;mogriumincome=10;category=ARA;");
    type("mobilelode","canmove=1;maxvelocity=1;bmcode=1;yardmap=S;");  // yardmap ignored: bmcode 1
    type("ship","canmove=1;maxvelocity=1;minwaterdepth=20;floater=1;");
    type("worker","builder=1;workertime=30;canmove=1;maxvelocity=1;builddistance=90;sightdistance=300;");
    type("castle","builder=1;workertime=30;buildcost=2000;");
    type("plain","isfeature=1;");
    file("canbuild/worker/lode.tdf","[CANBUILD]{sortbias=0;}");
    file("canbuild/castle/soldier.tdf","[CANBUILD]{sortbias=0;}");
    hpi::Vfs vfs;vfs.setMapFiles(files);
    sim::TypeRegistry reg;reg.loadDir(vfs,"units");reg.loadBuildTree(vfs,"canbuild");
    return reg;
}

static void typeInfoAndPriorities() {
    const auto reg=syntheticRegistry();
    ai::ClassicBrain brain(0,11,reg);
    const uint32_t built=brain.seed();
    const auto& c=brain.catalogue();
    auto idx=[&](const char* n){return size_t(c.byName.at(n));};
    const auto& info=brain.typeInfo();
    check(c.size()==9 && info[idx("tower")].armed && reg.find("tower")->weapons.empty() && info[idx("soldier")].armed &&
          !info[idx("worker")].armed,"armed = any [WEAPONn] block, even one with no damage");
    check(info[idx("lode")].manaSite && info[idx("lode")].lodestoneClass && info[idx("lode")].cost==92 &&
          info[idx("lode")].bmcode==0 && info[idx("tower")].structure && !info[idx("tower")].manaSite,
          "a yardmap S building is a mana site in the lodestone class; buildcost 0 falls back to buildtime");
    check(!info[idx("mobilelode")].manaSite && info[idx("mobilelode")].bmcode==1,
          "the yardmap is read for bmcode 0 only (retail's default bmcode is 0)");
    check(info[idx("ship")].minWaterDepth==20 && info[idx("ship")].floater && info[idx("soldier")].minWaterDepth==-10000,
          "minwaterdepth (type+0x194) is carried, -10000 for land units");
    check(info[idx("worker")].builder && info[idx("worker")].buildDistance==90 && info[idx("worker")].sight==300 &&
          info[idx("worker")].maxDamage==100 && info[idx("castle")].menu && info[idx("worker")].menu && !info[idx("tower")].menu &&
          info[idx("plain")].isFeature && c.categories.at("land").size()==1,
          "builder flag, builddistance, sight, maxdamage, menus, isfeature and category words");
    {
        const auto in=info[idx("tower")].priorityInputs(3,2,7);
        check(in.flags==0x10000u && in.secondaryFlags==0 && in.count==3 && in.completed==2 && in.desired==7 &&
              in.weapon==-10000 && in.cost==10,"the adapter fills retailBuildPriority's inputs by their retail meaning");
    }

    // A fixed World: what the clock sees, and the priorities it computes. Costs
    // stay <= 500 except the castle, so most values are fixed numbers.
    sim::World w;
    {
        std::vector<uint8_t> h(64*64,100);w.setVisPlayer(-1);w.setTerrain(std::move(h),64,64,20);
    }
    w.spawn(reg.find("tower"),200,200,0,0);
    const int unfinished=w.spawn(reg.find("worker"),300,300,0,0);
    w.unit(unfinished)->underConstruction=true;
    w.spawn(reg.find("soldier"),400,400,0,1);   // another player's: not counted
    auto& me=w.player(0);me.mana=0;me.storage=1000;
    for (auto& sample : me.displayResources.samples) sample={0.0f,5.0f};   // draining: shortfall
    ai::ClassicProfileScript script{"ai/test.txt","limit worker 4\nlimit castle -1\n"};
    brain.useProfile(script);
    auto think=[&](uint32_t tick){brain.tick(w,tick,[](const net::Command&){});};
    think(0);
    const auto& pr=brain.priorities();
    check(brain.priorityRefreshes()==1 && pr[idx("soldier")]==84 && pr[idx("tower")]==84 &&
          pr[idx("worker")]==24 && pr[idx("lode")]==100 && pr[idx("ship")]==12 && pr[idx("plain")]==0,
          "the creation pass (in the constructor, before the profile) sees empty caches, no limits and no store: "
          "armed 21x4, a builder with limit -1 (+15, a third short of mana) x4, the empty store wants lodestones, "
          "naval x3, isfeature 0");
    check(brain.counts()[idx("tower")]==1 && brain.completed()[idx("tower")]==1 &&
          brain.counts()[idx("worker")]==1 && brain.completed()[idx("worker")]==0 && brain.counts()[idx("soldier")]==0,
          "the per-tick recount: own live units by type, and the finished ones");
    // Mirror the brain's stream through its construction: the AI manager's
    // squads and rolls first (0x40fed0), then the creation pass.
    uint32_t mirror=ai::ClassicBrain::seedFor(0,11);
    {
        ai::ClassicSquads squads;
        ai::classicConstructSquads(squads,0,[&](int n){return sim::retailRandom(mirror,n);});
        ai::classicDrawBlockGrids([&](int n){return sim::retailRandom(mirror,n);});
    }
    for (size_t t=1;t<c.size();++t)
        if (info[t].cost>500.0f) { // the creation pass's draws (only the castle costs > 500)
            sim::RetailBuildPriority in=info[t].priorityInputs(0,0,-1);
            sim::retailBuildPriority(in,uint16_t(0),0.0f,true,[&](int n){return sim::retailRandom(mirror,n);});
        }
    check(mirror==built,"the brain is built in retail's order: the manager's squad rolls, then the build "
          "manager's block grids (eight draws), then the creation pass, which draws only for types costing over "
          "500, at population 0");
    // The scheduler's draws now share the stream, so watch for the refresh.
    uint32_t refreshAt=1;
    while (refreshAt<10000) { think(refreshAt);if (brain.priorityRefreshes()>1) break;++refreshAt; }
    check(brain.priorityRefreshes()==2 && refreshAt%7==0,"a refresh comes only in a 7-tick window");
    check(brain.priorityRefreshes()==2 && pr[idx("tower")]==42 && pr[idx("worker")]==2 &&
          pr[idx("soldier")]==84 && pr[idx("lode")]==100,
          "a refresh uses the cached counts: one tower x2, an unfinished builder gets no builder bonus");
    me.mana=900;   // 90% full: no lodestone wanted
    for (auto& sample : me.displayResources.samples) sample={5.0f,0.0f};
    while (brain.priorityRefreshes()==2) think(++refreshAt);
    check(pr[idx("lode")]==0 && pr[idx("worker")]==2,"a full, growing store wants no lodestone");
    check(brain.buildWeight(idx("soldier"))==84 && brain.buildWeight(idx("worker"))==2 &&
          brain.buildWeight(idx("plain"))==0,"build weight = pref x priority / 100 under the limit");
}

static void shippedData(const char* root) {
    auto vfs=hpi::mountRetailRoot(root);
    sim::TypeRegistry reg;sim::setupRegistry(reg,vfs,false);
    const auto map=hpi::findMap(vfs,"Abnar's Terrace");
    const auto key=ai::classicMapAiProfile(vfs,map);
    check(key=="DEFAULT" || key=="default","a skirmish map's .ota names the DEFAULT profile");
    const auto script=ai::classicProfileScript(vfs,key);
    check(ai::classicProfileScript(vfs,"").path=="ai/default.txt" &&
          ai::classicProfileScript(vfs,"no such profile").path=="ai/default.txt" &&
          ai::classicProfileScript(vfs,"default.txt").text==script.text && script.path=="ai/"+key+".txt",
          "an empty or unreadable aiprofile falls back to ai/default.txt; an extension is dropped");
    // IPData.hpi's copy is the one that names Creon units.
    check(script.text.find("cre")!=std::string::npos,"the VFS serves IPData's default.txt (it covers Creon)");
    ai::ClassicBrain brain(1,7,reg);brain.useProfile(script);
    const auto& c=brain.catalogue();const auto& p=brain.profile();
    auto idx=[&](const char* n){auto it=c.byName.find(n);return it==c.byName.end() ? size_t(0) : size_t(it->second);};
    check(idx("arasiege") && p.pref[idx("arasiege")]==100 && p.limit[idx("arasiege")]==-1,
          "shipped default.txt leaves arasiege (spelt araseige there) at 100, unlimited");
    check(idx("aratrans") && p.limit[idx("aratrans")]==-1 && idx("araking") && p.limit[idx("araking")]==-1,
          "unlisted types (the Ark, the Monarch) are unlimited");
    check(idx("aragod") && p.pref[idx("aragod")]==0 && p.limit[idx("aragod")]==0 &&
          idx("araarch") && p.pref[idx("araarch")]==100 && p.limit[idx("araarch")]>0,
          "gods are switched off; ordinary troops are on with a limit");
    bool onOff=true;
    for (size_t t=1;t<c.size();++t) onOff=onOff && (p.pref[t]==0 || p.pref[t]==100);
    check(onOff,"every shipped weight is an integer, so every preference is 0 or 100");
    check(c.categories.count("ara") && c.categories.count("all") && c.categories.at("all").size()==c.size()-1,
          "categories come from the FBI category= words, plus ALL");
}


// --- P3: the squad layer -----------------------------------------------------

static void squadConstruction() {
    using K=sim::RetailAiSquadKind;
    uint32_t seed=12345;
    ai::ClassicSquads s;
    ai::classicConstructSquads(s,0,[&](int n){return sim::retailRandom(seed,n);});
    bool kinds=s.schedule.assignmentCountdown==30 && !s.schedule.groups[0].present && !s.schedule.groups[99].present &&
               !s.firstPassDone;
    for (int i=1;i<=98;++i) {
        const auto& q=s.squads[size_t(i)];
        const K want=i<=20 ? K::Base : i<=80 ? (i%2 ? K::Strike : K::Backup) : i<=95 ? K::Vtol : i==96 ? K::Reserve :
                     i==97 ? K::Strike : K::Backup;
        kinds=kinds && s.schedule.groups[size_t(i)].present && s.schedule.groups[size_t(i)].deadline==0 && q.kind==want;
        if (want==K::Strike)
            kinds=kinds && q.parameters[0]==i+1 && q.parameters[3]==6 &&
                  q.parameters[1]==(i<51 ? 200000 : i<97 ? 500000 : 1000000);
        if (want==K::Backup) kinds=kinds && q.parameters[0]==i-1;
    }
    check(kinds,"0x40fed0: bases 1..20, ground strike/backup pairs 21..50, naval 51..80, VTOL 81..95, homeless 96, "
                "mass attack 97/98; every clock at 0, the countdown at 30");
    // The quota ranges and the wave rolls' shape over many constructions.
    bool ranges=true,negative=false,positive=false,capped=true;
    for (uint32_t k=1;k<400;++k) {
        uint32_t r=k*2654435761u|1u;
        ai::classicConstructSquads(s,k%300,[&](int n){return sim::retailRandom(r,n);});
        for (int i=1;i<=20;++i) {
            const auto& p=s.squads[size_t(i)].parameters;
            if (i==1) ranges=ranges && p[1]>=1 && p[1]<=2 && p[2]>=2 && p[2]<=5 && p[3]>=0 && p[3]<=3 && p[4]>=0 && p[4]<=3;
            else ranges=ranges && p[1]>=1 && p[1]<=2 && p[2]>=1 && p[2]<=2 && p[3]>=0 && p[3]<=1 && p[4]>=0 && p[4]<=1;
            for (int w=5;w<=7;++w) { capped=capped && p[w]!=0 && std::abs(p[w])<=50;negative|=p[w]<0;positive|=p[w]>0; }
        }
    }
    check(ranges,"0x40c560 quotas: home base p1 1..2, p2 2..5, p3/p4 0..3; other bases 1..2, 1..2, 0..1, 0..1");
    check(capped && negative && positive,"wave sizes are never 0, at most 50 in size, and either sign");
    // Draw counts of the rolls, by hand: rand(4)==0 skips the added terms.
    {
        std::vector<int> script{0,0, 59, 1, 1};   // rand(max(p2,2)), rand(4)=0, rand(60)!=0, rand(6)!=0, rand(2)!=0, sign
        size_t at=0;std::vector<int> asked;
        auto fake=[&](int n){asked.push_back(n);return at<script.size() ? script[at++]%std::max(n,1) : 1;};
        const int32_t p=ai::classicGroundWave(3,120,fake);
        check(p==1 && asked==std::vector<int>({3,4,60,6,2,2}),"ground roll: rand(4)==0 skips both enemy-size terms");
        asked.clear();at=0;script={1,1,1,0,1,0,0};
        // rand(3) 1, rand(4) 1 -> +rand(E/10=12) 1 -> rand(3) 0: no E/5 term; rand(60) 1, rand(6) 0 -> x5; sign 0
        const int32_t q=ai::classicGroundWave(3,120,fake);
        check(q==-15 && asked==std::vector<int>({3,4,12,3,60,6,2}),"ground roll: the E/5 term only inside the E/10 branch; x5; negated");
        asked.clear();at=0;script={1,1,1,1,1,1,1};
        const int32_t a=ai::classicAirWave(0,fake);
        check(a==3 && asked==std::vector<int>({2,4,2,100,10,2,2}),"air roll: a second rand(max(p3,2)), multipliers 1/100, 1/10, 1/2");
        asked.clear();at=0;
        ai::classicNavalWave(5,fake);
        check(asked==std::vector<int>({5,4,5,100,20,3,2,2}) || asked==std::vector<int>({5,4,5,100,20,3,2}),
              "naval roll: multipliers 1/100, 1/20, 1/3");
    }
    check(ai::classicMassAttackTicks("strategic opponent aramon massattack 90 logo 2")==2700 &&
          ai::classicMassAttackTicks("massattack0x10")==480 && ai::classicMassAttackTicks("massattack 010")==240 &&
          ai::classicMassAttackTicks("massattack x")==0 && ai::classicMassAttackTicks("opponent")==0,
          "massattack n: sscanf %i (0x and 0 prefixes), x30 ticks; none or no number is 0");
}

static ai::ClassicSquadUnit squadUnit(int id,int32_t x,int32_t z) {
    ai::ClassicSquadUnit u;u.id=id;
    u.flags=ai::ClassicSquadUnit::kLive|ai::ClassicSquadUnit::kSelectable;
    u.x=x<<16;u.z=z<<16;u.mover=true;
    return u;
}

static void squadAssignment() {
    uint32_t seed=7;
    ai::ClassicSquads s;
    ai::classicConstructSquads(s,0,[&](int n){return sim::retailRandom(seed,n);});
    s.starts={ai::ClassicCell{3,4},std::nullopt,ai::ClassicCell{60,60}};
    std::vector<ai::ClassicSquadUnit> u;
    u.push_back(squadUnit(10,100,100));u[0].commander=true;u[0].mover=true;
    u.push_back(squadUnit(11,900,900));u[1].flags|=ai::ClassicSquadUnit::kArmed;
    u.push_back(squadUnit(12,950,950));u[2].finished=false;u[2].flags&=~ai::ClassicSquadUnit::kSelectable;
    u.push_back(squadUnit(13,400,400));u[3].carried=true;
    std::vector<std::pair<char,int>> log;
    auto standing=[&](size_t i){log.push_back({'s',u[i].id});};
    auto clear=[&](size_t i){log.push_back({'c',u[i].id});};
    ai::classicAssign(s,u,{20,2,2},standing,clear);
    check(s.firstPassDone && u[0].squad==1 && s.squads[1].parameters[0]==3 && u[1].squad==1 && u[2].squad==0 &&
          u[3].squad==0 && log==std::vector<std::pair<char,int>>({{'s',11}}),
          "first pass (tick 20): the Monarch joins base 1 without standing orders and anchors it on start 2; "
          "a finished unit gets standing orders and joins the nearest base; unfinished and carried units wait");
    {
        const ai::detail::ClassicUnitIndex index(u);
        const auto c=ai::classicBaseCentre(s,u,index,1);
        check(c && c->first==(60<<20)+0x80000 && c->second==(60<<20)+0x80000,
              "an anchored base's centre is its start cell's centre, whatever its members' positions");
    }
    // Later passes: a second base, the swap-remove order and the dying cleanup.
    u.push_back(squadUnit(14,10,10));u.push_back(squadUnit(15,20,20));
    u[2].finished=true;u[2].flags|=ai::ClassicSquadUnit::kSelectable;
    log.clear();
    ai::classicAssign(s,u,{500,2,2},standing,clear);
    check(s.squads[1].members==std::vector<int>({10,11,12,14,15}) && log.size()==3,
          "later passes append newly finished units to the nearest occupied base");
    u[1].flags|=ai::ClassicSquadUnit::kDying;
    ai::classicDropDying(s,u);
    check(u[1].squad==-1 && s.squads[1].members==std::vector<int>({10,15,12,14}),
          "0x40fe80: a dying member leaves (squad -1); the squad's last member takes its place");
    ai::classicAssign(s,u,{510,2,2},standing,clear);
    check(u[1].squad==-1,"a dropped unit is never assigned again");
    {
        // No first pass after tick 30 and no occupied base: base 2.
        ai::ClassicSquads late=s;late.firstPassDone=false;for (auto& q:late.squads) q.members.clear();
        late.squads[1].parameters[0]=0;
        std::vector<ai::ClassicSquadUnit> v{squadUnit(1,5,5),squadUnit(2,6,6)};v[0].commander=true;
        ai::classicAssign(late,v,{31,2,0},[](size_t){},[](size_t){});
        check(late.firstPassDone && late.squads[1].parameters[0]==0 && v[0].squad==2 && v[1].squad==2,
              "a brain whose first pass comes after tick 30 never anchors base 1; with no occupied base units join base 2");
    }
    {
        // The campaign mass attack: armed movers without a menu go to 98, every pass.
        ai::ClassicSquads m=s;m.massAttack=600;
        std::vector<ai::ClassicSquadUnit> v=u;
        for (auto& w:v) w.flags|=ai::ClassicSquadUnit::kArmed;
        v[4].menu=2;v[5].mover=false;v[3].finished=false;
        log.clear();
        ai::classicAssign(m,v,{599,2,2},standing,clear);
        const bool before=log.empty();
        ai::classicAssign(m,v,{600,2,2},standing,clear);
        check(before && v[0].squad==98 && v[3].squad==98 && v[2].squad==98 && v[4].squad==1 && v[5].squad==1 &&
              log.front()==std::pair<char,int>{'s',10} && log[1]==std::pair<char,int>{'c',10},
              "from the massattack tick: standing orders, missions cleared, slot 98 -- the Monarch and unfinished "
              "units too, not a unit with a menu or without a mover");
        ai::classicAssign(m,v,{900,2,2},standing,clear);
        check(m.squads[98].members.size()==3,"the mass attack takes nobody twice");
    }
}


// The engine game test for P3: a Classic Taros seat on Abnar's Terrace against
// an idle seat. Units appear for it (finished ones, one that finishes later, a
// building), one is destroyed; every finished unit must be in a base squad
// within 10 ticks of being finished, the Monarch in base 1 anchored on its start
// with no Stance order, every other one given Stance 0 (applied by the World).
static void abnarSquads(const char* root) {
    auto vfs=hpi::mountRetailRoot(root);
    sim::TypeRegistry reg;sim::setupRegistry(reg,vfs,false);
    sim::World w;w.setVisPlayer(-1);
    sim::MatchConfig config;config.vfs=&vfs;
    const auto map=hpi::findMap(vfs,"Abnar's Terrace");
    config.mapPath=map;
    config.slots={{true,0,0,1.0f},{true,1,1,1.0f,true,false}};
    const auto starts=sim::setupMatch(w,reg,config);
    if (starts.size()<2) { check(false,"Abnar's Terrace seats two players");return; }
    ai::Controller controller(1,reg,ai::Profile{},0x5eed,ai::Difficulty::Classic,{starts[0]});
    controller.setClassicProfile(ai::classicProfileScript(vfs,ai::classicMapAiProfile(vfs,map)));
    controller.setClassicStart(1,starts[1].first,starts[1].second);
    const ai::ClassicBrain& brain=*controller.classicBrain();
    auto pick=[&](auto want)->const sim::UnitType* {
        for (const auto& [id,t]:reg.types()) if (t.side=="TAR" && want(t)) return &t;
        return nullptr;
    };
    const auto* soldier=pick([](const sim::UnitType& t){return !t.isStructure() && !t.weapons.empty() && !t.isBuilder && !t.canFly;});
    const auto* worker=pick([](const sim::UnitType& t){return !t.isStructure() && t.isBuilder && !t.commander;});
    const auto* flyer=pick([](const sim::UnitType& t){return t.canFly && !t.weapons.empty();});
    const auto* tower=pick([](const sim::UnitType& t){return t.isStructure() && !t.weapons.empty();});
    if (!soldier || !worker || !flyer || !tower) { check(false,"Taros has a soldier, a builder, a flyer and a tower");return; }
    int monarch=0;
    for (const auto& u:w.units()) if (u.player==1 && u.type && u.type->commander) monarch=u.id;
    const float sx=starts[1].first,sz=starts[1].second;
    std::map<int,uint32_t> finishedAt;   // unit -> tick it was first seen finished
    std::map<int,uint32_t> joinedAt;     // unit -> tick it was first seen in a base squad
    std::map<int,int> stances;
    std::set<int> spawned;   // the test's own units (the Monarch also builds, since P4b)
    int late=0,doomed=0;
    for (uint32_t tick=0;tick<400;++tick) {
        if (tick==40) {
            spawned.insert(w.spawn(soldier,sx+120,sz,0.0f,1));spawned.insert(w.spawn(soldier,sx-120,sz+40,0.0f,1));
            spawned.insert(w.spawn(worker,sx,sz+120,0.0f,1));spawned.insert(w.spawn(flyer,sx+60,sz-120,0.0f,1));
            doomed=w.spawn(soldier,sx+200,sz+200,0.0f,1);spawned.insert(doomed);
        }
        if (tick==55) spawned.insert(w.spawn(tower,sx-200,sz-160,0.0f,1));
        if (tick==60) { late=w.spawn(soldier,sx-60,sz-60,0.0f,1);w.unit(late)->underConstruction=true;spawned.insert(late); }
        if (tick==131) w.unit(late)->underConstruction=false;
        if (tick==200) {
            net::Command kill;kill.kind=net::Cmd::Destroy;kill.player=1;kill.unitId=doomed;
            sim::applyCommand(w,reg,kill);
        }
        controller.tick(w,tick,[&](const net::Command& c) {
            if (c.kind==net::Cmd::Stance) ++stances[c.unitId];
            sim::applyCommand(w,reg,c);
        });
        w.tick(1.f/30.f);
        for (const auto& u:w.units()) {
            if (u.player!=1 || !u.alive() || (u.id!=monarch && !spawned.count(u.id))) continue;
            if (!u.underConstruction) finishedAt.emplace(u.id,tick);
            const int32_t q=brain.squadOf(u.id);
            if (q>=1 && q<=20) joinedAt.emplace(u.id,tick);
        }
    }
    bool within=true;uint32_t worst=0;int units=0;
    for (const auto& [id,at]:finishedAt) {
        // The Monarch is there from tick 0 and waits for the first pass, which
        // the manager's 30-tick initial countdown holds back (checked below).
        if (id==doomed || id==monarch) continue;
        const auto j=joinedAt.find(id);
        const uint32_t delay=j==joinedAt.end() ? 9999 : j->second-at;
        within=within && delay<=10;worst=std::max(worst,delay);++units;
    }
    std::printf("  Abnar's Terrace: %d finished units besides the Monarch, worst delay to a base squad %u ticks\n",units,worst);
    check(units==6 && within,"every finished unit of the Classic seat is in a base squad within 10 ticks of being finished");
    const auto& sq=brain.squads();
    check(brain.squadOf(monarch)==1 && joinedAt.at(monarch)<=30 && sq.squads[1].parameters[0]==2 && !stances.count(monarch),
          "the Monarch joins base 1 by tick 30, base 1 is anchored on start 2, and it gets no Stance order");
    {
        const auto cell=*sq.starts[1];
        const int cx=cell.first*16+8,cz=cell.second*16+8;
        check(std::abs(cx-int(sx))<=8 && std::abs(cz-int(sz))<=8,"the anchor is the start location's 16-pixel cell");
    }
    bool stanced=true;
    for (const auto& [id,at]:finishedAt) {
        if (id==monarch || id==doomed) continue;
        const auto* u=w.unit(id);
        stanced=stanced && stances[id]==1 && u &&
                (!u->type->canSetStance || (u->stance==0 && u->moveState==1 && u->fireState==2));
    }
    check(stanced,"every other unit got exactly one Stance 0 order, and the World applied it (move 1, fire 2)");
    bool gone=brain.squadOf(doomed)<=0;
    for (const auto& q:sq.squads) gone=gone && std::find(q.members.begin(),q.members.end(),doomed)==q.members.end();
    check(gone,"a destroyed unit is in no squad");
    // Member counts per kind (the squads' own thinks are later phases, so every
    // unit is in a base), of the test's units and the Monarch.
    int bases=0,strikes=0,other=0,inBase1=0;
    for (int slot=1;slot<=98;++slot)
        for (int id:sq.squads[size_t(slot)].members) {
            if (id!=monarch && !spawned.count(id)) continue;
            (slot<=20 ? bases : slot<=80 || slot==97 || slot==98 ? strikes : other)+=1;
            inBase1+=slot==1;
        }
    std::printf("  members: bases %d (base 1: %d), strikes/backups %d, VTOL/homeless %d\n",bases,
                inBase1,strikes,other);
    check(bases==inBase1 && strikes==0 && other==0 && bases==units+1,
          "all of them are in base 1, the only occupied base");
}

// The engine game test for P4a: a Classic seat of each faction with a factory
// (Aramon keep, Veruna keep, Taros dungeon) given at the start on Abnar's
// Terrace, against an idle seat, for 2 minutes of the World's own economy. The
// factory joins base 1 and is planned by the base think: every Train is one unit
// of a same-side type from its menu with a draw weight > 0 (so under its profile
// limit), queued only while the factory has nothing in production; live counts
// never pass a profile limit; and trained units come out and join the base.
static void classicFactories(const char* root) {
    auto vfs=hpi::mountRetailRoot(root);
    sim::TypeRegistry reg;sim::setupRegistry(reg,vfs,false);
    const auto map=hpi::findMap(vfs,"Abnar's Terrace");
    // The last seat runs its own profile: the Swordsman locked at 2 (the first,
    // exact line wins), every other type at 0, so only Swordsmen may come out.
    struct Seat { int faction;const char* factory;const char* label;const char* script; };
    for (const Seat seat:{Seat{0,"arakeep","Aramon keep",nullptr},Seat{2,"verkeep","Veruna keep",nullptr},
                          Seat{1,"tardung","Taros dungeon",nullptr},
                          Seat{0,"arakeep","Aramon keep, limits 2 Swordsmen / 0 others","limit arasword 2\nlimit ALL 0\n"}}) {
        sim::World w;w.setVisPlayer(-1);
        sim::MatchConfig config;config.vfs=&vfs;config.mapPath=map;
        config.slots={{true,0,0,1.0f},{true,seat.faction,1,1.0f,true,false}};
        const auto starts=sim::setupMatch(w,reg,config);
        const auto* factoryType=reg.find(seat.factory);
        if (starts.size()<2 || !factoryType) { check(false,"Abnar's Terrace seats two players, and the factory type exists");return; }
        ai::Controller controller(1,reg,ai::Profile{},0x5eed+uint32_t(seat.faction),ai::Difficulty::Classic,{starts[0]});
        controller.setClassicProfile(seat.script ? ai::ClassicProfileScript{"test",seat.script}
                                                 : ai::classicProfileScript(vfs,ai::classicMapAiProfile(vfs,map)));
        controller.setClassicStart(1,starts[1].first,starts[1].second);
        const ai::ClassicBrain& brain=*controller.classicBrain();
        const auto& cat=brain.catalogue();
        const float sx=starts[1].first,sz=starts[1].second;
        // Toward the map's middle, clear of the Monarch.
        const float dx=sx<w.mapW()*8.0f ? 1.f : -1.f,dz=sz<w.mapH()*8.0f ? 1.f : -1.f;
        const int factory=w.spawn(factoryType,sx+dx*192,sz+dz*192,0.0f,1);
        const size_t ft=[&]{auto it=cat.byName.find(seat.factory);return it==cat.byName.end() ? size_t(0) : size_t(it->second);}();
        const auto& menu=brain.menu(ft);
        int trains=0,offMenu=0,busy=0,unweighted=0,wrongUnit=0,overLimit=0,produced=0;
        std::map<std::string,int> byType;
        std::set<int> seen;std::map<int,uint32_t> born;
        uint32_t firstOut=0,joined=0;
        for (uint32_t tick=0;tick<3600;++tick) {
            controller.tick(w,tick,[&](const net::Command& c) {
                // Since P4b the Monarch builds too; only the given factory's
                // production is checked here (classicBases covers the rest).
                if (c.kind==net::Cmd::Train && c.unitId==factory) {
                    ++trains;
                    if (c.targetId!=1) ++wrongUnit;
                    const std::string id=c.type;++byType[id];
                    const auto* t=reg.find(id);
                    const auto it=t ? cat.byName.find(t->classicFbi.unitName.empty() ? t->id : t->classicFbi.unitName)
                                    : cat.byName.end();
                    if (it==cat.byName.end() || std::find(menu.begin(),menu.end(),it->second)==menu.end() ||
                        t->side!=factoryType->side) ++offMenu;
                    else if (brain.buildWeight(it->second)<=0) ++unweighted;
                    if (const auto* f=w.unit(factory);!f || !f->buildQueue.empty()) ++busy;
                }
                sim::applyCommand(w,reg,c);
            });
            w.tick(1.f/30.f);
            std::map<const sim::UnitType*,int> live;
            for (const auto& u:w.units()) {
                if (u.player!=1 || !u.alive() || !u.type) continue;
                ++live[u.type];
                if (u.id!=factory && !u.type->commander && seen.insert(u.id).second) { ++produced;born[u.id]=tick; }
                if (!firstOut && born.count(u.id) && !u.underConstruction) firstOut=tick;
                if (!joined && born.count(u.id) && brain.squadOf(u.id)>=1 && brain.squadOf(u.id)<=20) joined=tick;
            }
            for (const auto& [t,n]:live) {
                const auto it=cat.byName.find(t->classicFbi.unitName.empty() ? t->id : t->classicFbi.unitName);
                if (it!=cat.byName.end() && t!=factoryType && !t->commander && !brain.profile().allows(it->second,n-1)) ++overLimit;
            }
        }
        std::string mix;
        for (const auto& [id,n]:byType) mix+=" "+id+" x"+std::to_string(n);
        std::printf("  %s: menu %zu, %d Train commands (%u queues),%s; %d units produced, first out at tick %u, "
                    "first in a base at %u\n",seat.label,menu.size(),trains,brain.factoryQueues(),mix.c_str(),produced,
                    firstOut,joined);
        const std::string what=std::string(seat.label)+": ";
        check(!menu.empty() && trains>=2 && wrongUnit==0,(what+"the factory trains, one unit per Train command").c_str());
        check(offMenu==0 && unweighted==0,(what+"every pick is a same-side type of its menu with a draw weight > 0").c_str());
        check(busy==0,(what+"a unit is queued only while the factory has nothing in production").c_str());
        check(overLimit==0,(what+"live counts stay within the profile's limits").c_str());
        check(produced>=1 && firstOut>0 && joined>=firstOut,(what+"trained units come out and join a base squad").c_str());
        if (seat.script)
            check(byType.size()==1 && byType.count("arasword") && byType["arasword"]==2,
                  (what+"exactly two Swordsmen are trained, nothing else").c_str());
    }
}

// The engine game test for P4b: a Classic seat of each of the five factions on
// Abnar's Terrace from a bare start (its Monarch only), against an idle seat, for
// 10 game-minutes of the World's own economy. Its Monarch and builders run
// retail's constructor planner and placers; factories (and Zhon's conjurers) the
// production planners. Recorded at 3, 6 and 10 minutes: finished lodestones,
// other structures by type, units produced, and the Build / Train commands. It
// must not stall: by 10 minutes it has built lodestones and structures and
// produced units. Every Build site lies inside the base's ring radius plus one
// grid block, a lodestone goes on a sacred site, no factory type is ordered
// while an idle one of that type stands in the base, and no lodestone while
// another member of the base is already building a mana site.
static void classicBases(const char* root) {
    auto vfs=hpi::mountRetailRoot(root);
    sim::TypeRegistry reg;sim::setupRegistry(reg,vfs,false);
    const auto map=hpi::findMap(vfs,"Abnar's Terrace");
    static const char* const kNames[5]={"Aramon","Taros","Veruna","Zhon","Creon"};
    std::printf("  P4b games (Abnar's Terrace, Classic seat vs an idle seat):\n");
    // A sixth game: a Zhon seed whose builders meet the busy-deposit test inside
    // one base think (two lodestone picks in a row), which the stale member
    // snapshot used to miss.
    for (int game=0;game<6;++game) {
        const int faction=game<5 ? game : 3;
        const uint32_t seed=game<5 ? 0xb450u+uint32_t(faction) : 36u;
        const std::string name=game<5 ? kNames[faction] : std::string(kNames[faction])+" (seed 36)";
        sim::World w;w.setVisPlayer(-1);
        sim::MatchConfig config;config.vfs=&vfs;config.mapPath=map;
        config.slots={{true,0,0,1.0f},{true,faction,1,1.0f,true,false}};
        const auto starts=sim::setupMatch(w,reg,config);
        if (starts.size()<2) { check(false,"Abnar's Terrace seats two players");return; }
        ai::Controller controller(1,reg,ai::Profile{},seed,ai::Difficulty::Classic,{starts[0]});
        controller.setClassicProfile(ai::classicProfileScript(vfs,ai::classicMapAiProfile(vfs,map)));
        controller.setClassicStart(1,starts[1].first,starts[1].second);
        const ai::ClassicBrain& brain=*controller.classicBrain();
        const float sx=starts[1].first,sz=starts[1].second;
        int builds=0,trains=0,outside=0,offDeposit=0,duplicate=0,refused=0,doubleDeposit=0;
        // A type's mana-site bit (type +0x264 bit 31), from the brain's own facts.
        auto manaSite=[&](const sim::UnitType* t) {
            const auto& types=brain.catalogue().types;
            for (size_t i=1;i<types.size();++i) if (types[i]==t) return brain.typeInfo()[i].manaSite;
            return false;
        };
        // What a unit is building or ordered to build (its first build mission).
        auto building=[&](const sim::Unit& u) -> const sim::UnitType* {
            if (u.buildSiteId) if (const auto* s=w.unit(u.buildSiteId)) return s->type;
            for (const auto& o:u.orders) if (o.buildType) return o.buildType;
            return nullptr;
        };
        int32_t worst=0;
        struct Snapshot { uint32_t tick;int lodes,structures,units,builds,trains;std::map<std::string,int> byType; };
        std::vector<Snapshot> snaps;
        std::set<int> producedIds;
        static const bool trace=std::getenv("TAK_CLASSIC_TRACE")!=nullptr;   // per-command log
        for (uint32_t tick=0;tick<18000;++tick) {
            controller.tick(w,tick,[&](const net::Command& c) {
                if (c.kind==net::Cmd::Train) ++trains;
                if (trace && (c.kind==net::Cmd::Build || c.kind==net::Cmd::Train || c.kind==net::Cmd::Stop)) {
                    const auto* b=w.unit(c.unitId);
                    std::printf("      t%u %s %s -> %s (%.0f,%.0f) mana %.0f alloc %.2f\n",tick,
                                c.kind==net::Cmd::Build ? "Build" : c.kind==net::Cmd::Train ? "Train" : "Stop",
                                b && b->type ? b->type->id.c_str() : "?",c.type,c.x,c.z,w.player(1).mana,
                                double(w.player(1).constructionAllocation));
                }
                if (c.kind==net::Cmd::Build) {
                    ++builds;
                    const auto* t=reg.find(c.type);
                    // The ring radius from the seat's structures (all in base 1).
                    int32_t foot=0;
                    for (const auto& u:w.units())
                        if (u.player==1 && u.alive() && u.type && u.type->isStructure() && !u.underConstruction)
                            foot+=u.type->footX+u.type->footZ;
                    const int32_t ring1=sim::retailAiBaseRadius(foot,1);
                    const float d=std::hypot(c.x-(std::floor(sx/16)*16+8),c.z-(std::floor(sz/16)*16+8));
                    worst=std::max(worst,int32_t(d));
                    if (d>float(ring1)+40*16) ++outside;
                    if (t && t->onMana && w.hasManaSpots() && !w.canPlace(t,c.x,c.z,1)) ++offDeposit;
                    // The rule reads the base's members (finished units the
                    // assignment pass has placed in base 1..20).
                    if (t && t->isStructure() && !reg.buildable(t->id).empty())
                        for (const auto& u:w.units())
                            if (u.player==1 && u.alive() && u.type==t && u.buildQueue.empty() &&
                                brain.squadOf(u.id)>=1 && brain.squadOf(u.id)<=20) ++duplicate;
                    // 0x40c830: no lodestone while another member of the builder's
                    // base is already on a mana site (an order given earlier in
                    // the same base think counts: retail's order is immediate).
                    if (t && manaSite(t))
                        for (const auto& u:w.units())
                            if (u.player==1 && u.alive() && u.id!=c.unitId && brain.squadOf(u.id)==brain.squadOf(c.unitId))
                                if (const auto* b=building(u);b && manaSite(b)) ++doubleDeposit;
                    sim::applyCommand(w,reg,c);
                    if (const auto* b=w.unit(c.unitId);!b || b->orders.empty() || !b->orders.back().buildType) ++refused;
                    return;
                }
                sim::applyCommand(w,reg,c);
            });
            w.tick(1.f/30.f);
            for (const auto& u:w.units())
                if (u.player==1 && u.alive() && u.type && !u.type->isStructure() && !u.type->commander && !u.underConstruction)
                    producedIds.insert(u.id);
            if (tick+1==5400 || tick+1==10800 || tick+1==18000) {
                Snapshot snap{tick+1,0,0,int(producedIds.size()),builds,trains,{}};
                for (const auto& u:w.units()) {
                    if (u.player!=1 || !u.alive() || !u.type || u.underConstruction) continue;
                    if (u.type->isStructure()) {
                        (u.type->onMana ? snap.lodes : snap.structures)++;
                        ++snap.byType[u.type->id];
                    } else if (!u.type->commander) ++snap.byType[u.type->id];
                }
                snaps.push_back(snap);
            }
        }
        for (const auto& snap:snaps) {
            std::string mix;
            for (const auto& [id,n]:snap.byType) mix+=" "+id+" x"+std::to_string(n);
            std::printf("    %s %2u min: %d lodestones, %d other structures, %d units produced; %d Build, %d Train;%s\n",
                        name.c_str(),snap.tick/1800,snap.lodes,snap.structures,snap.units,snap.builds,snap.trains,
                        mix.c_str());
        }
        std::printf("    %s: %u Build commands (%u lodestones), %d refused by the World, farthest site %d px\n",
                    name.c_str(),brain.builds(),brain.lodestoneBuilds(),refused,worst);
        const auto& last=snaps.back();
        const std::string what=name+": ";
        check(last.lodes>=1,(what+"lodestones are built within 10 minutes").c_str());
        check(last.structures>=1 || faction==3,(what+"basic structures are built within 10 minutes").c_str());
        check(last.units>=1,(what+"units are trained or conjured within 10 minutes").c_str());
        check(outside==0,(what+"every site is inside the ring radius plus a grid block").c_str());
        check(offDeposit==0,(what+"every lodestone is ordered onto a free deposit").c_str());
        check(duplicate==0,(what+"no factory type is ordered while an idle one stands in the base").c_str());
        check(doubleDeposit==0,(what+"no lodestone is ordered while another base member builds a mana site").c_str());
    }
}

// The engine game test for P5: a scripted raid into a Classic base on Abnar's
// Terrace. An Aramon seat (the Monarch and six armed soldiers given at its
// start) against an idle Veruna seat whose raiders the test places. Before the
// raid the soldiers are idle base members: they patrol inside the base ring
// (0x40d370 without a threat: points rand(r/2)+rand(r) from the centre). Raiders
// put inside the ring-0 radius, where the seat sees them, are a threat (0x412000):
// the soldiers are sent at them (points around a candidate, within its sight or
// weapon range), while the Monarch at full HP with that ground army around it
// does not flee. Below its HP threshold (1/8 with full mana and a castable
// weapon) it retreats (0x40d010): a move to a point inside the ring-0 radius
// farther from the raiders than it stands.
static void classicRaid(const char* root) {
    auto vfs=hpi::mountRetailRoot(root);
    sim::TypeRegistry reg;sim::setupRegistry(reg,vfs,false);
    const auto map=hpi::findMap(vfs,"Abnar's Terrace");
    sim::World w;w.setVisPlayer(-1);
    sim::MatchConfig config;config.vfs=&vfs;config.mapPath=map;
    config.slots={{true,2,0,1.0f},{true,0,1,1.0f,true,false}};
    const auto starts=sim::setupMatch(w,reg,config);
    if (starts.size()<2) { check(false,"Abnar's Terrace seats two players");return; }
    ai::Controller controller(1,reg,ai::Profile{},0x5a1du,ai::Difficulty::Classic,{starts[0]});
    // No building at all, so the Monarch's own choices do not muddy the test.
    controller.setClassicProfile(ai::ClassicProfileScript{"test","limit ALL 0\n"});
    controller.setClassicStart(1,starts[1].first,starts[1].second);
    const ai::ClassicBrain& brain=*controller.classicBrain();
    const auto* soldier=reg.find("arasword");   // Aramon Swordsman
    const auto* raider=reg.find("verman");      // Veruna Man-at-arms
    if (!soldier || !raider) { check(false,"the Swordsman and Man-at-arms types exist");return; }
    int monarch=0;
    for (const auto& u:w.units()) if (u.player==1 && u.type && u.type->commander) monarch=u.id;
    const float sx=starts[1].first,sz=starts[1].second;
    // Toward the map's middle.
    const float dx=sx<w.mapW()*8.0f ? 1.f : -1.f,dz=sz<w.mapH()*8.0f ? 1.f : -1.f;
    std::vector<int> defenders,raiders;
    const float centreX=std::floor(sx/16)*16+8,centreZ=std::floor(sz/16)*16+8;   // base 1's anchor cell
    auto distance=[](float ax,float az,float bx,float bz){return std::hypot(ax-bx,az-bz);};
    auto nearestRaider=[&](float x,float z){
        float best=1e9f;
        for (int id:raiders) if (const auto* r=w.unit(id);r && r->alive()) best=std::min(best,distance(x,z,r->x.toFloat(),r->z.toFloat()));
        return best;
    };
    // The ring radii with no structures in the base: 1120 / 800 px (rings 0 / 1).
    const float ring0=float(sim::retailAiBaseRadius(0,0)),ring1=float(sim::retailAiBaseRadius(0,1));
    int idlePatrols=0,idleOutside=0,threatOrders=0,threatNear=0,threatFar=0,earlyRetreats=0;
    std::set<int> reacted;
    struct Retreat { uint32_t tick;float x,z;float from; } retreat{0,0,0,0};
    constexpr uint32_t kRaid=900,kWound=1200,kEnd=1500;
    for (uint32_t tick=0;tick<kEnd;++tick) {
        if (tick==40)
            for (int k=0;k<6;++k)
                defenders.push_back(w.spawn(soldier,sx+dx*(60+40*float(k%3)),sz+dz*(80+60*float(k/3)),0.0f,1));
        if (tick==kRaid)
            // Between the Monarch and the map's middle, inside the ring-0 radius
            // and the Monarch's sight. They hold their ground and fire (passive
            // stance), so they stay where the seat sees them.
            for (int k=0;k<4;++k) {
                raiders.push_back(w.spawn(raider,sx+dx*(300+30*float(k)),sz+dz*(280-30*float(k)),0.0f,0));
                net::Command hold;hold.kind=net::Cmd::Stance;hold.player=0;hold.unitId=raiders.back();hold.targetId=2;
                sim::applyCommand(w,reg,hold);
            }
        if (tick==kWound) if (auto* m=w.unit(monarch)) m->hp=sim::Fixed::fromInt(m->type->maxHp/12);
        controller.tick(w,tick,[&](const net::Command& c) {
            const bool defender=std::find(defenders.begin(),defenders.end(),c.unitId)!=defenders.end();
            if (defender && (c.kind==net::Cmd::Move || c.kind==net::Cmd::Patrol)) {
                if (tick<kRaid) {
                    // 0x40d370 with no threat: around the centre.
                    if (c.kind==net::Cmd::Patrol) { ++idlePatrols;idleOutside+=distance(c.x,c.z,centreX,centreZ)>1.5f*ring1; }
                } else if (c.kind==net::Cmd::Move) {
                    // The first point: around a candidate raider (within its
                    // reach), unless it fell back to the centre. (The centre leg
                    // is dropped inside the ring, where retail's ends at once.)
                    ++threatOrders;
                    const float near=nearestRaider(c.x,c.z);
                    const auto* d=w.unit(c.unitId);
                    const float reach=d ? float(std::max(w.sightDistance(*d->type),d->type->weapons.front().range)) : 0.f;
                    if (near<=reach+16) { ++threatNear;reacted.insert(c.unitId); } else ++threatFar;
                }
            }
            if (c.unitId==monarch && c.kind==net::Cmd::Move && !c.queue && tick>=kRaid) {
                if (tick<kWound) ++earlyRetreats;
                else if (!retreat.tick) {
                    const auto* m=w.unit(monarch);
                    retreat={tick,c.x,c.z,m ? nearestRaider(m->x.toFloat(),m->z.toFloat()) : 0.f};
                }
            }
            sim::applyCommand(w,reg,c);
        });
        w.tick(1.f/30.f);
        if (std::getenv("TAK_CLASSIC_TRACE") && tick>=kRaid-30 && tick%15==0) {
            std::printf("t%u visible %zu:",tick,w.visibleEnemies(1).size());
            for (int id:defenders) if (const auto* d=w.unit(id))
                std::printf(" [%d d%.0f c%.0f o%zu%s]",id,double(nearestRaider(d->x.toFloat(),d->z.toFloat())),
                            double(distance(d->x.toFloat(),d->z.toFloat(),centreX,centreZ)),d->orders.size(),
                            d->orders.empty() ? "" : d->orders.front().autoTarget ? "A" : d->orders.front().patrol ? "P" : "M");
            std::printf("\n");
        }
    }
    std::printf("  raid on Abnar's Terrace: %d idle patrols before it (%d outside 1.5 x ring 1), %d defenders sent "
                "(%d first points within reach of a raider, %d not), %u retreats; ring 0 %.0f px\n",idlePatrols,
                idleOutside,int(reacted.size()),threatNear,threatFar,brain.retreats(),double(ring0));
    check(idlePatrols>=6 && idleOutside==0,"before the raid every idle soldier patrols inside the base ring");
    check(reacted.size()>=4,"raiders inside the ring-0 radius send the soldiers at them");
    check(threatNear>=threatFar,"most of the defenders' first points lie within reach of a raider");
    check(earlyRetreats==0,"the Monarch at full HP does not retreat with its ground army around it");
    check(retreat.tick>0 && retreat.tick<kWound+120,"below its HP threshold the Monarch retreats within 4 s");
    if (retreat.tick) {
        const float from=distance(retreat.x,retreat.z,centreX,centreZ);
        std::printf("    the Monarch retreats at tick %u to %.0f px from the centre; its nearest raider %.0f px -> %.0f px\n",
                    retreat.tick,double(from),double(retreat.from),double(nearestRaider(retreat.x,retreat.z)));
        check(from<=ring0+8,"the retreat point is inside the ring-0 radius");
        check(nearestRaider(retreat.x,retreat.z)>retreat.from,"the retreat point is farther from the raiders");
    }
}

// The port's side of tools/re/check_ai_assignment.py: 0x40fe80 then 0x40eeb0 on
// the case's units and squads (see the script for the line layout).
static int assignmentOracle() {
    std::string line;
    while (std::getline(std::cin,line)) {
        std::istringstream in(line);
        uint32_t tick,mass;int32_t mode,start;int first;
        in>>tick>>mode>>mass>>start>>first;
        ai::ClassicSquads s;
        uint32_t seed=1;
        ai::classicConstructSquads(s,0,[&](int n){return sim::retailRandom(seed,n);});
        s.firstPassDone=first!=0;s.massAttack=mass;
        auto table=[&](std::vector<std::optional<ai::ClassicCell>>& t) {
            size_t n;in>>n;t.resize(n);
            for (auto& r:t) { int valid,x,z;in>>valid>>x>>z;if (valid) r=ai::ClassicCell{int16_t(x),int16_t(z)}; }
        };
        table(s.starts);table(s.sites);
        for (int i=1;i<=20;++i) in>>s.squads[size_t(i)].parameters[0];
        size_t n;in>>n;
        std::vector<ai::ClassicSquadUnit> units(n);
        for (size_t i=0;i<n;++i) {
            auto& u=units[i];u.id=int(i);
            uint32_t flags,f108;int commander,sacred,builder,held,carrier,mover,fx,fz;
            in>>flags>>commander>>sacred>>builder>>f108>>held>>carrier>>mover>>u.menu>>u.x>>u.z>>fx>>fz>>u.squad;
            u.flags=flags;u.commander=commander;u.sacredSite=sacred;u.builder=builder;
            const float remaining=std::bit_cast<float>(f108);
            u.finished=!(remaining<0.0f || remaining>0.0f);   // fcomp: equal or unordered
            u.held=held!=0;u.carried=carrier!=0;u.carrierHolds=carrier==1;u.mover=mover!=0;
            u.footX=int16_t(fx);u.footZ=int16_t(fz);
        }
        for (int slot=1;slot<=98;++slot) {
            size_t k;in>>k;auto& m=s.squads[size_t(slot)].members;m.resize(k);
            for (auto& id:m) in>>id;
        }
        std::vector<std::pair<int,int>> log;
        ai::classicDropDying(s,units);
        ai::classicAssign(s,units,{tick,mode,start},
            [&](size_t i){log.push_back({int(i),2});},[&](size_t i){log.push_back({int(i),0});});
        std::printf("%d",int(s.firstPassDone));
        for (int i=1;i<=20;++i) std::printf(" %d",s.squads[size_t(i)].parameters[0]);
        for (const auto& u:units) std::printf(" %d",u.squad);
        for (int slot=1;slot<=98;++slot) {
            std::printf(" %zu",s.squads[size_t(slot)].members.size());
            for (int id:s.squads[size_t(slot)].members) std::printf(" %d",id);
        }
        std::printf(" %zu",log.size());
        for (const auto& [i,k]:log) std::printf(" %d %d",i,k);
        std::printf("\n");
    }
    return 0;
}

// The port's side of tools/re/check_ai_factory_think.py: the economy step's
// allocation (sim::RetailConstructionResources), then one base think through
// the brain's own templates -- the squad clock, classicBaseMembers and
// classicPlanFactory -- with the case's build-manager and type tables (see the
// script for the line layout).
static int factoryThinkOracle() {
    std::string line;
    while (std::getline(std::cin,line)) {
        std::istringstream in(line);
        uint32_t tick,seed,storedBits,overrideBits,producedBits,incomeBits,storageBits,demandBits;
        int slot,limited,population,threat;int32_t anchor;double total,excess;
        in>>tick>>slot>>anchor>>seed>>limited>>population>>threat>>storedBits>>overrideBits>>producedBits
          >>total>>excess>>incomeBits>>storageBits>>demandBits;
        sim::RetailConstructionResources r;
        r.stored=std::bit_cast<float>(storedBits);r.capacityOverride=std::bit_cast<float>(overrideBits);
        r.produced=std::bit_cast<float>(producedBits);r.totalProduced=total;r.excess=excess;
        r.beginTick(std::bit_cast<float>(incomeBits),std::bit_cast<float>(storageBits),std::bit_cast<float>(demandBits));
        ai::ClassicSquads s;
        auto table=[&](std::vector<std::optional<ai::ClassicCell>>& t) {
            size_t n;in>>n;t.resize(n);
            for (auto& c:t) { int valid,x,z;in>>valid>>x>>z;if (valid) c=ai::ClassicCell{int16_t(x),int16_t(z)}; }
        };
        table(s.starts);table(s.sites);
        struct Type { int pref,prio,limit,count,lode,side,commander,menuField;float cost;std::vector<uint16_t> menu; };
        size_t ntypes;in>>ntypes;
        std::vector<Type> types(ntypes);
        for (auto& t:types) {
            uint32_t cost;size_t k;
            in>>t.pref>>t.prio>>t.limit>>t.count>>t.lode>>t.side>>cost>>t.commander>>t.menuField>>k;
            t.cost=std::bit_cast<float>(cost);t.menu.resize(k);
            for (auto& e:t.menu) in>>e;
        }
        size_t n;in>>n;
        std::vector<ai::ClassicSquadUnit> units(n);
        for (size_t i=0;i<n;++i) {
            auto& u=units[i];u.id=int(i);
            uint32_t flags,f108;int t,held,carrier,mover,fx,fz,producing,first;
            in>>flags>>t>>f108>>held>>carrier>>mover>>u.x>>u.z>>fx>>fz>>producing>>first;
            u.flags=flags;u.type=uint16_t(t);u.commander=types[size_t(t)].commander!=0;
            const float remaining=std::bit_cast<float>(f108);
            u.finished=!(remaining<0.0f || remaining>0.0f);   // fcomp: equal or unordered
            u.held=held!=0;u.carried=carrier!=0;u.carrierHolds=carrier==1;u.mover=mover!=0;
            u.menu=types[size_t(t)].menuField;u.footX=int16_t(fx);u.footZ=int16_t(fz);
            u.producing=producing!=0;u.missionClass=uint8_t(first);u.squad=slot;
        }
        auto& base=s.squads[size_t(slot)];
        base.kind=sim::RetailAiSquadKind::Base;base.parameters[0]=anchor;
        size_t m;in>>m;base.members.resize(m);
        for (auto& id:base.members) in>>id;
        std::vector<int> draws;
        std::vector<std::array<int,3>> log;
        auto random=[&](int k){draws.push_back(k);return sim::retailRandom(seed,k);};
        sim::RetailAiGroupClock clock{true,0};
        sim::retailTickAiSquad(base,clock,tick,random,[](auto&){},[&](auto&) {
            ai::classicBaseMembers(s,units,slot,
                [&](const std::pair<int32_t,int32_t>&,int32_t){log.push_back({0,0,0});return threat!=0;},
                [&](size_t i) {
                    const auto& b=types[units[i].type];
                    ai::classicPlanFactory(r.allocation,units[i].producing,uint16_t(population),b.menu,
                        [&](uint16_t t){const auto& e=types[t];
                            return sim::retailAiBuildWeight(limited!=0,e.limit,int16_t(e.count),uint8_t(e.pref),int8_t(e.prio));},
                        [&](uint16_t t){return types[t].lode!=0;},
                        [&](uint16_t t){return types[t].side==b.side;},
                        [&](uint16_t t){return types[t].cost;},random,
                        [&](uint16_t t){log.push_back({1,int(i),int(t)});});
                },
                [&](size_t i,bool danger){log.push_back({2,int(i),int(danger)});},
                [&](size_t i,bool danger){log.push_back({3,int(i),int(danger)});});
        });
        std::printf("%u %d %u %zu",clock.deadline,base.parameters[0],seed,draws.size());
        for (int d:draws) std::printf(" %d",d);
        std::printf(" %zu",log.size());
        for (const auto& e:log) std::printf(" %d %d %d",e[0],e[1],e[2]);
        std::printf("\n");
    }
    return 0;
}

// The port's side of tools/re/check_ai_placement.py: the build manager's block
// grids (classicDrawBlockGrids, 0x4110f0) from a seed, then one 0x412f80 with
// them (classicPlaceSite over classicDepositSite / classicScatterSite) on the
// case's feature plane, sacred-site records and placement answers. Line:
//   seed width height ntypes <sacredsite bits>*ntypes ncells (x z feature backX backZ)*ncells
//   nsites (valid x z)*nsites centreX centreZ radius footX footZ minwaterdepth
//   bmcode armed range menu yardmap(-1 none, else its first byte) nanswers answer*nanswers
// Out: the ten grid fields, the seed after them, then "ok x z" (or "0"), the final
// seed, the draws (count then each n) and the placement test's cells (count then x z).
static int placementOracle() {
    std::string line;
    while (std::getline(std::cin,line)) {
        std::istringstream in(line);
        uint32_t seed;int w,h;size_t ntypes;
        in>>seed>>w>>h>>ntypes;
        std::vector<bool> sacred(ntypes);
        for (size_t i=0;i<ntypes;++i) { uint32_t b;in>>b;sacred[i]=ai::classicSacredValue(std::bit_cast<float>(b)); }
        std::vector<ai::ClassicFeatureCell> plane(size_t(w)*size_t(h));
        size_t n;in>>n;
        for (size_t i=0;i<n;++i) { int x,z,f,bx,bz;in>>x>>z>>f>>bx>>bz;plane[size_t(z)*w+x]={uint16_t(f),uint8_t(bx),uint8_t(bz)}; }
        std::vector<std::optional<std::pair<int16_t,int16_t>>> sites;
        in>>n;
        for (size_t i=0;i<n;++i) { int valid,x,z;in>>valid>>x>>z;sites.push_back(valid ? std::optional{std::pair{int16_t(x),int16_t(z)}} : std::nullopt); }
        int32_t cx,cz,radius;int fx,fz,mwd,bm,armed,range,menu,yard;
        in>>cx>>cz>>radius>>fx>>fz>>mwd>>bm>>armed>>range>>menu>>yard;
        std::vector<int> answers;in>>n;answers.resize(n);
        for (auto& a:answers) in>>a;
        std::vector<int> draws;
        auto random=[&](int k){draws.push_back(k);return sim::retailRandom(seed,k);};
        const auto grids=ai::classicDrawBlockGrids(random);
        const uint32_t gridSeed=seed;
        draws.clear();
        ai::ClassicSiteType type;
        type.footX=int16_t(fx);type.footZ=int16_t(fz);type.minWaterDepth=int16_t(mwd);
        type.tower=ai::classicSiteTower(uint8_t(bm),armed!=0,range,menu);
        type.lodestone=yard>=0 && (yard&0x80);
        std::vector<std::pair<int,int>> asked;size_t next=0;
        auto valid=[&](int16_t x,int16_t z){asked.push_back({x,z});return next<answers.size() && answers[next++]!=0;};
        auto cellAt=[&](int x,int z){
            if (x<0 || z<0 || x>=w || z>=h) return ai::ClassicFeatureCell{};
            return plane[size_t(z)*w+x];
        };
        auto isSacred=[&](int x,int z){
            return ai::classicSacredCell(x,z,uint32_t(ntypes),cellAt,[&](uint16_t f){return f<sacred.size() && sacred[f];});
        };
        const auto site=ai::classicPlaceSite(type,
            [&]{return ai::classicDepositSite(sites,{cx,cz},radius,type.footX,type.footZ,
                    [&](int16_t x,int16_t z){return x>=0 && z>=0 && x<w && z<h && isSacred(x,z);},valid);},
            [&]{return ai::classicScatterSite(grids,{cx,cz},radius,type,random,valid,isSacred);});
        for (const auto& g:{grids.land,grids.water})
            std::printf("%d %d %d %d %d ",g.stepX,g.stepZ,g.offX,g.offZ,g.margin);
        std::printf("%u",gridSeed);
        if (site) std::printf(" 1 %d %d",site->first,site->second); else std::printf(" 0");
        std::printf(" %u %zu",seed,draws.size());
        for (int d:draws) std::printf(" %d",d);
        std::printf(" %zu",asked.size());
        for (const auto& [x,z]:asked) std::printf(" %d %d",x,z);
        std::printf("\n");
    }
    return 0;
}

// The port's side of tools/re/check_ai_constructor.py: one constructor planner
// call (ai::classicPlanConstructor) for a base member, over the case's base,
// build manager, types, map and answers, with the brain's squad helpers and
// placers under it. Line layout (see the script):
//   tick alarm allocBits threat limited slot anchor p1 seed builder
//   grids(10) width height nft <sacredsite bits>*nft ncells (x z f bx bz)* nsites (valid x z)*
//   nstarts (valid x z)* ntypes (pref prio limit count yard side costBits commander menuField k menu*k
//     bmcode armed range footX footZ mwd buildDistance maxDamage manaSite unitCost canFly floater)*ntypes
//   nunits (flags type x z fx fz mission class flags5a buildingBuild buildsManaSite mover damage reload)*nunits
//   hp manaBits maxManaBits weapon costBits multBits cellX cellZ navigator
//   nmembers member* nplace answer* nreach answer*
// Out: the final seed, then the event log: 0 n (a draw), 1 x z (placement test),
// 2 sx sz x z w h (reachability), 3 type x z (build), 4 threat (defence),
// 5 (retreat), 6 (cancel).
static int constructorOracle() {
    std::string line;
    while (std::getline(std::cin,line)) {
        std::istringstream in(line);
        uint32_t tick,alarm,allocBits,seed;int threat,limited,slot,p1;int32_t anchor;size_t builder;
        in>>tick>>alarm>>allocBits>>threat>>limited>>slot>>anchor>>p1>>seed>>builder;
        ai::ClassicBlockGrids grids;
        for (auto* g:{&grids.land,&grids.water}) { int a,b,c,d,e;in>>a>>b>>c>>d>>e;*g={int16_t(a),int16_t(b),int16_t(c),int16_t(d),e}; }
        int w,h;size_t nft;in>>w>>h>>nft;
        std::vector<bool> sacred(nft);
        for (size_t i=0;i<nft;++i) { uint32_t b;in>>b;sacred[i]=ai::classicSacredValue(std::bit_cast<float>(b)); }
        std::vector<ai::ClassicFeatureCell> plane(size_t(w)*size_t(h));
        size_t n;in>>n;
        for (size_t i=0;i<n;++i) { int x,z,f,bx,bz;in>>x>>z>>f>>bx>>bz;plane[size_t(z)*w+x]={uint16_t(f),uint8_t(bx),uint8_t(bz)}; }
        ai::ClassicSquads s;
        auto table=[&](std::vector<std::optional<ai::ClassicCell>>& t) {
            size_t k;in>>k;t.resize(k);
            for (auto& c:t) { int valid,x,z;in>>valid>>x>>z;if (valid) c=ai::ClassicCell{int16_t(x),int16_t(z)}; }
        };
        table(s.sites);table(s.starts);
        struct Type { int pref,prio,limit,count,yard,side,commander,menuField,bmcode,armed,range,fx,fz,mwd,reach,maxDamage,manaSite,unitCost,canFly,floater;float cost;std::vector<uint16_t> menu; };
        size_t ntypes;in>>ntypes;
        std::vector<Type> types(ntypes);
        for (auto& t:types) {
            uint32_t cost;size_t k;
            in>>t.pref>>t.prio>>t.limit>>t.count>>t.yard>>t.side>>cost>>t.commander>>t.menuField>>k;
            t.cost=std::bit_cast<float>(cost);t.menu.resize(k);
            for (auto& e:t.menu) in>>e;
            in>>t.bmcode>>t.armed>>t.range>>t.fx>>t.fz>>t.mwd>>t.reach>>t.maxDamage>>t.manaSite>>t.unitCost>>t.canFly>>t.floater;
        }
        in>>n;
        std::vector<ai::ClassicSquadUnit> units(n);
        for (size_t i=0;i<n;++i) {
            auto& u=units[i];u.id=int(i);
            uint32_t flags;int t,fx,fz,mission,cls,f5a,bb,ms,mover,damage,reload;
            in>>flags>>t>>u.x>>u.z>>fx>>fz>>mission>>cls>>f5a>>bb>>ms>>mover>>damage>>reload;
            const Type& ty=types[size_t(t)];
            u.flags=flags;u.type=uint16_t(t);u.footX=int16_t(fx);u.footZ=int16_t(fz);
            u.commander=ty.commander!=0;u.menu=ty.menuField;u.mover=mover!=0;
            u.typeFlags=(ty.armed ? 0x10000u : 0u)|(ty.canFly ? 0x800u : 0u)|(ty.floater ? 0x80000u : 0u);
            u.secondaryFlags=(ty.commander ? 0x40000u : 0u)|(ty.manaSite ? 0x80000000u : 0u);
            u.minWaterDepth=int16_t(ty.mwd);
            u.mission=mission!=0;u.missionClass=uint8_t(cls);u.missionFlags=uint8_t(f5a);
            u.buildingBuild=bb!=0;u.buildsManaSite=ms!=0;
            u.weaponDamage=uint16_t(damage);u.weaponReload=uint16_t(reload);u.squad=slot;
        }
        int hp,weapon,cellX,cellZ,navigator;uint32_t manaBits,maxManaBits,costBits,multBits;
        in>>hp>>manaBits>>maxManaBits>>weapon>>costBits>>multBits>>cellX>>cellZ>>navigator;
        auto& base=s.squads[size_t(slot)];
        base.kind=sim::RetailAiSquadKind::Base;base.parameters[0]=anchor;base.parameters[1]=p1;
        in>>n;base.members.resize(n);
        for (auto& id:base.members) in>>id;
        std::vector<int> place,reach;
        in>>n;place.resize(n);for (auto& a:place) in>>a;
        in>>n;reach.resize(n);for (auto& a:reach) in>>a;

        std::vector<std::vector<int>> log;
        auto random=[&](int k){log.push_back({0,k});return sim::retailRandom(seed,k);};
        const ai::detail::ClassicUnitIndex index(units);
        const auto centre=*ai::classicBaseCentre(s,units,index,slot);
        const auto& me=units[builder];
        const Type& bt=types[me.type];
        ai::ClassicConstructorUnit u;
        u.commander=bt.commander!=0;u.hp=int16_t(hp);u.maxDamage=bt.maxDamage;
        u.mana=std::bit_cast<float>(manaBits);u.maxMana=std::bit_cast<float>(maxManaBits);
        u.weapon=weapon!=0;
        u.shotCost=ai::classicShotCost(std::bit_cast<float>(costBits),bt.unitCost ? 1.0f : std::bit_cast<float>(multBits));
        u.mission=me.mission;u.missionClass=me.missionClass;u.building=me.mission && (me.missionFlags&8);
        u.navigator=navigator!=0;
        size_t nextPlace=0,nextReach=0;
        auto cellAt=[&](int x,int z){
            if (x<0 || z<0 || x>=w || z>=h) return ai::ClassicFeatureCell{};
            return plane[size_t(z)*w+x];
        };
        auto isSacred=[&](int x,int z){
            return ai::classicSacredCell(x,z,uint32_t(nft),cellAt,[&](uint16_t f){return f<sacred.size() && sacred[f];});
        };
        struct Host {
            std::function<float()> allocation;
            std::function<int32_t()> firepower,builders,desiredBuilders;
            std::function<uint32_t()> tick,alarm;
            std::function<uint16_t(bool)> draw;
            std::function<ai::ClassicConstructorType(uint16_t)> type;
            std::function<bool(uint16_t)> idleFactory;
            std::function<int32_t(uint16_t)> typeCount;
            std::function<std::optional<int32_t>(unsigned)> radius;
            std::function<std::optional<std::pair<int32_t,int32_t>>(uint16_t,int32_t)> site;
            std::function<bool(uint16_t,std::pair<int32_t,int32_t>)> reachable;
            std::function<bool()> lodestoneBusy;
            std::function<void(uint16_t,std::pair<int32_t,int32_t>)> build;
            std::function<void()> cancel,retreat,combat;
        } host;
        host.allocation=[&]{return std::bit_cast<float>(allocBits);};
        host.firepower=[&]{return ai::classicBaseFirepower(s,units,index,slot,4);};
        host.builders=[&]{return ai::classicBaseCount(s,units,index,slot,2);};
        host.desiredBuilders=[&]{return base.parameters[1];};
        host.tick=[&]{return tick;};host.alarm=[&]{return alarm;};
        host.draw=[&](bool special){
            return ai::classicMenuDraw(bt.menu,special,
                [&](uint16_t t){const auto& e=types[t];
                    return sim::retailAiBuildWeight(limited!=0,e.limit,int16_t(e.count),uint8_t(e.pref),int8_t(e.prio));},
                [&](uint16_t t){return types[t].yard>=0 && (types[t].yard&0x80);},
                [&](uint16_t t){return types[t].side==bt.side;},random);
        };
        host.type=[&](uint16_t t){const auto& e=types[t];return ai::ClassicConstructorType{uint8_t(e.bmcode),e.menuField,e.armed!=0};};
        host.idleFactory=[&](uint16_t t){return ai::classicIdleFactory(s,units,index,slot,t);};
        host.typeCount=[&](uint16_t t){return t>=1 && t<types.size() ? int32_t(int16_t(types[t].count)) : 0;};
        host.radius=[&](unsigned ring){return ai::classicBaseRadius(s,units,index,slot,ring);};
        host.site=[&](uint16_t t,int32_t r){
            const auto& e=types[t];
            ai::ClassicSiteType st;
            st.footX=int16_t(e.fx);st.footZ=int16_t(e.fz);st.minWaterDepth=int16_t(e.mwd);
            st.tower=ai::classicSiteTower(uint8_t(e.bmcode),e.armed!=0,e.range,e.menuField);
            st.lodestone=e.yard>=0 && (e.yard&0x80);
            auto valid=[&](int16_t x,int16_t z){log.push_back({1,x,z});return nextPlace<place.size() && place[nextPlace++]!=0;};
            return ai::classicPlaceSite(st,
                [&]{return ai::classicDepositSite(s.sites,centre,r,st.footX,st.footZ,
                        [&](int16_t x,int16_t z){return x>=0 && z>=0 && x<w && z<h && isSacred(x,z);},valid);},
                [&]{return ai::classicScatterSite(grids,centre,r,st,random,valid,isSacred);});
        };
        host.reachable=[&](uint16_t t,std::pair<int32_t,int32_t> at){
            const auto& e=types[t];
            const auto r=ai::classicReachRect(at,int16_t(e.fx),int16_t(e.fz),uint16_t(bt.reach));
            log.push_back({2,cellX,cellZ,r.x,r.z,r.w,r.h});
            return nextReach<reach.size() && reach[nextReach++]!=0;
        };
        host.lodestoneBusy=[&]{return ai::classicLodestoneBusy(s,units,index,slot);};
        host.build=[&](uint16_t t,std::pair<int32_t,int32_t> at){log.push_back({3,int(t),at.first,at.second});};
        host.cancel=[&]{log.push_back({6});};
        host.retreat=[&]{log.push_back({5});};
        host.combat=[&]{log.push_back({4,threat});};
        struct Calls {
            Host& h;
            float allocation(){return h.allocation();}
            int32_t firepower(){return h.firepower();}
            int32_t builders(){return h.builders();}
            int32_t desiredBuilders(){return h.desiredBuilders();}
            uint32_t tick(){return h.tick();}
            uint32_t alarm(){return h.alarm();}
            uint16_t draw(bool special){return h.draw(special);}
            ai::ClassicConstructorType type(uint16_t t){return h.type(t);}
            bool idleFactory(uint16_t t){return h.idleFactory(t);}
            int32_t typeCount(uint16_t t){return h.typeCount(t);}
            std::optional<int32_t> radius(unsigned ring){return h.radius(ring);}
            std::optional<std::pair<int32_t,int32_t>> site(uint16_t t,int32_t r){return h.site(t,r);}
            bool reachable(uint16_t t,std::pair<int32_t,int32_t> at){return h.reachable(t,at);}
            bool lodestoneBusy(){return h.lodestoneBusy();}
            void build(uint16_t t,std::pair<int32_t,int32_t> at){h.build(t,at);}
            void cancel(){h.cancel();}
            void retreat(){h.retreat();}
            void combat(){h.combat();}
        } calls{host};
        ai::classicPlanConstructor(calls,u,threat!=0);
        std::printf("%u %zu",seed,log.size());
        for (const auto& e:log) for (int v:e) std::printf(" %d",v);
        std::printf("\n");
    }
    return 0;
}

// The port's side of tools/re/check_ai_threat.py: ai::classicThreat then
// ai::classicEnemiesNear on the case's lists (see the script for the layout).
static int threatOracle() {
    std::string line;
    while (std::getline(std::cin,line)) {
        std::istringstream in(line);
        int32_t cx,cz,radius;int upgrade;size_t n;
        in>>cx>>cz>>radius>>upgrade>>n;
        std::vector<ai::ClassicEnemy> all(n);
        for (size_t i=0;i<n;++i) { auto& e=all[i];uint32_t f;in>>f>>e.x>>e.z;e.flags=f;e.id=int(i); }
        auto list=[&]{ size_t k;in>>k;std::vector<ai::ClassicEnemy> l(k);for (auto& e:l) { size_t i;in>>i;e=all[i]; }return l; };
        const auto visible=list(),upgraded=list();
        const bool threat=ai::classicThreat(visible,{cx,cz},radius);
        const auto out=ai::classicEnemiesNear(visible,{cx,cz},radius,upgrade!=0,&upgraded);
        std::printf("%d %zu",int(threat),out.size());
        for (const auto& e:out) std::printf(" %d",e.id);
        std::printf("\n");
    }
    return 0;
}

// The port's side of tools/re/check_ai_defence.py (--defence: 0x40d370) and
// check_ai_retreat.py (--retreat: 0x40d010): ai::classicPlanCombat /
// ai::classicRetreat with the brain's squad helpers on the case's base, units,
// enemies and answers (see check_ai_defence.py for the line layout).
static int defenceOracle(bool retreat) {
    std::string line;
    while (std::getline(std::cin,line)) {
        std::istringstream in(line);
        int w,h,threat,upgrade;int32_t anchor;uint32_t seed;
        in>>w>>h>>anchor>>threat>>seed>>upgrade;
        ai::ClassicSquads s;
        auto table=[&](std::vector<std::optional<ai::ClassicCell>>& t) {
            size_t k;in>>k;t.resize(k);
            for (auto& c:t) { int valid,x,z;in>>valid>>x>>z;if (valid) c=ai::ClassicCell{int16_t(x),int16_t(z)}; }
        };
        table(s.sites);table(s.starts);
        constexpr int slot=1;
        size_t n;in>>n;
        std::vector<ai::ClassicSquadUnit> units(n);
        for (size_t i=0;i<n;++i) {
            auto& u=units[i];u.id=int(i);
            int fx,fz;in>>u.flags>>u.x>>u.z>>fx>>fz;
            u.footX=int16_t(fx);u.footZ=int16_t(fz);u.squad=slot;u.mover=true;
        }
        auto& base=s.squads[size_t(slot)];
        base.kind=sim::RetailAiSquadKind::Base;base.parameters[0]=anchor;
        in>>n;base.members.resize(n);
        for (auto& id:base.members) in>>id;
        ai::ClassicCombatUnit u;
        int commander,builder,canFly,hp,sight,melee,has,target;int64_t maxDamage;
        in>>commander>>builder>>canFly>>maxDamage>>hp>>sight>>u.range>>melee>>u.at[0]>>u.at[1]>>u.at[2];
        in>>has>>u.missionFlags>>target>>u.goalX>>u.goalZ;
        u.armed=units[0].flags&0x8000000u;u.commander=commander!=0;u.builder=builder!=0;u.canFly=canFly!=0;
        u.maxDamage=uint32_t(maxDamage);u.hp=int16_t(hp);u.sight=int16_t(sight);u.melee=melee!=0;
        u.mission=has!=0;u.missionTarget=target>=0 ? 1000+target : 0;
        size_t nen,nen2;in>>nen>>nen2;
        std::vector<ai::ClassicEnemy> visible,upgraded;std::map<int,int32_t> damage;
        for (size_t k=0;k<nen+nen2;++k) {
            ai::ClassicEnemy e;int cloak;int32_t d;
            in>>e.flags>>e.x>>e.y>>e.z>>cloak>>d;
            e.id=1000+int(k);e.cloaked=cloak!=0;damage[e.id]=d;
            (k<nen ? visible : upgraded).push_back(e);
        }
        std::vector<int> place,kinds;
        in>>n;place.resize(n);for (auto& a:place) in>>a;
        in>>n;kinds.resize(n);for (auto& a:kinds) in>>a;

        std::vector<std::vector<int>> log;
        const ai::detail::ClassicUnitIndex index(units);
        size_t nextPlace=0,nextKind=0;
        struct Host {
            std::function<std::optional<ai::ClassicPoint>()> centre;
            std::function<std::optional<int32_t>(unsigned)> radius;
            std::function<std::vector<ai::ClassicEnemy>(std::pair<int32_t,int32_t>,int32_t)> enemies;
            std::function<int32_t(const ai::ClassicEnemy&)> damage;
            std::function<int(int)> random;
            std::function<bool(int32_t,int32_t)> placeable;
            int w,h;
            std::function<uint8_t(int,const ai::ClassicPoint&)> resolve;
            std::function<void(uint8_t,bool,const ai::ClassicPoint&,int32_t,int32_t)> order;
            int width() const { return w; }
            int height() const { return h; }
        } host;
        host.centre=[&]()->std::optional<ai::ClassicPoint> {
            const auto c=ai::classicBaseCentre(s,units,index,slot);
            if (!c) return std::nullopt;
            return ai::ClassicPoint{c->first,0,c->second};
        };
        host.radius=[&](unsigned ring){return ai::classicBaseRadius(s,units,index,slot,ring);};
        host.enemies=[&](std::pair<int32_t,int32_t> at,int32_t r){return ai::classicEnemiesNear(visible,at,r,upgrade!=0,&upgraded);};
        host.damage=[&](const ai::ClassicEnemy& e){return damage[e.id];};
        host.random=[&](int k){log.push_back({0,k});return int(sim::retailRandom(seed,k));};
        host.placeable=[&](int32_t x,int32_t z){
            log.push_back({1,ai::classicFootprintCell(x,units[0].footX),ai::classicFootprintCell(z,units[0].footZ)});
            return nextPlace<place.size() && place[nextPlace++]!=0;
        };
        host.w=w;host.h=h;
        host.resolve=[&](int command,const ai::ClassicPoint& p){
            log.push_back({2,command,p[0],p[1],p[2]});
            return uint8_t(nextKind<kinds.size() ? kinds[nextKind++] : 0);
        };
        host.order=[&](uint8_t kind,bool queue,const ai::ClassicPoint& p,int32_t a6,int32_t a7){
            log.push_back({3,int(kind),int(queue),p[0],p[1],p[2],a6,a7});
        };
        struct Calls {
            Host& h;
            std::optional<ai::ClassicPoint> centre(){return h.centre();}
            std::optional<int32_t> radius(unsigned ring){return h.radius(ring);}
            std::vector<ai::ClassicEnemy> enemies(std::pair<int32_t,int32_t> at,int32_t r){return h.enemies(at,r);}
            int32_t damage(const ai::ClassicEnemy& e){return h.damage(e);}
            int random(int k){return h.random(k);}
            bool placeable(int32_t x,int32_t z){return h.placeable(x,z);}
            int width(){return h.width();}
            int height(){return h.height();}
            uint8_t resolve(int c,const ai::ClassicPoint& p){return h.resolve(c,p);}
            void order(uint8_t k,bool q,const ai::ClassicPoint& p,int32_t a6,int32_t a7){h.order(k,q,p,a6,a7);}
        } calls{host};
        if (retreat) ai::classicRetreat(calls,u);
        else ai::classicPlanCombat(calls,u,threat!=0);
        std::printf("%u %zu",seed,log.size());
        for (const auto& e:log) for (int v:e) std::printf(" %d",v);
        std::printf("\n");
    }
    return 0;
}

// The port's side of tools/re/check_ai_static_grade.py: "<width> <height> <footX>
// <footZ> <rating>*" per line (each cell's own rating, row-major) -> the static
// bit of every cell (ai::classicStaticPlane), row-major.
static int staticGradeOracle() {
    std::string line;
    while (std::getline(std::cin,line)) {
        std::istringstream in(line);
        int w,h,fx,fz;in>>w>>h>>fx>>fz;
        std::vector<int> rating(size_t(w)*size_t(h));
        for (auto& r:rating) in>>r;
        const auto plane=ai::classicStaticPlane(w,h,fx,fz,[&](int x,int z){return rating[size_t(z)*w+x]==0;});
        for (size_t i=0;i<plane.size();++i) std::printf(i ? " %d" : "%d",int(plane[i]));
        std::printf("\n");
    }
    return 0;
}

// The port's side of tools/re/check_ai_squad_ctor.py: "<seed> <E>" per line ->
// the manager's squads as 0x40fed0 leaves them, then the final seed.
static int squadCtorOracle() {
    uint32_t seed;int32_t e;
    while (std::cin>>seed>>e) {
        ai::ClassicSquads s;
        ai::classicConstructSquads(s,e,[&](int n){return sim::retailRandom(seed,n);});
        std::printf("%d",s.schedule.assignmentCountdown);
        for (int i=0;i<100;++i) {
            const auto& q=s.squads[size_t(i)];
            if (!s.schedule.groups[size_t(i)].present) { std::printf(" -1");continue; }
            std::printf(" %d %u",int(q.kind),s.schedule.groups[size_t(i)].deadline);
            for (int k=0;k<9;++k) std::printf(" %d",q.parameters[size_t(k)]);
            std::printf(" %u",q.active);
        }
        std::printf(" %u\n",seed);
    }
    return 0;
}

// The port's side of the PrintWeights check: the brain's own facts, profile and
// priority pass (ClassicBrain::recomputePriorities' steps) over retail's table.
static int printWeightsOracle(const char* root) {
    auto vfs=hpi::mountRetailRoot(root);
    sim::TypeRegistry reg;sim::setupRegistry(reg,vfs,false);
    ai::ClassicBrain brain(1,7,reg);
    brain.useProfile(ai::classicProfileScript(vfs,ai::classicMapAiProfile(vfs,hpi::findMap(vfs,"Abnar's Terrace"))));
    const auto& c=brain.catalogue();const auto& p=brain.profile();const auto& info=brain.typeInfo();
    auto find=[&](const std::string& n){auto it=c.byName.find(n);return it==c.byName.end() ? size_t(0) : size_t(it->second);};
    struct Row { size_t t; int count,completed; };
    std::vector<Row> table;
    unsigned population=0;float ratio=0;bool shortfall=false,creation=false;
    std::string line,word;
    while (std::getline(std::cin,line)) {
        std::istringstream in(line);in>>word;
        if (word=="facts") {
            std::string name;in>>name;const size_t t=find(name);
            if (!t) { std::printf("0 0 0 0 0 0 0\n");continue; }
            const auto f=info[t].priorityInputs(0,0,p.limit[t]);
            std::printf("1 %u %u %d %u %u %d\n",f.flags,f.secondaryFlags,int(f.weapon),
                        std::bit_cast<uint32_t>(f.cost),unsigned(p.pref[t]),p.limit[t]);
        } else if (word=="state") {
            float stored,capacity;size_t n;in>>population>>stored>>capacity>>n;
            table.clear();
            for (size_t i=0;i<n;++i) { std::string name;Row r{};in>>name>>r.count>>r.completed;r.t=find(name);table.push_back(r); }
            sim::RetailResourceHistory h;
            for (auto& s : h.samples) in>>s[0]>>s[1];
            ratio=ai::ClassicBrain::storeFill(stored,capacity);
            shortfall=h.shortfall(stored);
        } else if (word=="creation") {
            int on=0;in>>on;creation=on!=0;
        } else if (word=="seed") {
            uint32_t seed;in>>seed;unsigned draws=0;
            for (const auto& r : table) {
                // A name the port has no type for (retail's own table entry) prints -1.
                if (!r.t) { std::printf("-1 ");continue; }
                const auto in2=info[r.t].priorityInputs(r.count,r.completed,creation ? -1 : p.limit[r.t]);
                const unsigned v=sim::retailBuildPriority(in2,uint16_t(population),ratio,shortfall,
                    [&](int n){if (n>=2) ++draws;return sim::retailRandom(seed,n);});
                std::printf("%u ",v);
            }
            std::printf("%u %u\n",seed,draws);
        }
    }
    return 0;
}

int main(int argc,char** argv) {
    if (argc==2 && std::strcmp(argv[1],"--profile")==0) return profileOracle();
    if (argc==3 && std::strcmp(argv[1],"--printweights")==0) return printWeightsOracle(argv[2]);
    if (argc==2 && std::strcmp(argv[1],"--assignment")==0) return assignmentOracle();
    if (argc==2 && std::strcmp(argv[1],"--squad-ctor")==0) return squadCtorOracle();
    if (argc==2 && std::strcmp(argv[1],"--factory-think")==0) return factoryThinkOracle();
    if (argc==2 && std::strcmp(argv[1],"--placement")==0) return placementOracle();
    if (argc==2 && std::strcmp(argv[1],"--constructor")==0) return constructorOracle();
    if (argc==2 && std::strcmp(argv[1],"--static-grade")==0) return staticGradeOracle();
    if (argc==2 && std::strcmp(argv[1],"--threat")==0) return threatOracle();
    if (argc==2 && std::strcmp(argv[1],"--defence")==0) return defenceOracle(false);
    if (argc==2 && std::strcmp(argv[1],"--retreat")==0) return defenceOracle(true);
    if (argc==3 && std::strcmp(argv[1],"--raid")==0) {
        classicRaid(argv[2]);
        std::printf("%s\n",fails ? "FAILED" : "all passed");
        return fails ? 1 : 0;
    }
    if (argc==3 && std::strcmp(argv[1],"--bases")==0) {
        classicBases(argv[2]);
        std::printf("%s\n",fails ? "FAILED" : "all passed");
        return fails ? 1 : 0;
    }
    profileRules();
    typeFacts();
    typeInfoAndPriorities();
    squadConstruction();
    squadAssignment();
    if (argc>1) { shippedData(argv[1]);abnarSquads(argv[1]);classicFactories(argv[1]); }
    std::printf("%s\n",fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
