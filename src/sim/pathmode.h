#pragma once
#include <array>
#include <cstdint>
namespace tak::sim {
enum class PathfindingMode : uint8_t { Retail=0,Flowfield=1,Cooperative=2,RetailPlus=3,Legion=4 };
constexpr bool isRetailPathfinding(PathfindingMode mode) {
    return mode==PathfindingMode::Retail||mode==PathfindingMode::RetailPlus;
}
constexpr bool isSharedPathfinding(PathfindingMode mode) {
    return mode==PathfindingMode::Flowfield||mode==PathfindingMode::Cooperative;
}
// Group-planned, clearance-exact ground navigation (src/sim/legion*).
constexpr bool isLegionPathfinding(PathfindingMode mode) {return mode==PathfindingMode::Legion;}
constexpr bool validPathfindingMode(uint8_t mode) {
    return mode<=uint8_t(PathfindingMode::Legion);
}
constexpr const char* pathfindingModeName(PathfindingMode mode) {
    switch(mode) {
    case PathfindingMode::Retail:return "Retail";
    case PathfindingMode::Flowfield:return "Flowfield";
    case PathfindingMode::Cooperative:return "Cooperative";
    case PathfindingMode::RetailPlus:return "Retail+";
    case PathfindingMode::Legion:return "Legion";
    }
    return "Unknown";
}
constexpr PathfindingMode cyclePathfindingMode(PathfindingMode mode,int direction) {
    // Display order is independent of the stable saved/network identities.
    constexpr std::array order{PathfindingMode::Retail,PathfindingMode::RetailPlus,
                               PathfindingMode::Flowfield,PathfindingMode::Cooperative,
                               PathfindingMode::Legion};
    std::size_t index=0;
    for(std::size_t i=0;i<order.size();++i)if(order[i]==mode){index=i;break;}
    return order[(index+(direction<0?order.size()-1:1))%order.size()];
}
}
