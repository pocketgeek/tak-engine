// The Classic AI level's building blocks (src/ai/classic*): the retail AI profile
// baker, the per-type facts and the bookkeeping clock with its dynamic build
// priorities on a fixed World, and (with a data directory) the shipped profile
// through the real VFS.
//
//   classic_ai_test                 unit tests
//   classic_ai_test <retail-root>   + the shipped data checks
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
#include "ai/classicbrain.h"
#include "ai/classicprofile.h"
#include "ai/classictypes.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <iostream>
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
    // Mirror the brain's stream to find the first refresh, then check the values
    // the cache then held (the previous tick's recount).
    uint32_t mirror=ai::ClassicBrain::seedFor(0,11);
    for (size_t t=1;t<c.size();++t)
        if (info[t].cost>500.0f) { // the creation pass's draws (only the castle costs > 500)
            sim::RetailBuildPriority in=info[t].priorityInputs(0,0,-1);
            sim::retailBuildPriority(in,uint16_t(0),0.0f,true,[&](int n){return sim::retailRandom(mirror,n);});
        }
    check(mirror==brain.seed(),"the creation pass draws only for types costing over 500, at population 0");
    uint32_t refreshAt=0;
    for (uint32_t tick=7;tick<10000 && !refreshAt;tick+=7) {
        if (sim::retailRandom(mirror,30)==0) refreshAt=tick;
    }
    for (uint32_t tick=1;tick<refreshAt;++tick) think(tick);
    check(brain.priorityRefreshes()==1,"no refresh before the first rand(30)==0 of a 7-tick window");
    think(refreshAt);
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
    profileRules();
    typeFacts();
    typeInfoAndPriorities();
    if (argc>1) shippedData(argv[1]);
    std::printf("%s\n",fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
