// The Classic AI level's building blocks (src/ai/classic*): the retail AI profile
// baker, and (with a data directory) the shipped profile through the real VFS.
//
//   classic_ai_test                 unit tests
//   classic_ai_test <retail-root>   + the shipped data checks
//   classic_ai_test --profile       oracle mode for tools/re/check_ai_profile.py:
//       stdin  "types <n>" then n lines "<name> <category words...>", then any
//              number of "case <hex script>" lines
//       stdout per case, for each catalogue index 1..n: "pref limit prefLock limitLock"
#include "ai/classicbrain.h"
#include "ai/classicprofile.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
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

int main(int argc,char** argv) {
    if (argc==2 && std::strcmp(argv[1],"--profile")==0) return profileOracle();
    profileRules();
    if (argc>1) shippedData(argv[1]);
    std::printf("%s\n",fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
