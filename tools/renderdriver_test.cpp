// renderdriver_test -- the Options RENDERER choice, without a GPU.
//
// Covers the parts that decide what startup asks SDL for: the stored-id normaliser,
// the id -> plain-name mapping, the plan (AUTO untouched, Software's flags, an id this
// build lacks), and the "did SDL actually honour the hint" check that drives the
// fallback to AUTO. Then a real SDL query of this build's driver list, which needs no
// window or GPU.

#include "client/renderdriver.h"

#include <cstdio>
#include <string>
#include <vector>

namespace rd = tak::renderdriver;

static int g_fail = 0;
static void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++g_fail;
}

int main() {
    std::printf("renderdriver_test\n");

    // ---- normalise: what a settings file value becomes ----
    check(rd::normalize("") == "", "empty -> auto");
    check(rd::normalize("auto") == "", "'auto' -> auto");
    check(rd::normalize("  AUTO \r") == "", "'AUTO' with whitespace -> auto");
    check(rd::normalize("OpenGL") == "opengl", "case folded");
    check(rd::normalize(" direct3d11 ") == "direct3d11", "trimmed");
    check(rd::normalize("vulkan") == "vulkan", "unknown but well-formed id kept");
    check(rd::normalize("open gl") == "", "embedded space -> auto");
    check(rd::normalize("gl;rm -rf") == "", "punctuation -> auto");
    check(rd::normalize(std::string(33, 'a')) == "", "over-long -> auto");

    // ---- labels ----
    check(rd::label("") == "Auto", "label: auto");
    check(rd::label("opengl") == "OpenGL", "label: opengl");
    check(rd::label("opengles2") == "OpenGL ES 2", "label: opengles2");
    check(rd::label("opengles") == "OpenGL ES", "label: opengles");
    check(rd::label("direct3d") == "Direct3D 9", "label: direct3d");
    check(rd::label("direct3d11") == "Direct3D 11", "label: direct3d11");
    check(rd::label("direct3d12") == "Direct3D 12", "label: direct3d12");
    check(rd::label("metal") == "Metal", "label: metal");
    check(rd::label("software") == "Software", "label: software");
    check(rd::label("vulkan") == "vulkan", "label: unknown id shown as-is");
    for (const char* id : {"opengl", "opengles2", "opengles", "direct3d", "direct3d11",
                           "direct3d12", "metal", "software", "averyveryverylongid"})
        if (rd::shortLabel(id).size() > 7) { check(false, "short label <= 7 chars"); break; }
    check(rd::shortLabel("software") == "SW" && rd::shortLabel("opengl") == "GL", "short labels");

    // ---- plan ----
    const std::vector<std::string> linuxDrivers{"opengl", "opengles2", "opengles", "software"};
    const Uint32 vs = SDL_RENDERER_PRESENTVSYNC, acc = SDL_RENDERER_ACCELERATED;
    {
        auto p = rd::plan("", linuxDrivers, vs);
        check(p.hint.empty() && p.flags == vs && !p.fallback, "auto: no hint, today's flags");
    }
    {
        auto p = rd::plan("opengles2", linuxDrivers, vs);
        check(p.hint == "opengles2" && p.flags == vs && !p.fallback, "chosen: hint + today's flags");
    }
    {
        auto p = rd::plan("software", linuxDrivers, vs);
        check(p.hint == "software" && p.flags == SDL_RENDERER_SOFTWARE && !p.fallback,
              "software (vsync on): SOFTWARE flag only");
        p = rd::plan("software", linuxDrivers, acc);
        check(p.flags == SDL_RENDERER_SOFTWARE, "software (vsync off): never ACCELERATED");
    }
    {
        auto p = rd::plan("direct3d11", linuxDrivers, vs);
        check(p.fallback && p.hint.empty() && p.flags == vs && !p.reason.empty(),
              "missing from this build: fall back to auto");
        p = rd::plan("vulkan", linuxDrivers, vs);
        check(p.fallback && p.hint.empty(), "unknown id: fall back to auto");
        p = rd::plan("opengl", {}, vs);
        check(p.fallback, "no drivers listed: fall back to auto");
    }
    check(!rd::plan("OPENGL", linuxDrivers, vs).fallback, "case-insensitive availability");

    // ---- honoured: the created renderer is the proof ----
    check(rd::honoured("", "software"), "auto accepts whatever SDL made");
    check(rd::honoured("", nullptr), "auto accepts a null name");
    check(rd::honoured("opengles2", "opengles2"), "hint honoured");
    check(rd::honoured("opengl", "OpenGL"), "hint honoured (case-insensitive)");
    check(!rd::honoured("opengles2", "opengl"), "SDL fell through to another driver -> fallback");
    check(!rd::honoured("software", nullptr), "creation failed -> fallback");

    // ---- one-time notice ----
    {
        auto& a = rd::active();
        a = {};
        check(rd::pendingNotice().empty(), "no failure, no notice");
        a.failed = "opengles2";
        check(rd::pendingNotice() == "OpenGL ES 2 renderer unavailable - using Auto",
              "notice names the failed renderer");
        a.noticeShown = true;
        check(rd::pendingNotice().empty(), "notice shown only once");
        a = {};
    }

    // ---- this SDL build's real list (no window or GPU needed) ----
    {
        SDL_version v{};
        SDL_GetVersion(&v);
        std::printf("  linked SDL %d.%d.%d\n", v.major, v.minor, v.patch);
        auto ids = rd::available();
        std::printf("  SDL %d render driver(s):", int(ids.size()));
        for (auto& id : ids) std::printf(" %s(%s)", id.c_str(), rd::label(id).c_str());
        std::printf("\n");
        bool sane = !ids.empty();
        for (auto& id : ids) sane = sane && rd::normalize(id) == id;
        check(sane, "every SDL driver id survives normalise unchanged");
        bool sw = false;
        for (auto& id : ids) sw = sw || id == "software";
        check(sw, "software is always available");
    }

    std::printf(g_fail ? "renderdriver_test: %d FAILED\n" : "renderdriver_test: all ok\n", g_fail);
    return g_fail ? 1 : 0;
}
