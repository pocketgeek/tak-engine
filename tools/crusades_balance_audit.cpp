// Research-only metadata diff using the engine's retail VFS and TDF parser.
// Never emits whole definitions or descriptive game prose.
#include "hpi/hpi.h"
#include "tdf/tdf.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <set>
#include <string>

using Fields=std::map<std::string,std::string>;
static std::string lower(std::string s) {
    for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
static std::string escaped(const std::string& s) {
    std::string out;
    for(char c:s) {
        if(c=='\\')out+="\\\\";
        else if(c=='\t')out+="\\t";
        else if(c=='\r')out+="\\r";
        else if(c=='\n')out+="\\n";
        else out+=c;
    }
    return out;
}
static void flatten(const tak::tdf::Node& n,const std::string& prefix,Fields& out) {
    for(const auto& [key,value]:n.values)out.emplace(prefix+key,value);
    std::map<std::string,int> counts;
    for(const auto& [name,child]:n.orderedChildren()) {
        auto label=std::string(name);
        int occurrence=++counts[label];
        flatten(*child,prefix+label+"#"+std::to_string(occurrence)+"/",out);
    }
}
static Fields fields(const tak::hpi::Vfs& vfs,const std::string& path) {
    const auto bytes=vfs.read(path);
    Fields out;
    flatten(tak::tdf::parseText(std::string(bytes.begin(),bytes.end()),path),"",out);
    return out;
}
static bool prose(const std::string& key) {
    const auto leaf=key.substr(key.find_last_of('/')+1);
    return leaf=="name" || leaf=="description" || leaf=="designation";
}
int main(int argc,char**argv) {
    if(argc!=2){std::cerr<<"usage: crusades_balance_audit <retail-data-root>\n";return 2;}
    try {
        auto root=tak::hpi::mountRetailRoot(argv[1],tak::hpi::OverridePolicy::None);
        tak::hpi::Vfs vfs(&root,true); // downloaded map resources cannot change this audit
        std::cout<<"kind\tpath\tfield\tstandard\tcrusades\n";
        size_t compared=0,changedFiles=0,changedFields=0,addedFiles=0;
        for(const auto& prefix:{std::string("units"),std::string("canbuild")}) {
            const auto cb=prefix+"cb";
            const auto extension=prefix=="units"?".fbi":".tdf";
            std::map<std::string,std::string> paths;
            std::set<std::string> builders;
            for(const auto& path:vfs.list(cb)) {
                const auto key=lower(path);
                if(key.starts_with(cb+"/") && key.ends_with(extension)) {
                    paths.emplace(key,path);
                    if(prefix=="canbuild") {
                        const auto slash=key.find('/',cb.size()+1);
                        if(slash!=std::string::npos && key.find('/',slash+1)==std::string::npos)
                            builders.insert(key.substr(cb.size()+1,slash-cb.size()-1));
                    }
                }
            }
            // A CB menu replaces the builder's entire base menu. Missing entries
            // are removals, unlike units, whose absent CB definitions fall back.
            if(prefix=="canbuild")for(const auto& path:vfs.list(prefix)) {
                const auto key=lower(path);
                const auto slash=key.find('/',prefix.size()+1);
                if(!key.starts_with(prefix+"/") || !key.ends_with(extension) ||
                   slash==std::string::npos || key.find('/',slash+1)!=std::string::npos)continue;
                const auto builder=key.substr(prefix.size()+1,slash-prefix.size()-1);
                if(builders.count(builder))paths.try_emplace(cb+key.substr(prefix.size()),"");
            }
            for(const auto& [key,path]:paths) {
                const auto base=prefix+key.substr(cb.size());
                const bool exists=vfs.has(base);
                const auto a=exists?fields(vfs,base):Fields{};
                const auto b=path.empty()?Fields{}:fields(vfs,path);
                ++compared;
                if(!exists)++addedFiles;
                std::set<std::string> keys;
                for(const auto& [k,v]:a)keys.insert(k);
                for(const auto& [k,v]:b)keys.insert(k);
                bool changed=!exists || path.empty();
                if(changed)std::cout<<(path.empty()?"removed-menu-entry":"added-definition")
                    <<'\t'<<escaped(base)<<"\t<definition>\t"
                    <<(exists?"present":"<absent>")<<'\t'
                    <<(path.empty()?"<absent>":"present")<<'\n';
                for(const auto& k:keys) {
                    const auto left=a.find(k),right=b.find(k);
                    if(left!=a.end() && right!=b.end() && left->second==right->second)continue;
                    changed=true;++changedFields;
                    const auto value=[&](const auto& it,const auto& end) {
                        if(it==end)return std::string("<absent>");
                        return prose(k)?std::string("<presentation text omitted>"):escaped(it->second);
                    };
                    std::cout<<(prose(k)?"presentation":(path.empty()?"removed-menu-entry":
                        (!exists?"added-definition":"changed")))
                        <<'\t'<<escaped(base)<<'\t'<<escaped(k)<<'\t'
                        <<value(left,a.end())<<'\t'<<value(right,b.end())<<'\n';
                }
                if(changed)++changedFiles;
            }
        }
        if(compared==0)throw std::runtime_error("No Crusades definitions found");
        std::cerr<<compared<<" overlay files compared; "<<changedFiles<<" changed; "
                 <<addedFiles<<" without base; "<<changedFields<<" field differences\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
