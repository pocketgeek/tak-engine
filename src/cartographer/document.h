#pragma once

#include "cartographer/units.h"
#include "hpi/hpi.h"
#include "tnt/ota.h"
#include "tnt/tnt.h"

#include <filesystem>
#include <atomic>
#include <set>

namespace cart {

// One serialization path for normal saves, recovery, playtests and loose export.
// The input document is never changed by saving it.
std::vector<tak::hpi::PackFile> documentFiles(
    const tak::tnt::Map&, tak::tnt::Scenario metadata,
    const tak::crt::Scenario&, const std::vector<PlacedUnit>&,
    const std::set<std::string>& useOnly, const std::string& name);

// Atomic single-file replacement, with the previous file retained as .bak.
// Loose exports stage every member before replacement and roll back failures;
// use a KMP for an atomic whole-document save.
bool writeDocumentFiles(const std::filesystem::path& directory,
                        const std::vector<tak::hpi::PackFile>&,
                        std::string& error, const std::atomic_bool* cancel = nullptr);
bool writeDocumentBundle(const std::filesystem::path&,
                         const std::vector<tak::hpi::PackFile>&,
                         std::string& error, const std::atomic_bool* cancel = nullptr);
bool validDocumentName(const std::string&);

} // namespace cart
