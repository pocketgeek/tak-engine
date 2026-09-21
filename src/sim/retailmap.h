#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tak::sim {

struct RetailMapBoundaryCell {
    uint16_t feature=0xffff;
    uint8_t height=0,low=0;
};

struct RetailMapFeatureType {
    std::string name;
    int footX=1,footZ=1;
    bool blocking=false,indestructible=false;
    bool clearable=false; // No indestructible blocker in the dead/burnt replacement graph.
};

struct RetailMapFeatureCell : RetailMapBoundaryCell {
    uint8_t backX=0,backZ=0;
};

// Remove an anchor and its footprint tails. Boundary markers survive removal.
template<class CellAt>
void retailMapRemove(int x,int z,const RetailMapFeatureType& type,CellAt cellAt) {
    cellAt(x,z).feature=0xffff;
    for (int dz=0;dz<type.footZ;++dz) for (int dx=0;dx<type.footX;++dx) {
        auto& cell=cellAt(x+dx,z+dz);
        if (cell.feature==0xfffe) cell.feature=0xffff;
    }
}

// 495360: intersecting removable features disappear in scan order, even if a
// later obstruction refuses the new footprint. Removed reports each old anchor.
template<class CellAt,class Removed>
bool retailMapInstall(int width,int height,int x,int z,uint16_t index,
                      const std::vector<RetailMapFeatureType>& types,
                      CellAt cellAt,Removed removed) {
    const auto& type=types.at(index);
    if (type.name.empty() || type.footX<=0 || type.footZ<=0 || x<0 || z<0 ||
        x+type.footX>width || z+type.footZ>height) return false;
    for (int dz=0;dz<type.footZ;++dz) for (int dx=0;dx<type.footX;++dx) {
        auto& cell=cellAt(x+dx,z+dz);
        if (cell.feature==0xffff) continue;
        const int ox=x+dx-(cell.feature==0xfffe?cell.backX:0);
        const int oz=z+dz-(cell.feature==0xfffe?cell.backZ:0);
        const auto previous=cellAt(ox,oz).feature;
        if (previous>=types.size() || types[previous].indestructible) return false;
        retailMapRemove(ox,oz,types[previous],cellAt);
        removed(ox,oz,previous);
    }
    cellAt(x,z).feature=index;
    for (int dz=0;dz<type.footZ;++dz) for (int dx=0;dx<type.footX;++dx) {
        if (!dx && !dz) continue;
        auto& cell=cellAt(x+dx,z+dz);
        cell.feature=0xfffe;cell.backX=uint8_t(dx);cell.backZ=uint8_t(dz);
    }
    return true;
}

// 50eef0: mark the projected map boundary after feature installation. Named
// feature anchors and existing special markers survive; footprint tails do not.
// CellAt returns a mutable reference. The caller supplies valid map dimensions.
template<class CellAt>
void retailMapBoundary(int width,int height,int pixelHeight,int sea,bool blockWater,
                       CellAt cellAt) {
    auto mark=[&](int x,int z) {
        auto& cell=cellAt(x,z);
        if (cell.feature==0xffff || cell.feature==0xfffe) cell.feature=0xfffd;
    };
    for (int z=0;z<height;++z) {
        mark(width-2,z);
        mark(width-1,z);
    }
    for (int x=0;x<width;++x)
        for (int z=0;z*16-int(cellAt(x,z).height)/2<0;++z) mark(x,z);
    for (int x=0;x<width;++x) {
        int z=height-1;
        while (z*16-int(cellAt(x,z).height)/2>pixelHeight-128) {
            mark(x,z-1);
            --z;
        }
    }
    if (blockWater)
        for (int z=0;z<height;++z) for (int x=0;x<width;++x)
            if (int(cellAt(x,z).low)<=sea) mark(x,z);
}

} // namespace tak::sim
