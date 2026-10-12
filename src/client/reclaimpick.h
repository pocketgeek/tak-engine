#pragma once

#include <algorithm>
#include <optional>

namespace tak::client {

// Reclaim target picking, shared by the broom cursor, the right-click order and the
// armed CLEAR order (GameView::reclaimTargetAt), plus the right-drag area preview.
// Header-only so a test can drive it without a GameView.
//
// One radius rule for features AND corpses, from the FEATURE footprint. Retail
// installs a corpse as an ordinary map feature of its corpse/statue feature type
// (512ee0, verified cell for cell by tools/re/check_corpse_features.py), and the
// sim mirrors that in corpseFootprints_ with the feature type's fx/fz. Sizing a
// corpse from the dead UNIT's footX/footZ, as the click paths once did, made the
// cursor and the click disagree around every wreck whose feature is a different size.
inline float reclaimPickRadius(int fx,int fz) { return 18.f+8.f*float(std::max(fx,fz)); }

// Nearest candidate whose pick circle holds the point. Ids are sim ids: a feature
// id (which can be 0, the feature anchored at cell 0,0), or -unitId for a corpse.
struct ReclaimPick {
    ReclaimPick(float x,float z):wx(x),wz(z) {}
    float wx,wz;
    std::optional<int> id;
    float best=1e18f;
    void offer(int targetId,float x,float z,int fx,int fz) {
        const float dx=x-wx,dz=z-wz,d=dx*dx+dz*dz,r=reclaimPickRadius(fx,fz);
        if (d<r*r && d<best) { best=d;id=targetId; }
    }
};

// Area preview: does a footprint of 16px cells [cx,cx+fx) x [cz,cz+fz) touch the
// world box? The sim's tickReclaimArea resolves the area to footprint CELLS (via the
// placement cell's back-offset to the anchor), so a multi-cell tree whose centre lies
// outside the box is still cleared when one of its cells is inside -- the preview
// marks what the sim will take, not just centres.
inline bool reclaimFootprintInBox(int cx,int cz,int fx,int fz,float minx,float maxx,float minz,float maxz) {
    return float(cx*16)<=maxx && float((cx+fx)*16)>minx &&
           float(cz*16)<=maxz && float((cz+fz)*16)>minz;
}

}
