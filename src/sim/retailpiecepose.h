#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <vector>
#include <span>
#include "cob/retailpieces.h"

namespace tak::sim {

// 536143 rotates an integer pair and rounds each result to nearest/even.
// Q60 Taylor evaluation preserves that precision without host x87/libm state.
// The angle conversion is the observed decimal BAM-to-radians coefficient.
inline const std::array<int64_t,2>& retailRotationCoefficients(uint16_t angle) {
    // The coefficients depend only on the 16-bit heading. Construct once using
    // the same integer evaluation, then share immutable values across worlds.
    static const auto table=[] {
        std::vector<std::array<int64_t,2>> values(65536);
        for (uint32_t heading=0;heading<values.size();++heading) {
            using Wide=__int128_t;
            constexpr int64_t one=int64_t(1)<<60;
            const int64_t x=int64_t(Wide(std::bit_cast<int16_t>(uint16_t(heading)))*9587379924285LL*one/
                                    100000000000000000LL);
            const Wide square=Wide(x)*x/one;
            int64_t sine=x,cosine=one,st=x,ct=one;
            for (int n=1;n<19;++n) {
                st=int64_t(-Wide(st)*square/one/((2*n)*(2*n+1)));
                ct=int64_t(-Wide(ct)*square/one/((2*n-1)*(2*n)));
                sine+=st; cosine+=ct;
            }
            values[heading]={sine,cosine};
        }
        return values;
    }();
    return table[angle];
}

inline void retailRotatePair(int32_t& a,int32_t& b,uint16_t angle) {
    if (!angle) return;
    using Wide=__int128_t;
    const auto& coefficients=retailRotationCoefficients(angle);
    const int64_t sine=coefficients[0],cosine=coefficients[1];
    auto rounded=[](Wide value) {
        constexpr Wide scale=Wide(1)<<60;
        const bool negative=value<0;
        if (negative) value=-value;
        Wide whole=value/scale,rest=value%scale;
        if (rest>scale/2 || (rest==scale/2 && (whole&1))) ++whole;
        if (negative) whole=-whole;
        if (whole<INT32_MIN || whole>INT32_MAX) return int32_t(INT32_MIN);
        return int32_t(whole);
    };
    const int32_t nextA=rounded(Wide(a)*cosine-Wide(b)*sine);
    b=rounded(Wide(a)*sine+Wide(b)*cosine); a=nextA;
}

// 535d50 applies roll, pitch, then yaw; rounding occurs after each plane.
inline void retailRotatePiece(std::array<int32_t,3>& point,
                              const std::array<uint16_t,3>& xyz) {
    retailRotatePair(point[0],point[1],xyz[2]);
    retailRotatePair(point[1],point[2],xyz[0]);
    retailRotatePair(point[0],point[2],xyz[1]);
}

struct RetailModelPiece {
    std::array<int32_t,3> offset{};
    int parent=-1,scriptPiece=-1;
    std::array<std::array<int32_t,3>,2> emissionVertices{};
    uint8_t emissionVertexCount=0;
};

inline std::array<int32_t,3> retailPieceOrigin(std::span<const RetailModelPiece> model,
        std::span<const cob::RetailPiece> poses,int scriptPiece,uint16_t heading,uint16_t pitch=0,uint16_t roll=0) {
    int index=-1;
    for (size_t i=0;i<model.size();++i)
        if (model[i].scriptPiece==scriptPiece) { index=int(i); break; }
    if (index<0) return {};
    auto translated=[&](const RetailModelPiece& piece) {
        auto result=piece.offset;
        // The model loader mirrors authored X/Z before installing the tree.
        result[0]=std::bit_cast<int32_t>(0u-uint32_t(result[0]));
        result[2]=std::bit_cast<int32_t>(0u-uint32_t(result[2]));
        if (piece.scriptPiece>=0)
            for (size_t i=0;i<3;++i)
                result[i]=std::bit_cast<int32_t>(uint32_t(result[i])+uint32_t(poses[size_t(piece.scriptPiece)].move[i]));
        return result;
    };
    auto result=translated(model[size_t(index)]);
    for (index=model[size_t(index)].parent;index>=0;index=model[size_t(index)].parent) {
        const auto& parent=model[size_t(index)];
        std::array<uint16_t,3> angles{};
        if (parent.scriptPiece>=0)
            for (size_t i=0;i<3;++i) angles[i]=uint16_t(poses[size_t(parent.scriptPiece)].turn[i]);
        if (parent.parent<0) {
            angles[0]=uint16_t(angles[0]+pitch);
            angles[1]=uint16_t(angles[1]+heading);
            angles[2]=uint16_t(angles[2]+roll);
        }
        retailRotatePiece(result,angles);
        auto offset=translated(parent);
        for (size_t i=0;i<3;++i)
            result[i]=std::bit_cast<int32_t>(uint32_t(result[i])+uint32_t(offset[i]));
    }
    result[2]=std::bit_cast<int32_t>(0u-uint32_t(result[2]));
    return result;
}

// Add an instruction-time script-piece offset to the unit's fixed-point world
// origin. EMIT_SFX effects use this same wrapped XYZ placement in both the
// simulation host and the display-side death-script bridge.
inline std::array<int32_t,3> retailScriptEffectPosition(
        const std::array<int32_t,3>& worldPosition,
        std::span<const RetailModelPiece> model,
        std::span<const cob::RetailPiece> poses,int scriptPiece,
        uint16_t heading,uint16_t pitch=0,uint16_t roll=0) {
    const auto offset=retailPieceOrigin(model,poses,scriptPiece,heading,pitch,roll);
    std::array<int32_t,3> result{};
    for(size_t axis=0;axis<3;++axis)
        result[axis]=std::bit_cast<int32_t>(uint32_t(worldPosition[axis])+uint32_t(offset[axis]));
    return result;
}

// 4dd2a0: SweetSpot uses mirrored model-space vertex bounds, including zero.
// The native extrema start at zero, and signed center division truncates to zero.
struct RetailPieceBounds {
    std::array<int32_t,3> minimum{},maximum{};
    void add(const std::array<int32_t,3>& point) {
        for(size_t axis=0;axis<3;++axis) {
            minimum[axis]=std::min(minimum[axis],point[axis]);
            maximum[axis]=std::max(maximum[axis],point[axis]);
        }
    }
    std::array<int32_t,3> center() const {
        std::array<int32_t,3> result{};
        for(size_t axis=0;axis<3;++axis)
            result[axis]=std::bit_cast<int32_t>(uint32_t(minimum[axis])+uint32_t(maximum[axis]))/2;
        return result;
    }
};

} // namespace tak::sim
