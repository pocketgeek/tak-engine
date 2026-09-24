#pragma once

#include "sim/detmath.h"
#include "sim/retailmotion.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace tak::sim {

// The state written by BallisticWeapon::initShot (0x52be80) and advanced by
// BallisticWeapon::update (0x52bf90). Coordinates and velocities are 16.16;
// angles are BAM in native roll/yaw/pitch order.
struct RetailBallisticShot {
    std::array<int32_t,3> position{};
    std::array<int32_t,3> velocity{};
    std::array<uint16_t,3> angles{};
};

inline RetailBallisticShot retailBallisticLaunch(const std::array<int32_t,3>& muzzle,
        uint16_t bodyHeading,uint16_t aimHeading,uint16_t pitch,int32_t speedPerSubstep) {
    RetailBallisticShot shot;
    shot.position=muzzle;
    const uint16_t yaw=uint16_t(bodyHeading+aimHeading);
    const int32_t horizontal=retailScaledCosine(pitch,speedPerSubstep);
    shot.velocity={retailScaledSine(yaw,horizontal),
                   retailScaledSine(pitch,speedPerSubstep),
                   retailScaledCosine(yaw,horizontal)};
    shot.angles={0,yaw,pitch};
    return shot;
}

// 52bf90 converts velocity dwords to float before normalization, stores the
// normalized Y component as float, then derives pitch with asin and truncates
// to BAM. atan(y/sqrt(1-y^2)) is the same angle; detmath keeps peers bit-stable.
inline uint16_t retailBallisticPitchFromVelocity(const std::array<int32_t,3>& velocity) {
    const float x=float(velocity[0]),y=float(velocity[1]),z=float(velocity[2]);
    const double length=std::sqrt((double(x)*x+double(y)*y)+double(z)*z);
    if(length==0.0)return 0x8000;
    const float normalizedY=float(double(y)/length);
    const double ny=double(normalizedY);
    const double remainder=std::max(0.0,1.0-ny*ny);
    const double tangent=ny==1.0 ? INFINITY : ny==-1.0 ? -INFINITY : ny/std::sqrt(remainder);
    const double bam=tak::detmath::atan(tangent)*10430.378350470464;
    if(!std::isfinite(bam))return bam<0.0 ? 0xc000 : 0x4000;
    return uint16_t(static_cast<int32_t>(bam));
}

inline int32_t retailBallisticGravityStep(int32_t gravityRaw,float gravityAdjustment,
                                          uint32_t substeps) {
    if(!substeps)return INT32_MIN;
    const double n=double(substeps);
    const double value=(double(gravityRaw)*double(gravityAdjustment))/(n*n);
    if(!std::isfinite(value) || value<double(INT64_MIN) || value>double(INT64_MAX))
        return INT32_MIN;
    return std::bit_cast<int32_t>(uint32_t(static_cast<int64_t>(value)));
}

inline void retailBallisticTick(RetailBallisticShot& shot,int32_t gravityRaw,
        float gravityAdjustment,uint32_t substeps,const std::array<uint16_t,3>& spin) {
    if(!substeps)return;
    const int32_t gravityStep=retailBallisticGravityStep(gravityRaw,gravityAdjustment,substeps);
    for(uint32_t i=0;i<substeps;++i) {
        shot.velocity[1]=std::bit_cast<int32_t>(uint32_t(shot.velocity[1])-uint32_t(gravityStep));
        for(unsigned axis=0;axis<3;++axis)
            shot.position[axis]=std::bit_cast<int32_t>(uint32_t(shot.position[axis])+uint32_t(shot.velocity[axis]));
        if(spin[0] || spin[1] || spin[2]) {
            shot.angles[2]=uint16_t(shot.angles[2]+spin[2]);
            shot.angles[1]=uint16_t(shot.angles[1]+spin[1]);
            shot.angles[0]=uint16_t(shot.angles[0]+spin[0]);
        } else {
            shot.angles[2]=retailBallisticPitchFromVelocity(shot.velocity);
        }
    }
}

// The map OTA stores gravity in pixels/second^2; the retail simulation global
// is its 16.16, 30-Hz-per-axis acceleration value (default 112 -> 8155).
inline int32_t retailBallisticGravityRaw(double mapGravity) {
    const double raw=mapGravity*(65536.0/900.0);
    if(!std::isfinite(raw) || raw<double(INT32_MIN) || raw>double(INT32_MAX))return 8155;
    return static_cast<int32_t>(raw);
}

} // namespace tak::sim
