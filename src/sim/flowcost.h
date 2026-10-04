#pragma once
#include <algorithm>
#include <cstdint>

namespace tak::sim::flow {
// Static travel estimate, independent of animated support-point bobbing. Match
// the mover's road-before-water precedence and floater/upright-hover waterline.
// Non-upright hover support rests on the sea; ordinary walkers follow terrain.
inline uint16_t terrainCost(int grade,bool road,int terrain,int sea,bool upright,
                           bool floater,bool hover,int waterline,int32_t roadSpeed,int32_t waterSpeed) {
    if(grade<0)return 0;
    int support=terrain;
    if(upright) {if(hover)support=std::max(terrain,sea-int(uint8_t(waterline)));}
    else if(floater)support=sea-int(uint8_t(waterline));
    else if(hover)support=std::max(terrain,sea);
    const int32_t speed=road?roadSpeed:support<sea?waterSpeed:65536;
    return uint16_t(std::clamp<int64_t>((int64_t(grade<6?96:64)*65536)/std::max(1,speed),1,255));
}
}
