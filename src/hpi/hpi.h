#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace tak::hpi {

// TAK ships its data in HPI version-2 archives (header version 0x00020000),
// a revision of Total Annihilation's HPI format. Layout, verified against
// the GOG release and Joe D's HPIPack writer source:
//
//   HPIVERSION  { "HAPI", 0x00020000 }
//   HPIHEADER2  { dirBlock, dirSize, nameBlock, nameSize, dataStart, unused }
//   ...file data (per-file sequences of SQSH chunks, or raw)...
//   name block  (SQSH chunk or raw: null-terminated strings)
//   dir block   (SQSH chunk or raw: tree of 20-byte dir / 24-byte file entries)
//
// SQSH chunk: 19-byte packed header { "SQSH", u8 ver, u8 method(2=zlib),
// u8 encrypted, u32 compSize, u32 decompSize, u32 checksum } + payload.
// Encrypted payload bytes are decoded with b[i] = (b[i] - i) ^ i.

struct HeaderInfo {
    std::string magic;   // expected "HAPI"
    uint32_t version = 0;
    uint32_t dirBlock = 0, dirSize = 0;
    uint32_t nameBlock = 0, nameSize = 0;
    uint32_t dataStart = 0;
    uint64_t fileSize = 0;
};

struct Entry {
    std::string path;        // archive-internal path, '/'-separated
    bool isDirectory = false;
    uint32_t start = 0;      // file data offset in archive
    uint32_t decompressedSize = 0;
    uint32_t compressedSize = 0;  // 0 = stored uncompressed
    uint32_t date = 0;            // time_t
};

class Archive {
public:
    explicit Archive(const std::filesystem::path& file);

    const HeaderInfo& header() const { return header_; }
    const std::vector<Entry>& entries() const { return entries_; }

    // Extract one file entry's contents.
    std::vector<uint8_t> read(const Entry& entry) const;
    // Find a file entry by internal path (case-insensitive). nullptr if absent.
    const Entry* find(const std::string& path) const;

private:
    std::filesystem::path file_;
    HeaderInfo header_;
    std::vector<Entry> entries_;
};

HeaderInfo inspect(const std::filesystem::path& archive);
std::string describe(const HeaderInfo& info);

// A directory's worth of archives, layered exactly as the retail engine does
// (verified by disassembling KINGDOMS.icd's file-open path, 0x53aff0):
//
//   1. A loose file on disk (dir/<path>) overrides everything -- the engine
//      fopen()s the plain path first and only falls back to archives.
//   2. Otherwise all *.hpi then *.ufo in the directory are searched, and when
//      the same internal path exists in several, the one whose directory entry
//      has the NEWEST date (file entry +0x10) wins; a tie keeps the earlier
//      mount (*.hpi before *.ufo, alphabetical). This is why each retail patch
//      shipped a new HPI that simply superseded older copies of a file.
//
// So dropping newer patch archives (or a loose override file) into a game
// directory Just Works, same as the original.
class MountSet {
public:
    explicit MountSet(const std::filesystem::path& dir);

    // Is `path` resolvable (loose file or in some archive)?
    bool has(const std::string& path) const;
    // Read a file by internal path (case-insensitive, '/' or '\\'). Throws if
    // absent. A loose file on disk wins; otherwise the newest archive copy.
    std::vector<uint8_t> read(const std::string& path) const;
    // Every archive-provided path after conflict resolution (loose-only files
    // are not listed -- they're already on disk).
    std::vector<std::string> paths() const;
    // Human-readable winning source of `path` (for tooling/debug).
    std::string sourceOf(const std::string& path) const;

    const std::vector<std::filesystem::path>& archiveFiles() const { return archiveFiles_; }

private:
    struct Win { int archive; Entry entry; };   // archive index into archives_
    static std::string key(std::string p);       // lowercased, '/'-separated

    std::filesystem::path dir_;
    std::vector<Archive> archives_;
    std::vector<std::filesystem::path> archiveFiles_;
    std::unordered_map<std::string, Win> map_;   // winning archive entry per path
};

} // namespace tak::hpi
