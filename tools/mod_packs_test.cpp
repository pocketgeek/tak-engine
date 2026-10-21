#include "net/modpackage.h"
#include "net/client.h"
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace tak;
int main(){
 auto root=std::filesystem::temp_directory_path()/("tak-packs-"+crypto::toHex(crypto::randomVec(8)));
 int failures=0;auto check=[&](bool value,const char* label){if(!value){std::cerr<<"FAIL: "<<label<<'\n';++failures;}};
 auto put=[&](const std::string& path,const std::string& value){auto p=root/std::filesystem::u8path(path);std::filesystem::create_directories(p.parent_path());std::ofstream(p,std::ios::binary)<<value;};
 auto archive=[&](const std::string& path,std::vector<std::pair<std::string,std::string>> entries){
   std::vector<hpi::PackFile> files;for(auto& [name,value]:entries)files.push_back({name,std::vector<uint8_t>(value.begin(),value.end())});
   const auto bytes=hpi::pack(files);put(path,std::string(bytes.begin(),bytes.end()));};
 auto bytesOf=[](const std::string& v){return std::vector<uint8_t>(v.begin(),v.end());};
 auto rejects=[&](auto fn,const char* label){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}check(caught,label);};
 try{
  // A legacy overrides/ folder full of packs: never read, never touched.
  put("overrides/Alpha/sounds/legacy.wav","legacy");put("overrides/Alpha/units/legacy.fbi","legacy");archive("overrides/click.hpi",{{"sounds/legacy-click.wav","legacy"}});put("overrides/OldPack/sounds/old.wav","old");
  auto snapshot=[&]{std::map<std::string,std::string> all;for(const auto& e:std::filesystem::recursive_directory_iterator(root/"overrides")){
    std::string bytes;if(e.is_regular_file()){std::ifstream in(e.path(),std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(in),{});}
    all[e.path().lexically_relative(root).generic_string()]=bytes+"|"+std::to_string(std::filesystem::last_write_time(e.path()).time_since_epoch().count());}return all;};
  const auto legacyBefore=snapshot();
  // Files directly in mods/, mods/cosmetic/ and mods/game/ are never packs.
  put("mods/root-only.wav","forbidden");put("mods/cosmetic/root-only.wav","forbidden");put("mods/game/root-only.wav","forbidden");
  put("mods/cosmetic/Alpha/sounds/test.wav","a");put("mods/cosmetic/Zeta/sounds/test.wav","z");
  put("mods/cosmetic/Alpha/units/test.fbi","unit");put("mods/cosmetic/Alpha/features/test.tdf","feature");put("mods/cosmetic/Alpha/scripts/test.cob","script");put("mods/cosmetic/Alpha/objects3d/test.3do","model");put("mods/cosmetic/Alpha/ai/test.txt","ai");
  put("mods/game/Alpha/sounds/test.wav","game-a");put("mods/game/Zeta/sounds/test.wav","game-z");put("mods/game/Alpha/units/test.fbi","[UNITINFO]\n{\nUnitName=test;\nMaxDamage=5;\n}\n");put("mods/game/Alpha/features/test.tdf","[test]\n{\nreclaimable=1;\n}\n");put("mods/game/Alpha/ai/test.txt","ai");
  put("mods/game/Zeta/sounds/only-game.wav","g");
  check(hpi::modPacks(root,hpi::ModKind::Cosmetic)==std::vector<std::string>({"Alpha","Zeta"}),"only cosmetic pack directories listed");
  check(hpi::modPacks(root,hpi::ModKind::Game)==std::vector<std::string>({"Alpha","Zeta"}),"only game pack directories listed");
  check(hpi::hasLegacyOverridesDir(root),"legacy overrides/ folder detected");
  auto empty=hpi::mountRetailRoot(root);check(!empty.has("root-only.wav")&&!empty.has("sounds/test.wav")&&!empty.has("sounds/legacy.wav"),"root and unselected files never loaded");
  auto cosmetic=hpi::mountRetailRoot(root,{},{"Alpha"});check(cosmetic.has("sounds/test.wav") && cosmetic.read("sounds/test.wav")==std::vector<uint8_t>{'a'},"selected cosmetic sound loaded");
  for(const auto* p:{"units/test.fbi","features/test.tdf","scripts/test.cob","objects3d/test.3do","ai/test.txt"})check(!cosmetic.has(p),"cosmetic cannot change simulation inputs");
  check(hpi::gameplayHash(cosmetic)==hpi::gameplayHash(empty),"cosmetic packs never change the gameplay hash");
  auto game=hpi::mountRetailRoot(root,{"Zeta","Alpha"});
  check(game.has("units/test.fbi") && game.has("features/test.tdf") && game.has("ai/test.txt"),"game packs are not filtered");
  check(game.read("sounds/test.wav")==std::vector<uint8_t>({'g','a','m','e','-','z'}),"game selection keeps stable alphabetical precedence");
  check(hpi::gameplayHash(game)!=hpi::gameplayHash(empty),"game packs change the gameplay hash");
  auto both=hpi::mountRetailRoot(root,{"Alpha","Zeta"},{"Alpha"});
  check(both.read("sounds/test.wav")==std::vector<uint8_t>{'a'},"cosmetic packs win over game packs");
  check(both.has("units/test.fbi") && both.has("sounds/only-game.wav"),"game files stay under cosmetic packs");
  check(hpi::gameplayHash(both)==hpi::gameplayHash(game),"cosmetic packs on top of game packs keep the game hash");
  check(!hpi::mountRetailRoot(root,{},{"Alpha"}).has("sounds/only-game.wav"),"a cosmetic selection never loads game packs");
  rejects([&]{hpi::modFiles(root,hpi::ModKind::Game,{"../escape"});},"reject unknown/traversal pack");
  rejects([&]{hpi::modFiles(root,hpi::ModKind::Cosmetic,{"../game/Alpha"});},"reject a cosmetic name that reaches a game pack");
  rejects([&]{hpi::mountRetailRoot(root,{"OldPack"});},"overrides/ packs are not game packs");rejects([&]{hpi::mountRetailRoot(root,{},{"OldPack"});},"overrides/ packs are not cosmetic packs");
  {   // A selection whose name exists only in the other kind is refused.
    put("mods/game/OnlyGame/sounds/x.wav","x");
    rejects([&]{hpi::modFiles(root,hpi::ModKind::Cosmetic,{"OnlyGame"});},"a game pack is not a cosmetic pack");
  }
  {   // Always-on cosmetic archives: real *.hpi files directly in mods/cosmetic/.
    archive("mods/cosmetic/b.hpi",{{"sounds/test.wav","hb"},{"sounds/always.wav","b"},{"units/always.fbi","unit"},{"features/always.tdf","f"},{"scripts/always.cob","s"},{"ai/always.txt","ai"}});
    archive("mods/cosmetic/A.HPI",{{"sounds/test.wav","ha"},{"sounds/always.wav","a"},{"sounds/only-a.wav","a"}});
    archive("mods/cosmetic/c.ufo",{{"sounds/ufo.wav","u"}});archive("mods/cosmetic/d.kmp",{{"sounds/kmp.wav","k"}});
    archive("mods/root.hpi",{{"sounds/mods-root.wav","r"}});archive("mods/game/root.hpi",{{"sounds/game-root.wav","r"}});
    put("mods/cosmetic/loose.wav","loose");
    archive("mods/cosmetic/Nested/inner.hpi",{{"sounds/nested.wav","n"}});
    std::error_code ec;std::filesystem::create_symlink(root/"mods/cosmetic/b.hpi",root/"mods/cosmetic/link.hpi",ec);
    check(hpi::alwaysOnMods(root)==std::vector<std::string>({"A.HPI","b.hpi"}),"always-on lists only real *.hpi files in mods/cosmetic/ (any case)");
    const auto packs=hpi::modPacks(root,hpi::ModKind::Cosmetic);
    check(std::find(packs.begin(),packs.end(),"b.hpi")==packs.end() && std::find(packs.begin(),packs.end(),"A.HPI")==packs.end(),"always-on archives are never listed as selectable packs");
    const auto on=hpi::alwaysOnModFiles(root);
    check(on.count("sounds/test.wav") && on.at("sounds/test.wav")==bytesOf("hb"),"always-on archives layer alphabetically (later name wins)");
    check(on.count("sounds/only-a.wav"),"every always-on archive loads");
    for(const auto* q:{"units/always.fbi","features/always.tdf","scripts/always.cob","ai/always.txt"})check(!on.count(q),"always-on archives keep cosmetic files only");
    for(const auto* q:{"sounds/ufo.wav","sounds/kmp.wav","loose.wav","sounds/mods-root.wav","sounds/game-root.wav","sounds/nested.wav","sounds/legacy-click.wav"})check(!on.count(q),"only *.hpi files directly in mods/cosmetic/ are always on");
    auto plain=hpi::mountRetailRoot(root,{"Alpha"});
    check(!plain.has("sounds/always.wav"),"mountRetailRoot (handshake hash, map packages) never adds always-on archives");
    // A campaign game: no packs at all, the always-on archives still load.
    std::vector<std::string> skipped;
    hpi::Vfs campaign=hpi::mountPlayVfs(root,{},{},true,&skipped);
    check(skipped.empty(),"harmless cosmetic layers are never left out");
    check(campaign.has("sounds/always.wav") && campaign.read("sounds/test.wav")==bytesOf("hb"),"always-on archives load with no packs selected");
    check(!campaign.has("sounds/legacy-click.wav"),"a legacy overrides/ archive is never loaded");
    check(hpi::gameplayHash(campaign)==hpi::gameplayHash(empty),"always-on archives never change the gameplay hash");
    // A skirmish/MP game: game files, then always-on, then the ticked cosmetic packs.
    hpi::Vfs play=hpi::mountPlayVfs(root,hpi::modFiles(root,hpi::ModKind::Game,{"Alpha","Zeta"}),{"Zeta","Gone"});
    check(play.read("sounds/test.wav")==bytesOf("z"),"cosmetic pack folders win over always-on archives");
    check(play.read("sounds/always.wav")==bytesOf("b") && play.has("sounds/only-game.wav") && play.has("units/test.fbi"),"always-on archives sit over game packs");
    check(hpi::gameplayHash(play)==hpi::gameplayHash(game),"always-on + cosmetic layers keep the game-mods hash");
    hpi::Vfs over=hpi::mountPlayVfs(root,hpi::modFiles(root,hpi::ModKind::Game,{"Zeta"}),{});
    check(over.read("sounds/test.wav")==bytesOf("hb"),"always-on archives win over game packs");
    check(!hpi::mountPlayVfs(root,{},{"Zeta"},false).has("sounds/always.wav"),"--mods none|game leaves the always-on archives out");
    {   // A cosmetic layer never stops a game: a damaged always-on archive loads nothing.
      put("mods/cosmetic/broken.hpi","not an archive");skipped.clear();
      auto vfs=hpi::mountPlayVfs(root,{},{"Zeta"},true,&skipped);
      check(vfs.has("sounds/always.wav") && vfs.read("sounds/test.wav")==bytesOf("z"),
            "a damaged always-on archive never stops the other mods loading");
      std::filesystem::remove(root/"mods/cosmetic/broken.hpi");
    }
    {   // anims/ art passes the cosmetic filter, but a storm's frame timing is
        // gameplay: an always-on archive (or a ticked pack) that changes it is
        // left out, so the player's Loaded hash still matches the referee's.
      auto gaf=[](uint32_t delay,uint8_t pixel){   // one 1x1 frame named "stormfx"
        std::vector<uint8_t> art(89);auto word=[&](size_t at,uint32_t v,int n){for(int i=0;i<n;++i)art[at+size_t(i)]=uint8_t(v>>(i*8));};
        word(0,0x10100,4);word(4,1,4);word(12,16,4);word(16,1,2);word(18,1,2);std::memcpy(art.data()+24,"stormfx",7);
        word(56,64,4);word(60,delay,4);word(64,1,2);word(66,1,2);word(80,88,4);art.back()=pixel;return std::string(art.begin(),art.end());};
      put("mods/game/Storm/units/storm.fbi","[WEAPON1]\n{\nwanderstartart=stormfx;\n}\n");put("mods/game/Storm/anims/stormfx.gaf",gaf(3,1));
      archive("mods/cosmetic/fx.hpi",{{"anims/stormfx.gaf",gaf(9,1)},{"sounds/fx.wav","fx"}});
      archive("mods/cosmetic/recolor.hpi",{{"anims/stormfx.gaf",gaf(3,77)}});
      put("mods/cosmetic/Timing/anims/stormfx.gaf",gaf(5,1));
      const auto storm=hpi::modFiles(root,hpi::ModKind::Game,{"Storm"});
      hpi::Vfs room=hpi::mountRetailRoot(root,{"Storm"});
      skipped.clear();auto vfs=hpi::mountPlayVfs(root,storm,{"Timing"},true,&skipped);
      check(hpi::gameplayHash(vfs)==hpi::gameplayHash(room),"cosmetic layers never change the game-mods hash, even with anims/ timing");
      auto named=[&](const char* n){return std::any_of(skipped.begin(),skipped.end(),[&](const auto& s){return s.starts_with(std::string(n)+" (");});};
      check(named("fx.hpi") && named("Timing") && !named("recolor.hpi") && skipped.size()==2,"only the timing-changing cosmetic layers are left out");
      check(!vfs.has("sounds/fx.wav") && vfs.read("anims/stormfx.gaf")==bytesOf(gaf(3,77)),"a timing-preserving recolor still loads");
      skipped.clear();vfs=hpi::mountPlayVfs(root,{},{"Timing"},true,&skipped);
      check(skipped.empty() && vfs.has("sounds/fx.wav"),"anims/ art no unit uses keeps the layer");
      for(const char* f:{"mods/cosmetic/fx.hpi","mods/cosmetic/recolor.hpi"})std::filesystem::remove(root/f);
      std::filesystem::remove_all(root/"mods/cosmetic/Timing");std::filesystem::remove_all(root/"mods/game/Storm");
    }
    std::filesystem::remove(root/"mods/cosmetic/link.hpi",ec);
  }
  auto p=net::mods::build(root,{"Zeta","Alpha"});auto q=net::mods::build(root,{"Alpha","Zeta"});check(p->digest==q->digest,"selection order does not affect package digest");
  check(p->files->count("units/test.fbi")&&!p->files->count("root-only.wav"),"package includes selected files only");
  check(p->files->at("sounds/test.wav")==std::vector<uint8_t>({'g','a','m','e','-','z'}),"package carries game packs, never cosmetic ones");
  check(!p->files->count("sounds/legacy.wav"),"package never carries overrides/ files");
  check(!p->files->count("sounds/always.wav") && !p->files->count("sounds/only-a.wav"),"package never carries always-on cosmetic archives");
  auto bytes=p->bytes;bytes.back()^=1;rejects([&]{net::mods::decode(bytes,p->digest);},"reject corrupt download");
  net::Writer bad;bad.u32(0x32564f54);bad.u32(0);bad.u32(1);bad.str("../escape");bad.blob(std::vector<uint8_t>{1});auto hash=crypto::toHex(crypto::sha256(bad.b.data(),bad.b.size()));
  rejects([&]{net::mods::decode(bad.b,hash);},"reject network path traversal");
  {   // Files larger than the 4 KiB login-field cap survive whole: the 68,068-byte
      // Skitterhen 3DO was cut to 4096 bytes ("3DO index out of range").
    auto blob=[&](size_t n,uint32_t seed){std::string s(n,'\0');for(size_t i=0;i<n;++i){seed=seed*1103515245u+12345u;s[i]=char(seed>>16);}return s;};
    const auto model=blob(68068,1),script=blob(4097,2),art=blob(300000,3);
    put("mods/game/Big/objects3d/skitterhen.3do",model);put("mods/game/Big/scripts/skitterhen.cob",script);put("mods/game/Big/anims/skitterhen.gaf",art);
    auto big=net::mods::build(root,{"Big"});
    auto same=[&](const char* path,const std::string& want){auto it=big->files->find(path);return it!=big->files->end() && it->second.size()==want.size() && std::equal(want.begin(),want.end(),it->second.begin(),[](char a,uint8_t b){return uint8_t(a)==b;});};
    check(same("objects3d/skitterhen.3do",model),"68,068-byte 3DO round-trips whole");
    check(same("scripts/skitterhen.cob",script),"4,097-byte COB round-trips whole");
    check(same("anims/skitterhen.gaf",art),"300,000-byte GAF round-trips whole");
    auto again=net::mods::decode(big->bytes,big->digest);check(*again->files==*big->files,"large files survive decode of the transferred bytes");
    net::mods::saveCache(root,*big);auto bigCached=net::mods::loadCache(root,big->digest);check(bigCached&&*bigCached->files==*big->files,"large files survive the cache");
    auto oversize=[&](uint32_t claimed){net::Writer w;w.u32(0x32564f54);w.u32(0);w.u32(1);w.str("units/x.fbi");w.u32(claimed);w.b.push_back(1);return w.b;};
    for(uint32_t claimed:{uint32_t(2),uint32_t(0xFFFFFFFFu)}){auto b=oversize(claimed);auto h=crypto::toHex(crypto::sha256(b.data(),b.size()));rejects([&]{net::mods::decode(b,h);},"reject a file length past the end of the package");}
    net::Writer v1;v1.u32(0x31564f54);v1.u32(0);v1.u32(0);auto h1=crypto::toHex(crypto::sha256(v1.b.data(),v1.b.size()));rejects([&]{net::mods::decode(v1.b,h1);},"reject version-1 packages (truncating format)");
    net::Writer login;login.bytes(std::vector<uint8_t>(5000,7));check(login.b.size()==2+4096,"login-field bytes() keeps its 4 KiB cap");
  }
  net::mods::saveCache(root,*p);auto cached=net::mods::loadCache(root,p->digest);check(cached&&*cached->files==*p->files,"verified cache round trip");
  rejects([&]{net::mods::loadCache(root,p->digest,p->bytes.size()-1);},"mod actual budget");
  rejects([&]{net::mods::loadCache(root,p->digest,net::maps::kMaxBytes,p->bytes.size()-1);},"mod advertised size");
  put("ModCache/"+p->digest+".takmods","corrupt");
  check(!net::mods::loadCache(root,p->digest),"corrupt cache rejected");
  net::mods::saveCache(root,*p);check(bool(net::mods::loadCache(root,p->digest)),"corrupt cache repaired");
  {   // A full disk or tight quota must never cost an existing good entry.
    const auto entry=root/"ModCache"/(p->digest+".takmods");
    net::mods::saveCache(root,*p,1);check(bool(net::mods::loadCache(root,p->digest)),"existing cache needs no quota and survives it");
    put("ModCache/"+p->digest+".takmods","corrupt");
    rejects([&]{net::mods::saveCache(root,*p,1);},"quota accepted a replacement");
    check(std::filesystem::file_size(entry)==7,"quota failure deleted the existing entry first");
    net::mods::saveCache(root,*p);check(bool(net::mods::loadCache(root,p->digest)),"damaged entry replaced in place");
    auto fresh=net::mods::build(root,{"Alpha"});rejects([&]{net::mods::saveCache(root,*fresh,1);},"quota accepted a new entry");
    check(!std::filesystem::exists(root/"ModCache"/(fresh->digest+".takmods")),"quota rejection left a partial entry");
  }
  auto base=hpi::mountRetailRoot(root);hpi::Vfs roomA(&base),roomB(&base);roomA.setModFiles(p->files);check(roomA.has("units/test.fbi")&&!roomB.has("units/test.fbi"),"room mods do not leak");
  {   // A saved game-pack selection naming a folder since removed is dropped, so a
      // stale settings entry can never stop single-player games from building.
    net::MpClient client;client.setMapRoot(root);
    client.setGameModPacks({"Zeta","Removed","Alpha","Zeta"});
    check(client.gameModPacks()==std::vector<std::string>({"Alpha","Zeta"}),"stale or duplicate game packs are dropped from the selection");
    auto built=net::mods::build(root,client.gameModPacks());check(built->files->count("units/test.fbi"),"the filtered selection builds");
  }
  check(snapshot()==legacyBefore,"the legacy overrides/ folder is left byte-identical");
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';++failures;}
 std::error_code ec;std::filesystem::remove_all(root,ec);std::cout<<"mod packs: "<<failures<<" failures\n";return failures?1:0;
}
