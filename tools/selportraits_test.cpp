// The info bar's multi-selection portrait strip (client/selportraits.h): type
// grouping + order, the portrait click rule, the fit / shrink / overflow layout, and
// the bar's multi-selection HP / mana totals and squad label.
#include "client/selportraits.h"
#include <cstdio>
#include <stdexcept>
static void check(bool yes,const char* message) {if(!yes)throw std::runtime_error(message);}
static bool near(float a,float b) {return a-b<1e-3f && b-a<1e-3f;}
int main() try {
    namespace sp=tak::selportraits;
    // The front unit's type leads even when outnumbered; then count desc, ties by id.
    auto g=sp::group({{7,"zarch"},{1,"sword"},{2,"bow"},{3,"sword"},{4,"bow"},{5,"sword"},{6,"axe"},{8,"cat"}});
    check(g.size()==5,"one group per type");
    check(g[0].type=="zarch" && g[0].ids==std::vector<int>{7},"front unit's type first");
    check(g[1].type=="sword" && g[1].ids==std::vector<int>{1,3,5},"then the most numerous, ids in selection order");
    check(g[2].type=="bow" && g[2].ids.size()==2,"count descending");
    check(g[3].type=="axe" && g[4].type=="cat","equal counts by type id");
    check(sp::group({{1,"a"},{2,"a"}}).size()==1,"one type, one portrait");
    check(sp::group({}).empty(),"empty selection");
    // Click: plain keeps only that type, Shift drops it; order is preserved.
    const std::vector<int> sel{7,1,2,3,4,5};
    check(sp::click(sel,{1,3,5},false)==std::vector<int>{1,3,5},"plain click selects only that type");
    check(sp::click(sel,{1,3,5},true)==std::vector<int>{7,2,4},"shift click removes that type");
    check(sp::click({1,3},{1,3},true).empty(),"removing the last type empties the selection");
    // Layout (48x36 portraits, gap 3): full size while it fits...
    auto L=sp::layout(4,48,36,3,200,0.6f);
    check(L.shown==4 && L.rows==1 && near(L.scale,1) && !L.overflow && near(L.width,3*51),"fits at full size");
    check(sp::layout(1,48,36,3,0,0.6f).shown==1 && near(sp::layout(1,48,36,3,0,0.6f).width,0),"single type: no strip");
    // ...shrinks one row to fit...
    L=sp::layout(5,48,36,3,160,0.6f);
    check(L.shown==5 && L.rows==1 && !L.overflow && L.scale<1 && L.scale>=0.6f && L.width<=160+1e-3f,"shrinks to fit");
    // ...folds into two half-height rows below the shrink floor...
    L=sp::layout(8,48,36,3,160,0.6f);   // 7 extras: 4 columns of 2 rows
    check(L.shown==8 && L.rows==2 && !L.overflow && near(L.scale,33.f/72) && near(L.width,4*(3+48*33.f/72)),"folds to two rows");
    // ...then caps with a "+K" tile, never past the room.
    L=sp::layout(20,48,36,3,160,0.6f);
    const int cols=int(160/(3+48*33.f/72));
    check(L.overflow && L.rows==2 && L.shown==2*cols && L.width<=160+1e-3f,"overflow: the last cell is the +K tile");
    check(sp::layout(3,48,36,3,10,0.6f).shown==1 && !sp::layout(3,48,36,3,10,0.6f).overflow,"no room: primary only");
    // Slots run LEFT of the primary: one row centred on its height, two rows filling it.
    const sp::Rect p{300,10,48,36};
    sp::Rect r=sp::slot(sp::layout(3,48,36,3,500,0.6f),2,p,3);
    check(near(r.x,300-2*51) && near(r.y,10) && near(r.w,48),"full-size slot 2 two steps left");
    r=sp::slot(sp::layout(5,48,36,3,160,0.6f),1,p,3);
    check(r.x<300 && near(r.y+r.h/2,28),"shrunk row centred");
    L=sp::layout(8,48,36,3,160,0.6f);
    const sp::Rect a=sp::slot(L,1,p,3),b=sp::slot(L,2,p,3),c=sp::slot(L,3,p,3);
    check(near(a.x,b.x) && near(a.y,10) && near(b.y+b.h,46) && c.x<a.x && near(c.y,10),"two rows: column by column, top first");
    L=sp::layout(20,48,36,3,160,0.6f);
    r=sp::slot(L,L.shown,p,3);
    check(r.x>=300-160-1e-3f,"overflow tile inside the room");
    // Totals: HP over everyone, mana over only the units with a pool.
    auto t=sp::total({{50,100,0,0},{100,100,0,0},{30,60,200,1000},{0,60,900,1000}});
    check(near(t.hp,180) && near(t.maxHp,320) && near(t.hpFrac(),180.f/320),"HP is the sum over the selection");
    check(t.hasMana && near(t.manaFrac(),1100.f/2000),"mana only over the casters, not diluted by the knights");
    t=sp::total({{40,100,0,0},{100,100,0,0}});
    check(!t.hasMana && near(t.manaFrac(),0) && near(t.hpFrac(),0.7f),"no pool anywhere: empty mana bar");
    check(near(sp::total({}).hpFrac(),0) && !sp::total({}).hasMana,"nothing selected: empty bars");
    // The selection is a squad only when it is all of one, or its recall set (no builders).
    using SB=std::vector<std::pair<int,bool>>;
    check(sp::selectionSquad(SB{{3,false},{3,false},{3,false}},{})==3,"the whole group");
    check(sp::selectionSquad(SB{{-2,false},{-2,false}},{{1,false}})==-2,"a formation; another squad's units do not matter");
    check(sp::selectionSquad(SB{{3,false},{3,false}},{{3,true}})==3,"a recalled group leaves its builder out and still counts");
    check(sp::selectionSquad(SB{{3,false},{3,true}},{})==3,"a group with its builder is the group too");
    check(sp::selectionSquad(SB{{3,false},{3,true}},{{3,true}})==0,"some of its builders is not the group");
    check(sp::selectionSquad(SB{{5,true},{5,true}},{{5,true}})==0,"part of an all-builder group is not the group");
    check(sp::selectionSquad(SB{{3,false},{3,false}},{{3,false}})==0,"part of a group is not the group");
    check(sp::selectionSquad(SB{{3,false},{3,false},{0,false}},{})==0,"an extra unit outside it");
    check(sp::selectionSquad(SB{{3,false},{-3,false}},{})==0,"mid-retype mix of group and formation");
    check(sp::selectionSquad(SB{{3,false}},{})==0,"one unit keeps its own name");
    check(sp::selectionSquad(SB{{0,false},{0,false}},{})==0,"no squad");
    check(sp::squadLabel(3)=="GROUP 3" && sp::squadLabel(-2)=="FORMATION 2","group / formation label");
    check(sp::squadLabel(10)=="GROUP 0" && sp::squadLabel(-10)=="FORMATION 0" && sp::squadLabel(0).empty(),"squad 10 is key 0");
    std::puts("selportraits_test: ok");
    return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"selportraits_test: FAIL: %s\n",e.what());return 1;}
