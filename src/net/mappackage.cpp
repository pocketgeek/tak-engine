#include "net/mappackage.h"
#include "tnt/tnt.h"
#include "tnt/mapgen.h"
#include "tdf/tdf.h"
#include "util/virtualpath.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace tak::net::maps {
namespace {
std::string hash(const std::vector<uint8_t>& bytes) {
    return crypto::toHex(crypto::sha256(bytes.data(), bytes.size()));
}
std::string tilePath(uint32_t key) {
    char path[40]; std::snprintf(path, sizeof path, "terrain/%08x.jpg", key); return path;
}
bool safePath(const std::string& p) {
    if (p.empty() || p.size() > 512 || p != hpi::MountSet::key(p) || p.front() == '/' ||
        p.find('\\') != std::string::npos || p.find(':') != std::string::npos) return false;
    size_t start = 0;
    for (size_t i = 0; i <= p.size(); ++i) {
        if (i < p.size() && (uint8_t(p[i]) < 32 || p[i] == 127)) return false;
        if (i == p.size() || p[i] == '/') {
            auto part = p.substr(start, i - start);
            if (part.empty() || part == "." || part == "..") return false;
            start = i + 1;
        }
    }
    return true;
}
// Only companions of the selected map may travel with it. In particular this
// must not permit arbitrary gameplay definitions or executable scripts.
std::set<std::string> companionPaths(const std::string& path) {
    std::set<std::string> result;
    for (const char* extension : {".ota", ".crt", ".tdf", ".txt", ".editor", ".recipe"})
        result.insert(vpath::replaceExtension(path, extension));
    return result;
}
// Follow feature burn/death chains as well as their sprites and palettes. A
// feature file may define several names, so include dependencies of every
// definition it brings into the room, not only the initially placed feature.
std::set<std::string> featureResources(const tnt::Map& map, const hpi::Vfs& vfs) {
    std::map<std::string, tdf::Node> definitions;
    std::map<std::string, std::string> owner;
    for (const auto& path : vfs.list("features")) {
        if (!path.ends_with(".tdf")) continue;
        tdf::Node parsed;
        try {
            const auto data = vfs.read(path);
            parsed = tdf::parseText(std::string(data.begin(), data.end()), path);
        } catch (const std::exception&) { continue; } // Match the feature loader's handling of unrelated bad defs.
        const auto key = hpi::MountSet::key(path);
        for (const auto& name : parsed.childOrder) owner[hpi::MountSet::key(name)] = key;
        definitions.emplace(key, std::move(parsed));
    }
    std::vector<std::string> pending = map.featureNames;
    std::set<std::string> files;
    auto art = [&](const std::string& path) {
        const auto key = hpi::MountSet::key(path);
        if (safePath(key) && vfs.has(key)) files.insert(key);
    };
    while (!pending.empty()) {
        auto name = hpi::MountSet::key(pending.back()); pending.pop_back();
        auto found = owner.find(name);
        if (found == owner.end() || !files.insert(found->second).second) continue;
        for (const auto& [section, def] : definitions.at(found->second).children) {
            for (const char* chain : {"featureburnt", "featuredead"}) {
                auto next = def.valueOr(chain, ""); if (!next.empty()) pending.push_back(next);
            }
            auto file = def.valueOr("filename", "");
            if (!file.empty()) art("anims/" + file + ".gaf");
            auto world = def.valueOr("world", "aramon");
            art("palettes/" + world + "_features.pcx"); art("palettes/" + world + ".pcx");
            art("palettes/aramon_features.pcx");
            for (const char* flame : {"seqnamefrontflame", "seqnamebackflame"}) {
                auto name = def.valueOr(flame, "");
                if (!name.empty()) for (const char* suffix : {"_4444.taf", "_1555.taf", ".taf", ".gaf"})
                    art("anims/" + name + suffix);
            }
        }
    }
    return files;
}
std::shared_ptr<Package> encode(std::string path, std::shared_ptr<hpi::Vfs::Files> files) {
    Writer w; w.u32(2); w.str(path); w.u32(uint32_t(files->size()));
    for (const auto& [name, data] : *files) {
        if (w.b.size() + data.size() + name.size() + 6 > kMaxBytes)
            throw std::runtime_error("map package exceeds 256 MiB");
        w.str(name); w.u32(uint32_t(data.size())); w.b.insert(w.b.end(), data.begin(), data.end());
    }
    auto p = std::make_shared<Package>(); p->mapPath = std::move(path);
    p->files = std::move(files); p->bytes = std::move(w.b); p->digest = hash(p->bytes); return p;
}
void writeAtomic(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    // Unique staging names also handle a local server sharing its client's data root.
    auto tmp = path; tmp += "." + crypto::toHex(crypto::randomVec(8)) + ".tmp";
    try {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        out.close();
        if (!out) throw std::runtime_error("could not save map file");
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            // Windows cannot rename over an existing file. An identical concurrent
            // writer is fine; a corrupted cache is removed by loadCache instead.
            if (!std::filesystem::exists(path)) throw std::runtime_error("could not publish map file: " + ec.message());
            std::filesystem::remove(tmp);
        }
    } catch (...) { std::error_code ec; std::filesystem::remove(tmp, ec); throw; }
}
}
bool validDigest(const std::string& s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
static std::shared_ptr<Package> buildUncached(const hpi::Vfs& vfs, const std::string& path) {
    auto files = std::make_shared<hpi::Vfs::Files>();
    (*files)[path] = vfs.read(path);
    const auto map = tnt::Map::load(files->at(path), path);
    for (const auto& companion : companionPaths(path))
        if (auto data = vfs.tryRead(companion)) (*files)[companion] = std::move(*data);
    for (auto key : std::set<uint32_t>(map.tileKeys.begin(), map.tileKeys.end())) {
        auto tile = tilePath(key);
        // Missing art is an error, not a black map silently propagated to peers.
        (*files)[tile] = vfs.read(tile);
    }
    for (const auto& resource : featureResources(map, vfs)) (*files)[resource] = vfs.read(resource);
    return decode(encode(path, files)->bytes, "");
}

std::shared_ptr<Package> importSnapshot(const hpi::Vfs& base, const std::filesystem::path& path) {
    hpi::Archive archive(path);
    auto files = std::make_shared<hpi::Vfs::Files>();
    std::string mapPath;
    size_t total = 0;
    for (const auto& entry : archive.entries()) if (!entry.isDirectory) {
        if (entry.decompressedSize > kMaxBytes - total) throw std::runtime_error("Test map exceeds the map transfer size limit");
        total += entry.decompressedSize;
        const auto key = hpi::MountSet::key(entry.path);
        if (tak::vpath::extension(key) == ".tnt") {
            if (!mapPath.empty()) throw std::runtime_error("Test Map requires a KMP containing exactly one map");
            mapPath = key;
        }
        if (!files->emplace(key, archive.read(entry)).second)
            throw std::runtime_error("Test map has duplicate file names");
    }
    if (mapPath.empty()) throw std::runtime_error("Test map contains no terrain");
    hpi::Vfs view(&base);
    view.setMapFiles(files);
    const auto validated=buildUncached(view, mapPath); // current snapshot wins over cached revisions
    auto enabled=std::make_shared<hpi::Vfs::Files>(*validated->files);
    auto& ota=(*enabled)[tak::vpath::replaceExtension(mapPath,".ota")];
    const auto metadata=tdf::parseText(std::string(ota.begin(),ota.end()));
    const auto* test=metadata.child("takplaytest");
    if(!test || test->numberOr("authoredscenario",0)==0) {
        // Explicit production playtest opt-in. Ordinary retail maps may carry CRT
        // data too; their existing skirmish setup must remain unchanged. Append a
        // separate section rather than rewriting/dropping unknown retail OTA keys.
        const std::string marker="\n[TAKPlaytest]\n{\nauthoredscenario=1;\n}\n";
        ota.insert(ota.end(),marker.begin(),marker.end());
    }
    return decode(encode(mapPath,std::move(enabled))->bytes, "");
}

std::shared_ptr<Package> build(const hpi::Vfs& vfs, const std::string& mapId) {
    std::string path = hpi::findMap(vfs, mapId);
    if (path.empty() || mapgen::isGeneratedMapId(path)) throw std::runtime_error("map not installed: " + mapId);
    path = hpi::MountSet::key(path);
    if (auto cached = vfs.cachedMap(path)) {
        auto files = std::make_shared<hpi::Vfs::Files>(*cached->second);
        return decode(encode(cached->first, std::move(files))->bytes, "");
    }
    return buildUncached(vfs, path);
}
std::shared_ptr<Package> decode(std::vector<uint8_t> bytes, const std::string& digest) {
    if (bytes.empty() || bytes.size() > kMaxBytes) throw std::runtime_error("invalid map package size");
    const auto actual = hash(bytes);
    if (!digest.empty() && actual != digest) throw std::runtime_error("map checksum mismatch");
    Reader r(bytes.data(), bytes.size());
    if (r.u32() != 2) throw std::runtime_error("unsupported map package version");
    const auto path = r.str();
    const auto companions = companionPaths(path);
    if (!safePath(path) || !(path.starts_with("maps/") || path.starts_with("kmap/")) || !path.ends_with(".tnt"))
        throw std::runtime_error("invalid map path");
    auto files = std::make_shared<hpi::Vfs::Files>();
    const auto count = r.u32();
    if (count < 1 || count > 32768) throw std::runtime_error("invalid map file count");
    for (uint32_t i = 0; i < count; ++i) {
        auto name = r.str(); const auto size = r.u32();
        const bool terrain = name.size() == 20 && name.starts_with("terrain/") && name.ends_with(".jpg") &&
            name.substr(8,8).find_first_not_of("0123456789abcdef") == std::string::npos;
        const bool feature = name.starts_with("features/") && name.ends_with(".tdf");
        const bool art = (name.starts_with("anims/") && (name.ends_with(".gaf") || name.ends_with(".taf"))) ||
                         (name.starts_with("palettes/") && name.ends_with(".pcx"));
        const bool companion = companions.count(name) != 0;
        // Empty descriptions/restrictions are meaningful; geometry and art are not.
        if (!r.ok || !safePath(name) || (name != path && !companion && !terrain && !feature && !art) ||
            !r.avail(size) || (!size && !companion) || files->count(name)) throw std::runtime_error("invalid map resource");
        (*files)[name] = std::vector<uint8_t>(r.p, r.p + size); r.p += size;
    }
    if (!r.ok || r.p != r.end || !files->count(path)) throw std::runtime_error("incomplete map package");
    const auto& data = files->at(path);
    // Bound dimensions before allocating/parsing a remote TNT.
    Reader header(data.data(), data.size()); header.u32(); auto width = header.u32(), height = header.u32();
    if (!header.ok || width < 2 || height < 2 || width > 4096 || height > 4096 || width % 2 || height % 2)
        throw std::runtime_error("invalid map dimensions");
    auto map = tnt::Map::load(data, path);
    std::set<std::string> required{path};
    for (const auto& companion : companions)
        if (files->count(companion)) required.insert(companion);
    for (auto key : map.tileKeys) required.insert(tilePath(key));
    hpi::Vfs view; view.setMapFiles(files);
    const auto features = featureResources(map, view);
    required.insert(features.begin(), features.end());
    if (required.size() != files->size()) throw std::runtime_error("unreferenced map resources");
    for (const auto& name : required) if (!files->count(name)) throw std::runtime_error("missing map resource: " + name);
    auto p = std::make_shared<Package>(); p->mapPath = path; p->digest = actual;
    p->files = std::move(files); p->bytes = std::move(bytes); return p;
}
Writer offer(uint32_t room, const std::string& mapId, const Package& p) {
    Writer w; w.u32(room); w.str(mapId); w.str(p.digest); w.u32(uint32_t(p.bytes.size())); return w;
}
void Receiver::begin(uint32_t roomId, uint32_t count, const std::string& h) {
    if (!roomId || !count || count > kMaxBytes || !validDigest(h)) throw std::runtime_error("invalid map offer");
    room = roomId; size = count; digest = h; bytes.clear();
}
bool Receiver::append(Reader& r) {
    auto id = r.u32(), off = r.u32(); const size_t n = size_t(r.end - r.p);
    if (!r.ok || id != room || !size || off != bytes.size() || !n || n > kChunkBytes || n > size - bytes.size())
        throw std::runtime_error("invalid map chunk");
    bytes.insert(bytes.end(), r.p, r.end); return complete();
}
void Sender::pump(Conn& conn) {
    while (package && conn.txPending() < 2 * kChunkBytes) {
        auto n = std::min(kChunkBytes, package->bytes.size() - offset);
        Writer w; w.u32(room); w.u32(uint32_t(offset));
        w.b.insert(w.b.end(), package->bytes.begin() + offset, package->bytes.begin() + offset + n);
        conn.send(Msg::MapChunk, w); offset += n;
        if (offset == package->bytes.size()) { package.reset(); offset = 0; }
    }
}
void saveCache(const std::filesystem::path& root, const Package& p) {
    if (!validDigest(p.digest)) throw std::runtime_error("invalid map cache key");
    writeAtomic(root / "MapCache" / (p.digest + ".takmap"), p.bytes);
    // The picker sees a unique alias; only the selected archive is mounted at play.
    const auto archive = root / "MapCache" / (p.digest + ".kmp");
    if (!std::filesystem::exists(archive)) {
        std::vector<hpi::PackFile> files;
        for (const auto& [path, bytes] : *p.files) files.push_back({path, bytes});
        writeAtomic(archive, hpi::pack(files));
    }
}
std::shared_ptr<Package> loadCache(const std::filesystem::path& root, const std::string& digest) {
    if (!validDigest(digest)) return {};
    const auto path = root / "MapCache" / (digest + ".takmap");
    try {
        std::error_code ec; auto size = std::filesystem::file_size(path, ec);
        if (ec || !size || size > kMaxBytes) return {};
        std::vector<uint8_t> bytes(size); std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size));
        if (!in) return {};
        return decode(std::move(bytes), digest);
    } catch (const std::exception&) { std::error_code ec; std::filesystem::remove(path, ec); return {}; }
}
std::filesystem::path saveGenerated(const std::filesystem::path& root, const hpi::Vfs& vfs, const std::string& recipe) {
    if (!mapgen::isGeneratedMapId(recipe)) return {};
    const std::string id = crypto::toHex(crypto::sha256(recipe));
    const auto params = mapgen::decodeMapId(recipe);
    const std::string name = "Generated-" + std::string(mapgen::layoutName(params.layout)) + "-" +
        mapgen::friendlyLabel(params) + "-" + id.substr(0,16);
    const auto output = root / "Maps" / ("Generated-" + id + ".kmp");
    if (std::filesystem::exists(output)) return output;
    auto g = mapgen::generate(params, vfs);
    const std::string path = "kmap/" + name;
    std::vector<hpi::PackFile> files{{path + ".tnt", g.map.save()}};
    const char* worlds[] = {"Aramon","Taros","Veruna","Zhon","Creon"};
    std::string ota = "[GlobalHeader]\n{\nmissionname=" + name + ";\nmissiondescription=" +
        mapgen::friendlyLabel(params) + " Seed " + std::to_string(params.seed) + ";\nkingdom=" + worlds[params.mapType % 5] +
        ";\nnumplayers=" + std::to_string(g.starts.size()) + ";\ngravity=112;\nminwindspeed=100;\nmaxwindspeed=2000;\n[Map Data]\n{\n[Specials]\n{\n";
    for (size_t i = 0; i < g.starts.size(); ++i)
        ota += "[Special" + std::to_string(i) + "]\n{\nspecialwhat=StartPos" + std::to_string(i + 1) +
            ";\nxpos=" + std::to_string(g.starts[i].first) + ";\nzpos=" + std::to_string(g.starts[i].second) + ";\n}\n";
    ota += "}\n}\n}\n";
    files.push_back({path + ".ota", {ota.begin(), ota.end()}});
    files.push_back({path + ".recipe", {recipe.begin(), recipe.end()}});
    for (auto key : std::set<uint32_t>(g.map.tileKeys.begin(), g.map.tileKeys.end())) {
        auto tile = tilePath(key); files.push_back({tile, vfs.read(tile, g.map.stockTerrain)});
    }
    writeAtomic(output, hpi::pack(files)); return output;
}
}
