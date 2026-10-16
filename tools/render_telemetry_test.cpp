// Shared renderer vocabulary (client/rendertelemetry.h): generational handles
// must never alias a recycled slot, stats must stay inert when off, and the
// RENDERSTATS line keeps the key names tools/render_benchmark.py parses.
#include "client/rendertelemetry.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
}

int main() {
    using namespace tak::render;
    {   // Handles: capacity bound, stale-handle rejection, generation on reuse.
        HandleSlots slots(2);
        const RenderHandle a = slots.acquire(), b = slots.acquire();
        check(a.valid() && b.valid() && !(a == b), "two live handles");
        check(!slots.acquire().valid(), "capacity is a hard bound");
        check(slots.release(a) && !slots.valid(a), "released handle is stale");
        check(!slots.release(a), "double release refused");
        const RenderHandle c = slots.acquire();
        check(c.index == a.index && c.generation != a.generation, "recycled index gets a new generation");
        check(slots.valid(c) && !slots.valid(a), "stale handle cannot alias the new occupant");
        check(slots.live() == 2 && slots.allocated() == 2, "live/allocated accounting");
        check(!slots.valid(RenderHandle{}), "default handle invalid");
        check(!slots.valid(RenderHandle{7, 1}), "out-of-range index invalid");
        // Churn one slot many times: generations never repeat or hit zero.
        RenderHandle last = c;
        for (int i = 0; i < 1000; ++i) {
            slots.release(last);
            const RenderHandle next = slots.acquire();
            check(next.generation != last.generation && next.generation != 0, "generation advances");
            last = next;
        }
    }
    {   // Generations start live at 1; 0 is reserved for "never".
        GenerationCounter g;
        check(g.current() == 1 && g.current() != kNoGeneration, "generation starts at 1");
        const Generation before = g.current();
        check(g.bump() == before + 1 && g.current() == before + 1, "bump advances");
    }
    {   // Off: tallies and phase timers record nothing.
        FrameStats s;
        s.begin(false);
        s.add(Counter::VisibleUnits, 5);
        s.addMiss(MissCause::Cold);
        { ScopedPhase p(s, Phase::GeometryPrep); }
        check(s.get(Counter::VisibleUnits) == 0 && s.miss[0] == 0 && s.ms[size_t(Phase::GeometryPrep)] == 0,
              "disabled stats stay zero");
        // On: values recorded, begin() resets them.
        s.begin(true);
        s.add(Counter::VisibleUnits, 5);
        s.add(Counter::VisibleUnits);
        s.set(Counter::GpuTrackedBytes, 42);
        s.addMiss(MissCause::Owner, 3);
        { ScopedPhase p(s, Phase::BodySubmit); }
        check(s.get(Counter::VisibleUnits) == 6 && s.get(Counter::GpuTrackedBytes) == 42, "enabled tallies");
        check(s.ms[size_t(Phase::BodySubmit)] >= 0, "phase recorded");
        char line[4096];
        const int n = s.format(line, sizeof line);
        const std::string text(line, size_t(n));
        check(text.find(" visible_units=6") != std::string::npos, "counter key");
        check(text.find(" gpu_tracked_bytes=42") != std::string::npos, "last counter key");
        check(text.find(" miss_owner=3") != std::string::npos, "miss key");
        check(text.find(" t_body_submit=") != std::string::npos && text.find(" t_candidates=") != std::string::npos,
              "phase keys");
        s.begin(true);
        check(s.get(Counter::VisibleUnits) == 0, "begin resets");
        char tiny[16];
        s.format(tiny, sizeof tiny);
        check(std::strlen(tiny) < sizeof tiny, "truncated format stays terminated");
    }
    if (failures) return EXIT_FAILURE;
    std::printf("PASS: render telemetry handles, generations and stats\n");
    return EXIT_SUCCESS;
}
