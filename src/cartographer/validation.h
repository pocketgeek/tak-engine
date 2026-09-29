#pragma once
#include "cartographer/units.h"
#include "tnt/ota.h"
#include "tnt/tnt.h"
#include <set>
namespace tak::sim { class TypeRegistry; }
namespace tak::hpi { class Vfs; }
namespace cart {
struct MapIssue {
    enum class Severity { Warning, Error };
    Severity severity=Severity::Warning;
    std::string message;
    float x=-1,z=-1; // world position; negative means no navigable location
};
// Shared by Check Map and the rule form; warnings permit intentional scripts.
std::vector<MapIssue> validateRuleOperands(bool action,const tak::crt::Rule& rule,
    const tak::crt::Scenario& scenario,const tak::sim::TypeRegistry& registry);
// Observational: builds a private engine world; never modifies the document.
std::vector<MapIssue> validateMap(const tak::tnt::Map& map,
    const tak::tnt::Scenario& metadata,const tak::crt::Scenario& scenario,
    const std::vector<PlacedUnit>& units,const std::set<std::string>& useOnly,
    const tak::sim::TypeRegistry& registry,const tak::hpi::Vfs& vfs);
}
