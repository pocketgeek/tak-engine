// Our AI levels' built-in build profiles and their per-level game-mod files
// (ai/builtinprofile.h).
//
//   ai_profile_test           the tables, the line format, and which mod layers may
//                             supply a level's file (scratch mod folders, no data)
//   ai_profile_test <data>    also: the built-in Normal table still equals the
//                             retail default profile (the base install's winning
//                             ai/default.txt) the levels were tuned on
#include "ai/ai.h"
#include "ai/builtinprofile.h"
#include "hpi/hpi.h"
#include "net/crypto.h"
#include "net/modpackage.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace tak;

static int failures = 0;
static void check(bool ok, const std::string& label) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

static bool same(const ai::Profile& a, const ai::Profile& b) { return a.weight == b.weight && a.limit == b.limit; }

// The first difference between two profiles, for a failing comparison's log.
static std::string firstDifference(const ai::Profile& want, const ai::Profile& got) {
    for (const auto* kind : {"weight", "limit"}) {
        const auto& w = std::string(kind) == "weight" ? want.weight : want.limit;
        const auto& g = std::string(kind) == "weight" ? got.weight : got.limit;
        for (const auto& [unit, v] : w) {
            const auto it = g.find(unit);
            if (it == g.end()) return std::string(kind) + " " + unit + ": missing from the built-in table";
            if (it->second != v) return std::string(kind) + " " + unit + ": file " + std::to_string(v) +
                                        ", built-in " + std::to_string(it->second);
        }
        for (const auto& [unit, v] : g)
            if (!w.count(unit)) return std::string(kind) + " " + unit + ": built-in only";
    }
    return "none";
}

static void tables() {
    const auto& normal = ai::builtinProfile(ai::Difficulty::Normal);
    check(normal.weight.size() == 146 && normal.limit.size() == 146, "the built-in table holds 146 units, weight + limit each");
    check(normal.weight.at("aradrag") == 25 && normal.limit.at("aradrag") == 1, "an entry reads back (aradrag 25 / 1)");
    // Behaviour must not change yet: every level still plays the profile it was tuned on.
    for (uint8_t level = 0; level < ai::kAiLevels; ++level) {
        const auto d = ai::Difficulty(level);
        if (d == ai::Difficulty::Classic) continue;
        check(same(ai::builtinProfile(d), normal), "level " + std::to_string(level) + "'s table equals Normal's (no level diverges yet)");
    }
    const auto& classic = ai::builtinProfile(ai::Difficulty::Classic);
    check(classic.weight.empty() && classic.limit.empty() && !ai::levelProfileFile(ai::Difficulty::Classic),
          "Classic has no built-in table and no mod file");
    const std::pair<ai::Difficulty, const char*> names[] = {
        {ai::Difficulty::Passive, "d_ai.txt"}, {ai::Difficulty::Easy, "e_ai.txt"}, {ai::Difficulty::Normal, "n_ai.txt"},
        {ai::Difficulty::Hard, "h_ai.txt"}, {ai::Difficulty::Absurd, "a_ai.txt"}, {ai::Difficulty::AbsurdlyDefensive, "ad_ai.txt"}};
    bool ok = true;
    for (const auto& [d, file] : names) ok = ok && ai::levelProfileFile(d) && std::string(ai::levelProfileFile(d)) == file;
    check(ok, "each level names its own file (d/e/n/h/a/ad_ai.txt)");

    ai::Profile p;
    ai::applyProfileText("// comment\r\nweight AraBow 3\r\nlimit arabow 0\nlimit arabow 2 trailing\nplan x 1\n/weight arafly 9\nweight arafly\n", p);
    check(p.weight.size() == 1 && p.weight.at("arabow") == 3 && p.limit.size() == 1 && p.limit.at("arabow") == 2,
          "line format: lowercased units, CRLF, later line wins, comments/other keywords/short lines skipped");
}

static void put(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}
static void archive(const std::filesystem::path& file, std::vector<std::pair<std::string, std::string>> entries) {
    std::vector<hpi::PackFile> files;
    for (auto& [name, text] : entries) files.push_back({name, std::vector<uint8_t>(text.begin(), text.end())});
    const auto bytes = hpi::pack(files);
    put(file, std::string(bytes.begin(), bytes.end()));
}

static void modLayers() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("tak-ai-profiles-" + crypto::toHex(crypto::randomVec(8)));
    const std::string normalFile = "// a game mod's Normal profile\nlimit arabow 0\nweight AraBow 2\nweight modunit 7\n";
    // A loose file at a game pack's root, and one at the root of an archive in another.
    put(root / "mods/game/my_ai/N_AI.TXT", normalFile);
    archive(root / "mods/game/archived/ai.hpi", {{"n_ai.txt", "limit arafly 0\n"}, {"h_ai.txt", "limit arafly 1\n"}});
    // Not a level file: nested under a folder, and the old per-file ai/ path.
    put(root / "mods/game/nested/sub/n_ai.txt", "limit arabow 0\n");
    put(root / "mods/game/nested/ai/n_ai.txt", "limit arabow 0\n");
    // Cosmetic packs and the always-on cosmetic archives can never supply one.
    put(root / "mods/cosmetic/skin/n_ai.txt", "limit arabow 0\n");
    put(root / "mods/cosmetic/skin/sounds/x.wav", "x");
    archive(root / "mods/cosmetic/always.hpi", {{"n_ai.txt", "limit arabow 0\n"}, {"sounds/y.wav", "y"}});
    const auto& builtin = ai::builtinProfile(ai::Difficulty::Normal);
    try {
        const auto loose = hpi::modFiles(root, hpi::ModKind::Game, {"my_ai"});
        check(loose.count("n_ai.txt") == 1, "a game pack's loose root n_ai.txt is in its files (case-insensitive name)");
        std::string source;
        const auto normal = ai::levelProfile(ai::Difficulty::Normal, &loose, &source);
        check(source == "n_ai.txt" && normal.limit.at("arabow") == 0 && normal.weight.at("arabow") == 2 &&
              normal.weight.at("modunit") == 7, "n_ai.txt adjusts the Normal profile");
        ai::Profile expect = builtin;
        expect.limit["arabow"] = 0; expect.weight["arabow"] = 2; expect.weight["modunit"] = 7;
        check(same(normal, expect), "units the file does not name keep their built-in values");
        bool othersUnchanged = true;
        for (uint8_t level = 0; level < ai::kAiLevels; ++level) {
            const auto d = ai::Difficulty(level);
            if (d == ai::Difficulty::Normal) continue;
            std::string s;
            othersUnchanged = othersUnchanged && same(ai::levelProfile(d, &loose, &s), ai::builtinProfile(d)) && s.empty();
        }
        check(othersUnchanged, "n_ai.txt leaves every other level (Easy included) on its built-in table");

        const auto archived = hpi::modFiles(root, hpi::ModKind::Game, {"archived"});
        const auto viaArchive = ai::levelProfile(ai::Difficulty::Normal, &archived);
        check(viaArchive.limit.at("arafly") == 0 && viaArchive.limit.at("arabow") == builtin.limit.at("arabow"),
              "an n_ai.txt at an archive's root inside a game pack adjusts Normal");
        check(ai::levelProfile(ai::Difficulty::Hard, &archived).limit.at("arafly") == 1 &&
              same(ai::levelProfile(ai::Difficulty::Easy, &archived), ai::builtinProfile(ai::Difficulty::Easy)),
              "the same archive's h_ai.txt adjusts Hard only");

        const auto nested = hpi::modFiles(root, hpi::ModKind::Game, {"nested"});
        check(same(ai::levelProfile(ai::Difficulty::Normal, &nested), builtin), "n_ai.txt below a pack's root is not a level file");

        const auto cosmetic = hpi::modFiles(root, hpi::ModKind::Cosmetic, {"skin"});
        const auto always = hpi::alwaysOnModFiles(root);
        check(cosmetic.count("sounds/x.wav") && !cosmetic.count("n_ai.txt") && always.count("sounds/y.wav") && !always.count("n_ai.txt"),
              "cosmetic packs and always-on archives cannot carry n_ai.txt");
        const auto play = hpi::mountPlayVfs(root, {}, {"skin"});
        check(play.has("sounds/x.wav") && !play.has("n_ai.txt"), "a played VFS with a cosmetic pack has no n_ai.txt");

        // The real transfer codec: the host's package, decoded as the server receives it.
        const auto package = net::mods::build(root, {"my_ai", "archived"});
        const auto received = net::mods::decode(package->bytes, package->digest);
        const auto served = ai::levelProfile(ai::Difficulty::Normal, received->files.get());
        // Packs layer alphabetically (later wins): my_ai's file replaces archived's.
        check(received->files->count("n_ai.txt") && received->files->count("h_ai.txt") && same(served, normal),
              "the game-mod package carries the root files to the server (later pack wins)");
        // No game mods (the base install, a map package): only the built-in table.
        check(same(ai::levelProfile(ai::Difficulty::Normal, nullptr), builtin), "no game-mod files: the built-in table");
    } catch (const std::exception& e) {
        check(false, std::string("mod layers threw: ") + e.what());
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

static void retailDefault(const char* data) {
    const auto vfs = hpi::mountRetailRoot(data);
    check(vfs.has("ai/default.txt"), "the install has ai/default.txt");
    const auto file = ai::loadProfile(vfs);
    const auto& builtin = ai::builtinProfile(ai::Difficulty::Normal);
    check(same(file, builtin), "the built-in Normal table equals the base install's ai/default.txt (first difference: " +
                               firstDifference(file, builtin) + ")");
}

int main(int argc, char** argv) {
    tables();
    modLayers();
    if (argc > 1) retailDefault(argv[1]);
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
