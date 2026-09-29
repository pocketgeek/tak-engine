#pragma once
#include "hpi/hpi.h"
#include <filesystem>
#include <optional>
#include <string>

namespace cart {
// Owns the temporary snapshot while the external game runs. Polling never waits
// for the game; closing the editor leaves an already-running test undisturbed.
class Playtest {
public:
    ~Playtest();
    Playtest() = default;
    Playtest(const Playtest&) = delete;
    Playtest& operator=(const Playtest&) = delete;
    static std::filesystem::path clientPath(const std::filesystem::path& executableDirectory);
    bool start(const std::filesystem::path& client, const std::filesystem::path& data,
               const std::vector<tak::hpi::PackFile>& files, std::string& error);
    std::optional<int> poll();
    bool running() const { return process_ != 0; }
    const std::filesystem::path& snapshot() const { return snapshot_; }
private:
    intptr_t process_ = 0;
    std::filesystem::path directory_, snapshot_;
    void cleanup();
};
}
