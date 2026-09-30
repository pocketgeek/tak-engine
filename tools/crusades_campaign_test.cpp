// Synthetic, independently authored campaigns: no retail assets or guessed graph.
#include "server/crusades/campaign.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace c = tak::srv::crusades;
namespace {
int checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Action> void rejects(Action action, const std::string& label) {
    ++checks;
    try { action(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("accepted invalid campaign: " + label);
}
const std::string minimal = "campaign 1 \"synthetic\" \"Synthetic Campaign\"\nterritory 1 \"One\"\n";
const std::string graph = R"(campaign 1 "synthetic" "Synthetic Campaign"
territory 1 "One"
territory 2 "Two"
territory 3 "Three"
map 1 "maps/one.ota"
map 2 "maps/two.ota"
map 3 "maps/three.ota"
neighbors 1 2 3
neighbors 2 1
neighbors 3 1
native 1 "Aramon"
terrain 1 "forest"
)";

void parserValidation() {
    // Each case is deliberately a complete invalid document, not a parser mirror.
    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"empty document", ""},
        {"no territories", "campaign 1 \"synthetic\" \"Name\"\n"},
        {"absent campaign", "territory 1 \"One\"\n"},
        {"unsupported version", "campaign 2 \"synthetic\" \"Name\"\nterritory 1 \"One\"\n"},
        {"duplicate campaign", minimal + "campaign 1 \"other\" \"Other\"\n"},
        {"duplicate territory ID", minimal + "territory 1 \"Other\"\n"},
        {"numeric alias duplicate ID", minimal + "territory 01 \"Other\"\n"},
        {"zero ID", minimal + "territory 0 \"Zero\"\n"},
        {"negative ID", minimal + "territory -1 \"Minus\"\n"},
        {"overflow ID", minimal + "territory 18446744073709551616 \"Huge\"\n"},
        {"fractional ID", minimal + "territory 1.5 \"Fraction\"\n"},
        {"unknown directive", minimal + "toughness 1 10\n"},
        {"unquoted name", "campaign 1 \"synthetic\" \"Name\"\nterritory 1 One\n"},
        {"unterminated quote", "campaign 1 \"synthetic\" \"Name\"\nterritory 1 \"One\n"},
        {"trailing token", minimal + "terrain 1 \"forest\" junk\n"},
        {"unknown map reference", minimal + "map 9 \"maps/one.ota\"\n"},
        {"unknown terrain reference", minimal + "terrain 9 \"forest\"\n"},
        {"unknown native reference", minimal + "native 9 \"Aramon\"\n"},
        {"unknown adjacency source", minimal + "neighbors 9\n"},
        {"unknown adjacency target", minimal + "neighbors 1 9\n"},
        {"self adjacency", minimal + "neighbors 1 1\n"},
        {"duplicate neighbor", minimal + "territory 2 \"Two\"\nneighbors 1 2 2\nneighbors 2 1\n"},
        {"asymmetric adjacency", minimal + "territory 2 \"Two\"\nneighbors 1 2\nneighbors 2\n"},
        {"unresolved reverse adjacency", minimal + "territory 2 \"Two\"\nneighbors 1 2\n"},
        {"duplicate map", minimal + "map 1 \"one\"\nmap 1 \"two\"\n"},
        {"duplicate native", minimal + "native 1 \"Aramon\"\nnative 1 \"Zhon\"\n"},
        {"duplicate terrain", minimal + "terrain 1 \"forest\"\nterrain 1 \"desert\"\n"},
        {"duplicate neighbors", minimal + "neighbors 1\nneighbors 1\n"},
    };
    for (const auto& [label, input] : invalid)
        rejects([&]{ (void)c::loadDefinitionText(input); }, label);
}
} // namespace

namespace {
void deterministicLoading() {
    const std::string reordered = R"(# Authored synthetic map, intentionally shuffled.
terrain 1 "forest"
neighbors 3 1
map 3 "maps/three.ota"
territory 3 "Three"
native 1 "Aramon"
neighbors 1 3 2
map 1 "maps/one.ota"
territory 2 "Two"
neighbors 2 1
map 2 "maps/two.ota"
territory 1 "One"
campaign 1 "synthetic" "Synthetic Campaign"
)";
    std::vector<std::string> mapCalls;
    c::DefinitionLoadOptions options;
    options.requireMaps = options.requireAdjacency = true;
    options.mapExists = [&](std::string_view name) {
        mapCalls.emplace_back(name);
        return name == "maps/one.ota" || name == "maps/two.ota" || name == "maps/three.ota";
    };
    const auto first = c::loadDefinitionText(graph, options);
    const auto firstCalls = mapCalls;
    mapCalls.clear();
    const auto second = c::loadDefinitionText(reordered, options);
    check(first.id() == second.id() && first.displayName() == second.displayName(), "campaign metadata deterministic");
    check(firstCalls == mapCalls, "map validation order independent of declaration order");
    check(first.territories().size() == second.territories().size(), "territory count deterministic");
    for (const auto& [id, a] : first.territories()) {
        const auto* b = second.find(id);
        check(b && a.id == b->id && a.displayName == b->displayName &&
            a.nativeFaction == b->nativeFaction && a.terrain == b->terrain &&
            a.mapIdentifier == b->mapIdentifier && a.neighbors == b->neighbors,
            "complete territory value independent of declaration order");
    }
    check(first.find(1)->neighbors == std::optional<std::vector<c::TerritoryId>>({{2,3}}), "neighbors sorted canonically");
    check(first.find(99) == nullptr, "missing territory lookup is explicit");
    const auto incomplete = c::loadDefinitionText(minimal);
    check(!incomplete.find(1)->neighbors && !incomplete.find(1)->mapIdentifier &&
          !incomplete.find(1)->nativeFaction && !incomplete.find(1)->terrain,
          "missing historical data is unknown, not invented");
    const auto isolated = c::loadDefinitionText(minimal + "neighbors 1\n");
    check(isolated.find(1)->neighbors && isolated.find(1)->neighbors->empty(), "known isolation distinct from unknown graph");
    const auto escaped = c::loadDefinitionText(R"(campaign 1 "escapes" "A \"quoted\" title"
territory 1 "C:\\map")");
    check(escaped.displayName() == "A \"quoted\" title" && escaped.find(1)->displayName == "C:\\map",
        "quoted escapes and final line without newline preserved");
    const auto international = c::loadDefinitionText("campaign 1 \"utf8\" \"Café 世界\"\nterritory 4294967295 \"Île\"\n");
    check(international.displayName() == "Café 世界" && international.find(UINT32_MAX)->displayName == "Île", "UTF-8 and full uint32 territory range preserved");
    std::string crlf;
    for (char ch : graph) {if (ch == '\n') crlf += '\r'; crlf += ch;}
    check(c::loadDefinitionText(crlf).territories().size() == 3, "Windows line endings load");
}

void mapValidation() {
    c::DefinitionLoadOptions options;
    options.requireMaps = true;
    rejects([&]{ (void)c::loadDefinitionText(graph, options); }, "required maps without resolver");
    options.mapExists = [](std::string_view map) {return map == "maps/one.ota";};
    rejects([&]{ (void)c::loadDefinitionText(minimal, options); }, "required missing map");
    rejects([&]{ (void)c::loadDefinitionText(graph, options); }, "unknown map in required catalog");
    options.requireMaps = false;
    rejects([&]{ (void)c::loadDefinitionText(graph, options); }, "unknown optional supplied map");
    check(c::loadDefinitionText(minimal, options).territories().size() == 1, "catalog accepts absent optional map");
    check(c::loadDefinitionText(minimal + "map 1 \"maps/one.ota\"\n", options).find(1)->mapIdentifier.has_value(), "known map accepted");
    options.requireAdjacency = true;
    rejects([&]{ (void)c::loadDefinitionText(minimal, options); }, "required missing adjacency");
    check(c::loadDefinitionText(minimal + "neighbors 1\n", options).find(1)->neighbors->empty(), "required adjacency allows explicitly isolated territory");
}

void stateIsolation() {
    const auto definition = c::loadDefinitionText(graph);
    auto a = c::makeInitialState(definition);
    auto b = c::makeInitialState(definition);
    check(a.campaignId == "synthetic" && a.territories.size() == 3, "state references campaign and every territory");
    for (const auto& [id, state] : a.territories) {
        (void)id;
        check(!state.owner && !state.assignedMap, "state does not infer native ownership or runtime map assignment");
    }
    a.territories.at(1).owner = c::TerritoryOwner::Terror;
    a.territories.at(1).assignedMap = "maps/runtime.ota";
    c::validateState(definition, a);
    check(!b.territories.at(1).owner && !b.territories.at(1).assignedMap, "two campaigns have independent mutable state");
    check(definition.find(1)->nativeFaction == "Aramon" && definition.find(1)->mapIdentifier == "maps/one.ota", "runtime mutations preserve authored definition");
    a.territories.at(2).owner = c::TerritoryOwner::Contested;
    c::validateState(definition, a);
    check(a.territories.at(2).owner.has_value() && !a.territories.at(3).owner, "contested owner distinct from unknown");
    auto& recon = a.territories.at(1).recon;
    check(!recon.fatigueVictoryPoints && !recon.honor.requiredVictoryPoints &&
        !recon.honor.supportVictoryPoints && !recon.honor.battleVictoryPoints &&
        !recon.terror.requiredVictoryPoints && !recon.terror.supportVictoryPoints &&
        !recon.terror.battleVictoryPoints, "recon values start unknown, not zero");
    recon.fatigueVictoryPoints = 0;
    recon.honor.supportVictoryPoints = 1.25;
    recon.terror.battleVictoryPoints = -2.5;
    c::validateState(definition,a);
    check(recon.fatigueVictoryPoints.has_value() && *recon.fatigueVictoryPoints == 0 &&
        *recon.honor.supportVictoryPoints == 1.25 && *recon.terror.battleVictoryPoints == -2.5,
        "explicit zero and finite fractional/sign values preserved without invented rounding/ranges");
    check(!b.territories.at(1).recon.fatigueVictoryPoints && !b.territories.at(1).recon.honor.supportVictoryPoints,
        "recon state independent across live campaigns");
    for (double invalid : {std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        for (int field = 0; field < 7; ++field) {
            auto nonfinite = a;
            auto& r = nonfinite.territories.at(1).recon;
            std::optional<double>* fields[] = {&r.fatigueVictoryPoints,
                &r.honor.requiredVictoryPoints,&r.honor.supportVictoryPoints,&r.honor.battleVictoryPoints,
                &r.terror.requiredVictoryPoints,&r.terror.supportVictoryPoints,&r.terror.battleVictoryPoints};
            *fields[field] = invalid;
            rejects([&]{c::validateState(definition,nonfinite);}, "nonfinite recon parameter");
        }
    }
    auto bad = a;
    bad.campaignId = "different";
    rejects([&]{c::validateState(definition,bad);}, "state of wrong campaign");
    bad = a; bad.territories.erase(2);
    rejects([&]{c::validateState(definition,bad);}, "missing territory state");
    bad = a; bad.territories.emplace(99,c::TerritoryState{});
    rejects([&]{c::validateState(definition,bad);}, "extra territory state");
    bad = a; bad.territories.erase(2); bad.territories.emplace(99,c::TerritoryState{});
    rejects([&]{c::validateState(definition,bad);}, "wrong territory ID with matching state count");
    bad = a; bad.territories.at(1).owner = static_cast<c::TerritoryOwner>(999);
    rejects([&]{c::validateState(definition,bad);}, "invalid owner enumeration");
    bad = a; bad.territories.at(1).assignedMap = "";
    rejects([&]{c::validateState(definition,bad);}, "empty runtime map");
    c::DefinitionLoadOptions maps;
    maps.mapExists = [](std::string_view name){return name == "maps/runtime.ota";};
    c::validateState(definition,a,maps);
    maps.requireMaps = true;
    rejects([&]{c::validateState(definition,a,maps);}, "required runtime map remains unknown despite authored preference");
    bad = a;
    for (auto& [id,territory] : bad.territories) { (void)id; territory.assignedMap = "maps/runtime.ota"; }
    c::validateState(definition,bad,maps);
    maps.requireMaps = false;
    bad = a; bad.territories.at(1).assignedMap = "missing";
    rejects([&]{c::validateState(definition,bad,maps);}, "unknown runtime map");
}

void malformedAndFiles() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("tak-crusades-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path,ec); } } cleanup{root};
    const auto path = root / fs::path(u8"Café-世界.campaign");
    { std::ofstream out(path,std::ios::binary); out << graph; check(bool(out),"write synthetic campaign"); }
    check(c::loadDefinition(path).find(3)->displayName == "Three", "load campaign from Unicode filesystem path");
    rejects([&]{(void)c::loadDefinition(root / "absent.campaign");}, "missing campaign file");
    { std::ofstream out(path,std::ios::binary); out << "invalid"; }
    rejects([&]{(void)c::loadDefinition(path);}, "malformed campaign file");

    for (std::string badName : {std::string("\xc0\xaf"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("\xc3")})
        rejects([&]{(void)c::loadDefinitionText(minimal + "terrain 1 \"" + badName + "\"\n");}, "invalid UTF-8");
    rejects([&]{(void)c::loadDefinitionText(std::string(8*1024*1024+1,' '));}, "oversized input");
    try {
        (void)c::loadDefinitionText(minimal + "neighbors 1 1\n",{},"broken.campaign");
        check(false,"invalid adjacency should throw");
    } catch (const std::runtime_error& e) {
        check(std::string(e.what()).find("broken.campaign") != std::string::npos,"validation error identifies input origin");
    }
}
} // namespace

int main() {
    try {
        parserValidation();
        deterministicLoading();
        mapValidation();
        stateIsolation();
        malformedAndFiles();
        std::cout << "PASS: " << checks << " Crusades campaign model checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
