#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include "sim/retailweapon.h"
#include "sim/detmath.h"

namespace tak {
// Native weapon speed after fixed-point conversion and substep truncation.
inline int32_t retailAimSpeed(float speed) {
    const int32_t raw=int32_t(double(speed)*2184.5333333333333);
    if(raw<=0)return 0;
    const int steps=std::max(1,int((uint32_t(raw)+0xfffffu)>>20));
    return (raw/steps)*steps;
}

// 51ab9f..51acaa: lead from the shooter center to the target SweetSpot,
// using all three velocity components and truncating before each fixed multiply.
inline std::array<int32_t,3> retailAimLead(const std::array<int32_t,3>& shooter,
        std::array<int32_t,3> target,const std::array<int32_t,3>& velocity,
        int32_t speed,bool noLead) {
    if(noLead || !speed)return target;
    std::array<int32_t,3> delta{};
    for(size_t axis=0;axis<3;++axis)
        delta[axis]=std::bit_cast<int32_t>(uint32_t(shooter[axis])-uint32_t(target[axis]));
    const double distance=std::sqrt((double(delta[2])*delta[2]+double(delta[1])*delta[1])+double(delta[0])*delta[0]);
    const int32_t rawDistance=distance<=INT32_MAX ? int32_t(distance):INT32_MIN;
    const double duration=double(rawDistance)/speed*65536.0;
    const int32_t ticks=duration>=INT32_MIN && duration<=INT32_MAX ? int32_t(duration):0;
    const int32_t lead=std::bit_cast<int32_t>(uint32_t((int64_t(ticks)*0xcccc)>>16));
    for(size_t axis=0;axis<3;++axis)
        target[axis]=std::bit_cast<int32_t>(uint32_t(target[axis])+uint32_t((int64_t(velocity[axis])*lead)>>16));
    return target;
}

// 52bd10: ballistic elevation shared by simulation and display. Inputs use world units/tick,
// with the native float boundary before the double-precision trajectory solve.
// 0x8000 is the native unreachable/vertical-shot sentinel.
inline uint16_t retailBallisticPitch(float dx,float dy,float dz,float speed,
                                    float gravityScale,bool highArc,int32_t gravity=8155) {
    const double horizontalSquared=double(dx)*dx+double(dz)*dz;
    if(horizontalSquared==0)return 0x8000;
    const double horizontal=std::sqrt(horizontalSquared);
    const double ratio=double(speed)*speed/(double(gravity)/65536.0*gravityScale*horizontal);
    const double discriminant=ratio*ratio-2.0*double(dy)*ratio/horizontal-1.0;
    if(discriminant<0)return 0x8000;
    const double root=std::sqrt(discriminant);
    const double first=detmath::atan(ratio+root),second=detmath::atan(ratio-root);
    const double angle=(highArc ? std::max(first,second):std::min(first,second))*10430.378350470464;
    if(!std::isfinite(angle))return 0;
    return uint16_t(int32_t(angle));
}
}
