#include "cartographer/preferences.h"
#include "cartographer/document.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace cart {
void EditorPreferences::remember(const std::string& request) {
    if(request.empty() || request.size()>4096)return;
    recent.erase(std::remove(recent.begin(),recent.end(),request),recent.end());
    recent.insert(recent.begin(),request);if(recent.size()>12)recent.resize(12);
}
EditorPreferences loadEditorPreferences(const std::filesystem::path& folder) {
    EditorPreferences p;std::error_code error;
    const auto file=folder/"preferences.txt";
    if(std::filesystem::file_size(file,error)>65536 || error)return p;
    std::ifstream in(file);std::string line;
    while(std::getline(in,line)) {
        std::istringstream row(line);std::string key;row>>key;
        int value=0;
        if(key=="width" && row>>value && value>=640 && value<=8192)p.width=value;
        if(key=="height" && row>>value && value>=480 && value<=8192)p.height=value;
        if(key=="scale" && row>>value && value>=50 && value<=200)p.scalePercent=value;
        for(auto [name,setting]:{std::pair{"features",&p.showFeatures}, {"units",&p.showUnits}, {"starts",&p.showStarts}, {"regions",&p.showRegions}, {"grid",&p.showGrid}, {"guide",&p.guideSeen}, {"trace",&p.tracePlaytest}})
            if(key==name && row>>value && (value==0 || value==1))*setting=value!=0;
        if(key=="recent") {
            std::string name;if(row>>std::quoted(name) && !name.empty() && name.size()<=4096 && p.recent.size()<12 &&
                std::find(p.recent.begin(),p.recent.end(),name)==p.recent.end())p.recent.push_back(std::move(name));
        }
    }
    return p;
}
bool saveEditorPreferences(const std::filesystem::path& folder,const EditorPreferences& p,std::string& error) {
    if(folder.empty()) {error="No editor preferences folder";return false;}
    std::ostringstream text;text<<"width "<<p.width<<"\nheight "<<p.height<<"\nscale "<<p.scalePercent<<'\n';
    text<<"features "<<p.showFeatures<<"\nunits "<<p.showUnits<<"\nstarts "<<p.showStarts<<"\nregions "<<p.showRegions<<"\ngrid "<<p.showGrid<<'\n';
    text<<"guide "<<p.guideSeen<<'\n';
    text<<"trace "<<p.tracePlaytest<<'\n';
    for(size_t i=0;i<std::min(size_t(12),p.recent.size());++i)text<<"recent "<<std::quoted(p.recent[i])<<'\n';
    const auto bytes=text.str();return writeDocumentFiles(folder,{{"preferences.txt",{bytes.begin(),bytes.end()}}},error);
}
}
