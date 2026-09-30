#pragma once

#include "server/crusades/campaign.h"

namespace tak::srv::crusades {

enum class RulesMode { HistoricalDarien, Fixture, Modern };
struct RulesPolicy {
    RulesMode mode = RulesMode::HistoricalDarien;
    uint32_t fixtureCaptureWins = 3;
};

// Canonical, versioned identifiers. Fixture parameters are authored test data,
// never values recovered from Cavedog's servers. Modern is reserved/unimplemented.
std::string policyIdentifier(const RulesPolicy& policy);
RulesPolicy parsePolicyIdentifier(std::string_view identifier);

enum class RulesEvidence { Unknown, ReconstructedClient, AuthoredFixture, ConfirmedPresentation };
enum class RulesDisposition { UnknownRules, UnsupportedPolicy, Ineligible, Applied, NoChange };
struct RuleBattle {
    TerritoryId territory;
    // Trusted caller derives this only from an eligible verified referee result.
    // Missing means no eligible winner. Contested is never a winning alliance.
    std::optional<TerritoryOwner> winningSide;
};
struct RulesDecision {
    std::string policyId;
    RulesEvidence evidence;
    RulesDisposition disposition;
    std::string reason;
    CampaignState nextState;
    bool changed = false;
};

// Pure evaluation, no clock, database, network, randomness or simulation access.
// Historical rules remain unresolved and cannot change ownership or metrics.
// Fixture counts wins in battleVictoryPoints and captures only the target after
// N cumulative side wins. Counts saturate at N; no neighbor propagation/reset.
RulesDecision evaluateRules(const CampaignDefinition& definition, const CampaignState& state,
                            const RulesPolicy& policy, const RuleBattle& battle);

struct MomentumCounts { size_t honor = 0, terror = 0; };
// Reconstructed client presentation only: count uppercase H/T in the supplied
// server string until its first NUL. Other bytes are ignored; no 20-entry cap.
// This does not reconstruct server history/window policy or determine momentum.
MomentumCounts countDisplayedMomentum(std::string_view suppliedHistory);

enum class CaptureAssessment { Exceeds, DoesNotExceed, Indeterminate };
struct CaptureAdvisory {
    CaptureAssessment assessment = CaptureAssessment::Indeterminate;
    RulesEvidence evidence = RulesEvidence::ConfirmedPresentation;
    std::string reason;
};
// Illustrates the shipped recon explanation using explicitly supplied values.
// NOT an authoritative capture predicate: original arithmetic/precision, ties,
// battle-free capture and reset semantics are unknown. This never changes state.
// Missing values, equality, overflow, absent momentum or both sides exceeding
// their requirements are indeterminate. No displayed fallback zero is assumed.
CaptureAdvisory inspectHistoricalCapture(const ReconMetrics& metrics,
                                        std::optional<TerritoryOwner> explicitMomentum);

} // namespace tak::srv::crusades
