#include "server/crusades/campaign.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace tak::srv::crusades {
namespace {
constexpr size_t kMaxDefinitionBytes = 8 * 1024 * 1024;

bool validUtf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) {
            if (lead < 0x20 && lead != '\t' && lead != '\r' && lead != '\n') return false;
            if (lead == 0x7f) return false;
            continue;
        }
        unsigned count;
        uint32_t value, minimum;
        if (lead >= 0xc2 && lead <= 0xdf) { count = 1; value = lead & 0x1f; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { count = 2; value = lead & 0x0f; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; value = lead & 0x07; minimum = 0x10000; }
        else return false;
        while (count--) {
            if (i == text.size()) return false;
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ||
            (value >= 0x80 && value <= 0x9f)) return false;
    }
    return true;
}

struct Line {
    std::string_view text;
    std::string where;
    size_t pos = 0;
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(where + ": " + message);
    }
    void space() { while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r')) ++pos; }
    bool end() { space(); return pos == text.size(); }
    std::string_view word() {
        space(); const size_t start = pos;
        while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\r') ++pos;
        if (start == pos) fail("missing token");
        return text.substr(start, pos - start);
    }
    TerritoryId number() {
        const auto token = word();
        TerritoryId value = 0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || value == 0)
            fail("expected positive 32-bit integer");
        return value;
    }
    std::string quoted() {
        space();
        if (pos == text.size() || text[pos++] != '"') fail("expected quoted string");
        std::string value;
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"') {
                if (value.empty()) fail("empty string");
                if (pos < text.size() && text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\r')
                    fail("expected whitespace after string");
                return value;
            }
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c == '\\') {
                if (pos == text.size()) fail("unterminated escape");
                c = text[pos++];
                if (c != '\\' && c != '"') fail("unsupported string escape");
            }
            value += c;
        }
        fail("unterminated string");
    }
    void finish() { if (!end()) fail("unexpected trailing input"); }
};

void validateOptions(const DefinitionLoadOptions& options) {
    if (options.requireMaps && !options.mapExists)
        throw std::runtime_error("required map validation needs a map resolver");
}

void validateMap(const std::optional<std::string>& map, const DefinitionLoadOptions& options,
                 const std::string& context) {
    if (!map) {
        if (options.requireMaps) throw std::runtime_error(context + ": missing map");
    } else {
        if (map->empty() || !validUtf8(*map) || map->find_first_of("\t\r\n") != std::string::npos)
            throw std::runtime_error(context + ": invalid map identifier");
        if (options.mapExists && !options.mapExists(*map))
            throw std::runtime_error(context + ": unknown map " + *map);
    }
}
} // namespace

const TerritoryDefinition* CampaignDefinition::find(TerritoryId id) const {
    const auto it = territories_.find(id);
    return it == territories_.end() ? nullptr : &it->second;
}

CampaignDefinition loadDefinitionText(std::string_view text,
        const DefinitionLoadOptions& options, const std::string& origin) {
    validateOptions(options);
    if (text.size() > kMaxDefinitionBytes) throw std::runtime_error(origin + ": definition exceeds 8 MiB");
    if (!validUtf8(text)) throw std::runtime_error(origin + ": invalid UTF-8 or control character");
    CampaignDefinition result;
    bool campaignSeen = false;
    struct Pending {
        std::optional<std::string> native, terrain, map;
        std::optional<std::vector<TerritoryId>> neighbors;
    };
    std::map<TerritoryId, Pending> pending;
    size_t lineNumber = 0, offset = 0;
    while (offset < text.size()) {
        const size_t newline = text.find('\n', offset);
        const size_t end = newline == std::string_view::npos ? text.size() : newline;
        Line line{text.substr(offset, end - offset), origin + ":" + std::to_string(++lineNumber)};
        offset = end == text.size() ? end : end + 1;
        if (line.end() || line.text[line.pos] == '#') continue;
        const auto command = line.word();
        if (command == "campaign") {
            if (campaignSeen) line.fail("duplicate campaign declaration");
            campaignSeen = true;
            if (line.number() != 1) line.fail("unsupported campaign format version");
            result.id_ = line.quoted();
            result.displayName_ = line.quoted();
        } else if (command == "territory") {
            const TerritoryId id = line.number();
            TerritoryDefinition territory{id, line.quoted(), {}, {}, {}, {}};
            if (!result.territories_.emplace(id, std::move(territory)).second)
                line.fail("duplicate territory ID " + std::to_string(id));
        } else if (command == "native" || command == "terrain" || command == "map") {
            const TerritoryId id = line.number();
            auto& fields = pending[id];
            auto& field = command == "native" ? fields.native : command == "terrain" ? fields.terrain : fields.map;
            if (field) line.fail("duplicate territory field");
            field = line.quoted();
        } else if (command == "neighbors") {
            const TerritoryId id = line.number();
            auto& neighbors = pending[id].neighbors;
            if (neighbors) line.fail("duplicate adjacency declaration");
            neighbors.emplace();
            while (!line.end()) neighbors->push_back(line.number());
            std::sort(neighbors->begin(), neighbors->end());
            if (std::adjacent_find(neighbors->begin(), neighbors->end()) != neighbors->end())
                line.fail("duplicate neighbor");
        } else line.fail("unknown directive " + std::string(command));
        line.finish();
    }
    if (!campaignSeen) throw std::runtime_error(origin + ": missing campaign declaration");
    if (result.territories_.empty()) throw std::runtime_error(origin + ": campaign has no territories");
    for (auto& [id, fields] : pending) {
        auto it = result.territories_.find(id);
        if (it == result.territories_.end())
            throw std::runtime_error(origin + ": field references unknown territory " + std::to_string(id));
        it->second.nativeFaction = std::move(fields.native);
        it->second.terrain = std::move(fields.terrain);
        it->second.mapIdentifier = std::move(fields.map);
        it->second.neighbors = std::move(fields.neighbors);
    }
    for (const auto& [id, territory] : result.territories_) {
        const std::string context = origin + ": territory " + std::to_string(id);
        validateMap(territory.mapIdentifier, options, context);
        if (!territory.neighbors) {
            if (options.requireAdjacency) throw std::runtime_error(context + ": missing adjacency");
            continue;
        }
        for (const TerritoryId neighbor : *territory.neighbors) {
            if (neighbor == id) throw std::runtime_error(context + ": self adjacency");
            const auto* other = result.find(neighbor);
            if (!other) throw std::runtime_error(context + ": unknown neighbor " + std::to_string(neighbor));
            if (!other->neighbors || !std::binary_search(other->neighbors->begin(), other->neighbors->end(), id))
                throw std::runtime_error(context + ": asymmetric adjacency");
        }
    }
    return result;
}

CampaignDefinition loadDefinition(const std::filesystem::path& path, const DefinitionLoadOptions& options) {
    // Use native filesystem paths; diagnostics don't require lossy Windows path conversion.
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open campaign definition");
    std::string text;
    char buffer[8192];
    while (in.read(buffer, sizeof buffer) || in.gcount() != 0) {
        text.append(buffer, static_cast<size_t>(in.gcount()));
        if (text.size() > kMaxDefinitionBytes) throw std::runtime_error("campaign definition exceeds 8 MiB");
    }
    if (in.bad() || !in.eof()) throw std::runtime_error("cannot read campaign definition");
    return loadDefinitionText(text, options, "<campaign file>");
}

CampaignState makeInitialState(const CampaignDefinition& definition) {
    CampaignState state;
    state.campaignId = definition.id();
    for (const auto& [id, territory] : definition.territories()) {
        (void)territory;
        state.territories.emplace(id, TerritoryState{});
    }
    return state;
}

void validateState(const CampaignDefinition& definition, const CampaignState& state,
        const DefinitionLoadOptions& options) {
    validateOptions(options);
    if (state.campaignId != definition.id()) throw std::runtime_error("state campaign ID mismatch");
    if (state.territories.size() != definition.territories().size())
        throw std::runtime_error("state territory count mismatch");
    for (const auto& [id, territory] : state.territories) {
        if (!definition.find(id)) throw std::runtime_error("state has unknown territory " + std::to_string(id));
        if (territory.owner && *territory.owner != TerritoryOwner::Contested &&
            *territory.owner != TerritoryOwner::Honor && *territory.owner != TerritoryOwner::Terror)
            throw std::runtime_error("state has invalid territory owner");
        validateMap(territory.assignedMap, options, "state territory " + std::to_string(id));
        const auto finite = [](const std::optional<double>& value) {
            if (value && !std::isfinite(*value)) throw std::runtime_error("state has non-finite recon metric");
        };
        finite(territory.recon.fatigueVictoryPoints);
        for (const auto* side : {&territory.recon.honor, &territory.recon.terror}) {
            finite(side->requiredVictoryPoints);
            finite(side->supportVictoryPoints);
            finite(side->battleVictoryPoints);
        }
    }
}

} // namespace tak::srv::crusades
