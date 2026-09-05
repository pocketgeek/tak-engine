#include "hpi/hpi.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace tak::hpi {

namespace {

uint32_t u32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    std::vector<uint8_t> data(std::filesystem::file_size(path));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in) throw std::runtime_error("short read on " + path.string());
    return data;
}

constexpr size_t kChunkHeaderSize = 19;

// Decompress one SQSH chunk at `off`; returns decompressed payload and
// advances `off` past the chunk.
std::vector<uint8_t> readChunk(const std::vector<uint8_t>& data, size_t& off) {
    if (off + kChunkHeaderSize > data.size() || std::memcmp(&data[off], "SQSH", 4) != 0)
        throw std::runtime_error("expected SQSH chunk at offset " + std::to_string(off));
    uint8_t method = data[off + 5];
    uint8_t encrypted = data[off + 6];
    uint32_t compSize = u32(&data[off + 7]);
    uint32_t decompSize = u32(&data[off + 11]);
    off += kChunkHeaderSize;
    if (off + compSize > data.size())
        throw std::runtime_error("SQSH chunk overruns archive");

    std::vector<uint8_t> payload(data.begin() + off, data.begin() + off + compSize);
    off += compSize;
    if (encrypted) {
        for (uint32_t i = 0; i < compSize; ++i)
            payload[i] = uint8_t((payload[i] - i) ^ i);
    }

    if (method == 0) return payload;  // stored
    if (method != 2)
        throw std::runtime_error("unsupported SQSH compression method " + std::to_string(method));

    std::vector<uint8_t> out(decompSize);
    uLongf outLen = decompSize;
    int rc = uncompress(out.data(), &outLen, payload.data(), compSize);
    if (rc != Z_OK || outLen != decompSize)
        throw std::runtime_error("zlib uncompress failed (rc=" + std::to_string(rc) + ")");
    return out;
}

// A block (dir/name) is either a single SQSH chunk or stored raw.
std::vector<uint8_t> readBlock(const std::vector<uint8_t>& data, uint32_t off, uint32_t size) {
    if (off + 4 <= data.size() && std::memcmp(&data[off], "SQSH", 4) == 0) {
        size_t pos = off;
        return readChunk(data, pos);
    }
    if (uint64_t(off) + size > data.size())
        throw std::runtime_error("block overruns archive");
    return {data.begin() + off, data.begin() + off + size};
}

std::string nameAt(const std::vector<uint8_t>& names, uint32_t off) {
    if (off >= names.size()) throw std::runtime_error("name offset out of range");
    const char* s = reinterpret_cast<const char*>(&names[off]);
    return std::string(s, strnlen(s, names.size() - off));
}

void walk(const std::vector<uint8_t>& dir, const std::vector<uint8_t>& names,
          uint32_t off, const std::string& prefix, std::vector<Entry>& out) {
    if (uint64_t(off) + 20 > dir.size()) throw std::runtime_error("dir entry out of range");
    uint32_t namePtr = u32(&dir[off]);
    uint32_t firstSub = u32(&dir[off + 4]);
    uint32_t subCount = u32(&dir[off + 8]);
    uint32_t firstFile = u32(&dir[off + 12]);
    uint32_t fileCount = u32(&dir[off + 16]);

    std::string name = nameAt(names, namePtr);
    std::string path = prefix.empty() ? name : (name.empty() ? prefix : prefix + "/" + name);
    if (!path.empty())
        out.push_back({.path = path, .isDirectory = true});

    for (uint32_t i = 0; i < subCount; ++i)
        walk(dir, names, firstSub + i * 20, path, out);

    for (uint32_t i = 0; i < fileCount; ++i) {
        uint32_t f = firstFile + i * 24;
        if (uint64_t(f) + 24 > dir.size()) throw std::runtime_error("file entry out of range");
        Entry e;
        std::string fname = nameAt(names, u32(&dir[f]));
        e.path = path.empty() ? fname : path + "/" + fname;
        e.start = u32(&dir[f + 4]);
        e.decompressedSize = u32(&dir[f + 8]);
        e.compressedSize = u32(&dir[f + 12]);
        e.date = u32(&dir[f + 16]);
        out.push_back(std::move(e));
    }
}

bool iequals(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y));
           });
}

} // namespace

Archive::Archive(const std::filesystem::path& file) : file_(file) {
    header_ = inspect(file);
    if (header_.magic != "HAPI")
        throw std::runtime_error(file.string() + ": not an HPI archive");
    if (header_.version != 0x00020000)
        throw std::runtime_error(file.string() + ": not a TAK (v2) archive");

    auto data = readFile(file_);
    auto dirBlock = readBlock(data, header_.dirBlock, header_.dirSize);
    auto nameBlock = readBlock(data, header_.nameBlock, header_.nameSize);
    walk(dirBlock, nameBlock, 0, "", entries_);
}

const std::vector<uint8_t>& Archive::bytes() const {
    if (!loaded_) { fileData_ = readFile(file_); loaded_ = true; }
    return fileData_;
}

std::vector<uint8_t> Archive::read(const Entry& entry) const {
    if (entry.isDirectory) throw std::runtime_error(entry.path + " is a directory");
    const std::vector<uint8_t>& data = bytes();

    if (entry.compressedSize == 0) {
        if (uint64_t(entry.start) + entry.decompressedSize > data.size())
            throw std::runtime_error(entry.path + ": raw data overruns archive");
        return {data.begin() + entry.start,
                data.begin() + entry.start + entry.decompressedSize};
    }

    std::vector<uint8_t> out;
    out.reserve(entry.decompressedSize);
    size_t pos = entry.start;
    while (out.size() < entry.decompressedSize) {
        auto chunk = readChunk(data, pos);
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    if (out.size() != entry.decompressedSize)
        throw std::runtime_error(entry.path + ": decompressed size mismatch");
    return out;
}

const Entry* Archive::find(const std::string& path) const {
    for (const auto& e : entries_)
        if (!e.isDirectory && iequals(e.path, path)) return &e;
    return nullptr;
}

HeaderInfo inspect(const std::filesystem::path& archive) {
    std::ifstream in(archive, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + archive.string());

    HeaderInfo info;
    info.fileSize = std::filesystem::file_size(archive);

    uint8_t raw[32] = {};
    in.read(reinterpret_cast<char*>(raw), sizeof raw);
    if (!in) throw std::runtime_error(archive.string() + ": too short for an HPI header");

    info.magic.assign(reinterpret_cast<char*>(raw), 4);
    info.version = u32(raw + 4);
    info.dirBlock = u32(raw + 8);
    info.dirSize = u32(raw + 12);
    info.nameBlock = u32(raw + 16);
    info.nameSize = u32(raw + 20);
    info.dataStart = u32(raw + 24);
    return info;
}

std::string describe(const HeaderInfo& info) {
    std::ostringstream out;
    out << "magic:      " << (info.magic == "HAPI" ? "HAPI (ok)" : "'" + info.magic + "' (not an HPI archive?)") << "\n";

    char buf[96];
    std::snprintf(buf, sizeof buf, "version:    0x%08x", info.version);
    out << buf;
    switch (info.version) {
        case 0x00010000: out << " (classic Total Annihilation)"; break;
        case 0x00020000: out << " (TA: Kingdoms)"; break;
        case 0x4B4E4142: out << " ('BANK' savegame)"; break;
        default:         out << " (unknown)"; break;
    }
    out << "\n";
    std::snprintf(buf, sizeof buf,
                  "dir block:  0x%08x (%u bytes)\nname block: 0x%08x (%u bytes)\ndata start: 0x%08x\n",
                  info.dirBlock, info.dirSize, info.nameBlock, info.nameSize, info.dataStart);
    out << buf;
    out << "size:       " << info.fileSize << " bytes\n";
    return out.str();
}

// ---- MountSet: retail asset precedence over a directory of archives --------

std::string MountSet::key(std::string p) {
    for (char& c : p) {
        if (c == '\\') c = '/';
        else c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    if (!p.empty() && p.front() == '/') p.erase(p.begin());
    return p;
}

MountSet::MountSet(const std::filesystem::path& dir, MountConfig cfg)
    : dir_(dir), cfg_(std::move(cfg)) {
    namespace fs = std::filesystem;
    // Case-insensitive filename sort, like the retail FindFirstFile scan (so a
    // tie in file date breaks the same way it did originally).
    auto ci = [](const fs::path& a, const fs::path& b) {
        std::string x = a.filename().string(), y = b.filename().string();
        for (char& c : x) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : y) c = char(std::tolower(static_cast<unsigned char>(c)));
        return x < y;
    };
    // Collect archives one extension group at a time, each group alphabetical,
    // groups concatenated in cfg order -- reproducing the retail
    // FindFirstFile("*.HPI") then ("*.UFO") mount order for the default config.
    for (const std::string& want : cfg_.archiveExts) {
        std::vector<fs::path> group;
        if (fs::is_directory(dir_))
            for (const auto& e : fs::directory_iterator(dir_)) {
                if (!e.is_regular_file()) continue;
                std::string ext = e.path().extension().string();
                for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
                if (ext == want) group.push_back(e.path());
            }
        std::sort(group.begin(), group.end(), ci);
        archiveFiles_.insert(archiveFiles_.end(), group.begin(), group.end());
    }

    // Mount each; resolve conflicts by newest entry date (a strict '>' keeps the
    // earlier mount on a tie, matching the retail 'jae' comparison).
    for (const auto& file : archiveFiles_) {
        int idx = int(archives_.size());
        try {
            archives_.emplace_back(file);
        } catch (const std::exception&) {
            continue;   // skip an unreadable archive rather than abort the mount
        }
        for (const auto& e : archives_.back().entries()) {
            if (e.isDirectory) continue;
            if (cfg_.keep && !cfg_.keep(e.path)) continue;   // filtered (e.g. cosmetic tier)
            std::string k = key(e.path);
            auto it = map_.find(k);
            if (it == map_.end() || e.date > it->second.entry.date)
                map_[k] = Win{idx, e};
        }
    }
}

bool MountSet::has(const std::string& path) const {
    if (cfg_.keep && !cfg_.keep(path)) return false;
    if (cfg_.includeLoose && std::filesystem::is_regular_file(dir_ / path)) return true;
    return map_.count(key(path)) != 0;
}

std::vector<uint8_t> MountSet::read(const std::string& path) const {
    if (cfg_.keep && !cfg_.keep(path)) throw std::runtime_error("filtered: " + path);
    // 1. A loose file on disk overrides archives (the engine fopen()s first).
    if (cfg_.includeLoose) {
        std::filesystem::path loose = dir_ / path;
        if (std::filesystem::is_regular_file(loose)) {
            std::ifstream f(loose, std::ios::binary);
            return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        }
    }
    // 2. Otherwise the winning (newest) archive copy.
    auto it = map_.find(key(path));
    if (it == map_.end()) throw std::runtime_error("not in mount set: " + path);
    return archives_[size_t(it->second.archive)].read(it->second.entry);
}

std::vector<std::string> MountSet::paths() const {
    std::vector<std::string> out;
    out.reserve(map_.size());
    for (const auto& [k, w] : map_) out.push_back(w.entry.path);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> MountSet::list(const std::string& prefix) const {
    namespace fs = std::filesystem;
    std::string kp = key(prefix);
    if (!kp.empty() && kp.back() != '/') kp += '/';   // treat as a directory prefix
    std::unordered_map<std::string, std::string> out;  // key -> winning original path
    auto under = [&](const std::string& k) { return kp.empty() || k.compare(0, kp.size(), kp) == 0; };
    // Archive winners first...
    for (const auto& [k, w] : map_)
        if (under(k)) out[k] = w.entry.path;
    // ...then loose files override (same precedence as read()).
    if (cfg_.includeLoose) {
        fs::path base = kp.empty() ? dir_ : dir_ / prefix;
        std::error_code ec;
        if (fs::is_directory(base, ec))
            for (auto it = fs::recursive_directory_iterator(base, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                std::string rel = fs::relative(it->path(), dir_, ec).generic_string();
                if (rel.empty()) continue;
                if (cfg_.keep && !cfg_.keep(rel)) continue;
                out[key(rel)] = rel;
            }
    }
    std::vector<std::string> paths;
    paths.reserve(out.size());
    for (const auto& [k, p] : out) paths.push_back(p);
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string MountSet::sourceOf(const std::string& path) const {
    if (cfg_.includeLoose) {
        std::filesystem::path loose = dir_ / path;
        if (std::filesystem::is_regular_file(loose)) return loose.string() + " (loose)";
    }
    auto it = map_.find(key(path));
    if (it == map_.end()) return "<absent>";
    return archiveFiles_[size_t(it->second.archive)].filename().string() +
           "!" + it->second.entry.path;
}

// ---- Vfs: layered runtime read-path over a retail install root -------------

void Vfs::addLayer(MountSet ms, const std::string& prefix) {
    std::string p = MountSet::key(prefix);
    if (!p.empty() && p.back() != '/') p += '/';
    layers_.push_back({std::move(ms), std::move(p)});
}

// Try each layer highest-precedence-first; if the requested path is under a
// layer's virtual prefix, strip the prefix and delegate to that MountSet.
std::optional<std::vector<uint8_t>> Vfs::tryRead(const std::string& path) const {
    std::string kp = MountSet::key(path);
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        if (!it->prefix.empty() && kp.compare(0, it->prefix.size(), it->prefix) != 0) continue;
        std::string sub = path.substr(it->prefix.size());
        if (it->ms.has(sub)) return it->ms.read(sub);
    }
    return std::nullopt;
}

std::vector<uint8_t> Vfs::read(const std::string& path) const {
    if (auto b = tryRead(path)) return std::move(*b);
    throw std::runtime_error("not in data set: " + path);
}

bool Vfs::has(const std::string& path) const {
    std::string kp = MountSet::key(path);
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        if (!it->prefix.empty() && kp.compare(0, it->prefix.size(), it->prefix) != 0) continue;
        if (it->ms.has(path.substr(it->prefix.size()))) return true;
    }
    return false;
}

std::vector<std::string> Vfs::list(const std::string& prefix) const {
    std::string kp = MountSet::key(prefix);
    std::unordered_map<std::string, std::string> out;   // key -> winning original path
    // Lowest precedence first, so higher layers overwrite the reported source.
    for (const auto& L : layers_) {
        // The virtual prefix each of this layer's paths carries.
        if (L.prefix.empty()) {
            for (const auto& p : L.ms.list(prefix))
                out[MountSet::key(p)] = p;
        } else if (kp.size() <= L.prefix.size()) {
            // Requested prefix is an ancestor of (or equals) the layer prefix:
            // list the whole layer, re-prefix, keep those under the request.
            if (L.prefix.compare(0, kp.size(), kp) != 0) continue;
            for (const auto& p : L.ms.list("")) {
                std::string full = L.prefix + p;
                out[MountSet::key(full)] = full;
            }
        } else {
            // Requested prefix reaches into the layer: strip the layer prefix.
            if (kp.compare(0, L.prefix.size(), L.prefix) != 0) continue;
            std::string sub = prefix.substr(L.prefix.size());
            for (const auto& p : L.ms.list(sub))
                out[MountSet::key(L.prefix + p)] = L.prefix + p;
        }
    }
    std::vector<std::string> paths;
    paths.reserve(out.size());
    for (const auto& [k, p] : out) paths.push_back(p);
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::string Vfs::sourceOf(const std::string& path) const {
    std::string kp = MountSet::key(path);
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
        if (!it->prefix.empty() && kp.compare(0, it->prefix.size(), it->prefix) != 0) continue;
        std::string sub = path.substr(it->prefix.size());
        if (it->ms.has(sub)) return it->ms.sourceOf(sub);
    }
    return "<absent>";
}

// ---- gameplay classification + retail-root mounting ------------------------

bool affectsGameplay(const std::string& path) {
    std::string k = MountSet::key(path);
    auto ext = [&](const char* e) {
        size_t n = std::strlen(e);
        return k.size() >= n && k.compare(k.size() - n, n, e) == 0;
    };
    // Files our deterministic sim consumes: unit stats, weapon/side/game data,
    // build lists, map geometry + scenario data. Everything else (art, models,
    // animation scripts, sound, music, fonts, gui) is cosmetic.
    if (ext(".fbi") || ext(".tnt") || ext(".ota") || ext(".crt")) return true;
    if (ext(".tdf")) {
        // soundclasses maps unit sound events -> cosmetic; all other .tdf
        // (weapons, sidedata, moveinfo, gods, explosions, build menus) is sim data.
        return k.find("soundclass") == std::string::npos;
    }
    // canbuild build-tree files carry no extension in a subdir; key on the path.
    if (k.find("canbuild") != std::string::npos) return true;
    return false;
}

Vfs mountRetailRoot(const std::filesystem::path& root, OverridePolicy overrides) {
    namespace fs = std::filesystem;
    Vfs vfs;
    // Case-fold the retail subdir names (they ship capitalised: Maps/ Music/
    // Movies/), so this resolves on a case-sensitive filesystem too.
    auto findSub = [&](const char* want) -> fs::path {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(root, ec)) {
            if (ec) break;
            if (!e.is_directory(ec)) continue;
            std::string n = e.path().filename().string();
            std::string l = n;
            for (char& c : l) c = char(std::tolower(static_cast<unsigned char>(c)));
            std::string w = want;
            for (char& c : w) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (l == w) return e.path();
        }
        return {};
    };

    // Lowest precedence: loose music tracks, mapped under music/.
    if (fs::path music = findSub("Music"); !music.empty())
        vfs.addLayer(MountSet(music, MountConfig{true, {}}), "music/");
    // The base game + expansions: ONLY the *.hpi archives in the root (no loose
    // files), layered by the retail newest-entry-date rule. maps.hpi and
    // terrain.hpi ride in here too (Maps/*.tnt, terrain/*.jpg).
    vfs.addLayer(MountSet(root, MountConfig{false, {".hpi"}}));
    // Single-map .kmp archives (each an HPI -> kmap/<name>.*) plus any loose maps.
    if (fs::path maps = findSub("Maps"); !maps.empty())
        vfs.addLayer(MountSet(maps, MountConfig{true, {".kmp", ".hpi", ".ufo"}}));
    // Highest precedence: user overrides (loose files OR archives), filtered by
    // the multiplayer policy. Cosmetic drops any gameplay-affecting override so
    // it cannot diverge between peers; Full mounts everything.
    if (overrides != OverridePolicy::None) {
        if (fs::path ov = findSub("overrides"); !ov.empty()) {
            MountConfig cfg{true, {".hpi", ".ufo", ".kmp"}, {}};
            if (overrides == OverridePolicy::Cosmetic)
                cfg.keep = [](const std::string& p) { return !affectsGameplay(p); };
            vfs.addLayer(MountSet(ov, std::move(cfg)), "");
        }
    }
    return vfs;
}

std::vector<std::pair<std::string, std::string>> listMaps(const Vfs& vfs) {
    std::unordered_map<std::string, std::string> byName;   // lower(name) -> path
    for (const char* ns : {"Maps", "kmap"})
        for (const std::string& p : vfs.list(ns)) {
            std::filesystem::path fp(p);
            std::string ext = fp.extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".tnt") continue;
            std::string name = fp.stem().string();
            std::string lname = name;
            for (char& c : lname) c = char(std::tolower(static_cast<unsigned char>(c)));
            byName.emplace(lname, p);   // first namespace (Maps) wins a name tie
        }
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(byName.size());
    for (const auto& [ln, path] : byName)
        out.push_back({std::filesystem::path(path).stem().string(), path});
    std::sort(out.begin(), out.end());
    return out;
}

std::string findMap(const Vfs& vfs, const std::string& name) {
    std::string want = name;
    // Accept either a bare name or a name with a .tnt suffix.
    if (want.size() > 4) {
        std::string tail = want.substr(want.size() - 4);
        for (char& c : tail) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (tail == ".tnt") want = want.substr(0, want.size() - 4);
    }
    for (char& c : want) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (const char* ns : {"Maps", "kmap"})
        for (const std::string& p : vfs.list(ns)) {
            std::filesystem::path fp(p);
            std::string ext = fp.extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".tnt") continue;
            std::string stem = fp.stem().string();
            for (char& c : stem) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (stem == want) return p;
        }
    return {};
}

} // namespace tak::hpi
