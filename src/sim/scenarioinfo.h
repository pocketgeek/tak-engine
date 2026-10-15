#pragma once

// MAP SCENARIO (protocol 259): what a map's own scenario would do if a room turned it
// on -- read from the map's .ota, .crt and Use Only .tdf alone, without building a world.
//
// One inspector serves the create screens (the map list's marker and the selected map's
// button), the room's rule lines and seat tags, the server's START checks, the lobby
// capacity and the tests. It is deliberately separate from setupMatch, which mixes the
// same checks with world mutations (custom stats, the neutral owner); scenario_info_test
// keeps the two in agreement: with a registry, status Ok exactly when setupMatch with
// MatchConfig::mapScenario does not refuse the map (placement bounds aside, below).
//
// Not part of the hashed simulation: nothing here touches a World.

#include <cstdint>
#include <string>
#include <vector>

namespace tak::hpi { class Vfs; }

namespace tak::sim {

class TypeRegistry;

enum class ScenarioStatus : uint8_t {
    Ok,             // offerable
    NoScenario,     // no/unparseable .ota, or hasscenario=0 without CRT content
    Leftover,       // hasscenario=0 but the .crt has placements or rules (retail never ran it)
    CrtMissing,     // hasscenario=1, no .crt
    CrtDamaged,     // .crt is not version 1
    Empty,          // no placed unit and no rule group with a condition or action
    BadOwner,       // a placement owner outside 0..8 (or rules past the neutral bank)
    UseOnlyBroken,  // useonlyunits is not "<map>.tdf" beside the map, or the file is missing/unreadable
    MissingType,    // (registry check) a placed, custom or Use Only type this install lacks
};

struct ScenarioInfo {
    ScenarioStatus status = ScenarioStatus::NoScenario;
    bool hasScenario = false;   // [GlobalHeader] hasscenario != 0
    bool marker = false;        // [TAKPlaytest] authoredscenario != 0 (a Test Map copy)
    bool crtValid = false;      // .crt present and version 1
    int placed = 0;             // placed units, every owner
    uint8_t placedSeats = 0;    // bit p: owner p (0..7) has placed units
    uint8_t ruleSeats = 0;      // bit p: rule bank p+1 (Player p+1) has a group with rules
    bool sharedRules = false;   // bank 0 (All Players) has a group with rules
    bool outcomes = false;      // some group has a Victory/Defeat action
    int useOnlyTypes = -1;      // -1 = unrestricted, else the Use Only entry count (0 = nothing buildable)
    std::string detail;         // MissingType: the type; UseOnlyBroken: the file name
    bool runnable() const { return hasScenario && crtValid; }
    bool content() const { return placed > 0 || ruleSeats != 0 || sharedRules; }
    uint8_t requiredSeats() const { return placedSeats | ruleSeats; }   // == setupMatch's seat rule
    bool offerable() const { return status == ScenarioStatus::Ok && !marker; }
    // Whether a room plays the scenario. Callers also exclude missions, generated maps,
    // stress tests and benchmarks.
    bool active(bool option) const { return runnable() && (marker || (option && content())); }
};

// Reads <map>.ota, <map>.crt and the named Use Only .tdf through `vfs` (a package view
// or the local install). Never throws. With `reg`, also checks every placed, custom and
// Use Only type (setupMatch's refusals, except placement bounds -- setupMatch keeps that
// check and its message, which the room shows as the server's start refusal; checking it
// here would mean decompressing every scenario map's TNT while the map list is built).
// The counts and masks are filled whenever the files parse, whatever the status, so the
// room can describe a Test Map copy too.
ScenarioInfo inspectScenario(const hpi::Vfs& vfs, const std::string& mapPath, const TypeRegistry* reg = nullptr);

// 1-based seat list: "1-4", "1 3", "2 5-8" (runs of three or more collapse). "" for none.
std::string seatList(uint8_t seats);

struct ScenarioLine { std::string text; bool warning = false; };
// Why the create screen greys the button (uppercase, <= 26 characters a line). Empty for
// an offerable map. A Test Map copy is the caller's case (TEST COPY: ALWAYS ON).
std::vector<std::string> scenarioReason(const ScenarioInfo&);
// What turning the scenario on changes: compact for the create screens (<= 26 characters
// a line), full for the room's rule lines (<= 44).
std::vector<ScenarioLine> scenarioSummary(const ScenarioInfo&, bool compact);

}  // namespace tak::sim
