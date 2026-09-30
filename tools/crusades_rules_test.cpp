#include "server/crusades/rules.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace c = tak::srv::crusades;
namespace {
int checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function action, const std::string& message) {
    ++checks;
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("accepted " + message);
}
c::CampaignDefinition definition() {
    return c::loadDefinitionText("campaign 1 \"synthetic\" \"Synthetic rules\"\n"
        "territory 1 \"West\"\nterritory 2 \"East\"\nneighbors 1 2\nneighbors 2 1\n");
}
c::ReconMetrics metrics() {
    c::ReconMetrics result;
    result.fatigueVictoryPoints = 2;
    result.honor = {10, 3, 6}; // supplied illustrative total 11
    result.terror = {20, 1, 1}; // supplied illustrative total 4
    return result;
}
void policies() {
    check(c::policyIdentifier({}) == "historical-darien-v1", "historical is default");
    check(c::parsePolicyIdentifier("historical-darien-v1").mode == c::RulesMode::HistoricalDarien, "historical policy roundtrip");
    check(c::parsePolicyIdentifier("modern-reserved-v1").mode == c::RulesMode::Modern, "modern policy explicitly separate");
    for (uint32_t wins : {1u, 2u, 3u, 1000000u}) {
        c::RulesPolicy policy{c::RulesMode::Fixture, wins};
        const auto parsed = c::parsePolicyIdentifier(c::policyIdentifier(policy));
        check(parsed.mode == c::RulesMode::Fixture && parsed.fixtureCaptureWins == wins, "fixture policy roundtrip");
    }
    for (const char* invalid : {"", "historical-darien-v2", "fixture-capture-0-v1", "fixture-capture-01-v1",
                              "fixture-capture--1-v1", "fixture-capture-+1-v1", "fixture-capture-1000001-v1",
                              "fixture-capture-4294967296-v1", "fixture-capture-3-v1x", "fixture-capture-3-v2"})
        rejects([&]{ (void)c::parsePolicyIdentifier(invalid); }, invalid);
    rejects([]{ (void)c::policyIdentifier({static_cast<c::RulesMode>(99), 3}); }, "unknown policy enum");
    rejects([]{ (void)c::policyIdentifier({c::RulesMode::HistoricalDarien, 1}); }, "fixture parameters in historical mode");
    rejects([]{ (void)c::policyIdentifier({c::RulesMode::Modern, 1}); }, "fixture parameters in modern mode");
}
void historical() {
    const auto def = definition(); auto state = c::makeInitialState(def);
    state.territories.at(1).recon = metrics();
    state.territories.at(2).owner = c::TerritoryOwner::Terror;
    for (const auto winner : {c::TerritoryOwner::Honor, c::TerritoryOwner::Terror}) {
        const auto decision = c::evaluateRules(def, state, {}, {1, winner});
        check(decision.disposition == c::RulesDisposition::UnknownRules && !decision.changed, "unknown historical formulas cannot mutate state");
        check(decision.evidence == c::RulesEvidence::Unknown && !decision.reason.empty(), "historical authority limit explicit");
        check(!decision.nextState.territories.at(1).owner && decision.nextState.territories.at(1).recon.honor.battleVictoryPoints == 6,
              "historical presentation values do not trigger capture or earned points");
        check(decision.nextState.territories.at(2).owner == c::TerritoryOwner::Terror, "no invented neighboring transition");
    }
    const auto modern = c::evaluateRules(def, state, {c::RulesMode::Modern, 3}, {1, c::TerritoryOwner::Honor});
    check(modern.disposition == c::RulesDisposition::UnsupportedPolicy && !modern.changed, "modern reserved mode fail closed");
    const auto ineligible = c::evaluateRules(def, state, {}, {1, {}});
    check(ineligible.disposition == c::RulesDisposition::Ineligible && !ineligible.changed, "no winner yields no historical mutation");
    rejects([&]{ (void)c::evaluateRules(def, state, {}, {99, c::TerritoryOwner::Honor}); }, "unknown territory reference");
    rejects([&]{ (void)c::evaluateRules(def, state, {}, {1, c::TerritoryOwner::Contested}); }, "contested side cannot win");
    rejects([&]{ (void)c::evaluateRules(def, state, {}, {1, static_cast<c::TerritoryOwner>(99)}); }, "invalid winning side enum");
    auto broken = state; broken.territories.erase(1);
    rejects([&]{ (void)c::evaluateRules(def, broken, {}, {1, c::TerritoryOwner::Honor}); }, "malformed live state");
}
void fixture() {
    const auto def = definition(); const c::RulesPolicy policy{c::RulesMode::Fixture, 3};
    auto state = c::makeInitialState(def);
    state.territories.at(1).recon.fatigueVictoryPoints = 42;
    state.territories.at(1).recon.honor.supportVictoryPoints = 99;
    state.territories.at(1).recon.honor.requiredVictoryPoints = 999;
    state.territories.at(2).owner = c::TerritoryOwner::Terror;
    for (int win = 1; win <= 3; ++win) {
        const auto decision = c::evaluateRules(def, state, policy, {1, c::TerritoryOwner::Honor});
        check(decision.changed && decision.disposition == c::RulesDisposition::Applied, "fixture increments target counter");
        check(decision.evidence == c::RulesEvidence::AuthoredFixture && decision.policyId == "fixture-capture-3-v1", "fixture provenance persisted in decision");
        const auto& target = decision.nextState.territories.at(1);
        check(target.recon.honor.battleVictoryPoints == win, "fixture count exactly tracks eligible wins");
        check(target.owner == (win == 3 ? std::optional<c::TerritoryOwner>(c::TerritoryOwner::Honor) : std::nullopt), "fixture captures exactly at authored threshold");
        check(target.recon.fatigueVictoryPoints == 42 && target.recon.honor.supportVictoryPoints == 99 && target.recon.honor.requiredVictoryPoints == 999,
              "fixture does not reinterpret support fatigue toughness");
        check(decision.nextState.territories.at(2).owner == c::TerritoryOwner::Terror && !decision.nextState.territories.at(2).recon.honor.battleVictoryPoints,
              "adjacency does not trigger fixture propagation");
        check(state.territories.at(1).recon.honor.battleVictoryPoints != win, "pure evaluation preserves input state");
        state = decision.nextState;
    }
    const auto saturated = c::evaluateRules(def, state, policy, {1, c::TerritoryOwner::Honor});
    check(!saturated.changed && saturated.disposition == c::RulesDisposition::NoChange, "saturated fixture does not emit spurious mutation");
    for (int n = 0; n < 3; ++n) state = c::evaluateRules(def, state, policy, {1, c::TerritoryOwner::Terror}).nextState;
    check(state.territories.at(1).owner == c::TerritoryOwner::Terror && state.territories.at(1).recon.honor.battleVictoryPoints == 3,
          "opponent capture leaves cumulative counters unchanged");
    const auto recapture = c::evaluateRules(def, state, policy, {1, c::TerritoryOwner::Honor});
    check(recapture.nextState.territories.at(1).owner == c::TerritoryOwner::Honor, "explicit fixture no-reset behavior permits recapture");
    const auto noWinner = c::evaluateRules(def, state, policy, {1, {}});
    check(!noWinner.changed && noWinner.disposition == c::RulesDisposition::Ineligible, "noneligible fixture result does not count");
    for (double invalid : {-1.0, 1.5, 4.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto bad = state; bad.territories.at(1).recon.honor.battleVictoryPoints = invalid;
        rejects([&]{ (void)c::evaluateRules(def, bad, policy, {1, c::TerritoryOwner::Honor}); }, "invalid fixture counter");
    }
    const auto immediate = c::evaluateRules(def, c::makeInitialState(def), {c::RulesMode::Fixture, 1}, {2, c::TerritoryOwner::Terror});
    check(immediate.nextState.territories.at(2).owner == c::TerritoryOwner::Terror, "explicit threshold1 fixture");
    check(c::evaluateRules(def, state, policy, {1, c::TerritoryOwner::Honor}).reason == recapture.reason, "repeated pure evaluation deterministic");
}
void presentation() {
    auto counts = c::countDisplayedMomentum("HTHTHhhtx");
    check(counts.honor == 3 && counts.terror == 2, "client count exact uppercase H/T only");
    counts = c::countDisplayedMomentum(std::string(25, 'H') + std::string(26, 'T'));
    check(counts.honor == 25 && counts.terror == 26, "display reader does not impose FAQ20-battle server window");
    counts = c::countDisplayedMomentum(std::string("H\0T", 3));
    check(counts.honor == 1 && counts.terror == 0, "native display reader stops at first NUL");
    auto m = metrics();
    auto advisory = c::inspectHistoricalCapture(m, c::TerritoryOwner::Honor);
    check(advisory.assessment == c::CaptureAssessment::Exceeds && advisory.evidence == c::RulesEvidence::ConfirmedPresentation,
          "documented greater-than explanation is advisory only");
    check(c::inspectHistoricalCapture(m, c::TerritoryOwner::Terror).assessment == c::CaptureAssessment::DoesNotExceed,
          "explicit opposite momentum side below requirement");
    check(c::inspectHistoricalCapture(m, {}).assessment == c::CaptureAssessment::Indeterminate, "momentum not inferred from arithmetic");
    check(c::inspectHistoricalCapture(m, c::TerritoryOwner::Contested).assessment == c::CaptureAssessment::Indeterminate, "ambiguous momentum stays unresolved");
    m.honor.requiredVictoryPoints = 11;
    check(c::inspectHistoricalCapture(m, c::TerritoryOwner::Honor).assessment == c::CaptureAssessment::Indeterminate, "equality semantics unrecovered");
    m = metrics(); m.terror.requiredVictoryPoints = 3;
    check(c::inspectHistoricalCapture(m, c::TerritoryOwner::Honor).assessment == c::CaptureAssessment::Indeterminate, "simultaneous qualifying totals unrecovered");
    m = metrics(); m.honor.supportVictoryPoints = std::numeric_limits<double>::max(); m.fatigueVictoryPoints = std::numeric_limits<double>::max();
    check(c::inspectHistoricalCapture(m, c::TerritoryOwner::Honor).assessment == c::CaptureAssessment::Indeterminate, "overflow does not imply capture");
    for (int i = 0; i < 7; ++i) {
        m = metrics();
        std::optional<double>* fields[]{&m.fatigueVictoryPoints, &m.honor.requiredVictoryPoints, &m.honor.supportVictoryPoints,
            &m.honor.battleVictoryPoints, &m.terror.requiredVictoryPoints, &m.terror.supportVictoryPoints, &m.terror.battleVictoryPoints};
        fields[i]->reset();
        check(c::inspectHistoricalCapture(m, c::TerritoryOwner::Honor).assessment == c::CaptureAssessment::Indeterminate, "every absent metric stays unknown");
        *fields[i] = std::numeric_limits<double>::quiet_NaN();
        check(c::inspectHistoricalCapture(m, c::TerritoryOwner::Honor).assessment == c::CaptureAssessment::Indeterminate, "every nonfinite metric rejected by advisory");
    }
    check(!advisory.reason.empty(), "advisory includes authority limitation");
}
} // namespace
int main() {
    try { policies(); historical(); fixture(); presentation(); std::cout << "PASS: " << checks << " Crusades pure rules checks\n"; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
