#include "server/crusades/rules.h"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace tak::srv::crusades {

std::string policyIdentifier(const RulesPolicy& policy) {
    switch (policy.mode) {
    case RulesMode::HistoricalDarien:
        if (policy.fixtureCaptureWins != 3) throw std::runtime_error("fixture parameter in historical policy");
        return "historical-darien-v1";
    case RulesMode::Fixture:
        if (policy.fixtureCaptureWins < 1 || policy.fixtureCaptureWins > 1000000)
            throw std::runtime_error("fixture capture wins outside authored range");
        return "fixture-capture-" + std::to_string(policy.fixtureCaptureWins) + "-v1";
    case RulesMode::Modern:
        if (policy.fixtureCaptureWins != 3) throw std::runtime_error("fixture parameter in modern policy");
        return "modern-reserved-v1";
    }
    throw std::runtime_error("unknown campaign rules mode");
}

RulesPolicy parsePolicyIdentifier(std::string_view id) {
    if (id == "historical-darien-v1") return {};
    if (id == "modern-reserved-v1") return {RulesMode::Modern, 3};
    constexpr std::string_view prefix = "fixture-capture-", suffix = "-v1";
    if (id.size() > prefix.size() + suffix.size() && id.substr(0, prefix.size()) == prefix &&
        id.substr(id.size() - suffix.size()) == suffix) {
        const auto digits = id.substr(prefix.size(), id.size() - prefix.size() - suffix.size());
        uint32_t wins = 0;
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), wins);
        if (result.ec == std::errc{} && result.ptr == digits.data() + digits.size()) {
            RulesPolicy policy{RulesMode::Fixture, wins};
            if (policyIdentifier(policy) == id) return policy;
        }
    }
    throw std::runtime_error("unknown or noncanonical campaign rules policy");
}

RulesDecision evaluateRules(const CampaignDefinition& definition, const CampaignState& state,
                            const RulesPolicy& policy, const RuleBattle& battle) {
    const auto id = policyIdentifier(policy);
    validateState(definition, state);
    if (!definition.find(battle.territory)) throw std::runtime_error("rules reference unknown territory");
    if (battle.winningSide && *battle.winningSide != TerritoryOwner::Honor && *battle.winningSide != TerritoryOwner::Terror)
        throw std::runtime_error("invalid rules winning alliance");
    RulesDecision decision{id, RulesEvidence::Unknown, RulesDisposition::UnknownRules,
        "Historical server point weights, fatigue/support calculations, capture comparison, resets and adjacency transitions remain unknown.", state, false};
    if (policy.mode == RulesMode::Fixture) decision.evidence = RulesEvidence::AuthoredFixture;
    if (!battle.winningSide) {
        decision.disposition = RulesDisposition::Ineligible;
        decision.reason = "No eligible verified winner; territory state remains unchanged.";
        return decision;
    }
    if (policy.mode == RulesMode::HistoricalDarien) return decision;
    if (policy.mode == RulesMode::Modern) {
        decision.disposition = RulesDisposition::UnsupportedPolicy;
        decision.reason = "Modern campaign rules are reserved and have not been implemented.";
        return decision;
    }
    auto& territory = decision.nextState.territories.at(battle.territory);
    auto& side = *battle.winningSide == TerritoryOwner::Honor ? territory.recon.honor : territory.recon.terror;
    const double previous = side.battleVictoryPoints.value_or(0);
    if (previous < 0 || previous > policy.fixtureCaptureWins || std::floor(previous) != previous)
        throw std::runtime_error("fixture win count is not an integer within the authored threshold");
    const double next = previous < policy.fixtureCaptureWins ? previous + 1 : previous;
    decision.changed = !side.battleVictoryPoints || next != previous;
    side.battleVictoryPoints = next;
    if (next >= policy.fixtureCaptureWins && territory.owner != battle.winningSide) {
        territory.owner = battle.winningSide;
        decision.changed = true;
    }
    decision.disposition = decision.changed ? RulesDisposition::Applied : RulesDisposition::NoChange;
    decision.reason = "Authored fixture: one cumulative point per eligible win, target capture at " +
        std::to_string(policy.fixtureCaptureWins) + " side wins; no historical formula or neighbor propagation.";
    return decision;
}

MomentumCounts countDisplayedMomentum(std::string_view suppliedHistory) {
    MomentumCounts counts;
    for (const char value : suppliedHistory) {
        if (value == '\0') break;
        if (value == 'H') ++counts.honor;
        else if (value == 'T') ++counts.terror;
    }
    return counts;
}

CaptureAdvisory inspectHistoricalCapture(const ReconMetrics& metrics,
                                        std::optional<TerritoryOwner> momentum) {
    const auto uncertain = [](const char* why) { return CaptureAdvisory{CaptureAssessment::Indeterminate,
        RulesEvidence::ConfirmedPresentation, why}; };
    if (!momentum || (*momentum != TerritoryOwner::Honor && *momentum != TerritoryOwner::Terror))
        return uncertain("An explicit, unambiguous momentum side was not supplied.");
    const auto valid = [](const std::optional<double>& value) { return value && std::isfinite(*value); };
    if (!valid(metrics.fatigueVictoryPoints)) return uncertain("Fatigue is missing or non-finite; display fallback zero is not evidence.");
    double totals[2];
    const SideReconMetrics* sides[]{&metrics.honor, &metrics.terror};
    for (size_t i = 0; i < 2; ++i) {
        const auto& side = *sides[i];
        if (!valid(side.requiredVictoryPoints) || !valid(side.supportVictoryPoints) || !valid(side.battleVictoryPoints))
            return uncertain("Required, support or battle victory points are missing or non-finite.");
        totals[i] = (*metrics.fatigueVictoryPoints + *side.supportVictoryPoints) + *side.battleVictoryPoints;
        if (!std::isfinite(totals[i])) return uncertain("Displayed arithmetic overflowed; original numeric behavior is unknown.");
        if (totals[i] == *side.requiredVictoryPoints) return uncertain("Equality is unresolved in the original server rules.");
    }
    if (totals[0] > *metrics.honor.requiredVictoryPoints && totals[1] > *metrics.terror.requiredVictoryPoints)
        return uncertain("Both sides exceed their requirements; original conflict resolution is unknown.");
    const size_t active = *momentum == TerritoryOwner::Honor ? 0 : 1;
    const bool exceeds = totals[active] > *sides[active]->requiredVictoryPoints;
    return {exceeds ? CaptureAssessment::Exceeds : CaptureAssessment::DoesNotExceed,
        RulesEvidence::ConfirmedPresentation,
        "Advisory only: supplied binary64 values " + std::string(exceeds ? "exceed" : "do not exceed") +
        " the explicit momentum side's requirement under the shipped recon explanation; this is not recovered server arithmetic."};
}

} // namespace tak::srv::crusades
