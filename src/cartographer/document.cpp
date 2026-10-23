#include "cartographer/document.h"
#include "cartographer/scenarioinfo.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace cart {
namespace fs = std::filesystem;
namespace {
std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(),s.end()}; }
void replace(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw fs::filesystem_error("replace",from,to,std::error_code(GetLastError(),std::system_category()));
#else
    fs::rename(from,to);
#endif
}
void write(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    if (!f) throw std::runtime_error("Cannot create staged map file");
    f.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));
    f.flush();
    if (!f) throw std::runtime_error("Cannot write map (disk full or write error)");
    f.close();
    if (!f) throw std::runtime_error("Cannot close map file after writing");
}
}

bool validDocumentName(const std::string& name) {
    if (name.empty() || name=="." || name==".." || name.back()=='.' || name.back()==' ') return false;
    for (unsigned char c:name) if(c<32 || std::string("/\\:*?\"<>|").find(char(c))!=std::string::npos) return false;
    auto base=name.substr(0,name.find('.'));
    for(auto& c:base)if(c>='a' && c<='z')c=char(c-'a'+'A');
    if(base=="CON" || base=="PRN" || base=="AUX" || base=="NUL" || base=="CONIN$" || base=="CONOUT$")return false;
    if(base.size()==4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3]>='1' && base[3]<='9')return false;
    return true;
}

bool aiFileKey(const std::string& path) {
    const auto key=tak::hpi::MountSet::key(path);
    return key.starts_with("ai/") && key.ends_with(".txt") && key.find('/',3)==std::string::npos &&
           key.size()>7 && validDocumentName(key.substr(3));
}

AiFiles aiFilesFromArchive(const tak::hpi::Archive& archive) {
    AiFiles result;
    for(const auto& entry:archive.entries()) if(!entry.isDirectory && aiFileKey(entry.path))
        result[tak::hpi::MountSet::key(entry.path)]=archive.read(entry);
    return result;
}

AiFiles aiFilesFromFolder(const fs::path& mapFolder) {
    AiFiles result;std::error_code ec;
    for(const auto& folder:fs::directory_iterator(mapFolder.empty()?fs::path("."):mapFolder,ec)) {
        const auto name=folder.path().filename().u8string();
        if(tak::hpi::MountSet::key(std::string(name.begin(),name.end()))!="ai" || !folder.is_directory(ec))continue;
        for(const auto& file:fs::directory_iterator(folder.path(),ec)) {
            const auto text=file.path().filename().u8string();const std::string path="ai/"+std::string(text.begin(),text.end());
            if(!file.is_regular_file(ec) || !aiFileKey(path))continue;
            std::ifstream stream(file.path(),std::ios::binary);
            if(stream)result[tak::hpi::MountSet::key(path)]={std::istreambuf_iterator<char>(stream),{}};
        }
    }
    return result;
}

AiFiles aiFilesFromMapArchive(const tak::hpi::Vfs& vfs, const std::string& mapPath) {
    AiFiles result;
    for(const auto& path:vfs.mapArchivePaths(mapPath)) if(aiFileKey(path))
        if(auto data=vfs.readFromMapArchive(mapPath,path))result[tak::hpi::MountSet::key(path)]=std::move(*data);
    return result;
}

AiFiles liftAiFiles(tak::hpi::Vfs::Files& files) {
    AiFiles result;
    for(auto it=files.begin();it!=files.end();) {
        if(aiFileKey(it->first)) {result[tak::hpi::MountSet::key(it->first)]=std::move(it->second);it=files.erase(it);}
        else ++it;
    }
    return result;
}

fs::path installMapsFolder(const fs::path& dataRoot) {
    std::error_code ec;
    if(dataRoot.empty())return {};
    for(const auto& entry:fs::directory_iterator(dataRoot,ec)) {
        const auto name=entry.path().filename().u8string();
        if(tak::hpi::MountSet::key(std::string(name.begin(),name.end()))=="maps" && entry.is_directory(ec))return entry.path();
    }
    return {};
}

bool insideFolder(const fs::path& path, const fs::path& folder) {
    if(folder.empty())return false;
    std::error_code ec;
    const auto p=fs::weakly_canonical(fs::absolute(path.empty()?fs::path("."):path),ec);if(ec)return false;
    const auto f=fs::weakly_canonical(fs::absolute(folder),ec);if(ec)return false;
    // Windows and macOS folders usually ignore case; compare names that way there.
    auto same=[](const fs::path& a,const fs::path& b) {
#if defined(_WIN32) || defined(__APPLE__)
        const auto x=a.u8string(),y=b.u8string();
        return tak::hpi::MountSet::key(std::string(x.begin(),x.end()))==tak::hpi::MountSet::key(std::string(y.begin(),y.end()));
#else
        return a==b;
#endif
    };
    auto pi=p.begin();
    for(auto fi=f.begin();fi!=f.end();++fi) {
        if(fi->empty())continue;   // a trailing separator
        if(pi==p.end() || !same(*pi,*fi))return false;
        ++pi;
    }
    return true;
}

DocumentFiles documentFiles(
    const tak::tnt::Map& map, tak::tnt::Scenario metadata,
    const tak::crt::Scenario& scenario, const std::vector<PlacedUnit>& units,
    const std::set<std::string>& useOnly, const AiFiles& aiFiles, const std::string& name) {
    if (!validDocumentName(name)) throw std::runtime_error("Choose a map name without path separators or reserved filename characters");
    metadata.hasScenario = !units.empty() || !scenario.customTypes.empty() || !scenario.regions.empty();
    for (const auto& player:scenario.players) if(!player.empty()) metadata.hasScenario=true;
    metadata.sizeW=map.width/32; metadata.sizeH=map.height/32;
    metadata.useOnlyUnits=useOnly.empty()?"":name+".tdf";
    DocumentFiles files;
    files.members={{name+".tnt",map.save()}, {name+".ota",bytes(metadata.write())},
            {name+".crt",saveScenario(scenario,units)},
            {name+".editor",bytes(scenarioInfo(scenario))},
            // Always write even an empty restriction file to retire stale exports.
            {name+".tdf",bytes(writeUseOnly({useOnly.begin(),useOnly.end()}))},
            {name+".txt",bytes(metadata.missionDescription.empty()?metadata.missionName:metadata.missionDescription)}};
    for(const auto& [path,data]:aiFiles) {
        if(path!=tak::hpi::MountSet::key(path) || !aiFileKey(path))throw std::runtime_error("Invalid AI profile file name: "+path);
        // A map package rejects an empty AI file; empty means deleted.
        if(!data.empty())files.rootMembers.push_back({path,data});
    }
    return files;
}

bool writeDocumentFiles(const fs::path& directory, const std::vector<tak::hpi::PackFile>& files,
                        std::string& error, const std::atomic_bool* cancel) {
    fs::path stage;
    std::vector<bool> existed;
    size_t installed=0;
    bool preserveStage=false;
    error.clear();
    try {
        auto checkCancel=[&] {if(cancel && cancel->load())throw std::runtime_error("Save cancelled");};
        checkCancel();
        const fs::path dir=directory.empty()?fs::path("."):directory;
        if(!fs::is_directory(dir)) throw std::runtime_error("Save folder does not exist");
        std::set<std::string> names;
        for(const auto& file:files) {
            if(!validDocumentName(file.path) || !names.insert(file.path).second)
                throw std::runtime_error("Invalid or duplicate export filename");
            const auto target=dir/fs::u8path(file.path);
            if(fs::exists(target) && !fs::is_regular_file(target))
                throw std::runtime_error("Save destination is not a regular file");
        }
        static std::atomic<unsigned> serial{0};
        do {
            stage=dir/(".tak-save-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial++));
        } while(!fs::create_directory(stage));
        for(size_t i=0;i<files.size();++i) {
            checkCancel();
            write(stage/(std::to_string(i)+".new"),files[i].data);
            const auto target=dir/fs::u8path(files[i].path);
            existed.push_back(fs::exists(target));
            if(existed.back()) {
                fs::copy_file(target,stage/(std::to_string(i)+".old"));
                fs::copy_file(target,stage/(std::to_string(i)+".backup"));
            }
        }
        // Cancellation ends at publication: finish this short transaction atomically
        // (or roll it back) instead of abandoning a partly installed document.
        checkCancel();
        // Finish every write/backup before replacing any member of the document.
        for(size_t i=0;i<files.size();++i) if(existed[i])
            replace(stage/(std::to_string(i)+".backup"),dir/fs::u8path(files[i].path+".bak"));
        for(size_t i=0;i<files.size();++i) {
            replace(stage/(std::to_string(i)+".new"),dir/fs::u8path(files[i].path));
            ++installed;
        }
    } catch(const std::exception& e) {
        error=e.what();
        const fs::path dir=directory.empty()?fs::path("."):directory;
        for(size_t i=0;i<installed;++i) {
            try {
                const auto target=dir/fs::u8path(files[i].path);
                if(existed[i]) replace(stage/(std::to_string(i)+".old"),target);
                else fs::remove(target);
            } catch(const std::exception& restore) {
                preserveStage=true;
                error += "\nRollback failed: "+std::string(restore.what())+". Recovery copies remain in "+stage.string();
            }
        }
    }
    if(!stage.empty() && !preserveStage) { std::error_code ec;fs::remove_all(stage,ec); }
    return error.empty();
}

bool writeDocumentBundle(const fs::path& path, const DocumentFiles& files,
                         std::string& error, const std::atomic_bool* cancel) {
    try {
        if(cancel && cancel->load())throw std::runtime_error("Save cancelled");
        const auto name=path.stem().u8string();
        const std::string stem(name.begin(),name.end());
        if(!validDocumentName(stem)) throw std::runtime_error("Invalid map filename");
        auto members=files.members;
        for(auto& member:members) member.path="kmap/"+stem+"/"+member.path;
        // AI profiles sit at the archive root, where the game's VFS reads ai/<x>.txt.
        for(const auto& member:files.rootMembers) members.push_back(member);
        const auto filename=path.filename().u8string();
        return writeDocumentFiles(path.parent_path(),{{std::string(filename.begin(),filename.end()),tak::hpi::pack(members)}},error,cancel);
    } catch(const std::exception& e) { error=e.what();return false; }
}
bool writeDocumentLoose(const fs::path& directory, const DocumentFiles& files, const fs::path& installMaps,
                        std::string& error, const std::atomic_bool* cancel) {
    error.clear();
    if(!files.rootMembers.empty()) {
        if(insideFolder(directory,installMaps)) {
            error="This map has AI profiles. Loose files in the game's Maps folder would apply to every map. Save as .kmp to keep AI profiles with the map.";
            return false;
        }
        try {
            const fs::path dir=directory.empty()?fs::path("."):directory;
            if(!fs::is_directory(dir)) throw std::runtime_error("Save folder does not exist");
            fs::path ai;std::error_code ec;
            for(const auto& entry:fs::directory_iterator(dir,ec)) {
                const auto name=entry.path().filename().u8string();
                if(tak::hpi::MountSet::key(std::string(name.begin(),name.end()))=="ai") {ai=entry.path();break;}
            }
            if(ai.empty()) {ai=dir/"ai";fs::create_directory(ai);}
            std::vector<tak::hpi::PackFile> profiles;
            for(const auto& member:files.rootMembers) profiles.push_back({member.path.substr(3),member.data});
            // The profiles first: a failure here leaves the map files untouched.
            if(!writeDocumentFiles(ai,profiles,error,cancel))return false;
        } catch(const std::exception& e) {error=e.what();return false;}
    }
    return writeDocumentFiles(directory,files.members,error,cancel);
}
} // namespace cart
