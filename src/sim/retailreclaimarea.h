#pragma once
#include <cstdint>
#include <optional>
#include <utility>

namespace tak::sim {
// ReclaimArea selector (509cc0): sample the rectangle in 16-world-unit rows,
// retain the first sample on ties, and compare the sum of the HIGH halves of
// the two squared signed 16.16 differences. This is not float distance.
struct RetailReclaimArea {
    int32_t minX=0,minZ=0,maxX=0,maxZ=0;
    bool approached=false;
};
template<class Eligible>
std::optional<std::pair<int32_t,int32_t>> retailReclaimAreaTarget(
        const RetailReclaimArea& area,int32_t builderX,int32_t builderZ,Eligible eligible) {
    std::optional<std::pair<int32_t,int32_t>> result;
    int32_t best=0;
    for (int64_t z=area.minZ;z<=area.maxZ;z+=16*65536)
        for (int64_t x=area.minX;x<=area.maxX;x+=16*65536) {
            if (!eligible(int32_t(x),int32_t(z))) continue;
            const int64_t dx=int32_t(uint32_t(builderX)-uint32_t(x));
            const int64_t dz=int32_t(uint32_t(builderZ)-uint32_t(z));
            const int32_t distance=int32_t(uint32_t((dx*dx)>>32)+uint32_t((dz*dz)>>32));
            if (!result || distance<best) {result={{int32_t(x),int32_t(z)}};best=distance;}
        }
    return result;
}
} // namespace tak::sim
