#include "net/crusades.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace c = tak::net::crusades;
namespace {
int checks = 0;
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
template<class Function> void rejects(Function function, const std::string& why) {
    ++checks; try { function(); } catch (const c::DecodeError&) { return; } throw std::runtime_error("accepted " + why);
}
c::Snapshot snapshot() {
    c::Snapshot s; s.requestId = 7; s.campaignId = "test"; s.displayName = "Synthetic \xc3\xa9"; s.revision = 12; s.rulesPolicy = "historical-darien-v1";
    c::Territory a; a.id = 1; a.displayName = "First"; a.nativeFaction = "Neutral"; a.terrain = "Forest"; a.mapIdentifier = "maps/one";
    a.neighbors = std::vector<uint32_t>{2}; a.owner = c::Owner::Honor; a.assignedMap = "runtime/map";
    a.recon = {0.0, 1.25, -2.0, -0.0, 7.5, 9.0, std::numeric_limits<double>::max()};
    c::Territory b; b.id = 2; b.displayName = "Second"; b.neighbors = std::vector<uint32_t>{1};
    c::Territory d; d.id = 3; d.displayName = "Isolated"; d.neighbors.emplace(); d.owner = c::Owner::Contested;
    c::Territory u; u.id = 4; u.displayName = "Unknown";
    s.territories = {a,b,d,u}; return s;
}
c::BattleStatus battleStatus() {
    c::BattleStatus b; b.requestId = 3; b.campaignId = "test"; b.battleId = "issued:123"; b.campaignRevision = 12;
    b.territory = 1; b.status = c::BattlePhase::Completed; b.mapIdentifier = "maps/one"; b.expiresUnix = 100; b.roomId = 0;
    b.result = c::BattleResult{c::Outcome::Victory, 1000, UINT64_MAX, {"alice"}}; return b;
}
void roundTrips() {
    const std::vector<c::Request> requests{
        c::ListRequest{1,"",64}, c::ListRequest{UINT32_MAX,"last",1}, c::SnapshotRequest{2,"test",c::kUnknownRevision},
        c::SnapshotRequest{3,"test",uint64_t(INT64_MAX)}, c::PlayerStatusRequest{4,"test"}, c::BattleStatusRequest{5,"issued:123"}};
    for (const auto& request : requests) {
        const auto bytes = c::encode(request);
        check(bytes[0] == c::kVersion && bytes[1] == 0, "explicit little-endian version");
        check(c::encode(c::decodeRequest(c::kindOf(request), bytes)) == bytes, "request roundtrip");
    }
    c::CampaignList list; list.requestId = 5; list.entries = {{"a","First",0,"historical-darien-v1"},{"b","Second",uint64_t(INT64_MAX),"fixture-capture-3-v1"}}; list.nextCursor = "b";
    c::PlayerStatus player; player.requestId = 6; player.campaignId = "test"; player.campaignRevision = 12;
    player.allegiance = c::PlayerAllegiance{c::Alliance::Terror,4,100,200}; player.battles = {{"old",1,c::BattlePhase::Cancelled},{"new",2,c::BattlePhase::Started}}; player.battlesTruncated = true;
    c::PlayerStatus absent; absent.campaignId = "test";
    const std::vector<c::Response> responses{list,snapshot(),player,absent,battleStatus(),c::Error{9,c::ErrorCode::StaleRevision,"test",13,"Refresh required"},c::Error{0,c::ErrorCode::Disabled,"",{},"Disabled"}};
    for (const auto& response : responses) {
        const auto bytes = c::encode(response);
        check(c::encode(c::decodeResponse(c::kindOf(response), bytes)) == bytes, "response roundtrip");
        for (size_t n = 0; n < bytes.size(); ++n)
            rejects([&]{ (void)c::decodeResponse(c::kindOf(response), c::Bytes(bytes.begin(),bytes.begin()+n)); }, "every truncated response prefix");
        auto trailing = bytes; trailing.push_back(0);
        rejects([&]{ (void)c::decodeResponse(c::kindOf(response), trailing); }, "trailing response bytes");
    }
    auto decoded = c::decodeSnapshot(c::encode(c::Response{snapshot()}));
    check(std::signbit(*decoded.territories[0].recon.honorBattleVictoryPoints), "binary64 signed zero preserved");
    check(decoded.territories[0].recon.terrorBattleVictoryPoints == std::numeric_limits<double>::max(), "binary64 finite extreme preserved");
    check(!decoded.territories[1].owner && decoded.territories[2].owner == c::Owner::Contested, "unknown and contested owners distinct");
    check(decoded.territories[2].neighbors && decoded.territories[2].neighbors->empty() && !decoded.territories[3].neighbors, "unknown graph differs from isolated territory");
    check(decoded.territories[0].recon.fatigueVictoryPoints == 0.0 && !decoded.territories[1].recon.fatigueVictoryPoints, "missing metrics differ from zero");
    auto cancelled = battleStatus(); cancelled.status = c::BattlePhase::Cancelled;
    for (unsigned outcome = 2; outcome <= 9; ++outcome) {
        cancelled.result = c::BattleResult{static_cast<c::Outcome>(outcome),0,0,{}};
        check(std::get<c::BattleStatus>(c::decodeResponse(c::ResponseKind::BattleStatus,c::encode(c::Response{cancelled}))).result->outcome == static_cast<c::Outcome>(outcome), "all no-credit outcomes roundtrip");
    }
}
void malformed() {
    auto bytes = c::encode(c::Request{c::ListRequest{1,"",1}});
    bytes[0] = c::kVersion + 1;
    try { (void)c::decodeRequest(c::RequestKind::List,bytes); throw std::runtime_error("accepted unknown version"); }
    catch (const c::DecodeError& e) { check(e.code == c::ErrorCode::UnsupportedVersion, "unknown version classified"); }
    rejects([]{ (void)c::encode(c::Request{c::ListRequest{0,"",1}}); }, "zero request correlation");
    rejects([]{ (void)c::encode(c::Request{c::ListRequest{1,"",0}}); }, "zero page size");
    rejects([]{ (void)c::encode(c::Request{c::ListRequest{1,"",65}}); }, "oversized page size");
    rejects([]{ (void)c::encode(c::Request{c::SnapshotRequest{1,"x",uint64_t(INT64_MAX)+1}}); }, "invalid expected revision");
    rejects([]{ (void)c::decodeRequest(static_cast<c::RequestKind>(99),{1,0,1,0,0,0}); }, "invalid request kind");
    rejects([]{ (void)c::decodeResponse(static_cast<c::ResponseKind>(99),{1,0,1,0,0,0}); }, "invalid response kind");
    for (const auto& invalid : std::vector<std::string>{std::string("a\0b",3),"a\nb","\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82","\xc2\x85"}) {
        rejects([&]{ (void)c::encode(c::Request{c::PlayerStatusRequest{1,invalid}}); }, "invalid UTF-8/control identifier");
        c::Bytes payload{1,0,1,0,0,0,uint8_t(invalid.size()),0}; payload.insert(payload.end(),invalid.begin(),invalid.end());
        rejects([&]{ (void)c::decodeRequest(c::RequestKind::PlayerStatus,payload); }, "malformed wire UTF-8/control string");
    }
    c::PlayerStatus p; p.campaignId = "a";
    bytes = c::encode(c::Response{p}); bytes[17] = 2; // prefix6 + str3 + revision8 => presence
    rejects([&]{ (void)c::decodeResponse(c::ResponseKind::PlayerStatus,bytes); }, "invalid optional flag");
    p.allegiance = c::PlayerAllegiance{static_cast<c::Alliance>(0),0,0,0};
    rejects([&]{ (void)c::encode(c::Response{p}); }, "unknown alliance");
    p.allegiance = c::PlayerAllegiance{c::Alliance::Honor,0,4,3};
    rejects([&]{ (void)c::encode(c::Response{p}); }, "backward allegiance timestamp");
    p.allegiance.reset(); p.battles = {{"a",1,c::BattlePhase::Issued},{"a",2,c::BattlePhase::Started}};
    rejects([&]{ (void)c::encode(c::Response{p}); }, "duplicate battle references");
    p.battles.clear(); p.battlesTruncated = true;
    rejects([&]{ (void)c::encode(c::Response{p}); }, "empty truncated battle list");
    auto b = battleStatus(); b.result->winners = {"Alice"};
    rejects([&]{ (void)c::encode(c::Response{b}); }, "noncanonical winner account");
    b = battleStatus(); b.status = c::BattlePhase::Started;
    rejects([&]{ (void)c::encode(c::Response{b}); }, "result attached to nonterminal battle");
    b = battleStatus(); b.result->outcome = static_cast<c::Outcome>(10);
    rejects([&]{ (void)c::encode(c::Response{b}); }, "unknown outcome");
    b = battleStatus(); b.result->finalTick = 0;
    rejects([&]{ (void)c::encode(c::Response{b}); }, "zero tick victory");
    b = battleStatus(); b.result->winners.push_back("bob");
    rejects([&]{ (void)c::encode(c::Response{b}); }, "multiple duel winners");
    b = battleStatus(); b.status = static_cast<c::BattlePhase>(5); b.result.reset();
    rejects([&]{ (void)c::encode(c::Response{b}); }, "unknown battle phase");
    b = battleStatus(); b.roomId = 55;
    rejects([&]{ (void)c::encode(c::Response{b}); }, "terminal battle exposing active room");
    rejects([]{ (void)c::encode(c::Response{c::Error{1,static_cast<c::ErrorCode>(0),"",{},"bad"}}); }, "unknown error code");
    c::CampaignList list; list.entries = {{"b","B",0,"p"},{"a","A",0,"p"}};
    rejects([&]{ (void)c::encode(c::Response{list}); }, "unordered campaign page");
    list.entries.pop_back(); list.nextCursor = "c";
    rejects([&]{ (void)c::encode(c::Response{list}); }, "mismatched page cursor");
}
void snapshotValidation() {
    auto wireSource = snapshot(); wireSource.territories.resize(1);
    wireSource.territories[0].neighbors.reset(); wireSource.territories[0].owner.reset();
    wireSource.territories[0].assignedMap.reset(); wireSource.territories[0].recon = {};
    wireSource.territories[0].recon.terrorBattleVictoryPoints = 0;
    const auto canonicalWire = c::encode(c::Response{wireSource});
    auto malformedWire = canonicalWire;
    // The final optional metric is present and stores eight little-endian bytes.
    malformedWire[malformedWire.size()-3] = 0xf8; malformedWire[malformedWire.size()-2] = 0x7f;
    rejects([&]{ (void)c::decodeSnapshot(malformedWire); }, "NaN in received metric bytes");
    malformedWire = canonicalWire;
    // Owner precedes assigned-map flag, seven metric flags and one double.
    malformedWire[malformedWire.size()-18] = 255;
    rejects([&]{ (void)c::decodeSnapshot(malformedWire); }, "invalid received owner byte");
    malformedWire = c::encode(c::Response{snapshot()});
    const c::Bytes second{2,0,0,0,6,0,'S','e','c','o','n','d'};
    const auto duplicate = std::search(malformedWire.begin(),malformedWire.end(),second.begin(),second.end());
    check(duplicate != malformedWire.end(), "locate second territory in actual wire");
    *duplicate = 1;
    rejects([&]{ (void)c::decodeSnapshot(malformedWire); }, "duplicate received territory IDs");
    auto bad = snapshot(); bad.territories[1].id = 1;
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "duplicate territories");
    bad = snapshot(); bad.territories[0].id = 0;
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "zero territory ID");
    bad = snapshot(); bad.territories[0].neighbors = std::vector<uint32_t>{99};
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "unknown neighbor");
    bad = snapshot(); bad.territories[0].neighbors = std::vector<uint32_t>{1};
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "self adjacency");
    bad = snapshot(); bad.territories[0].neighbors = std::vector<uint32_t>{2,2};
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "duplicate neighbors");
    bad = snapshot(); bad.territories[1].neighbors.reset();
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "asymmetric adjacency");
    bad = snapshot(); bad.territories[0].owner = static_cast<c::Owner>(0);
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "explicit invalid owner");
    for (const double value : {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
        bad = snapshot(); bad.territories[0].recon.honorSupportVictoryPoints = value;
        rejects([&]{ (void)c::encode(c::Response{bad}); }, "nonfinite metric");
    }
    bad = snapshot(); bad.territories[0].mapIdentifier = "";
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "empty optional map");
    bad = snapshot(); bad.revision = c::kUnknownRevision;
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "unknown snapshot revision sentinel");
    bad = snapshot(); bad.territories.clear();
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "empty campaign snapshot");
    bad = snapshot(); bad.displayName.assign(c::kMaxDisplayName+1,'x');
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "oversized display name");
    bad = snapshot(); bad.campaignId.assign(c::kMaxIdentifier+1,'x');
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "oversized campaign ID");
    bad = snapshot(); bad.territories[0].assignedMap = std::string(c::kMaxMapIdentifier+1,'x');
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "oversized map ID");
    bad = snapshot(); bad.territories.clear();
    for (uint32_t n = 1; n <= c::kMaxTerritories; ++n) { c::Territory t; t.id = n; t.displayName = "T"; bad.territories.push_back(t); }
    check(c::decodeSnapshot(c::encode(c::Response{bad})).territories.size() == c::kMaxTerritories, "maximum territory count supported");
    auto extra = bad.territories.back(); ++extra.id; bad.territories.push_back(extra);
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "oversized territory count");
    bad.territories.pop_back(); for (auto& t : bad.territories) t.displayName.assign(1024,'x');
    rejects([&]{ (void)c::encode(c::Response{bad}); }, "oversized total snapshot payload");
    rejects([]{ (void)c::decodeSnapshot(c::Bytes(c::kMaxPayload+1,0)); }, "oversized raw payload");
}
void activity() {
    auto s=snapshot(); s.territories[0].activity=c::BattleActivity{0,0};
    auto decoded=c::decodeSnapshot(c::encode(c::Response{s}));
    check(decoded.territories[0].activity && decoded.territories[0].activity->active==0 && !decoded.territories[1].activity,"zero versus unknown activity lost");
    c::Replica replica;(void)replica.apply(s);
    s.territories[0].activity=c::BattleActivity{3,4};
    check(replica.apply(s)==c::ApplyResult::Updated && replica.find("test")->territories[0].activity->offered==3,"same revision live activity rejected");
    check(replica.apply(s)==c::ApplyResult::Unchanged,"identical activity not unchanged");
    auto conflict=s;conflict.territories[0].owner=c::Owner::Terror;
    check(replica.apply(conflict)==c::ApplyResult::Conflict,"activity bypassed ownership conflict");
    --s.revision;s.territories[0].activity=c::BattleActivity{9,9};
    check(replica.apply(s)==c::ApplyResult::Stale && replica.find("test")->territories[0].activity->offered==3,"old revision rolled activity backward");
    s=snapshot();s.territories[0].activity=c::BattleActivity{1000001,0};
    rejects([&]{(void)c::encode(c::Response{s});},"unbounded offered activity accepted");
    s.territories[0].activity=c::BattleActivity{0,1000001};
    rejects([&]{(void)c::encode(c::Response{s});},"unbounded active activity accepted");
}
void replica() {
    c::Replica replica; auto s = snapshot(); const auto original = c::encode(c::Response{s});
    check(replica.apply(original) == c::ApplyResult::Inserted, "initial snapshot installed");
    s.requestId = 99; std::reverse(s.territories.begin(),s.territories.end());
    check(replica.apply(s) == c::ApplyResult::Unchanged, "same content different ordering/correlation unchanged");
    s.displayName = "changed";
    check(replica.apply(s) == c::ApplyResult::Conflict, "same revision conflict rejected");
    check(c::encode(c::Response{*replica.find("test")}) == original, "conflict preserves old snapshot atomically");
    --s.revision;
    check(replica.apply(s) == c::ApplyResult::Stale, "lower revision rejected");
    check(replica.find("test")->revision == 12, "stale revision cannot replace current state");
    s.revision = 13;
    check(replica.apply(s) == c::ApplyResult::Updated && replica.find("test")->displayName == "changed", "newer full snapshot replaces state");
    const auto updated = c::encode(c::Response{*replica.find("test")}); auto truncated = updated; truncated.pop_back();
    rejects([&]{ (void)replica.apply(truncated); }, "truncated snapshot applied");
    check(c::encode(c::Response{*replica.find("test")}) == updated, "malformed update has no partial mutation");
    s.territories[0].id = 0; ++s.revision;
    rejects([&]{ (void)replica.apply(s); }, "invalid newer DTO applied");
    check(replica.find("test")->revision == 13, "invalid DTO does not advance revision");
    replica.erase("test"); check(replica.find("test") == nullptr, "erase releases snapshot");
    s = snapshot();
    for (size_t n = 0; n < c::kMaxCampaigns; ++n) { s.campaignId = "campaign-"+std::to_string(n); (void)replica.apply(s); }
    s.campaignId = "overflow"; rejects([&]{ (void)replica.apply(s); }, "unbounded replica cache");
    replica.clear(); check(replica.apply(s) == c::ApplyResult::Inserted, "clear permits reconnect cache reset");
}
} // namespace
int main() {
    try { roundTrips(); malformed(); snapshotValidation(); activity(); replica(); std::cout << "PASS: " << checks << " Crusades protocol/replica checks\n"; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
