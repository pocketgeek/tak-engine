#pragma once

// HIDDEN UNITS, SHADOW on SDL's OpenGL backend: the stencil form (client/hiddenunits.h
// has the policy and the redraw form other renderers use).
//
// The world is drawn straight into the window's framebuffer, which is asked for an
// 8-bit stencil before any window exists (main.cpp). In the body pass every body
// drawn from the first hidden unit on marks the stencil with whether the topmost
// body at that pixel is a hidden unit (1) or not (0); a cover over such a unit is
// then drawn twice, opaque where the stencil holds 0 and see-through where it holds
// 1, and leaves the stencil alone. The ghost is exact per pixel: no unit is drawn
// again and the part in sight is the OFF frame's.
//
//   Off     no test, no writes (and outside the frame's stencil window: nothing set)
//   Unit    writes 1 where the draw leaves a texel (alpha above 0)
//   Other   writes 0 likewise
//   Under0  draws only where the stencil holds 0, writes nothing
//   Under1  draws only where it holds 1, writes nothing
//
// SDL's GL renderer never touches the stencil or the alpha test, so these states
// hold across SDL's own draws; SDL queues its draws, so a state change first flushes
// what SDL has queued (not inside a native pass: nothing is queued there). The
// alpha test (GREATER 0) only drops fragments that blending would leave unchanged.
//
// Render thread, with the renderer's context current.

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace tak {

class HiddenStencilGL {
public:
    enum class State : uint8_t { Off, Unit, Other, Under0, Under1 };

    // The opengl backend with a stencil on the window's framebuffer (checked once
    // per GL context) and the GL 1.1 entry points.
    bool available(SDL_Renderer* renderer) {
        SDL_RendererInfo info{};
        if (SDL_GetRendererInfo(renderer, &info) != 0 || !info.name || std::strcmp(info.name, "opengl")) return false;
        const auto context = SDL_GL_GetCurrentContext();
        if (!context) return false;
        if (context != context_) {
            context_ = context;
            state_ = State::Off;
            bits_ = 0;
            SDL_RenderFlush(renderer);
            // SDL's query fails on any GL error left pending: clear them first.
            using GetError = GLenum(APIENTRY*)();
            if (auto getError = reinterpret_cast<GetError>(SDL_GL_GetProcAddress("glGetError")))
                for (int i = 0; i < 16 && getError() != GL_NO_ERROR; ++i) {}
            if (SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &bits_) != 0) bits_ = 0;
            available_ = bits_ > 0 && load();
            std::fprintf(stderr, "hidden units: window stencil %d bits: SHADOW %s\n", bits_,
                         available_ ? "by the stencil" : "redraws (no stencil)");
        }
        return available_;
    }
    int bits() const { return bits_; }

    // The stencil of the window cleared to 0 (whole framebuffer: the scissor is
    // lifted for it and put back, so SDL's cached clip stays true).
    void clear(SDL_Renderer* renderer) {
        SDL_RenderFlush(renderer);
        const GLboolean scissor = isEnabled_(GL_SCISSOR_TEST);
        if (scissor) disable_(GL_SCISSOR_TEST);
        stencilMask_(~GLuint(0));
        clearStencil_(0);
        clear_(GL_STENCIL_BUFFER_BIT);
        if (scissor) enable_(GL_SCISSOR_TEST);
    }

    State state() const { return state_; }
    // inPass: inside an open native pass (no SDL call may be made: nothing to flush).
    void use(SDL_Renderer* renderer, State state, bool inPass = false) {
        if (state == state_) return;
        if (!inPass) SDL_RenderFlush(renderer);
        ++changes_;
        const bool test = state != State::Off, write = state == State::Unit || state == State::Other;
        if (test != (state_ != State::Off)) (test ? enable_ : disable_)(GL_STENCIL_TEST);
        const bool wrote = state_ == State::Unit || state_ == State::Other;
        if (write != wrote) {
            if (write) { alphaFunc_(GL_GREATER, 0.0f); enable_(GL_ALPHA_TEST); }
            else disable_(GL_ALPHA_TEST);
        }
        if (test) {
            const GLint ref = state == State::Unit || state == State::Under1 ? 1 : 0;
            stencilFunc_(write ? GL_ALWAYS : GL_EQUAL, ref, 1);
            stencilOp_(GL_KEEP, GL_KEEP, write ? GL_REPLACE : GL_KEEP);
        }
        state_ = state;
    }
    // State changes since the last call (telemetry/tests).
    uint64_t takeChanges() { const uint64_t n = changes_; changes_ = 0; return n; }

private:
    template <class T> static bool proc(T& out, const char* name) {
        out = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
        return out != nullptr;
    }
    bool load() {
        return proc(enable_, "glEnable") && proc(disable_, "glDisable") && proc(isEnabled_, "glIsEnabled") &&
               proc(stencilFunc_, "glStencilFunc") && proc(stencilOp_, "glStencilOp") &&
               proc(stencilMask_, "glStencilMask") && proc(clearStencil_, "glClearStencil") &&
               proc(clear_, "glClear") && proc(alphaFunc_, "glAlphaFunc");
    }
    SDL_GLContext context_ = nullptr;
    bool available_ = false;
    int bits_ = 0;
    State state_ = State::Off;
    uint64_t changes_ = 0;
    void (APIENTRY* enable_)(GLenum) = nullptr;
    void (APIENTRY* disable_)(GLenum) = nullptr;
    GLboolean (APIENTRY* isEnabled_)(GLenum) = nullptr;
    void (APIENTRY* stencilFunc_)(GLenum, GLint, GLuint) = nullptr;
    void (APIENTRY* stencilOp_)(GLenum, GLenum, GLenum) = nullptr;
    void (APIENTRY* stencilMask_)(GLuint) = nullptr;
    void (APIENTRY* clearStencil_)(GLint) = nullptr;
    void (APIENTRY* clear_)(GLbitfield) = nullptr;
    void (APIENTRY* alphaFunc_)(GLenum, GLclampf) = nullptr;
};

}  // namespace tak
