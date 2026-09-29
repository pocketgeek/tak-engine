#include "cartographer/document.h"

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

std::vector<tak::hpi::PackFile> documentFiles(
    const tak::tnt::Map& map, tak::tnt::Scenario metadata,
    const tak::crt::Scenario& scenario, const std::vector<PlacedUnit>& units,
    const std::set<std::string>& useOnly, const std::string& name) {
    if (!validDocumentName(name)) throw std::runtime_error("Choose a map name without path separators or reserved filename characters");
    metadata.hasScenario = !units.empty() || !scenario.customTypes.empty() || !scenario.regions.empty();
    for (const auto& player:scenario.players) if(!player.empty()) metadata.hasScenario=true;
    metadata.sizeW=map.width/32; metadata.sizeH=map.height/32;
    metadata.useOnlyUnits=useOnly.empty()?"":name+".tdf";
    return {{name+".tnt",map.save()}, {name+".ota",bytes(metadata.write())},
            {name+".crt",saveScenario(scenario,units)},
            // Always write even an empty restriction file to retire stale exports.
            {name+".tdf",bytes(writeUseOnly({useOnly.begin(),useOnly.end()}))},
            {name+".txt",bytes(metadata.missionDescription.empty()?metadata.missionName:metadata.missionDescription)}};
}

bool writeDocumentFiles(const fs::path& directory, const std::vector<tak::hpi::PackFile>& files,
                        std::string& error) {
    fs::path stage;
    std::vector<bool> existed;
    size_t installed=0;
    bool preserveStage=false;
    error.clear();
    try {
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
            write(stage/(std::to_string(i)+".new"),files[i].data);
            const auto target=dir/fs::u8path(files[i].path);
            existed.push_back(fs::exists(target));
            if(existed.back()) {
                fs::copy_file(target,stage/(std::to_string(i)+".old"));
                fs::copy_file(target,stage/(std::to_string(i)+".backup"));
            }
        }
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

bool writeDocumentBundle(const fs::path& path, const std::vector<tak::hpi::PackFile>& files,
                         std::string& error) {
    try {
        const auto name=path.stem().u8string();
        const std::string stem(name.begin(),name.end());
        if(!validDocumentName(stem)) throw std::runtime_error("Invalid map filename");
        auto members=files;
        for(auto& member:members) member.path="kmap/"+stem+"/"+member.path;
        const auto filename=path.filename().u8string();
        return writeDocumentFiles(path.parent_path(),{{std::string(filename.begin(),filename.end()),tak::hpi::pack(members)}},error);
    } catch(const std::exception& e) { error=e.what();return false; }
}
} // namespace cart
