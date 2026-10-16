#pragma once

// Telemetry only: which stage's inputs changed between two exact unit-geometry
// keys (GameView::buildUnitGeom). The key is an exact word vector, never a hash;
// this reads its layout to say WHY a retained entry could not be reused. It does
// not decide reuse -- the whole-key comparison still does -- so a mistake here can
// only mislabel a miss, never change a frame.
//
// Key layout (keep in step with buildUnitGeom):
//   [0, kGeometryKeyFixedWords)  fixed words, one cause each (kGeometryKeyWordCause)
//   [kGeometryKeyFixedWords]     animation clock, only while conjuring
//   [textureBegin, poseBegin)    animated texture frame indices (or the game tick
//                                under TAK_TEXTURE_TICK_REFERENCE)
//   [poseBegin]                  piece-name table pointer (only with a VM pose)
//   (poseBegin, end)             piece poses
// textureBegin/poseBegin are recorded per key because the conjuring word and the
// frame list vary in length.

#include "client/rendertelemetry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tak::render {

inline constexpr size_t kGeometryKeyFixedWords = 26;
inline constexpr MissCause kGeometryKeyWordCause[kGeometryKeyFixedWords] = {
    MissCause::Owner,                                           // 0 unit id
    MissCause::Resource, MissCause::Resource, MissCause::Resource, // 1 type, 2 visual, 3 atlas
    MissCause::Material, MissCause::Material,                   // 4 player, 5 colour slot
    MissCause::Pose, MissCause::Pose,                           // 6 body pitch, 7 roll
    MissCause::Material, MissCause::Material, MissCause::Material, // 8 veteran, 9 under construction, 10 ghost
    MissCause::Pose, MissCause::Pose,                           // 11 corpse phase, 12 statue
    MissCause::Settings,                                        // 13 shadows on
    MissCause::Material, MissCause::Material, MissCause::Material, // 14 conjuring, 15 emote, 16 emote colour
    MissCause::Zoom,                                            // 17 zoom
    MissCause::Placement, MissCause::Placement,                 // 18 anchor x, 19 anchor y
    MissCause::Placement,                                       // 20 wall occlusion line
    MissCause::Pose, MissCause::Pose,                           // 21 altitude, 22 facing
    MissCause::Material, MissCause::Material,                   // 23 emote bob, 24 emote mix
    MissCause::Material,                                        // 25 opacity / build progress
};
inline constexpr size_t kGeometryKeyConjuringWord = 14;

inline constexpr uint32_t missBit(MissCause c) { return 1u << unsigned(c); }

struct GeometryKeyLayout {
    uint32_t textureBegin = 0, poseBegin = 0;
};

// Bit set of MissCause values whose inputs differ. Owner differences are
// reported alone (an owner change makes every other comparison meaningless).
inline uint32_t geometryKeyChanges(std::span<const uint64_t> before, GeometryKeyLayout beforeLayout,
                                   std::span<const uint64_t> after, GeometryKeyLayout afterLayout) {
    if (before.size() < kGeometryKeyFixedWords || after.size() < kGeometryKeyFixedWords)
        return missBit(MissCause::Changed);
    if (before[0] != after[0]) return missBit(MissCause::Owner);
    uint32_t mask = 0;
    for (size_t i = 1; i < kGeometryKeyFixedWords; ++i)
        if (before[i] != after[i]) mask |= missBit(kGeometryKeyWordCause[i]);
    const bool layoutOk = beforeLayout.textureBegin <= beforeLayout.poseBegin &&
        beforeLayout.poseBegin <= before.size() && afterLayout.textureBegin <= afterLayout.poseBegin &&
        afterLayout.poseBegin <= after.size() && beforeLayout.textureBegin >= kGeometryKeyFixedWords &&
        afterLayout.textureBegin >= kGeometryKeyFixedWords;
    if (!layoutOk) return mask | missBit(MissCause::Changed);
    // Conjuring clock: present in both only when both were conjuring (word 14
    // equal and set); otherwise the conjuring flag itself already differed.
    if (beforeLayout.textureBegin == afterLayout.textureBegin &&
        afterLayout.textureBegin > kGeometryKeyFixedWords &&
        before[kGeometryKeyFixedWords] != after[kGeometryKeyFixedWords])
        mask |= missBit(MissCause::Material);
    const auto range = [](std::span<const uint64_t> key, size_t b, size_t e) { return key.subspan(b, e - b); };
    const auto texBefore = range(before, beforeLayout.textureBegin, beforeLayout.poseBegin);
    const auto texAfter = range(after, afterLayout.textureBegin, afterLayout.poseBegin);
    if (!std::equal(texBefore.begin(), texBefore.end(), texAfter.begin(), texAfter.end()))
        mask |= missBit(MissCause::Material);
    const auto poseBefore = before.subspan(beforeLayout.poseBegin);
    const auto poseAfter = after.subspan(afterLayout.poseBegin);
    if (poseBefore.empty() != poseAfter.empty()) mask |= missBit(MissCause::Resource);
    else if (!poseBefore.empty()) {
        if (poseBefore[0] != poseAfter[0]) mask |= missBit(MissCause::Resource);   // piece table
        if (!std::equal(poseBefore.begin() + 1, poseBefore.end(), poseAfter.begin() + 1, poseAfter.end()))
            mask |= missBit(MissCause::Pose);
    }
    return mask ? mask : missBit(MissCause::Changed);
}

// The single cause a miss is filed under: the most fundamental stage that changed.
inline MissCause primaryMissCause(uint32_t mask) {
    for (MissCause c : {MissCause::Owner, MissCause::Resource, MissCause::Settings, MissCause::Pose,
                        MissCause::Material, MissCause::Zoom, MissCause::Placement})
        if (mask & missBit(c)) return c;
    return MissCause::Changed;
}

}  // namespace tak::render
