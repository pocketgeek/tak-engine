// Our AI levels' built-in build profiles and their per-level game-mod files
// (ai/builtinprofile.h).
//
//   ai_profile_test           the tables, the line format, and which mod layers may
//                             supply a level's file (scratch mod folders, no data)
//   ai_profile_test <data>    also: the built-in Normal table still equals the
//                             retail default profile (the base install's winning
//                             ai/default.txt) the levels were tuned on; and the
//                             Classic AI's profile file travels in a map's package
//                             when the map's own archive carries it, but a level's
//                             ai/n_ai.txt there does not
#include "ai/ai.h"
#include "ai/builtinprofile.h"
#include "ai/classicprofile.h"
#include "hpi/hpi.h"
#include "net/crypto.h"
#include "net/mappackage.h"
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
        {ai::Difficulty::Passive, "ai/d_ai.txt"}, {ai::Difficulty::Easy, "ai/e_ai.txt"},
        {ai::Difficulty::Normal, "ai/n_ai.txt"}, {ai::Difficulty::Hard, "ai/h_ai.txt"},
        {ai::Difficulty::Absurd, "ai/a_ai.txt"}, {ai::Difficulty::AbsurdlyDefensive, "ai/ad_ai.txt"}};
    bool ok = true;
    for (const auto& [d, file] : names) ok = ok && ai::levelProfileFile(d) && std::string(ai::levelProfileFile(d)) == file;
    check(ok, "each level names its own file (ai/d/e/n/h/a/ad_ai.txt)");

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
    // A loose file in a game pack's ai/ folder, and ai/ inside an archive in another.
    put(root / "mods/game/my_ai/AI/N_AI.TXT", normalFile);
    archive(root / "mods/game/archived/ai.hpi", {{"ai/n_ai.txt", "limit arafly 0\n"}, {"ai/h_ai.txt", "limit arafly 1\n"}});
    // Not a level file: at a pack's root (loose or an archive's), or in another folder.
    put(root / "mods/game/rooted/n_ai.txt", "limit arabow 0\n");
    archive(root / "mods/game/rooted/root.hpi", {{"h_ai.txt", "limit arabow 0\n"}});
    put(root / "mods/game/nested/sub/n_ai.txt", "limit arabow 0\n");
    put(root / "mods/game/nested/sub/ai/n_ai.txt", "limit arabow 0\n");
    // Cosmetic packs and the always-on cosmetic archives can never supply one.
    put(root / "mods/cosmetic/skin/ai/n_ai.txt", "limit arabow 0\n");
    put(root / "mods/cosmetic/skin/sounds/x.wav", "x");
    archive(root / "mods/cosmetic/always.hpi", {{"ai/n_ai.txt", "limit arabow 0\n"}, {"sounds/y.wav", "y"}});
    // The base install's own ai/ folder (in a root archive) is not a game mod.
    archive(root / "data.hpi", {{"ai/n_ai.txt", "limit arabow 0\n"}, {"ai/h_ai.txt", "limit arabow 0\n"},
                                {"gamedata/x.tdf", "x"}});
    const auto& builtin = ai::builtinProfile(ai::Difficulty::Normal);
    try {
        const auto loose = hpi::modFiles(root, hpi::ModKind::Game, {"my_ai"});
        check(loose.count("ai/n_ai.txt") == 1, "a game pack's loose ai/n_ai.txt is in its files (case-insensitive name)");
        std::string source;
        const auto normal = ai::levelProfile(ai::Difficulty::Normal, &loose, &source);
        check(source == "ai/n_ai.txt" && normal.limit.at("arabow") == 0 && normal.weight.at("arabow") == 2 &&
              normal.weight.at("modunit") == 7, "ai/n_ai.txt adjusts the Normal profile");
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
        check(othersUnchanged, "ai/n_ai.txt leaves every other level (Easy included) on its built-in table");

        const auto archived = hpi::modFiles(root, hpi::ModKind::Game, {"archived"});
        const auto viaArchive = ai::levelProfile(ai::Difficulty::Normal, &archived);
        check(viaArchive.limit.at("arafly") == 0 && viaArchive.limit.at("arabow") == builtin.limit.at("arabow"),
              "an ai/n_ai.txt inside an archive in a game pack adjusts Normal");
        check(ai::levelProfile(ai::Difficulty::Hard, &archived).limit.at("arafly") == 1 &&
              same(ai::levelProfile(ai::Difficulty::Easy, &archived), ai::builtinProfile(ai::Difficulty::Easy)),
              "the same archive's ai/h_ai.txt adjusts Hard only");

        const auto rooted = hpi::modFiles(root, hpi::ModKind::Game, {"rooted"});
        check(rooted.count("n_ai.txt") && rooted.count("h_ai.txt") &&
              same(ai::levelProfile(ai::Difficulty::Normal, &rooted), builtin) &&
              same(ai::levelProfile(ai::Difficulty::Hard, &rooted), ai::builtinProfile(ai::Difficulty::Hard)),
              "a root n_ai.txt / h_ai.txt (loose or an archive's) is not read");
        const auto nested = hpi::modFiles(root, hpi::ModKind::Game, {"nested"});
        check(same(ai::levelProfile(ai::Difficulty::Normal, &nested), builtin),
              "n_ai.txt in another folder (sub/, sub/ai/) is not a level file");

        const auto cosmetic = hpi::modFiles(root, hpi::ModKind::Cosmetic, {"skin"});
        const auto always = hpi::alwaysOnModFiles(root);
        check(cosmetic.count("sounds/x.wav") && !cosmetic.count("ai/n_ai.txt") && always.count("sounds/y.wav") &&
              !always.count("ai/n_ai.txt"), "cosmetic packs and always-on archives cannot carry ai/n_ai.txt");

        // The real transfer codec: the host's package, decoded as the server receives it.
        const auto package = net::mods::build(root, {"my_ai", "archived"});
        const auto received = net::mods::decode(package->bytes, package->digest);
        const auto served = ai::levelProfile(ai::Difficulty::Normal, received->files.get());
        // Packs layer alphabetically (later wins): my_ai's file replaces archived's.
        check(received->files->count("ai/n_ai.txt") && received->files->count("ai/h_ai.txt") && same(served, normal),
              "the game-mod package carries ai/*_ai.txt to the server (later pack wins)");
        // The base install's ai/ folder: in the played data, never in the package.
        const auto play = hpi::mountPlayVfs(root, {}, {"skin"});
        check(play.has("ai/n_ai.txt") && play.has("ai/h_ai.txt") && play.has("sounds/x.wav"),
              "the played data holds the base install's ai/n_ai.txt and ai/h_ai.txt");
        const auto none = net::mods::build(root, {"rooted"});
        const auto noneReceived = net::mods::decode(none->bytes, none->digest);
        check(!received->files->count("gamedata/x.tdf") && !noneReceived->files->count("ai/n_ai.txt") &&
              !noneReceived->files->count("ai/h_ai.txt") &&
              same(ai::levelProfile(ai::Difficulty::Normal, noneReceived->files.get()), builtin) &&
              same(ai::levelProfile(ai::Difficulty::Hard, noneReceived->files.get()), ai::builtinProfile(ai::Difficulty::Hard)),
              "the base install's ai/<x>_ai.txt never reaches the package, so no level reads it");
        // No game mods (the base install, a map package): only the built-in table.
        check(same(ai::levelProfile(ai::Difficulty::Normal, nullptr), builtin), "no game-mod files: the built-in table");
    } catch (const std::exception& e) {
        check(false, std::string("mod layers threw: ") + e.what());
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

static bool isHpi(const std::filesystem::path& p) { return hpi::MountSet::key(p.extension().string()) == ".hpi"; }

static void retailDefault(const char* data) {
    const auto vfs = hpi::mountRetailRoot(data);
    check(vfs.has("ai/default.txt"), "the install has ai/default.txt");
    const auto file = ai::loadProfile(vfs);
    const auto& builtin = ai::builtinProfile(ai::Difficulty::Normal);
    check(same(file, builtin), "the built-in Normal table equals the base install's ai/default.txt (first difference: " +
                               firstDifference(file, builtin) + ")");
}

// A single-map archive under Maps/ that carries its own ai/default.txt: the map's
// package carries the file (and only that map's package does), the server's view of
// the room (base + package) gives it to the Classic AI, and a package naming an AI
// file the map's .ota does not choose is refused.
static void mapPackages(const char* data) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("tak-ai-map-" + crypto::toHex(crypto::randomVec(8)));
    try {
        fs::create_directories(root);
        for (const auto& e : fs::directory_iterator(data))
            if (e.is_regular_file() && isHpi(e.path())) fs::create_symlink(fs::absolute(e.path()), root / e.path().filename());
        const auto base = hpi::mountRetailRoot(data);
        const std::string own = "// the map's own profile\nlimit tarcage 0\n";
        // The map archive also carries an ai/n_ai.txt: not a Classic file, so it stays home.
        std::vector<std::pair<std::string, std::string>> entries{{"ai/default.txt", own},
                                                                 {"ai/n_ai.txt", "limit tarcage 0\n"}};
        for (const char* ext : {".tnt", ".ota", ".crt", ".txt"})
            if (auto b = base.tryRead(std::string("maps/Inner Circle") + ext))
                entries.push_back({std::string("kmap/AI Circle") + ext, std::string(b->begin(), b->end())});
        archive(root / "Maps/AI Circle.kmp", entries);
        const auto vfs = hpi::mountRetailRoot(root);
        const auto pkg = net::maps::build(vfs, "AI Circle");
        check(pkg->files->count("ai/default.txt") &&
              std::string(pkg->files->at("ai/default.txt").begin(), pkg->files->at("ai/default.txt").end()) == own,
              "a map archive's own ai/default.txt travels in its package");
        check(!pkg->files->count("ai/n_ai.txt"), "a map archive's ai/n_ai.txt does not (map packages cannot adjust a level)");
        const auto stock = net::maps::build(vfs, "Inner Circle");
        bool noAi = true;
        for (const auto& [name, bytes] : *stock->files) noAi = noAi && !name.starts_with("ai/");
        check(noAi, "another map's package carries no AI file (nor the base's)");
        const auto received = net::maps::decode(pkg->bytes, pkg->digest);
        hpi::Vfs room(&base);
        room.setMapFiles(received->files);
        const auto script = ai::classicProfileScript(room, ai::classicMapAiProfile(room, received->mapPath));
        check(hpi::MountSet::key(script.path) == "ai/default.txt" && script.text == own, "the room's view gives Classic the map's file");
        const hpi::Vfs noMaps(&vfs, true);
        const auto plain = ai::classicProfileScript(noMaps, "");
        check(!plain.text.empty() && plain.text != own, "a view without the map package skips Maps/ archives' AI files");
        // The same package with an AI file the .ota does not choose.
        auto files = *received->files;
        files["ai/other.txt"] = {'x'};
        net::Writer w; w.u32(2); w.str(received->mapPath); w.u32(uint32_t(files.size()));
        for (const auto& [name, bytes] : files) { w.str(name); w.u32(uint32_t(bytes.size())); w.b.insert(w.b.end(), bytes.begin(), bytes.end()); }
        bool refused = false;
        try { net::maps::decode(w.b, ""); } catch (const std::exception&) { refused = true; }
        check(refused, "a package carrying an AI file its map does not name is refused");
    } catch (const std::exception& e) {
        check(false, std::string("map packages threw: ") + e.what());
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

int main(int argc, char** argv) {
    tables();
    modLayers();
    if (argc > 1) { retailDefault(argv[1]); mapPackages(argv[1]); }
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
