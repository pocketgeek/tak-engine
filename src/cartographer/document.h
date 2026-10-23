#pragma once

#include "cartographer/units.h"
#include "hpi/hpi.h"
#include "tnt/ota.h"
#include "tnt/tnt.h"

#include <filesystem>
#include <atomic>
#include <map>
#include <set>

namespace cart {

// The map's AI profiles: every root ai/<name>.txt the document carries, keyed by
// MountSet::key, as raw bytes. Kept losslessly -- the bytes are the truth, files
// outside the map-package whitelist included (they stay in the .kmp but never travel).
using AiFiles = std::map<std::string, std::vector<uint8_t>>;
// A root ai/<name>.txt path: no further '/', a name usable as a file name.
bool aiFileKey(const std::string& path);
AiFiles aiFilesFromArchive(const tak::hpi::Archive&);
// <mapFolder>/ai/*.txt beside a loose .tnt (the folder name in any case).
AiFiles aiFilesFromFolder(const std::filesystem::path& mapFolder);
// An installed map opened by name: the root ai/<name>.txt files of its own archive
// (Vfs::readFromMapArchive), not another installed map's that wins the same path.
// None for a loose map or one in the base install.
AiFiles aiFilesFromMapArchive(const tak::hpi::Vfs&, const std::string& mapPath);
// Moves the root ai/ entries out of a recovery snapshot's files.
AiFiles liftAiFiles(tak::hpi::Vfs::Files&);

struct DocumentFiles {
    std::vector<tak::hpi::PackFile> members;      // kmap/<stem>/ in a .kmp; beside the .tnt loose
    std::vector<tak::hpi::PackFile> rootMembers;  // ai/<x>.txt: the .kmp root; <dir>/ai/ loose
};
// One serialization path for normal saves, recovery, playtests and loose export.
// The input document is never changed by saving it. Empty AI files are dropped.
DocumentFiles documentFiles(
    const tak::tnt::Map&, tak::tnt::Scenario metadata,
    const tak::crt::Scenario&, const std::vector<PlacedUnit>&,
    const std::set<std::string>& useOnly, const AiFiles&, const std::string& name);

// Atomic single-file replacement, with the previous file retained as .bak.
// Loose exports stage every member before replacement and roll back failures;
// use a KMP for an atomic whole-document save.
bool writeDocumentFiles(const std::filesystem::path& directory,
                        const std::vector<tak::hpi::PackFile>&,
                        std::string& error, const std::atomic_bool* cancel = nullptr);
bool writeDocumentBundle(const std::filesystem::path&, const DocumentFiles&,
                         std::string& error, const std::atomic_bool* cancel = nullptr);
// Loose export: members into `directory`, AI files into its ai/ folder. Refused when
// AI files would land in (or under) the install's Maps folder, where a loose
// Maps/ai/<x>.txt is read for every map and never counts as this map's own.
bool writeDocumentLoose(const std::filesystem::path& directory, const DocumentFiles&,
                        const std::filesystem::path& installMaps,
                        std::string& error, const std::atomic_bool* cancel = nullptr);
// The data root's Maps folder (any case), or empty.
std::filesystem::path installMapsFolder(const std::filesystem::path& dataRoot);
bool insideFolder(const std::filesystem::path& path, const std::filesystem::path& folder);
bool validDocumentName(const std::string&);

} // namespace cart
