// legion_scenario -- run one .scn scenario (tools/scenarios/README.scn-format.txt)
// in Legion and Retail on the same binary and print its observer keys.
//
//   legion_scenario <file.scn | builtin:NAME>... [--mode legion|retail|both]
//       [--offsets 0,1,-1] [--workers|--serial|--both-exec] [--window R]
//       [--data <install>] [--json] [--ticks N] [--no-observer] [--neutral]
//       [--wanderers on|off]   (fbi types keep / lose Standby_wander; default: the file's)
//       [--check <baseline.json> [--step ID] [--cumulative] [--ratchet "reason"]]
//       [--baseline <baseline.json> --reason "text" [--since ID]]
//       [--exit-table <old.json|run.jsonl>] [--anchor <anchor.json>]
//
// For each mode and each start offset (cells, applied to every spawn) the
// file is built (tools/legion_scn.h), its orders are clicked through
// issueSelection's HUD mirror and uplink (64 commands per tick; with --window
// R, or the file's `uplink R`, at most 512 outstanding over a round trip of R
// ticks), and the world runs `ticks` ticks with the observer
// (tools/legion_observe.h) attached: Observer::sample every tick and the
// NavWork adaptor feeding LegionNavigator::Stats as per-tick work. --both-exec
// (the default) runs serial and workers and fails unless their state hashes,
// checkpoint digests and observer keys are equal.
//
// Output, one line per mode (--json), keys in a fixed order:
//   {"scenario","mode","ticks","units","exec","window","offsets":[..],
//    "hash":{"<offset>":"<16 hex>",..},"serial_eq_workers",
//    "keys":{key: median over offsets},"varying":{key:[value per offset]}}
// "varying" lists only keys whose offsets disagree (the rest equal "keys").
// The median of 3 is the middle value; time keys (t50, t90, done,
// orders_done) read -1 ("never") as the worst value. A work.* key missing
// from an offset counts 0 there. No wall time is printed to stdout, so two
// runs print identical JSON.
//
// Keys beside the observer's:
//   commands             commands landed in the sim
//   last_command_tick    tick the last of them landed
//   legion_groups        Legion only: distinct LegionNavigator groups among
//                        the scenario's units with orders, sampled after the
//                        tick the last command landed
//   legion_groups_peak   Legion only: the most such groups on any tick
//   region.<name>.inside units whose centre cell is in the region at the end
//
// Shapes become observer probes:
//   gate, a segment (x0 == x1 or z0 == z1): a line crossed across it; the
//       probe is an 8-cell strip centred on it (lateral axis = the segment's).
//   gate, a rectangle: a strip travelled along its longer side (the lateral
//       axis is the shorter one).
//   line, vertical or horizontal: a Side at that x (or z); far side >= it.
//   region: region.<name>.inside (above).
// Wall metrics (wall_touch, clearance) need a static mask, so they are
// reported for ascii and flat maps only.
//
// --neutral: instead of keys, checks that the observer cannot change the
// world: per file, mode and offset, serial and workers each with the observer
// attached and detached must give the same hash and digest (and serial and
// workers the same keys). Only --neutral takes several files.
//
// Gate modes (PLAN 3.0 / 3.8; the logic is tools/legion_check.py, kept in one
// place): --check, --baseline, --exit-table and --anchor run the files (several
// allowed, JSON forced) and hand the JSON lines to the checker, which prints
// the verdict; the exit status is the checker's (1 a gate failed, 2 tooling).
// --check compares with a baseline (eq / one-sided bands / bounds / Retail
// floor), --ratchet REASON also writes the improvements into the references
// and a history line, --baseline retakes the base (needs --reason),
// --exit-table lists every key moved over 5% against an older base,
// --anchor prints the drift against the frozen anchor. Mode defaults to both
// (the Retail floor needs Retail beside Legion).
//
// Exit: 0 ok, 1 a mismatch (serial != workers, or --neutral), 2 usage or a
// bad file, 77 the file needs --data (ctest SKIP).
#include "legion_observe.h"
#include "legion_scn.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <sys/wait.h>
#endif

using tak::sim::PathfindingMode;
namespace obs = tak::legion_observe;
namespace scn = tak::scn;

namespace {

// Built-in fixtures (builtin:NAME), for the ctests: a file of the same text
// behaves identically.
const std::map<std::string, const char*> kBuiltins = {
    // An Alt+N formation of 28 movers and 4 flyers round the end of a wall.
    {"formation", R"(scn 1
name formation
ticks 900
map flat 96 64
wall 44 0 4 40
type m mover 2 2500 10 1.8
type f flyer 2 2500 10 2.5
group A 0 m 28 rect 6 30 27 42 squad=f1
group F 0 f 4 rect 6 46 18 49 squad=f1
gate tip 46 40 46 63
line east 70 0 70 64
at 2 move A,F 80 20
)"},
};

struct Options {
    std::vector<std::string> files;   // several only with --neutral
    std::vector<PathfindingMode> modes{PathfindingMode::Legion, PathfindingMode::Retail};
    std::vector<int> offsets{0, 1, -1};
    enum Exec { Serial, Workers, Both } exec = Both;
    int window = -2;        // -2: the file's uplink
    const char* data = nullptr;
    bool json = false, observer = true, neutral = false;
    uint32_t ticks = 0;     // 0: the file's
    int wanderers = -1;     // --wanderers on|off overrides the file's; -1: the file's
    // Gate modes: the JSON lines go to tools/legion_check.py instead of stdout.
    std::string gate;                  // check | baseline | exit-table | anchor | "" (none)
    std::string gateFile;              // the baseline (check, baseline), old base (exit-table), anchor
    std::vector<std::string> gateArgs; // pass-through options of the checker
};

// JSON lines of a gate-mode run; empty capture = print to stdout.
std::string* g_capture = nullptr;
void emit(const std::string& line) {
    if (g_capture) *g_capture += line + "\n";
    else std::puts(line.c_str());
}

[[noreturn]] void usage(const char* why) {
    std::fprintf(stderr, "legion_scenario: %s\n"
        "usage: legion_scenario <file.scn|builtin:NAME> [--mode legion|retail|both] [--offsets 0,1,-1]\n"
        "       [--workers|--serial|--both-exec] [--window R] [--data <install>] [--json] [--ticks N]\n"
        "       [--no-observer] [--neutral] [--wanderers on|off]\n"
        "       [--check B.json [--step ID] [--cumulative] [--ratchet REASON]] [--baseline B.json --reason TEXT\n"
        "       [--since ID]] [--exit-table OLD] [--anchor A.json]   (gate modes: tools/legion_check.py)\n", why);
    std::exit(2);
}

const char* modeName(PathfindingMode m) { return m == PathfindingMode::Retail ? "retail" : "legion"; }

std::string hex(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
    return b;
}

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

bool timeKey(std::string_view k) {
    for (std::string_view s : {".t50", ".t90", ".done", ".orders_done"})
        if (k.size() >= s.size() && k.substr(k.size() - s.size()) == s) return true;
    return false;
}

int cellLo(float v) { return int(std::floor(v)); }

// A lane line: the segment's cells (a horizontal one is laid along x, its lateral cell is x; a vertical one
// along z). A segment of extent covers floor(lo) .. ceil(hi)-1, a point its own cell.
obs::Line laneLine(const scn::Scenario& s, float x0, float z0, float x1, float z1, int sign) {
    auto span = [](float a, float b) {
        const float lo = std::min(a, b), hi = std::max(a, b);
        return std::pair<int, int>{cellLo(lo), hi > lo ? int(std::ceil(hi)) - 1 : cellLo(lo)};
    };
    (void)s;
    obs::Line l;
    const auto [xa, xb] = span(x0, x1);
    const auto [za, zb] = span(z0, z1);
    l.region = {xa, za, xb, zb};
    l.lateral = z0 == z1 ? 0 : 1;
    l.sign = sign;
    return l;
}

// A group list of a `pair` shape: comma separated, a trailing '*' takes every group with that prefix.
std::string expandGroups(const scn::Scenario& s, const std::string& list) {
    std::string out;
    size_t at = 0;
    while (at <= list.size()) {
        size_t e = list.find(',', at);
        if (e == std::string::npos) e = list.size();
        const std::string item = list.substr(at, e - at);
        at = e + 1;
        if (!item.empty() && item.back() == '*') {
            const std::string prefix = item.substr(0, item.size() - 1);
            for (const auto& g : s.groups)
                if (g.name.compare(0, prefix.size(), prefix) == 0) out += (out.empty() ? "" : ",") + g.name;
        } else out += (out.empty() ? "" : ",") + item;
    }
    return out;
}

// The observer's configuration for one built world.
obs::Config configFor(const scn::Scenario& s, const scn::Built& b) {
    const auto& w = *b.world;
    auto centroid = [&](const std::string& g, bool firstOnly) {
        int64_t x = 0, z = 0, n = 0;
        for (int id : b.groups.at(g)) {
            const auto* u = w.unit(id);
            if (!u) continue;
            x += u->x.v >> 16; z += u->z.v >> 16; ++n;
            if (firstOnly) break;
        }
        return n ? std::pair<int, int>{int(x / n), int(z / n)} : std::pair<int, int>{0, 0};
    };
    obs::Config cfg;
    for (const auto& g : s.groups) {
        obs::Group og;
        og.name = g.name;
        og.ids = b.groups.at(g.name);
        std::tie(og.clickX, og.clickZ) = centroid(g.name, false);
        // The last order that moves this group decides its click.
        for (const auto& o : s.orders) {
            if (o.selection[0] != "all" && !std::count(o.selection.begin(), o.selection.end(), g.name)) continue;
            switch (o.verb) {
            case scn::Verb::Move: case scn::Verb::Fight: case scn::Verb::Patrol:
                if (o.point) { og.clickX = int(std::lround(o.x * 16)); og.clickZ = int(std::lround(o.z * 16)); }
                else std::tie(og.clickX, og.clickZ) = centroid(o.target, false);
                break;
            case scn::Verb::Attack: case scn::Verb::Guard:
                std::tie(og.clickX, og.clickZ) = centroid(o.target, true);
                break;
            default: break;
            }
        }
        cfg.groups.push_back(std::move(og));
    }
    for (const auto& sh : s.shapes) {
        const float x0 = std::min(sh.x0, sh.x1), x1 = std::max(sh.x0, sh.x1);
        const float z0 = std::min(sh.z0, sh.z1), z1 = std::max(sh.z0, sh.z1);
        if (sh.kind == "gate") {
            obs::Gate g;
            g.name = sh.name;
            if (x0 == x1) {          // crossed west-east: lateral z
                g.lateral = 1;
                g.region = {cellLo(x0) - 4, cellLo(z0), cellLo(x0) + 3, cellLo(z1)};
            } else if (z0 == z1) {   // crossed north-south: lateral x
                g.lateral = 0;
                g.region = {cellLo(x0), cellLo(z0) - 4, cellLo(x1), cellLo(z0) + 3};
            } else {
                g.lateral = (x1 - x0) >= (z1 - z0) ? 1 : 0;
                g.region = {cellLo(x0), cellLo(z0), int(std::ceil(x1)) - 1, int(std::ceil(z1)) - 1};
            }
            if (sh.lateral >= 0) g.lateral = sh.lateral;
            if (sh.band) g.band = sh.band;
            if (sh.minCount) g.minCount = sh.minCount;
            if (sh.edge) g.edge = sh.edge;
            if (sh.pairWindow) g.pairWindow = sh.pairWindow;
            if (sh.flip) g.flipCells = sh.flip;
            cfg.gates.push_back(g);
        } else if (sh.kind == "lane") {
            obs::LaneOrder l;
            l.name = sh.name;
            l.before = laneLine(s, sh.x0, sh.z0, sh.x1, sh.z1, sh.beforeSign);
            l.after = laneLine(s, sh.ax0, sh.az0, sh.ax1, sh.az1, sh.afterSign);
            if (sh.window) l.window = sh.window;
            if (sh.minCells) l.minCells = sh.minCells;
            l.sameGroup = !sh.acrossGroups;
            cfg.laneOrders.push_back(l);
        } else if (sh.kind == "pair") {
            obs::Pair p;
            p.name = sh.name;
            p.a = expandGroups(s, sh.a);
            p.b = expandGroups(s, sh.b);
            for (const std::string* list : {&p.a, &p.b}) {
                size_t at = 0;
                while (at <= list->size()) {
                    size_t e = list->find(',', at);
                    if (e == std::string::npos) e = list->size();
                    if (list->empty() || !s.group(list->substr(at, e - at)))
                        throw std::runtime_error(s.origin + ": pair '" + sh.name + "' names no group in '" + *list + "'");
                    at = e + 1;
                }
            }
            if (sh.cells) p.cells = sh.cells;
            cfg.pairs.push_back(p);
        } else if (sh.kind == "line") {
            obs::Side side;
            side.name = sh.name;
            if (x0 == x1) { side.axis = 0; side.at = cellLo(x0); }
            else if (z0 == z1) { side.axis = 1; side.at = cellLo(z0); }
            else throw std::runtime_error(s.origin + ": line '" + sh.name + "' must be vertical or horizontal");
            cfg.sides.push_back(side);
        }
    }
    return cfg;
}

// Wall cells of an ascii or flat map (walls never move with the start offset).
std::vector<uint8_t> wallMask(const scn::Scenario& s, int W, int H) {
    std::vector<uint8_t> m(size_t(W) * size_t(H), 0);
    if (s.map.kind == scn::MapSpec::Ascii)
        for (int z = 0; z < H; ++z)
            for (int x = 0; x < W; ++x)
                if (s.map.rows[size_t(z)][size_t(x)] == '#') m[size_t(z) * W + x] = 1;
    for (const auto& v : s.map.overlays)
        if (v.kind == "wall")
            for (int z = v.z; z < std::min(H, v.z + v.h); ++z)
                for (int x = v.x; x < std::min(W, v.x + v.w); ++x) m[size_t(z) * W + x] = 1;
    return m;
}

struct Run {
    std::string skipped;
    uint64_t hash = 0, digest = 0;
    obs::Keys keys;
    double ms = 0;
};

Run runOnce(const scn::Scenario& s, PathfindingMode mode, int offset, bool serial, bool observe,
            const Options& opt) {
    Run r;
    scn::BuildOptions bo;
    bo.mode = mode;
    bo.serial = serial;
    bo.offset = offset;
    bo.data = opt.data;
    auto b = scn::build(s, bo);
    if (!b->skipped.empty()) { r.skipped = b->skipped; return r; }
    auto& w = *b->world;
    scn::OrderFeed feed(s, *b, opt.window);
    std::optional<obs::Observer> o;
    obs::NavWork navWork;
    std::vector<int> all;
    for (const auto& g : s.groups)
        for (int id : b->groups.at(g.name)) all.push_back(id);
    if (observe) {
        o.emplace(configFor(s, *b));
        if (s.map.kind == scn::MapSpec::Ascii || s.map.kind == scn::MapSpec::Flat)
            o->setStaticBlocked(wallMask(s, b->width, b->height), b->width, b->height);
    }
    // The navigator is made on the world's first Legion tick: re-read it.
    const tak::sim::LegionNavigator* nav = nullptr;
    int64_t commands = 0, lastCommand = -1, groupsAfter = -1, groupsPeak = 0;
    std::vector<int> ids;
    auto legionGroups = [&]() -> int64_t {
        ids.clear();
        for (int id : all)
            if (const auto* u = w.unit(id); u && u->alive() && !u->orders.empty())
                if (const int g = nav->unitGroup(id); g) ids.push_back(g);
        std::sort(ids.begin(), ids.end());
        return std::unique(ids.begin(), ids.end()) - ids.begin();
    };
    const auto start = std::chrono::steady_clock::now();
    for (uint32_t t = 0; t < s.ticks; ++t) {
        const size_t landed = feed.apply(t);
        if (landed) { commands += int64_t(landed); lastCommand = t; }
        w.tick(1.f / 30);
        nav = w.legionNavigator();
        if (o) {
            navWork.sample(*o, w);
            o->sample(w, t);
            if (nav) {
                const int64_t n = legionGroups();
                groupsPeak = std::max(groupsPeak, n);
                if (lastCommand == int64_t(t)) groupsAfter = n;
            }
        }
        if ((t + 1) % 100 == 0) r.digest = mix(r.digest, w.stateHash());
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    r.hash = w.stateHash();
    r.digest = mix(r.digest, r.hash);
    if (o) {
        r.keys = o->report();
        r.keys.emplace_back("commands", commands);
        r.keys.emplace_back("last_command_tick", lastCommand);
        if (nav) {
            r.keys.emplace_back("legion_groups", groupsAfter);
            r.keys.emplace_back("legion_groups_peak", groupsPeak);
        }
        for (const auto& sh : s.shapes) {
            if (sh.kind != "region") continue;
            const obs::Rect box{cellLo(sh.x0), cellLo(sh.z0), int(std::ceil(sh.x1)) - 1, int(std::ceil(sh.z1)) - 1};
            int64_t inside = 0;
            for (int id : all)
                if (const auto* u = w.unit(id); u && u->alive())
                    inside += box.contains(int(u->x.v >> 20), int(u->z.v >> 20));
            r.keys.emplace_back("region." + sh.name + ".inside", inside);
        }
    }
    return r;
}

std::string execName(Options::Exec e) {
    return e == Options::Serial ? "serial" : e == Options::Workers ? "workers" : "both";
}

std::vector<int> parseOffsets(const std::string& list) {
    std::vector<int> out;
    size_t at = 0;
    while (at <= list.size()) {
        const size_t comma = std::min(list.find(',', at), list.size());
        const std::string item = list.substr(at, comma - at);
        char* end = nullptr;
        const long v = std::strtol(item.c_str(), &end, 10);
        if (item.empty() || *end || v < -64 || v > 64) usage(("bad offset '" + item + "'").c_str());
        out.push_back(int(v));
        at = comma + 1;
    }
    return out;
}

int runFile(const std::string& file, Options opt);

// Hand the captured JSON lines to tools/legion_check.py (the one place the
// gate logic lives) and return its exit status.
int runChecker(const Options& opt, const std::string& lines) {
#ifdef _WIN32
    (void)opt; (void)lines;
    std::fprintf(stderr, "legion_scenario: gate modes need python3 and a POSIX shell\n");
    return 2;
#else
    namespace fs = std::filesystem;
    fs::path script = fs::path(__FILE__).parent_path() / "legion_check.py";
    if (!fs::exists(script)) script = fs::path("tools") / "legion_check.py";
    if (!fs::exists(script)) {
        std::fprintf(stderr, "legion_scenario: tools/legion_check.py not found\n");
        return 2;
    }
    fs::path tmp = fs::temp_directory_path() /
                   ("legion_check_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    ".jsonl");
    {
        std::ofstream f(tmp);
        f << lines;
        if (!f) {
            std::fprintf(stderr, "legion_scenario: cannot write %s\n", tmp.c_str());
            return 2;
        }
    }
    auto q = [](const std::string& x) {
        std::string o = "'";
        for (char c : x) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return o + "'";
    };
    std::string cmd = "python3 " + q(script.string()) + " " + opt.gate;
    if (opt.gate == "exit-table") cmd += " --base " + q(opt.gateFile);
    else if (opt.gate == "anchor") cmd += " --anchor " + q(opt.gateFile);
    else cmd += " --baseline " + q(opt.gateFile);
    cmd += " --results " + q(tmp.string());
    for (const auto& a : opt.gateArgs) cmd += " " + q(a);
    std::fflush(stdout);
    const int rc = std::system(cmd.c_str());
    std::error_code ec;
    fs::remove(tmp, ec);
    if (rc == -1) return 2;
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 2;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) usage((a + " needs a value").c_str());
            return argv[++i];
        };
        if (a == "--mode") {
            const auto v = value();
            if (v == "legion") opt.modes = {PathfindingMode::Legion};
            else if (v == "retail") opt.modes = {PathfindingMode::Retail};
            else if (v == "both") opt.modes = {PathfindingMode::Legion, PathfindingMode::Retail};
            else usage("--mode legion|retail|both");
        } else if (a == "--offsets") opt.offsets = parseOffsets(value());
        else if (a == "--workers") opt.exec = Options::Workers;
        else if (a == "--serial") opt.exec = Options::Serial;
        else if (a == "--both-exec") opt.exec = Options::Both;
        else if (a == "--window") {
            const auto v = value();
            opt.window = std::atoi(v.c_str());
            if (opt.window < 1 || opt.window > 600) usage("--window R takes 1..600 ticks");
        } else if (a == "--data") { static std::string d; d = value(); opt.data = d.c_str(); }
        else if (a == "--json") opt.json = true;
        else if (a == "--ticks") {
            const long v = std::atol(value().c_str());
            if (v < 1 || v > 10'000'000) usage("--ticks N takes 1..10000000");
            opt.ticks = uint32_t(v);
        } else if (a == "--wanderers") {
            const auto v = value();
            if (v != "on" && v != "off") usage("--wanderers on|off");
            opt.wanderers = v == "on" ? 1 : 0;
        } else if (a == "--check" || a == "--baseline" || a == "--exit-table" || a == "--anchor") {
            if (!opt.gate.empty()) usage("one of --check, --baseline, --exit-table, --anchor");
            opt.gate = a == "--check" ? "check" : a == "--baseline" ? "baseline" : a == "--exit-table" ? "exit-table" : "anchor";
            opt.gateFile = value();
        } else if (a == "--step" || a == "--reason" || a == "--since" || a == "--ratchet" || a == "--intended" ||
                   a == "--intended-file" || a == "--cluster" || a == "--keys") {
            opt.gateArgs.push_back(a);
            opt.gateArgs.push_back(value());
        } else if (a == "--cumulative" || a == "--require-all" || a == "--hashes") {
            opt.gateArgs.push_back(a);
        } else if (a == "--no-observer") opt.observer = false;
        else if (a == "--neutral") opt.neutral = true;
        else if (a == "-h" || a == "--help") usage("help");
        else if (!a.empty() && a[0] == '-') usage(("unknown option " + a).c_str());
        else opt.files.push_back(a);
    }
    if (opt.files.empty()) usage("no scenario file");
    const bool gate = !opt.gate.empty();
    if (opt.files.size() > 1 && !opt.neutral && !gate) usage("one scenario file (several only with --neutral or a gate mode)");
    if (gate && opt.neutral) usage("--neutral and a gate mode do not combine");
    for (size_t i = 0; i + 1 < opt.gateArgs.size(); ++i)
        if (opt.gateArgs[i] == "--ratchet" && opt.gate != "check") usage("--ratchet goes with --check");
    std::string capture;
    if (gate) { opt.json = true; g_capture = &capture; }
    int status = 0, skipped = 0;
    for (const auto& file : opt.files) {
        const int one = runFile(file, opt);
        if (one == 77) { ++skipped; continue; }   // data-gated: the others still run
        if (one) status = status ? status : one;
    }
    if (gate) {
        g_capture = nullptr;
        if (status == 2) return 2;
        const int verdict = runChecker(opt, capture);
        return verdict ? verdict : status;
    }
    return status ? status : skipped == int(opt.files.size()) ? 77 : 0;
}

namespace {
int runFile(const std::string& file, Options opt) {
    scn::Scenario s;
    try {
        if (file.rfind("builtin:", 0) == 0) {
            const auto it = kBuiltins.find(file.substr(8));
            if (it == kBuiltins.end()) usage(("no " + file).c_str());
            s = scn::parse(it->second, file);
        } else {
            s = scn::load(file);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "legion_scenario: %s\n", e.what());
        return 2;
    }
    if (opt.ticks) s.ticks = opt.ticks;
    if (opt.wanderers >= 0) s.wanderers = opt.wanderers == 1;
    const std::string name = !s.name.empty() ? s.name : std::filesystem::path(file).stem().string();
    if (const auto why = s.needsData(); !why.empty() && !opt.data) {
        if (opt.json) emit("{\"scenario\":\"" + name + "\",\"skipped\":\"needs --data: " + why + "\"}");
        else std::printf("%s: SKIPPED (needs --data: %s)\n", name.c_str(), why.c_str());
        return 77;
    }
    const int window = opt.window != -2 ? opt.window : s.roundTrip;
    opt.window = window;
    int units = 0;
    for (const auto& g : s.groups) units += g.count;

    int status = 0;
    try {
        if (opt.neutral) {
            for (auto mode : opt.modes)
                for (int off : opt.offsets) {
                    struct V { bool serial, observe; Run r; };
                    std::vector<V> v{{true, true, {}}, {true, false, {}}, {false, true, {}}, {false, false, {}}};
                    for (auto& x : v) x.r = runOnce(s, mode, off, x.serial, x.observe, opt);
                    bool same = true;
                    for (const auto& x : v) same &= x.r.hash == v[0].r.hash && x.r.digest == v[0].r.digest;
                    same &= v[0].r.keys == v[2].r.keys;   // observer keys: serial == workers
                    std::printf("observer_neutral %s %s offset=%d hash=%s digest=%s %s\n", name.c_str(),
                                modeName(mode), off, hex(v[0].r.hash).c_str(), hex(v[0].r.digest).c_str(),
                                same ? "OK" : "MISMATCH");
                    if (!same) {
                        for (const auto& x : v)
                            std::printf("  %s observer=%s hash=%s digest=%s\n", x.serial ? "serial " : "workers",
                                        x.observe ? "on " : "off", hex(x.r.hash).c_str(), hex(x.r.digest).c_str());
                        status = 1;
                    }
                }
            return status;
        }

        for (auto mode : opt.modes) {
            std::vector<Run> runs;
            bool eq = true;
            for (int off : opt.offsets) {
                Run main = runOnce(s, mode, off, opt.exec != Options::Workers, opt.observer, opt);
                std::fprintf(stderr, "%s %s offset=%d %s %.0f ms\n", name.c_str(), modeName(mode), off,
                             opt.exec == Options::Workers ? "workers" : "serial", main.ms);
                if (opt.exec == Options::Both) {
                    const Run other = runOnce(s, mode, off, false, opt.observer, opt);
                    std::fprintf(stderr, "%s %s offset=%d workers %.0f ms\n", name.c_str(), modeName(mode), off,
                                 other.ms);
                    if (other.hash != main.hash || other.digest != main.digest || other.keys != main.keys) {
                        std::fprintf(stderr, "%s %s offset=%d: serial != workers (hash %s vs %s, digest %s vs %s, keys %s)\n",
                                     name.c_str(), modeName(mode), off, hex(main.hash).c_str(),
                                     hex(other.hash).c_str(), hex(main.digest).c_str(), hex(other.digest).c_str(),
                                     other.keys == main.keys ? "equal" : "differ");
                        eq = false;
                        status = 1;
                    }
                }
                runs.push_back(std::move(main));
            }
            // Keys in first-seen order over the offsets; missing counts 0.
            std::vector<std::string> order;
            std::set<std::string> seen;
            for (const auto& r : runs)
                for (const auto& [k, v] : r.keys)
                    if (seen.insert(k).second) order.push_back(k);
            std::vector<std::pair<std::string, int64_t>> medians;
            std::vector<std::pair<std::string, std::vector<int64_t>>> varying;
            for (const auto& k : order) {
                std::vector<int64_t> vals;
                for (const auto& r : runs) vals.push_back(obs::Observer::get(r.keys, k, 0));
                const bool never = timeKey(k);
                auto sorted = vals;
                for (auto& x : sorted) if (never && x < 0) x = INT64_MAX;
                std::sort(sorted.begin(), sorted.end());
                int64_t med = sorted[(sorted.size() - 1) / 2];
                if (med == INT64_MAX) med = -1;
                medians.emplace_back(k, med);
                if (std::adjacent_find(vals.begin(), vals.end(), std::not_equal_to<>()) != vals.end())
                    varying.emplace_back(k, vals);
            }
            if (opt.json) {
                std::string j = "{\"scenario\":\"" + name + "\",\"mode\":\"" + modeName(mode) +
                                "\",\"ticks\":" + std::to_string(s.ticks) + ",\"units\":" + std::to_string(units) +
                                ",\"exec\":\"" + execName(opt.exec) + "\",\"window\":" + std::to_string(window) +
                                ",\"offsets\":[";
                for (size_t i = 0; i < opt.offsets.size(); ++i) j += (i ? "," : "") + std::to_string(opt.offsets[i]);
                j += "],\"hash\":{";
                for (size_t i = 0; i < runs.size(); ++i)
                    j += (i ? ",\"" : "\"") + std::to_string(opt.offsets[i]) + "\":\"" + hex(runs[i].hash) + "\"";
                j += "},\"serial_eq_workers\":";
                j += opt.exec == Options::Both ? (eq ? "true" : "false") : "null";
                j += ",\"keys\":" + obs::Observer::json(medians) + ",\"varying\":{";
                for (size_t i = 0; i < varying.size(); ++i) {
                    j += (i ? ",\"" : "\"") + varying[i].first + "\":[";
                    for (size_t k = 0; k < varying[i].second.size(); ++k)
                        j += (k ? "," : "") + std::to_string(varying[i].second[k]);
                    j += "]";
                }
                j += "}}";
                emit(j);
            } else {
                std::printf("%s %s: %d units, %u ticks, exec %s, window %d\n", name.c_str(), modeName(mode), units,
                            s.ticks, execName(opt.exec).c_str(), window);
                for (size_t i = 0; i < runs.size(); ++i)
                    std::printf("  offset %+d hash %s\n", opt.offsets[i], hex(runs[i].hash).c_str());
                if (opt.exec == Options::Both) std::printf("  serial == workers: %s\n", eq ? "yes" : "NO");
                for (const auto& [k, v] : medians) {
                    std::printf("  %-40s %lld", k.c_str(), (long long)v);
                    for (const auto& [vk, vals] : varying)
                        if (vk == k) {
                            std::printf("   (");
                            for (size_t i = 0; i < vals.size(); ++i) std::printf("%s%lld", i ? " " : "", (long long)vals[i]);
                            std::printf(")");
                        }
                    std::printf("\n");
                }
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "legion_scenario: %s\n", e.what());
        return 2;
    }
    return status;
}
}  // namespace
