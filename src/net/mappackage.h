#pragma once

#include "hpi/hpi.h"
#include "net/conn.h"
#include "net/crypto.h"
#include <memory>
#include "util/stoptoken.h"

namespace tak::net::maps {
constexpr size_t kMaxBytes = 256u << 20;
constexpr size_t kChunkBytes = 64u << 10;
struct Package {
    std::string mapPath, digest;
    std::shared_ptr<const hpi::Vfs::Files> files;
    std::vector<uint8_t> bytes;
};
// Canonical map geometry, scenario/start positions, and referenced terrain art.
// Feature definitions, their burn/death chains, sprites and palettes travel too.
// Unit definitions/scripts remain covered by the base gameplay-data agreement.
std::shared_ptr<Package> build(const hpi::Vfs& vfs, const std::string& mapId);
std::shared_ptr<Package> decode(std::vector<uint8_t> bytes, const std::string& digest, tak::StopToken stop = {});
// Validate a local single-map KMP using the same whitelist as network maps.
std::shared_ptr<Package> importSnapshot(const hpi::Vfs& base, const std::filesystem::path& path);
// Only explicit, structurally valid authored scenarios may start with one player.
// Network callers pass the verified package, never the host's map-name claim.
bool authoredScenario(const Package& package);
bool authoredScenario(const hpi::Vfs& vfs, const std::string& mapPath);
bool validDigest(const std::string& digest);
Writer offer(uint32_t room, const std::string& mapId, const Package& package);
struct Receiver {
    uint32_t room = 0, size = 0;
    std::string digest;
    std::vector<uint8_t> bytes;
    void begin(uint32_t roomId, uint32_t count, const std::string& hash);
    // Rejects reordered, duplicate, oversized, or unsolicited chunks.
    bool append(Reader& r);
    bool complete() const { return size && bytes.size() == size; }
};
struct Sender {
    uint32_t room = 0;
    size_t offset = 0;
    std::shared_ptr<const Package> package;
    void pump(Conn& conn, Msg kind=Msg::MapChunk);
};
// Content-addressed cache; never extract network paths into the filesystem.
void saveCache(const std::filesystem::path& root, const Package& package,uint64_t quota=0);
std::shared_ptr<Package> loadCache(const std::filesystem::path& root, const std::string& digest, size_t maxBytes=kMaxBytes, size_t expected=0, tak::StopToken stop = {});
// A reusable .kmp, including stock tile artwork and authored start positions.
// Called on game start, not while previewing or creating a room.
std::filesystem::path saveGenerated(const std::filesystem::path& root,
                                    const hpi::Vfs& vfs, const std::string& recipe,uint64_t quota=0);
}
