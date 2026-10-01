#pragma once
#include "server/crusades/store.h"
#include "net/crusades.h"
#include <filesystem>

namespace tak::srv::crusades {
// Read-only artifact access. Identity comes from the verified database record,
// never from a client path. Header/size checks are bounded; the downloader must
// verify the complete SHA-256 before publishing or playing the bytes.
class ReplayFiles {
public:
    explicit ReplayFiles(std::filesystem::path directory) : directory_(std::move(directory)) {}
    std::optional<net::crusades::ReplayMetadata> inspect(const IssuedBattle&, const VerifiedMatchResult&) const;
    std::optional<net::crusades::ReplayChunk> read(const IssuedBattle&, const VerifiedMatchResult&,
        uint32_t requestId, uint64_t offset, uint32_t limit) const;
private:
    std::filesystem::path directory_;
};
}
