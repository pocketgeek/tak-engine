#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
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

} // namespace tak::hpi
