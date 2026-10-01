// Offline, trusted administration. Mutation authorization comes from local OS
// access; actor IDs are audit attribution, never account authentication.
#include "server/crusades/servicelease.h"
#include "server/crusades/store.h"
#include "server/crusades/network.h"
#include "util/winargv.h"
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace c = tak::srv::crusades;
namespace fs = std::filesystem;

void help() {
    std::cout <<
        "Usage: crusades_admin <database.sqlite> <command> [arguments]\n"
        "Stop takserver before EVERY command; an exclusive service lock is required.\n"
        "  status | health [--limit N]          bounded database health (default 4096)\n"
        "  campaigns [--after ID] [--limit N]   campaign catalog\n"
        "  campaign ID [--after TERRITORY] [--limit N]\n"
        "  battle ID                           inspect without capability tokens\n"
        "  events ID [--after REV] [--limit N]  campaign event metadata\n"
        "  audit ID [--after SEQ] [--limit N]   append-only admin audit\n"
        "  backup NEWFILE                      SQLite backup; destination must be new\n"
        "  start DEFINITION --actor ID --reason TEXT\n"
        "  reset ID --expected-revision REV --actor ID --reason TEXT\n"
        "  cancel-battle ID --expected-revision REV --expected-state issued|started\n"
        "                   --actor ID --reason TEXT\n"
        "List limits are 1..64 (default 64); --after is an exclusive cursor.\n"
        "Health limits are 1..100000; incomplete scans return failure.\n"
        "Actor IDs are canonical lowercase account IDs, 3..20 characters.\n"
        "Start/reset use unknown initial ownership and metrics; start selects\n"
        "historical-darien-v1. Reset requires outstanding battles cancelled first.\n"
        "Start rejects definitions that cannot fit the campaign network format.\n"
        "Cancel expects the current campaign revision; terminal battles reject.\n"
        "Output is JSON lines; long display strings are clipped at 1024 bytes.\n"
        "Existing databases are required except for start. Restore while stopped by\n"
        "copying a verified backup to a NEW database path, then run health there.\n";
}

std::string json(std::string_view input) {
    // Avoid splitting UTF-8 when bounding operator-visible strings.
    bool clipped = input.size() > 1024;
    if (clipped) {
        size_t end = 1024;
        while (end && (static_cast<unsigned char>(input[end]) & 0xc0) == 0x80) --end;
        input = input.substr(0, end);
    }
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : input) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch < 0x20 || ch == 0x7f) {
            out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15];
        } else out += static_cast<char>(ch);
    }
    if (clipped) out += " [truncated]";
    return out + '"';
}
std::string optionalText(const std::optional<std::string>& value) {
    return value ? json(*value) : "null";
}
std::string metric(const std::optional<double>& value) {
    if (!value) return "null";
    std::ostringstream out; out << std::setprecision(17) << *value; return out.str();
}
std::string side(const c::SideReconMetrics& value) {
    return "{\"required\":" + metric(value.requiredVictoryPoints) +
        ",\"support\":" + metric(value.supportVictoryPoints) +
        ",\"battle\":" + metric(value.battleVictoryPoints) + "}";
}
const char* status(c::BattleStatus value) {
    switch (value) {
    case c::BattleStatus::Issued: return "issued";
    case c::BattleStatus::Started: return "started";
    case c::BattleStatus::Cancelled: return "cancelled";
    case c::BattleStatus::Completed: return "completed";
    case c::BattleStatus::Expired: return "expired";
    }
    throw std::runtime_error("invalid stored battle status");
}
std::string optionalStatus(const std::optional<c::BattleStatus>& value) {
    return value ? json(status(*value)) : "null";
}
int64_t integer(const std::string& value, int64_t minimum, int64_t maximum) {
    int64_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data()+value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data()+value.size() ||
        parsed < minimum || parsed > maximum || std::to_string(parsed) != value)
        throw std::runtime_error("expected canonical integer in range " +
            std::to_string(minimum) + ".." + std::to_string(maximum));
    return parsed;
}
void identity(const std::string& value) {
    if (value.empty() || value.size() > 256)
        throw std::runtime_error("identity must contain 1..256 bytes");
    for (const unsigned char ch : value)
        if (ch <= 0x20 || ch == 0x7f) throw std::runtime_error("identity contains whitespace/control bytes");
}
void actorId(const std::string& value) {
    const auto alnum = [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'); };
    if (value.size() < 3 || value.size() > 20 || !alnum(value.front()))
        throw std::runtime_error("actor must be a canonical lowercase account ID");
    for (char ch : value)
        if (!alnum(ch) && ch != '_' && ch != '-' && ch != '.')
            throw std::runtime_error("actor must be a canonical lowercase account ID");
}
void backupDestination(const fs::path& source, const fs::path& destination) {
    const auto target = fs::weakly_canonical(fs::absolute(destination));
    for (const char* suffix : {"", "-journal", "-wal", "-shm", ".service-lock"}) {
        auto reserved = source; reserved += suffix;
#ifdef _WIN32
        // Newly created sidecars may differ only in case from the reserved
        // spelling on the Windows filesystem, so path::operator== is unsafe.
        const bool same = CompareStringOrdinal(target.c_str(),-1,reserved.c_str(),-1,TRUE) == CSTR_EQUAL;
#else
        const bool same = target == reserved;
#endif
        if (same) throw std::runtime_error("backup destination cannot be the source database or its SQLite/service companion files");
    }
}

struct Command {
    std::string name, argument;
    std::map<std::string, std::string> options;
    std::string get(const std::string& name, const std::string& fallback) const {
        const auto found = options.find(name); return found == options.end() ? fallback : found->second;
    }
    std::string required(const std::string& name) const {
        const auto found = options.find(name);
        if (found == options.end()) throw std::runtime_error("missing required option " + name);
        return found->second;
    }
    size_t limit(size_t fallback=64, size_t maximum=64) const {
        return static_cast<size_t>(integer(get("--limit",std::to_string(fallback)),1,static_cast<int64_t>(maximum)));
    }
    c::AdminRequest request(int64_t expected) const {
        const auto actor = required("--actor"), reason = required("--reason");
        actorId(actor);
        if (reason.empty() || reason.size() > 512 || reason.find_first_not_of(' ') == std::string::npos)
            throw std::runtime_error("reason must contain 1..512 bytes of explicit text");
        for (const unsigned char ch : reason)
            if (ch < 0x20 || ch == 0x7f) throw std::runtime_error("reason contains control bytes");
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return {actor, reason, expected, now};
    }
};
Command parse(int argc, char** argv) {
    Command command; command.name = argv[2];
    std::string allowed;
    bool argument = false;
    if (command.name == "health" || command.name == "status") allowed = " --limit ";
    else if (command.name == "campaigns") allowed = " --after --limit ";
    else if (command.name == "campaign" || command.name == "events" || command.name == "audit") {
        argument = true; allowed = " --after --limit ";
    } else if (command.name == "battle" || command.name == "backup") argument = true;
    else if (command.name == "start") { argument = true; allowed = " --actor --reason "; }
    else if (command.name == "reset") { argument = true; allowed = " --expected-revision --actor --reason "; }
    else if (command.name == "cancel-battle") {
        argument = true; allowed = " --expected-revision --expected-state --actor --reason ";
    } else throw std::runtime_error("unknown command; run crusades_admin --help");
    int offset = 3;
    if (argument) {
        if (offset == argc || std::string_view(argv[offset]).starts_with("--"))
            throw std::runtime_error("command requires one positional argument");
        command.argument = argv[offset++];
        if (command.argument.empty()) throw std::runtime_error("empty positional argument");
    }
    while (offset < argc) {
        const std::string name = argv[offset++];
        if (!name.starts_with("--") || allowed.find(" " + name + " ") == std::string::npos)
            throw std::runtime_error("unexpected option or extra argument: " + name);
        if (offset == argc || std::string_view(argv[offset]).starts_with("--"))
            throw std::runtime_error("missing value for " + name);
        if (!command.options.emplace(name,argv[offset++]).second)
            throw std::runtime_error("duplicate option " + name);
    }
    return command;
}
void page(bool truncated, const std::string& next) {
    std::cout << "{\"kind\":\"page\",\"truncated\":" << (truncated ? "true" : "false")
              << ",\"next_after\":" << next << "}\n";
}
void battle(const c::IssuedBattle& value) {
    std::cout << "{\"kind\":\"battle\",\"id\":" << json(value.id)
        << ",\"campaign\":" << json(value.campaignId) << ",\"campaign_revision\":" << value.campaignRevision
        << ",\"territory\":" << value.territory << ",\"state\":" << json(status(value.status))
        << ",\"policy\":" << json(value.policyId) << ",\"map\":" << json(value.context.mapIdentifier)
        << ",\"created_unix\":" << value.createdUnix << ",\"changed_unix\":" << value.changedUnix
        << ",\"expires_unix\":" << value.expiresUnix << ",\"participants\":[";
    for (size_t i=0; i<value.context.participants.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << json(value.context.participants[i]);
    }
    std::cout << "]}\n";
}

int run(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") { help(); return 0; }
    if (argc < 3) { help(); return 2; }
    try {
        const auto command = parse(argc,argv);
        // Parse/validate mutation arguments before any store can create a file.
        std::optional<c::AdminRequest> request;
        if (command.name == "start") request = command.request(-1);
        if (command.name == "reset" || command.name == "cancel-battle")
            request = command.request(integer(command.required("--expected-revision"),0,std::numeric_limits<int64_t>::max()));
        c::BattleStatus expectedStatus = c::BattleStatus::Issued;
        if (command.name == "cancel-battle") {
            const auto expected = command.required("--expected-state");
            if (expected == "started") expectedStatus = c::BattleStatus::Started;
            else if (expected != "issued") throw std::runtime_error("expected state must be issued or started");
        }
        if (command.options.contains("--limit"))
            (void)command.limit(64,command.name == "health" || command.name == "status" ? 100000 : 64);
        if (command.options.contains("--after")) {
            const auto after = command.required("--after");
            if (command.name == "campaigns") identity(after);
            else (void)integer(after,command.name == "events" ? -1 : 0,
                command.name == "campaign" ? std::numeric_limits<uint32_t>::max() : std::numeric_limits<int64_t>::max());
        }
        if (command.name != "start" && command.name != "backup" && !command.argument.empty()) identity(command.argument);
        const auto path = fs::u8path(argv[1]);
        if (path.empty() || path == fs::path(":memory:")) throw std::runtime_error("a persistent database path is required");
        std::optional<c::CampaignDefinition> definition;
        if (command.name == "start") {
            definition = c::loadDefinition(fs::u8path(command.argument));
            identity(definition->id());
            c::validateCampaignNetworkState(*definition,c::makeInitialState(*definition));
        }
        if (command.name != "start" && !fs::is_regular_file(path))
            throw std::runtime_error("database does not exist or is not a regular file; only start creates a database");
        c::CampaignServiceLease lease(path);
        if (command.name != "start" && !fs::is_regular_file(lease.databasePath()))
            throw std::runtime_error("database disappeared before acquisition of its service lock");
        if (command.name == "backup") backupDestination(lease.databasePath(),fs::u8path(command.argument));
        c::CampaignStore store(lease.databasePath());
        if (command.name == "status" || command.name == "health") {
            const auto report = store.health(command.limit(4096,100000));
            std::cout << "{\"kind\":\"health\",\"healthy\":" << (report.healthy ? "true" : "false")
                << ",\"complete\":" << (report.complete ? "true" : "false") << ",\"schema_version\":" << report.schemaVersion
                << ",\"campaigns\":" << report.campaigns << ",\"events\":" << report.events
                << ",\"memberships\":" << report.memberships << ",\"battles\":" << report.battles
                << ",\"results\":" << report.results << ",\"admin_events\":" << report.adminEvents
                << ",\"checked_rows\":" << report.checkedRows << ",\"battle_states\":{\"issued\":" << report.battleStatuses[0]
                << ",\"started\":" << report.battleStatuses[1] << ",\"cancelled\":" << report.battleStatuses[2]
                << ",\"completed\":" << report.battleStatuses[3] << ",\"expired\":" << report.battleStatuses[4] << "}}\n";
            for (const auto& issue : report.issues) std::cout << "{\"kind\":\"issue\",\"message\":" << json(issue) << "}\n";
            if (!report.complete) std::cerr << "health scan incomplete; raise --limit (maximum 100000) for more coverage\n";
            return report.healthy && report.complete ? 0 : 1;
        } else if (command.name == "campaigns") {
            const auto after = command.get("--after",""); if (!after.empty()) identity(after);
            const auto result = store.campaignIds(after,command.limit());
            for (const auto& id : result.ids) std::cout << "{\"kind\":\"campaign_id\",\"id\":" << json(id) << "}\n";
            page(result.truncated,result.ids.empty() ? "null" : json(result.ids.back()));
        } else if (command.name == "campaign") {
            const auto after = integer(command.get("--after","0"),0,std::numeric_limits<uint32_t>::max());
            const auto limit = command.limit();
            const auto value = store.load(command.argument);
            std::cout << "{\"kind\":\"campaign\",\"id\":" << json(value.definition.id())
                << ",\"name\":" << json(value.definition.displayName()) << ",\"revision\":" << value.revision
                << ",\"policy\":" << json(c::policyIdentifier(value.rules))
                << ",\"territories\":" << value.state.territories.size() << "}\n";
            auto it = value.state.territories.upper_bound(static_cast<c::TerritoryId>(after));
            size_t count = 0; std::optional<c::TerritoryId> last;
            for (; it != value.state.territories.end() && count < limit; ++it,++count) {
                const auto& [id,state] = *it; last = id;
                const auto* authored = value.definition.find(id);
                std::string owner = "null";
                if (state.owner) owner = json(*state.owner == c::TerritoryOwner::Honor ? "honor" :
                    *state.owner == c::TerritoryOwner::Terror ? "terror" : "contested");
                std::cout << "{\"kind\":\"territory\",\"id\":" << id << ",\"name\":" << json(authored->displayName)
                    << ",\"owner\":" << owner << ",\"authored_map\":" << optionalText(authored->mapIdentifier)
                    << ",\"assigned_map\":" << optionalText(state.assignedMap)
                    << ",\"fatigue\":" << metric(state.recon.fatigueVictoryPoints)
                    << ",\"honor\":" << side(state.recon.honor) << ",\"terror\":" << side(state.recon.terror) << "}\n";
            }
            page(it != value.state.territories.end(),last ? std::to_string(*last) : "null");
        } else if (command.name == "battle") {
            battle(store.battle(command.argument));
        } else if (command.name == "events") {
            const auto after = integer(command.get("--after","-1"),-1,std::numeric_limits<int64_t>::max());
            const auto result = store.events(command.argument,after,command.limit());
            for (const auto& event : result.entries)
                std::cout << "{\"kind\":\"event\",\"revision\":" << event.revision << ",\"reason\":" << json(event.reason)
                    << ",\"battle_id\":" << optionalText(event.battleId) << ",\"territories\":" << event.state.territories.size() << "}\n";
            page(result.truncated,result.entries.empty() ? "null" : std::to_string(result.entries.back().revision));
        } else if (command.name == "audit") {
            const auto after = integer(command.get("--after","0"),0,std::numeric_limits<int64_t>::max());
            const auto result = store.adminHistory(command.argument,after,command.limit());
            for (const auto& event : result.entries)
                std::cout << "{\"kind\":\"admin_event\",\"sequence\":" << event.sequence
                    << ",\"campaign\":" << json(event.campaignId) << ",\"action\":" << json(event.action)
                    << ",\"actor\":" << json(event.actor) << ",\"reason\":" << json(event.reason)
                    << ",\"battle_id\":" << optionalText(event.battleId) << ",\"expected_revision\":" << event.expectedRevision
                    << ",\"before_revision\":" << event.beforeRevision << ",\"after_revision\":" << event.afterRevision
                    << ",\"before_state\":" << optionalStatus(event.beforeStatus) << ",\"after_state\":" << optionalStatus(event.afterStatus)
                    << ",\"recorded_unix\":" << event.recordedUnix << "}\n";
            page(result.truncated,result.entries.empty() ? "null" : std::to_string(result.entries.back().sequence));
        } else if (command.name == "backup") {
            store.backupTo(fs::u8path(command.argument));
            std::cout << "{\"kind\":\"backup\",\"created\":true}\n";
        } else if (command.name == "start") {
            store.adminStart(*definition,{},*request);
            std::cout << "{\"kind\":\"started\",\"campaign\":" << json(definition->id()) << ",\"revision\":0}\n";
        } else if (command.name == "reset") {
            const auto revision = store.adminReset(command.argument,{},*request);
            std::cout << "{\"kind\":\"reset\",\"campaign\":" << json(command.argument) << ",\"revision\":" << revision << "}\n";
        } else if (command.name == "cancel-battle") {
            battle(store.adminCancelBattle(command.argument,expectedStatus,*request));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "crusades_admin: " << error.what() << '\n'; return 1;
    }
}
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    (void)argc; (void)argv;
    return tak::utf8Main(run);
#else
    return run(argc,argv);
#endif
}
