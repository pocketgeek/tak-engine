#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace tak {

// Display-only smoke particle (retail 4f1cf0/4f1bd0). Coordinates are 16.16.
struct RetailSmokeParticle {
    std::array<int32_t,3> position{};
    int32_t period=8;
    uint32_t frameLimit=0, countdown=8, frame=0;
    bool small=false, steam=false;

    template<class Random>
    bool tick(int32_t windX,int32_t windZ,int32_t gravity,Random random) {
        const std::array<uint32_t,3> delta={uint32_t(windX)*8u,
            uint32_t(gravity)*4u,uint32_t(windZ)*8u};
        for(unsigned i=0;i<3;++i)
            position[i]=std::bit_cast<int32_t>(uint32_t(position[i])+delta[i]);
        if(--countdown!=0)return true;
        const int32_t half=period/2;
        countdown=uint32_t(half+int64_t(random())*half/32768);
        return ++frame!=frameLimit;
    }
};

} // namespace tak
