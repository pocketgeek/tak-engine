#pragma once

// Retained body vertices for the native submission path (rendering
// optimization workstream H, docs/render-optimization-progress.md).
//
// Before H every visible body was copied every frame: each unit's screen-space
// vertices (UnitGeom::verts) into one assembled buffer (GameView::bodyVerts_),
// and that buffer again by the driver, which copies GL client arrays when a
// draw is issued (or by SDL, which converts every vertex of a small batch).
// A stationary army was copied twice per frame although almost none of it
// changed.
//
// Here the body pass is an ordered list of commands -- texture + ranges of
// per-unit sources, in painter order, with features, special units and the
// ground/air shadow boundary as command boundaries -- and on a capable
// renderer the sources live in one GL buffer (the ARENA):
//
//   * A source is one unit's body vertices (identity = geometry entry handle,
//     content = the entry's `revision`, which advances whenever any vertex can
//     change and is never repeated) or a distant-image quad (compared by value:
//     6 vertices).
//   * A source whose content is resident keeps its range: nothing is copied or
//     uploaded for it. Changed and new sources are APPENDED behind everything
//     written since the storage was (re)allocated.
//   * When the appended data would not fit, or the storage must grow or shrink,
//     the frame REBUILDS: the storage is orphaned (glBufferData with no data --
//     the driver keeps the old storage for draws still in flight) and every
//     source of the frame is written contiguously in painter order.
//   * A frame whose content is mostly new (camera motion, a moving army) does
//     not use the arena at all: it STREAMS exactly as before H (assembled
//     buffer, client arrays / SDL), and resident ranges stay valid for later.
//
// No range the GPU may still read is ever written: writes go only to vertices
// not written since the storage was last (re)allocated, and every draw reads
// only ranges written in the current storage. So no fence, persistent mapping
// or unsynchronized overwrite is needed; the only GL used is buffer objects
// (GL 1.5: glBufferData to (re)allocate/orphan, glBufferSubData to write), with
// glMultiDrawArrays (GL 1.4) optional. (A glMapBufferRange write window was
// measured and rejected: mapping freshly orphaned storage cost ~23 ms per
// 14 MB rebuild on the reference driver, see the progress document.)
//
// The planner below is pure bookkeeping (no GL), so it is tested on its own
// (tools/vertex_arena_test.cpp) against a simulated buffer. Everything here is
// presentation state on the render thread; nothing reaches the simulation.

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "client/gpuvram.h"

namespace tak::render {

inline constexpr uint32_t kArenaNoSlot = ~0u;     // a source with no retained identity: always written
inline constexpr uint32_t kArenaValueVerts = 6;   // by-value sources: one distant-image quad

struct ArenaSource {
    const SDL_Vertex* data = nullptr;
    uint32_t count = 0;
    uint32_t slot = kArenaNoSlot;   // retained identity: the geometry entry's handle index
    uint32_t generation = 0;        // ... and its handle generation
    // Content revision; 0 = compared by value (count must be kArenaValueVerts).
    // A nonzero revision must change whenever any vertex can change and must
    // never repeat for another content of this slot.
    uint64_t revision = 0;
};

enum class ArenaMode : uint8_t { Stream, Append, Rebuild };
enum class ArenaStreamReason : uint8_t { None, Disallowed, Empty, Retry, Unstable, Budget };

struct ArenaPolicy {
    size_t minCapacity = size_t(1) << 16;                                // vertices (1.25 MiB)
    size_t maxCapacity = (size_t(64) << 20) / sizeof(SDL_Vertex);        // 64 MiB, as GeometrySubmit's retained buffers
    size_t granule = size_t(1) << 14;                                    // capacity rounding (vertices)
    double headroom = 2.0;          // capacity on (re)allocation = this frame's vertices x headroom
    double minHeadroom = 1.125;     // a capacity below this frame x minHeadroom is not worth it: stream
    // Retain only when at least this fraction of the frame's vertices is resident
    // or unchanged since the previous frame (otherwise the frame streams).
    double stableFraction = 0.5;
    uint64_t idleFrames = 120, idleMs = 2000;      // storage released after no arena frame for both
    uint64_t shrinkFrames = 300, shrinkMs = 5000;  // reallocated smaller after capacity > ratio x demand for both
    double shrinkRatio = 4.0;
    uint64_t retryFrames = 120;                    // after a failed allocation or write
};

struct ArenaUpload { uint32_t source; uint32_t dst; };

struct ArenaFrameStats {
    ArenaMode mode = ArenaMode::Stream;
    ArenaStreamReason reason = ArenaStreamReason::None;
    size_t sources = 0, verts = 0, residentVerts = 0, stableVerts = 0, uploadedVerts = 0;
};

class VertexArenaPlanner {
public:
    explicit VertexArenaPlanner(ArenaPolicy policy = {}) : policy_(policy) {}
    const ArenaPolicy& policy() const { return policy_; }
    void setPolicy(const ArenaPolicy& p) { policy_ = p; }

    // ---- per frame: begin, add every source in painter order, plan, write, end ----
    void begin(uint64_t frame, uint64_t nowMs) {
        frame_ = frame; nowMs_ = nowMs;
        sources_.clear(); firsts_.clear(); resident_.clear(); uploads_.clear();
        stats_ = {}; mode_ = ArenaMode::Stream; writeBegin_ = writeEnd_ = 0;
        maxSlot_ = 0; planned_ = false;
    }
    uint32_t add(const ArenaSource& s) {
        sources_.push_back(s);
        if (s.slot != kArenaNoSlot) maxSlot_ = std::max<size_t>(maxSlot_, size_t(s.slot) + 1);
        return uint32_t(sources_.size() - 1);
    }
    // allowed: the caller can draw from the arena this frame. budget: the most
    // vertices the storage may hold if it is (re)allocated now (GPU budget).
    ArenaMode plan(bool allowed, size_t budget) {
        planned_ = true;
        size_t total = 0, resident = 0, stable = 0;
        resident_.assign(sources_.size(), 0);
        firsts_.assign(sources_.size(), 0);
        for (size_t i = 0; i < sources_.size(); ++i) {
            const ArenaSource& s = sources_[i];
            total += s.count;
            if (s.slot == kArenaNoSlot || s.slot >= records_.size()) continue;
            const Record& r = records_[s.slot];
            if (isResident(r, s)) {
                resident_[i] = 1; firsts_[i] = r.offset;
                resident += s.count; stable += s.count;
            } else if (isStable(r, s)) stable += s.count;
        }
        stats_.sources = sources_.size(); stats_.verts = total;
        stats_.residentVerts = resident; stats_.stableVerts = stable;
        auto stream = [&](ArenaStreamReason why) {
            mode_ = ArenaMode::Stream; stats_.mode = mode_; stats_.reason = why;
            return mode_;
        };
        if (!allowed) return stream(ArenaStreamReason::Disallowed);
        if (total == 0) return stream(ArenaStreamReason::Empty);
        if (frame_ < retryFrame_) return stream(ArenaStreamReason::Retry);
        if (double(stable) < policy_.stableFraction * double(total)) return stream(ArenaStreamReason::Unstable);
        const size_t dirty = total - resident;
        const bool shrink = shrinkDue();
        if (capacity_ > 0 && top_ + dirty <= capacity_ && !shrink) {
            mode_ = ArenaMode::Append;
            uint32_t at = uint32_t(top_);
            writeBegin_ = top_;
            for (size_t i = 0; i < sources_.size(); ++i) {
                if (resident_[i]) continue;
                uploads_.push_back({uint32_t(i), at});
                firsts_[i] = at; at += sources_[i].count;
            }
            writeEnd_ = at;
        } else {
            const size_t need = size_t(std::ceil(double(total) * policy_.minHeadroom));
            size_t want = size_t(std::ceil(double(total) * policy_.headroom));
            want = (want + policy_.granule - 1) / policy_.granule * policy_.granule;
            want = std::clamp(want, policy_.minCapacity, std::max(policy_.minCapacity, policy_.maxCapacity));
            // Keep the present size when it holds this frame with headroom and is
            // not oversized: the rebuild then only orphans (no new size).
            size_t cap = capacity_ >= need && !shrink && capacity_ <= policy_.maxCapacity ? capacity_ : want;
            cap = std::min(cap, budget);
            if (cap < need || cap > policy_.maxCapacity) return stream(ArenaStreamReason::Budget);
            mode_ = ArenaMode::Rebuild;
            newCapacity_ = cap;
            writeBegin_ = 0;
            uint32_t at = 0;
            for (size_t i = 0; i < sources_.size(); ++i) {
                uploads_.push_back({uint32_t(i), at});
                firsts_[i] = at; at += sources_[i].count;
            }
            writeEnd_ = at;
        }
        stats_.mode = mode_;
        stats_.uploadedVerts = writeEnd_ - writeBegin_;
        return mode_;
    }
    ArenaMode mode() const { return mode_; }
    const ArenaFrameStats& stats() const { return stats_; }
    // Valid after plan() for Append/Rebuild:
    bool reallocate() const { return mode_ == ArenaMode::Rebuild; }
    size_t newCapacity() const { return newCapacity_; }      // vertices (Rebuild)
    size_t writeBegin() const { return writeBegin_; }
    size_t writeEnd() const { return writeEnd_; }
    std::span<const ArenaUpload> uploads() const { return uploads_; }
    std::span<const ArenaSource> sources() const { return sources_; }
    uint32_t first(uint32_t source) const { return firsts_[source]; }
    bool resident(uint32_t source) const { return resident_[source] != 0; }

    // The frame's write succeeded (ok) or failed: storage contents unknown, so
    // nothing is resident and the arena stays off for retryFrames. Records the
    // frame's observations either way (the stable test of the next frame).
    void end(bool ok) {
        if (!planned_) plan(false, 0);
        const bool wrote = mode_ != ArenaMode::Stream && ok;
        if (mode_ != ArenaMode::Stream && !ok) {
            invalidateStorage();
            retryFrame_ = frame_ + policy_.retryFrames;
            ++failures_;
        }
        if (wrote) {
            if (mode_ == ArenaMode::Rebuild) {
                epoch_ = nextEpoch_++;
                capacity_ = newCapacity_;
                ++rebuilds_;
                trimRecords();
            }
            top_ = writeEnd_;
            lastUseFrame_ = frame_; lastUseMs_ = nowMs_;
            // Shrink tracking: demand well below capacity for a sustained time.
            if (capacity_ > policy_.minCapacity &&
                double(capacity_) > policy_.shrinkRatio * double(stats_.verts)) {
                if (!shrinkSince_) { shrinkSince_ = true; shrinkSinceFrame_ = frame_; shrinkSinceMs_ = nowMs_; }
            } else shrinkSince_ = false;
        }
        if (maxSlot_ > records_.size()) records_.resize(maxSlot_);
        for (size_t i = 0; i < sources_.size(); ++i) {
            const ArenaSource& s = sources_[i];
            if (s.slot == kArenaNoSlot) continue;
            Record& r = records_[s.slot];
            const bool byValue = s.revision == 0;
            const bool uploaded = wrote && (mode_ == ArenaMode::Rebuild || !resident_[i]);
            if (uploaded) {
                r.epoch = epoch_; r.offset = uint32_t(firsts_[i]); r.count = s.count;
                r.generation = s.generation; r.revision = s.revision; r.byValue = byValue;
            }
            if (byValue) {
                if (s.count != kArenaValueVerts) { r.byValue = false; r.epoch = 0; continue; }
                if (values_.size() <= s.slot) values_.resize(size_t(s.slot) + 1);
                auto& v = values_[s.slot];
                const bool same = r.seenByValue && std::memcmp(v.data(), s.data, sizeof(v)) == 0;
                if (uploaded) r.valueResident = true;
                else if (!same) r.valueResident = false;   // the resident range no longer holds this value
                if (!same) std::memcpy(v.data(), s.data, sizeof(v));
            }
            r.seenFrame = frame_; r.seenGeneration = s.generation; r.seenRevision = s.revision;
            r.seenCount = s.count; r.seenByValue = byValue;
        }
        planned_ = false;
    }

    // ---- storage lifetime (the caller owns the GL buffer) ----
    // No arena frame for idleFrames and idleMs: release the storage.
    bool shouldRelease(uint64_t frame, uint64_t nowMs) const {
        return capacity_ > 0 && frame >= lastUseFrame_ + policy_.idleFrames &&
               nowMs >= lastUseMs_ + policy_.idleMs;
    }
    // The storage was released or lost (reset, context loss, idle, budget).
    void release() { invalidateStorage(); trimRecords(); }
    size_t capacity() const { return capacity_; }   // vertices of the current storage (0 = none)
    size_t top() const { return top_; }             // vertices written since the storage was (re)allocated
    uint32_t epoch() const { return epoch_; }
    uint64_t rebuilds() const { return rebuilds_; }
    uint64_t failures() const { return failures_; }
    size_t cpuBytes() const {
        return records_.capacity() * sizeof(Record) + values_.capacity() * sizeof(values_[0]) +
               sources_.capacity() * sizeof(ArenaSource) + firsts_.capacity() * sizeof(uint32_t) +
               resident_.capacity() + uploads_.capacity() * sizeof(ArenaUpload);
    }
    size_t recordCount() const { return records_.size(); }

private:
    struct Record {
        // Resident content (valid while epoch == the storage epoch).
        uint32_t epoch = 0, offset = 0, count = 0, generation = 0;
        uint64_t revision = 0;
        bool byValue = false, valueResident = false;
        // Last observation, resident or not: the stable test.
        bool seenByValue = false;
        uint32_t seenGeneration = 0, seenCount = 0;
        uint64_t seenRevision = 0, seenFrame = ~uint64_t(0);
    };
    bool isResident(const Record& r, const ArenaSource& s) const {
        if (epoch_ == 0 || r.epoch != epoch_ || r.count != s.count) return false;
        if (s.revision == 0)
            return s.count == kArenaValueVerts && r.byValue && r.valueResident && s.slot < values_.size() &&
                   std::memcmp(values_[s.slot].data(), s.data, sizeof(values_[0])) == 0;
        return !r.byValue && r.generation == s.generation && r.revision == s.revision;
    }
    bool isStable(const Record& r, const ArenaSource& s) const {
        if (r.seenFrame + 1 != frame_ || r.seenCount != s.count) return false;
        if (s.revision == 0)
            return r.seenByValue && s.count == kArenaValueVerts && s.slot < values_.size() &&
                   std::memcmp(values_[s.slot].data(), s.data, sizeof(values_[0])) == 0;
        return !r.seenByValue && r.seenGeneration == s.generation && r.seenRevision == s.revision;
    }
    bool shrinkDue() const {
        return shrinkSince_ && frame_ >= shrinkSinceFrame_ + policy_.shrinkFrames &&
               nowMs_ >= shrinkSinceMs_ + policy_.shrinkMs;
    }
    void invalidateStorage() {
        epoch_ = 0; capacity_ = 0; top_ = 0; shrinkSince_ = false;
        for (auto& r : records_) { r.epoch = 0; r.valueResident = false; }
    }
    // Records past the highest slot of this frame describe entries not drawn
    // now; after a rebuild or release none of them is resident, so they go.
    void trimRecords() {
        if (records_.size() > maxSlot_) records_.resize(maxSlot_);
        if (values_.size() > maxSlot_) values_.resize(maxSlot_);
        if (records_.capacity() > 2 * records_.size() + 256) records_.shrink_to_fit();
        if (values_.capacity() > 2 * values_.size() + 256) values_.shrink_to_fit();
    }

    ArenaPolicy policy_;
    std::vector<Record> records_;                                    // by slot
    std::vector<std::array<SDL_Vertex, kArenaValueVerts>> values_;  // by slot: last seen by-value content
    std::vector<ArenaSource> sources_;
    std::vector<uint32_t> firsts_;
    std::vector<uint8_t> resident_;
    std::vector<ArenaUpload> uploads_;
    ArenaFrameStats stats_;
    ArenaMode mode_ = ArenaMode::Stream;
    uint64_t frame_ = 0, nowMs_ = 0;
    size_t capacity_ = 0, top_ = 0, newCapacity_ = 0, writeBegin_ = 0, writeEnd_ = 0, maxSlot_ = 0;
    uint32_t epoch_ = 0, nextEpoch_ = 1;   // 0 = no storage; epochs are never reused
    uint64_t lastUseFrame_ = 0, lastUseMs_ = 0, retryFrame_ = 0;
    bool shrinkSince_ = false, planned_ = false;
    uint64_t shrinkSinceFrame_ = 0, shrinkSinceMs_ = 0;
    uint64_t rebuilds_ = 0, failures_ = 0;
};

// The arena's storage: one GL buffer object (or, simulated, CPU memory for
// tests and the software renderer). Render thread only. Writes come from a
// caller-owned CPU copy (GameView stages them in the tail of its assembled
// body buffer, so they need no memory of their own) and go up with one
// glBufferSubData per frame.
//
// GPU memory: the storage is counted in gpuvram while it lives; when it is
// orphaned (reallocated) or released, its bytes stay counted for
// kInFlightFrames more frames -- the driver keeps an orphaned store until the
// draws that read it have executed. That is an estimate (the driver's own
// retention is not observable); it is never undercounted for a frame in which
// the GPU may still read it.
class VertexArenaBuffer {
public:
    static constexpr uint32_t kInFlightFrames = 3;
    VertexArenaBuffer() = default;
    VertexArenaBuffer(const VertexArenaBuffer&) = delete;
    VertexArenaBuffer& operator=(const VertexArenaBuffer&) = delete;
    ~VertexArenaBuffer() { release(); dropInFlight(); }

    // Simulated storage (debug / software renderer / tests): CPU memory, never
    // counted as GPU memory. Changing the mode releases the storage.
    void setSimulated(bool on) { if (on != simulated_) { release(); dropInFlight(); simulated_ = on; } }
    bool simulated() const { return simulated_; }

    // Can the arena run now? The current GL context has buffer objects. A new
    // context (context loss) forgets the old buffer without touching it.
    bool ready() {
        if (simulated_) return true;
        const SDL_GLContext context = SDL_GL_GetCurrentContext();
        if (!context) return false;
        if (!checked_ || context != context_) { forget(); checked_ = true; context_ = context; available_ = load(); }
        return available_;
    }
    // (Re)allocate the storage at `bytes`. The previous storage is orphaned:
    // pending draws keep reading it, nothing written from now on can reach it.
    bool allocate(size_t bytes) {
        retire();
        if (simulated_) {
            storage_.assign(bytes / sizeof(SDL_Vertex), SDL_Vertex{});
            capacity_ = bytes; ++allocations_;
            return true;
        }
        if (!ready()) return false;
        if (!buffer_) genBuffers_(1, &buffer_);
        if (!buffer_) return false;
        while (getError_() != GL_NO_ERROR) {}
        GLint previous = 0; getInteger_(GL_ARRAY_BUFFER_BINDING, &previous);
        bindBuffer_(GL_ARRAY_BUFFER, buffer_);
        bufferData_(GL_ARRAY_BUFFER, GLsizeiptr(bytes), nullptr, GL_DYNAMIC_DRAW);
        GLint allocated = 0; getBufferParameter_(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &allocated);
        const bool failed = getError_() == GL_OUT_OF_MEMORY || size_t(allocated) != bytes;
        bindBuffer_(GL_ARRAY_BUFFER, GLuint(previous));
        if (failed) {
            deleteBuffers_(1, &buffer_); buffer_ = 0;
            gpuvram::noteFail(); ++allocationFailures_;
            return false;
        }
        capacity_ = bytes; gpuvram::addBuffer(bytes); ++allocations_;
        return true;
    }
    // Write `count` vertices at vertex `first`. The planner only writes past
    // what the storage holds since its last (re)allocation, so no pending draw
    // reads the range and the upload needs no synchronization. false: the
    // storage contents are lost (upload error) -- the caller must not draw
    // from it and releases it.
    bool write(size_t first, const SDL_Vertex* data, size_t count) {
        const size_t offset = first * sizeof(SDL_Vertex), bytes = count * sizeof(SDL_Vertex);
        if (offset + bytes > capacity_) return false;
        if (!count) return true;
        ++writes_;
        if (simulated_) { std::memcpy(storage_.data() + first, data, bytes); return true; }
        if (!buffer_) return false;
        while (getError_() != GL_NO_ERROR) {}
        GLint previous = 0; getInteger_(GL_ARRAY_BUFFER_BINDING, &previous);
        bindBuffer_(GL_ARRAY_BUFFER, buffer_);
        bufferSubData_(GL_ARRAY_BUFFER, GLintptr(offset), GLsizeiptr(bytes), data);
        const bool ok = getError_() == GL_NO_ERROR;
        bindBuffer_(GL_ARRAY_BUFFER, GLuint(previous));
        return ok;
    }
    // Release the storage (its bytes stay counted while draws may be in flight).
    void release() {
        if (simulated_) { std::vector<SDL_Vertex>().swap(storage_); capacity_ = 0; return; }
        retire();
        if (buffer_ && SDL_GL_GetCurrentContext() == context_ && deleteBuffers_) deleteBuffers_(1, &buffer_);
        buffer_ = 0;
    }
    // The context is gone (device reset / new context): its objects died with
    // it, so nothing is deleted and nothing stays counted.
    void forget() {
        gpuvram::removeBuffer(capacity_); capacity_ = 0; buffer_ = 0; dropInFlight();
        std::vector<SDL_Vertex>().swap(storage_);
    }
    // Once per frame: in-flight bytes of orphaned/released storage expire.
    void endFrame() {
        size_t kept = 0;
        for (auto& r : retired_) {
            if (--r.framesLeft == 0) gpuvram::removeBuffer(r.bytes);
            else retired_[kept++] = r;
        }
        retired_.resize(kept);
    }

    GLuint id() const { return buffer_; }
    const SDL_Vertex* simulatedData() const { return storage_.data(); }
    size_t capacityBytes() const { return capacity_; }
    size_t inFlightBytes() const { size_t b = 0; for (const auto& r : retired_) b += r.bytes; return b; }
    uint64_t allocations() const { return allocations_; }
    uint64_t allocationFailures() const { return allocationFailures_; }
    uint64_t writes() const { return writes_; }

private:
    struct Retired { size_t bytes; uint32_t framesLeft; };
    void retire() {
        if (!simulated_ && capacity_) retired_.push_back({capacity_, kInFlightFrames});
        capacity_ = 0;
    }
    void dropInFlight() {
        for (const auto& r : retired_) gpuvram::removeBuffer(r.bytes);
        retired_.clear();
    }
    template <class T> static bool proc(T& out, const char* name) {
        out = reinterpret_cast<T>(SDL_GL_GetProcAddress(name)); return out != nullptr;
    }
    bool load() {
        if (!proc(genBuffers_, "glGenBuffers") || !proc(deleteBuffers_, "glDeleteBuffers") ||
            !proc(bindBuffer_, "glBindBuffer") || !proc(bufferData_, "glBufferData") ||
            !proc(bufferSubData_, "glBufferSubData") || !proc(getBufferParameter_, "glGetBufferParameteriv") ||
            !proc(getInteger_, "glGetIntegerv") || !proc(getError_, "glGetError"))
            return false;
        return true;
    }

    bool simulated_ = false, checked_ = false, available_ = false;
    SDL_GLContext context_ = nullptr;
    GLuint buffer_ = 0;
    size_t capacity_ = 0;
    std::vector<SDL_Vertex> storage_;
    std::vector<Retired> retired_;
    uint64_t allocations_ = 0, allocationFailures_ = 0, writes_ = 0;
    void (APIENTRY* genBuffers_)(GLsizei, GLuint*) = nullptr;
    void (APIENTRY* deleteBuffers_)(GLsizei, const GLuint*) = nullptr;
    void (APIENTRY* bindBuffer_)(GLenum, GLuint) = nullptr;
    void (APIENTRY* bufferData_)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (APIENTRY* bufferSubData_)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
    void (APIENTRY* getBufferParameter_)(GLenum, GLenum, GLint*) = nullptr;
    void (APIENTRY* getInteger_)(GLenum, GLint*) = nullptr;
    GLenum (APIENTRY* getError_)() = nullptr;
};

// Merge a command's arena ranges: consecutive ranges that continue each other
// become one (a rebuilt arena holds a run of painter-ordered sources in one
// piece). Appends to firsts/counts; returns the number of merged ranges added.
inline size_t appendArenaRange(std::vector<GLint>& firsts, std::vector<GLsizei>& counts, size_t commandBegin,
                               uint32_t first, uint32_t count) {
    if (count == 0) return 0;
    if (firsts.size() > commandBegin && size_t(firsts.back()) + size_t(counts.back()) == first) {
        counts.back() += GLsizei(count);
        return 0;
    }
    firsts.push_back(GLint(first)); counts.push_back(GLsizei(count));
    return 1;
}

}  // namespace tak::render
