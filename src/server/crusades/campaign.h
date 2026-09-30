#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tak::srv::crusades {

using TerritoryId = uint32_t;

struct TerritoryDefinition {
    TerritoryId id;
    std::string displayName;
    // Native faction is descriptive historical data, never current ownership.
    std::optional<std::string> nativeFaction;
    std::optional<std::string> terrain;
    // These are authored campaign data. Darien's graph/map table is NOT known.
    std::optional<std::string> mapIdentifier;
    // Missing means unknown; present and empty means explicitly isolated.
    std::optional<std::vector<TerritoryId>> neighbors;
};

struct DefinitionLoadOptions {
    // When present, validates every supplied map, even when maps are optional.
    std::function<bool(std::string_view)> mapExists;
    // Requires authored maps during definition load; requires runtime assigned
    // maps during state validation. Both uses require a resolver.
    bool requireMaps = false;
    // Definition loading only; validateState does not revalidate definitions.
    bool requireAdjacency = false;
};

class CampaignDefinition {
public:
    const std::string& id() const { return id_; }
    const std::string& displayName() const { return displayName_; }
    const std::map<TerritoryId, TerritoryDefinition>& territories() const { return territories_; }
    const TerritoryDefinition* find(TerritoryId id) const;

private:
    CampaignDefinition() = default;
    std::string id_, displayName_;
    std::map<TerritoryId, TerritoryDefinition> territories_;
    friend CampaignDefinition loadDefinitionText(std::string_view,
        const DefinitionLoadOptions&, const std::string&);
};

// Engine-owned versioned format, NOT the original Darien.def format.
// Throws std::runtime_error with origin/line diagnostics; never returns partial data.
CampaignDefinition loadDefinitionText(std::string_view text,
    const DefinitionLoadOptions& options = {}, const std::string& origin = "<memory>");
CampaignDefinition loadDefinition(const std::filesystem::path& path,
    const DefinitionLoadOptions& options = {});

enum class TerritoryOwner { Contested, Honor, Terror };

struct SideReconMetrics {
    std::optional<double> requiredVictoryPoints; // historical toughness label
    std::optional<double> supportVictoryPoints;
    std::optional<double> battleVictoryPoints;
};

struct ReconMetrics {
    // A modern finite-double representation of reported values, NOT a claim
    // about original server precision, rounding, ranges or capture arithmetic.
    std::optional<double> fatigueVictoryPoints;
    SideReconMetrics honor;
    SideReconMetrics terror;
};

struct TerritoryState {
    // No starting ownership is inferred from nativeFaction or PreInit.jje.
    std::optional<TerritoryOwner> owner;
    // Runtime assignment is separate from the authored map preference.
    std::optional<std::string> assignedMap;
    ReconMetrics recon;
};

struct CampaignState {
    std::string campaignId;
    std::map<TerritoryId, TerritoryState> territories;
};

// Fresh state has one entry per territory, with unknown owner/map assignment.
CampaignState makeInitialState(const CampaignDefinition& definition);
// Rejects wrong campaign, missing/extra territory state, invalid runtime maps,
// invalid ownership enum values and non-finite recon metrics.
void validateState(const CampaignDefinition& definition, const CampaignState& state,
    const DefinitionLoadOptions& options = {});

} // namespace tak::srv::crusades
