#include "net/overridepackage.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace tak;
int main(){
 auto root=std::filesystem::temp_directory_path()/("tak-packs-"+crypto::toHex(crypto::randomVec(8)));
 int failures=0;auto check=[&](bool value,const char* label){if(!value){std::cerr<<"FAIL: "<<label<<'\n';++failures;}};
 auto put=[&](const std::string& path,const std::string& value){auto p=root/std::filesystem::u8path(path);std::filesystem::create_directories(p.parent_path());std::ofstream(p)<<value;};
 auto rejects=[&](auto fn,const char* label){bool caught=false;try{fn();}catch(const std::exception&){caught=true;}check(caught,label);};
 try{
  put("overrides/root-only.wav","forbidden");put("overrides/Alpha/sounds/test.wav","a");put("overrides/Zeta/sounds/test.wav","z");
  put("overrides/Alpha/units/test.fbi","unit");put("overrides/Alpha/features/test.tdf","feature");put("overrides/Alpha/scripts/test.cob","script");put("overrides/Alpha/objects3d/test.3do","model");put("overrides/Alpha/ai/test.txt","ai");
  auto names=hpi::overridePacks(root);check(names==std::vector<std::string>({"Alpha","Zeta"}),"only pack directories listed");
  auto empty=hpi::mountRetailRoot(root,hpi::OverridePolicy::Full);check(!empty.has("root-only.wav")&&!empty.has("sounds/test.wav"),"root and unselected files never loaded");
  auto cosmetic=hpi::mountRetailRoot(root,hpi::OverridePolicy::Cosmetic,{"Alpha"});check(cosmetic.has("sounds/test.wav"),"selected cosmetic sound loaded");
  for(const auto* p:{"units/test.fbi","features/test.tdf","scripts/test.cob","objects3d/test.3do","ai/test.txt"})check(!cosmetic.has(p),"cosmetic cannot change simulation inputs");
  auto full=hpi::mountRetailRoot(root,hpi::OverridePolicy::Full,{"Zeta","Alpha"});check(full.has("units/test.fbi") && full.read("sounds/test.wav")==std::vector<uint8_t>{'z'},"full selection and stable alphabetical precedence");
  check(!hpi::mountRetailRoot(root,hpi::OverridePolicy::None,{"Alpha"}).has("sounds/test.wav"),"off ignores remembered selections");
  rejects([&]{hpi::overrideFiles(root,hpi::OverridePolicy::Full,{"../escape"});},"reject unknown/traversal pack");
  auto p=net::overrides::build(root,{"Zeta","Alpha"});auto q=net::overrides::build(root,{"Alpha","Zeta"});check(p->digest==q->digest,"selection order does not affect package digest");
  check(p->files->count("units/test.fbi")&&!p->files->count("root-only.wav"),"package includes selected files only");
  auto bytes=p->bytes;bytes.back()^=1;rejects([&]{net::overrides::decode(bytes,p->digest);},"reject corrupt download");
  net::Writer bad;bad.u32(0x31564f54);bad.u32(0);bad.u32(1);bad.str("../escape");bad.bytes(std::vector<uint8_t>{1});auto hash=crypto::toHex(crypto::sha256(bad.b.data(),bad.b.size()));
  rejects([&]{net::overrides::decode(bad.b,hash);},"reject network path traversal");
  net::overrides::saveCache(root,*p);auto cached=net::overrides::loadCache(root,p->digest);check(cached&&*cached->files==*p->files,"verified cache round trip");
  put("OverrideCache/"+p->digest+".takoverrides","corrupt");
  check(!net::overrides::loadCache(root,p->digest),"corrupt cache rejected");
  net::overrides::saveCache(root,*p);check(bool(net::overrides::loadCache(root,p->digest)),"corrupt cache repaired");
  auto base=hpi::mountRetailRoot(root,hpi::OverridePolicy::None);hpi::Vfs roomA(&base),roomB(&base);roomA.setOverrideFiles(p->files);check(roomA.has("units/test.fbi")&&!roomB.has("units/test.fbi"),"room overrides do not leak");
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';++failures;}
 std::error_code ec;std::filesystem::remove_all(root,ec);std::cout<<"override packs: "<<failures<<" failures\n";return failures?1:0;
}
