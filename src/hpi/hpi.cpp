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

std::vector<uint8_t> Archive::read(const Entry& entry) const {
    if (entry.isDirectory) throw std::runtime_error(entry.path + " is a directory");
    auto data = readFile(file_);

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

MountSet::MountSet(const std::filesystem::path& dir) : dir_(dir) {
    namespace fs = std::filesystem;
    // Collect *.hpi then *.ufo, each group alphabetical -- the order the retail
    // FindFirstFile("*.HPI") / ("*.UFO") scan yields and mounts them in.
    std::vector<fs::path> hpis, ufos;
    if (fs::is_directory(dir_))
        for (const auto& e : fs::directory_iterator(dir_)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".hpi") hpis.push_back(e.path());
            else if (ext == ".ufo") ufos.push_back(e.path());
        }
    // Case-insensitive filename sort, like the retail FindFirstFile scan (so a
    // tie in file date breaks the same way it did originally).
    auto ci = [](const fs::path& a, const fs::path& b) {
        std::string x = a.filename().string(), y = b.filename().string();
        for (char& c : x) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : y) c = char(std::tolower(static_cast<unsigned char>(c)));
        return x < y;
    };
    std::sort(hpis.begin(), hpis.end(), ci);
    std::sort(ufos.begin(), ufos.end(), ci);
    archiveFiles_ = hpis;
    archiveFiles_.insert(archiveFiles_.end(), ufos.begin(), ufos.end());

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
            std::string k = key(e.path);
            auto it = map_.find(k);
            if (it == map_.end() || e.date > it->second.entry.date)
                map_[k] = Win{idx, e};
        }
    }
}

bool MountSet::has(const std::string& path) const {
    std::filesystem::path loose = dir_ / path;
    if (std::filesystem::is_regular_file(loose)) return true;
    return map_.count(key(path)) != 0;
}

std::vector<uint8_t> MountSet::read(const std::string& path) const {
    // 1. A loose file on disk overrides archives (the engine fopen()s first).
    std::filesystem::path loose = dir_ / path;
    if (std::filesystem::is_regular_file(loose)) {
        std::ifstream f(loose, std::ios::binary);
        return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
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

std::string MountSet::sourceOf(const std::string& path) const {
    std::filesystem::path loose = dir_ / path;
    if (std::filesystem::is_regular_file(loose)) return loose.string() + " (loose)";
    auto it = map_.find(key(path));
    if (it == map_.end()) return "<absent>";
    return archiveFiles_[size_t(it->second.archive)].filename().string() +
           "!" + it->second.entry.path;
}

} // namespace tak::hpi
