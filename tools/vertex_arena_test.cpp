// Workstream H: the retained vertex arena planner (client/vertexarena.h)
// against a simulated GPU buffer.
//
// Every frame builds painter-ordered commands (texture runs of ranges of unit
// sources, some units with several runs, distant-image quads by value) from a
// scripted scene with stationary stretches, changes, churn, slot reuse,
// reordering, camera motion, budget limits, write failures, resets and a long
// sparse tail. The simulated buffer is reallocated and poisoned on every
// rebuild and remembers which vertices were written since; the test asserts
//   * every write lands on a vertex not written since the storage was
//     (re)allocated (nothing a pending draw may read is ever overwritten),
//   * every command gathered from the buffer through its merged ranges equals
//     the command assembled from the sources (what the streaming path draws),
//   * a frame in which nothing changed uploads nothing,
//   * camera motion streams (no arena write),
//   * capacity stays within the policy and budget, records stay bounded, and
//     the storage shrinks and is released after demand falls.
#include "client/vertexarena.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>

using tak::render::ArenaMode;
using tak::render::ArenaSource;
using tak::render::VertexArenaPlanner;

namespace {
int failures = 0;
void check(bool ok, const char* what, uint64_t frame) {
    if (!ok && failures++ < 20) std::printf("FAIL frame %llu: %s\n", (unsigned long long)frame, what);
}

struct Unit {
    uint32_t slot = 0, generation = 1;
    uint64_t revision = 0;
    std::vector<SDL_Vertex> verts;
    std::vector<uint32_t> runs;    // vertex counts per run (texture changes inside a unit)
    std::array<SDL_Vertex, 6> quad{};
    bool asQuad = false;
    bool alive = true;
    int texture = 0;
};

uint64_t g_revision = 0;
std::mt19937 rng(12345);
int irand(int n) { return int(rng() % uint32_t(n)); }

SDL_Vertex vertex(uint32_t seed) {
    SDL_Vertex v{};
    v.position = {float(seed % 997) * 0.37f, float(seed % 613) * 1.13f};
    v.color = {uint8_t(seed), uint8_t(seed >> 8), uint8_t(seed >> 16), 255};
    v.tex_coord = {float(seed % 101) / 101.f, float(seed % 37) / 37.f};
    return v;
}
void rebuildUnit(Unit& u, bool newCount) {
    u.revision = ++g_revision;
    if (newCount || u.verts.empty()) {
        const int n = 3 * (8 + irand(120));
        u.verts.resize(size_t(n));
        u.runs.clear();
        int left = n;
        while (left > 0) {
            int c = std::min(left, 3 * (1 + irand(60)));
            if (irand(3)) c = left;
            u.runs.push_back(uint32_t(c));
            left -= c;
        }
    }
    for (auto& v : u.verts) v = vertex(uint32_t(rng()));
}
void moveQuad(Unit& u) { for (auto& v : u.quad) v = vertex(uint32_t(rng())); }

struct Range { uint32_t source, offset, count; };
struct Command { int texture; std::vector<Range> ranges; };

bool sameVerts(const SDL_Vertex* a, const SDL_Vertex* b, size_t n) {
    return std::memcmp(a, b, n * sizeof(SDL_Vertex)) == 0;
}
}  // namespace

int main() {
    tak::render::ArenaPolicy policy;
    policy.minCapacity = 4096;
    policy.granule = 1024;
    policy.maxCapacity = 1 << 20;
    policy.idleFrames = 30; policy.idleMs = 500;
    policy.shrinkFrames = 40; policy.shrinkMs = 600;
    policy.retryFrames = 5;
    VertexArenaPlanner planner(policy);

    std::vector<Unit> units;
    uint32_t nextSlot = 0;
    std::vector<uint32_t> freeSlots;
    auto spawn = [&] {
        Unit u;
        if (!freeSlots.empty() && irand(2)) { u.slot = freeSlots.back(); freeSlots.pop_back(); }
        else u.slot = nextSlot++;
        static uint32_t generation = 1;
        u.generation = ++generation;   // a reused slot never repeats a generation
        u.texture = irand(3);
        rebuildUnit(u, true);
        moveQuad(u);
        units.push_back(std::move(u));
    };
    for (int i = 0; i < 400; ++i) spawn();

    // Simulated GPU buffer.
    std::vector<SDL_Vertex> storage;
    std::vector<uint8_t> written;
    const SDL_Vertex poison = vertex(0xdeadbeef);

    uint64_t frames = 0, appendFrames = 0, rebuildFrames = 0, streamFrames = 0, uploaded = 0, retained = 0;
    uint64_t steadyZeroUpload = 0, gathered = 0, commandsChecked = 0, rangesDrawn = 0, failuresInjected = 0;
    uint64_t releasedIdle = 0, shrinks = 0, cameraStreams = 0, quadResident = 0;
    size_t maxRecords = 0, peakCapacity = 0;
    const int kFrames = 2600;
    int lastDisturb = 0;
    for (int frame = 1; frame <= kFrames; ++frame) {
        // ---- scripted scene ----
        const bool camera = (frame >= 700 && frame < 760) || (frame >= 1500 && frame < 1520);
        const bool steady = (frame % 97) < 20 && !camera;   // stretches where nothing changes
        const bool sparse = frame >= 2000;                  // demand falls, then disappears
        if (!steady) {
            for (auto& u : units) {
                if (camera) { rebuildUnit(u, false); moveQuad(u); continue; }
                const int r = irand(1000);
                if (r < 30) rebuildUnit(u, false);              // pose / placement / material
                else if (r < 36) rebuildUnit(u, true);          // vertex count changed
                else if (r < 40) u.asQuad = !u.asQuad;          // distant image on/off
                else if (r < 46 && u.asQuad) moveQuad(u);       // image re-placed
                else if (r < 50) u.texture = irand(3);          // segment structure changes
            }
            if (frame % 50 == 0)                                // churn: destroy, respawn (slot reuse)
                for (int k = 0; k < 20 && !units.empty(); ++k) {
                    const size_t victim = size_t(irand(int(units.size())));
                    freeSlots.push_back(units[victim].slot);
                    units.erase(units.begin() + long(victim));
                    spawn();
                }
            if (frame % 37 == 0 && units.size() > 1)          // depth reorder
                for (int k = 0; k < 30; ++k) std::swap(units[size_t(irand(int(units.size())))], units[size_t(irand(int(units.size())))]);
        }
        if (sparse) {
            const size_t keep = frame >= 2300 ? 0 : (frame >= 2100 ? 12 : 60);
            while (units.size() > keep) { freeSlots.push_back(units.back().slot); units.pop_back(); }
        }
        // visibility churn: a few units leave and re-enter view without changing
        std::vector<const Unit*> visible;
        for (const auto& u : units)
            if (camera || steady || irand(100) >= 3) visible.push_back(&u);

        // ---- painter-ordered commands ----
        planner.begin(uint64_t(frame), uint64_t(frame) * 16);
        std::vector<Command> commands;
        int segTexture = -1;
        for (const Unit* u : visible) {
            const uint32_t source = u->asQuad
                ? planner.add({u->quad.data(), 6, u->slot, u->generation, 0})
                : planner.add({u->verts.data(), uint32_t(u->verts.size()), u->slot, u->generation, u->revision});
            if (frame % 211 == 0 && irand(4) == 0) segTexture = -1;   // a feature closes the segment
            if (u->asQuad) {
                const int tex = 100;
                if (tex != segTexture) { commands.push_back({tex, {}}); segTexture = tex; }
                commands.back().ranges.push_back({source, 0, 6});
                continue;
            }
            uint32_t offset = 0, run = 0;
            for (uint32_t c : u->runs) {
                const int tex = u->texture * 10 + int(run++ % 2);
                if (tex != segTexture) { commands.push_back({tex, {}}); segTexture = tex; }
                commands.back().ranges.push_back({source, offset, c});
                offset += c;
            }
        }
        // ---- plan; inject budget limits and write failures ----
        size_t budget = policy.maxCapacity;
        if (frame >= 1100 && frame < 1130) {                // GPU pressure: the storage yields, no reallocation fits
            budget = 1000;
            if (frame == 1100) { storage.clear(); written.clear(); planner.release(); }
        }
        const bool allowed = !(frame >= 1300 && frame < 1310);
        const size_t capBefore = planner.capacity();
        const ArenaMode mode = planner.plan(allowed, budget);
        bool ok = true;
        if (mode != ArenaMode::Stream) {
            if (planner.reallocate()) {
                check(planner.newCapacity() <= std::min(budget, policy.maxCapacity), "capacity over budget/policy", uint64_t(frame));
                check(planner.newCapacity() >= planner.writeEnd(), "capacity below the frame", uint64_t(frame));
                storage.assign(planner.newCapacity(), poison);
                written.assign(planner.newCapacity(), 0);
                if (planner.newCapacity() < capBefore) ++shrinks;
            }
            for (const auto& up : planner.uploads()) {
                const ArenaSource& s = planner.sources()[up.source];
                check(up.dst + s.count <= storage.size(), "write past the storage", uint64_t(frame));
                check(up.dst >= planner.writeBegin() && up.dst + s.count <= planner.writeEnd(), "write outside the window", uint64_t(frame));
                for (uint32_t k = 0; k < s.count && up.dst + k < storage.size(); ++k) {
                    check(!written[up.dst + k], "overwrote a vertex written since the last allocation", uint64_t(frame));
                    written[up.dst + k] = 1;
                    storage[up.dst + k] = s.data[k];
                }
                uploaded += s.count;
            }
            if (frame % 173 == 0) { ok = false; ++failuresInjected; }   // unmap / upload failure
        }
        // ---- draw: gather every command from the buffer through merged ranges ----
        if (mode != ArenaMode::Stream && ok) {
            for (const auto& c : commands) {
                std::vector<GLint> firsts; std::vector<GLsizei> counts;
                std::vector<SDL_Vertex> expected, drawn;
                for (const auto& r : c.ranges) {
                    const ArenaSource& s = planner.sources()[r.source];
                    expected.insert(expected.end(), s.data + r.offset, s.data + r.offset + r.count);
                    tak::render::appendArenaRange(firsts, counts, 0, planner.first(r.source) + r.offset, r.count);
                    if (planner.resident(r.source)) retained += r.count;
                    if (planner.resident(r.source) && s.revision == 0) ++quadResident;
                }
                for (size_t k = 0; k < firsts.size(); ++k) {
                    check(size_t(firsts[k]) + size_t(counts[k]) <= storage.size(), "range past the storage", uint64_t(frame));
                    for (GLsizei v = 0; v < counts[k]; ++v) {
                        const size_t at = size_t(firsts[k]) + size_t(v);
                        check(at < written.size() && written[at], "drew a vertex not written in this storage", uint64_t(frame));
                        drawn.push_back(at < storage.size() ? storage[at] : poison);
                    }
                }
                rangesDrawn += firsts.size();
                check(drawn.size() == expected.size() && sameVerts(drawn.data(), expected.data(), drawn.size()),
                      "arena command differs from the assembled command", uint64_t(frame));
                gathered += drawn.size();
                ++commandsChecked;
            }
        }
        if (mode == ArenaMode::Append) {
            ++appendFrames;
            // A failure, reset or release before the stretch legitimately rewrites once.
            const bool settled = steady && (frame % 97) > 2 && frame > lastDisturb + 8;
            if (settled && planner.uploads().empty()) ++steadyZeroUpload;
            if (settled) check(planner.uploads().empty(), "a frame with no change uploaded", uint64_t(frame));
        } else if (mode == ArenaMode::Rebuild) ++rebuildFrames;
        else ++streamFrames;
        if (camera && frame > 702 && frame < 760) {
            check(mode == ArenaMode::Stream, "camera motion used the arena", uint64_t(frame));
            cameraStreams += mode == ArenaMode::Stream;
        }
        if (frame >= 1100 && frame < 1130) check(mode == ArenaMode::Stream, "arena past its budget", uint64_t(frame));
        if (!allowed) check(mode == ArenaMode::Stream, "arena while disallowed", uint64_t(frame));
        planner.end(ok);
        // The planner alone must forget the failed storage (no release() here).
        if (!ok) { storage.clear(); written.clear(); lastDisturb = frame; }
        if (frame % 911 == 0) { storage.clear(); written.clear(); planner.release(); lastDisturb = frame; }   // renderer reset
        if (planner.shouldRelease(uint64_t(frame), uint64_t(frame) * 16)) {
            storage.clear(); written.clear(); planner.release(); ++releasedIdle; lastDisturb = frame;
        }
        if (mode == ArenaMode::Stream || !allowed) lastDisturb = std::max(lastDisturb, frame);
        maxRecords = std::max(maxRecords, planner.recordCount());
        peakCapacity = std::max(peakCapacity, planner.capacity());
        check(planner.capacity() <= policy.maxCapacity, "capacity over the policy maximum", uint64_t(frame));
        check(planner.recordCount() <= nextSlot, "records beyond the slot space", uint64_t(frame));
        ++frames;
    }
    // Demand fell to nothing: the storage must be gone, the records trimmed.
    check(planner.capacity() == 0, "storage kept after the view emptied", frames);
    check(planner.recordCount() <= 60, "records not trimmed after demand fell", frames);
    check(appendFrames > 500 && rebuildFrames > 5 && streamFrames > 50, "scene did not exercise every mode", frames);
    check(steadyZeroUpload > 100, "steady frames were not upload-free", frames);
    check(shrinks > 0, "storage never shrank after demand fell", frames);
    check(releasedIdle > 0, "storage never released when idle", frames);
    check(quadResident > 1000, "quads were never kept by value", frames);
    check(failuresInjected > 0 && planner.failures() == failuresInjected, "write failures not recorded", frames);
    std::printf("%s: %llu frames (append %llu, rebuild %llu, stream %llu; camera %llu), %llu commands / %llu vertices "
                "gathered, %llu ranges, uploaded %llu, retained %llu, steady upload-free %llu, shrinks %llu, idle "
                "releases %llu, failures %llu, peak capacity %zu, max records %zu, cpu %zu B\n",
                failures ? "FAIL" : "PASS", (unsigned long long)frames, (unsigned long long)appendFrames,
                (unsigned long long)rebuildFrames, (unsigned long long)streamFrames, (unsigned long long)cameraStreams,
                (unsigned long long)commandsChecked, (unsigned long long)gathered, (unsigned long long)rangesDrawn,
                (unsigned long long)uploaded, (unsigned long long)retained, (unsigned long long)steadyZeroUpload,
                (unsigned long long)shrinks, (unsigned long long)releasedIdle, (unsigned long long)failuresInjected,
                peakCapacity, maxRecords, planner.cpuBytes());
    return failures ? 1 : 0;
}
