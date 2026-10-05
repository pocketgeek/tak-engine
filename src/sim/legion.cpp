#include "legion.h"
#include "sim.h"
#include "footprint.h"
#include "retailplacement.h"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>
#include <vector>

// Legion navigation. See docs/legion-pathfinding.md for the design.
//
// Everything below is integer arithmetic on the footprint-ORIGIN lattice: a
// unit of footprint fx*fz whose centre is at x occupies origin column
// footprintOrigin(x,fx). That is the coordinate the mover's legality test
// (World::mobilePlacement / commitGroundStep) takes, so planning on it makes
// "legal for the planner" and "legal for the mover" the same predicate.

namespace tak::sim {
namespace {
constexpr uint16_t kUnreached=0xffff;
constexpr uint16_t kOrthogonal=5,kDiagonal=7;   // 7/5 = 1.4 ~ sqrt(2)
constexpr uint64_t kFieldQuota=4'000'000;      // relaxations per tick, all fields
constexpr size_t kMaxFields=48,kMaxPlanes=24;
constexpr uint32_t kFieldTenure=300;          // ticks a field is safe from eviction
constexpr uint32_t kTrappedRetire=9000;        // ticks (5 min) a trapped order waits for terrain to open
constexpr int kClusterCells=16;
constexpr uint32_t kAreaSettle=300;            // ticks a shared-point member may stand still in the area before it settles there
constexpr int kDetourCells=12;                 // local detour search radius                // group goals linked within this
constexpr int kLineCells=160;                  // direct-line probe reach
constexpr int kFormationLineCells=640;         // ... for a member with a formation slot
constexpr int kGoalSearchCells=24;             // blocked click -> nearest legal
constexpr std::array<std::array<int,2>,8> kDirections{{
    {1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}}};

uint64_t mix(uint64_t h,uint64_t v) {
    h^=v+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2);
    h=(h^(h>>30))*0xbf58476d1ce4e5b9ull;
    return h^(h>>27);
}
int64_t isqrtFloor(uint64_t n) {return int64_t(isqrt64(n));}
}

struct LegionNavigator::Impl {
    // Static legality of every footprint origin for one mobility class.
    struct Plane {
        int maxDepth=0,minDepth=0,maxSlope=0,maxWaterSlope=0,footX=1,footZ=1;
        bool legacy=false;
        uint64_t epoch=0;   // World placement epoch + structure signature
        std::vector<uint8_t> legal;
        // Static connected component of every legal origin (-1 if illegal),
        // under the same step rule the field and the mover use.
        std::vector<int32_t> comp;
        uint64_t lastUse=0;
    };
    // Integer distance field from a group's goal origins, built with a
    // bounded bucket queue; resumable across ticks under the work quota.
    struct Field {
        int plane=-1;
        uint64_t epoch=0;
        std::vector<uint16_t> potential;
        std::array<std::vector<int>,8> buckets;
        uint32_t current=0;
        size_t queued=0;
        bool done=false;
        uint64_t work=0;
    };
    struct Group {
        int id=0,player=0,plane=-1;
        uint32_t issuedTick=0;
        int minX=0,minZ=0,maxX=0,maxZ=0;
        std::vector<int> seeds;          // sorted unique goal origins (cell index)
        std::map<int,int> sharing;       // goal origin -> member count
        std::map<int,int> peak;          // goal origin -> most members it ever had (area size)
        int members=0;
        int comp=-1;                     // static component of every seed
        std::unique_ptr<Field> field;    // the field members steer by (done or building)
        // After a static change the finished field keeps steering (the mover
        // re-proves every step) while its replacement builds in `next`.
        std::unique_ptr<Field> next;
        bool stale=false;
        uint64_t lastUse=0;
        uint32_t built=0;
        // Packed arrival slots for goals several members share, inside-out
        // by field potential; claimed on approach so the area fills from
        // the point outward and nobody has to cross a settled body.
        struct Slots {std::vector<int> cells;std::vector<uint8_t> taken;bool built=false,stale=false;uint16_t reach=0;};
        std::map<int,Slots> slots;
    };
    enum State : uint8_t {None=0,Moving=1,Holding=2,Waiting=3,Arrived=4,Trapped=5};
    struct Member {
        uint64_t controller=0;
        int group=0,goal=-1;              // goal origin cell index
        int lineCell=-1;                  // origin cell the line probe ran from
        bool line=false;
        State state=None;
        uint16_t best=kUnreached;         // best potential reached
        uint32_t held=0,stalled=0,progress=0xffffffffu;
        uint32_t trappedSince=0;uint64_t trappedEpoch=0;
        int requested=-1;                 // the goal origin the order named
        int slot=-1;                      // claimed slot index (shared goals)
        int detour=-1;                    // committed side-step cell
        uint32_t detourTicks=0;
        bool detourFace=true;             // side-step turns the body (keep-right) or not (shuffle)
        std::tuple<int,uint32_t,int32_t,int32_t> point{}; // player, issue tick, requested point
        std::vector<int> route;           // committed local detour around still bodies
        uint32_t routeTicks=0,nextDetour=8,detourCount=0;
    };

    World& w;
    std::vector<Plane> planes;
    std::map<int,Group> groups;
    std::map<int,Member> members;
    // Settled Legion arrivals: the goal origin each one completed on, and how
    // many times it has stepped aside since. A settled body may yield one
    // cell (never farther than one cell from that goal origin, so it stays
    // inside its destination area) to open a lane for a same-player member
    // whose own goal it walls in. Capped per body: no endless shuffling.
    struct Anchor {int goal=-1;uint8_t yields=0;};
    std::map<int,Anchor> anchors;
    // Committed yield steps in progress: unit id -> target origin cell.
    struct Yield {int cell=-1;uint32_t ticks=0;};
    std::map<int,Yield> yielding;
    static constexpr uint8_t kMaxYields=3;
    // Cells claimed by arrival slots of every group sent to one point in one
    // command (mixed footprints form one group per class but share the area).
    // A shared point's members get their slots all at once, by formation:
    // each member's offset from the group's centroid, scaled into the
    // packed area around the point (see assignFormation).
    struct Point {
        int refs=0;std::set<int> cells;
        bool assigned=false;
        int64_t centreX=0,centreZ=0,scaleNum=1,scaleDen=1,limit=0;   // px
    };
    std::map<std::tuple<int,uint32_t,int32_t,int32_t>,Point> points;
    void slotCells(const Member& m,int fx,int fz,bool claim) {
        auto& cells=points[m.point].cells;
        const int W=width(),x=m.goal%W,z=m.goal/W;
        for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
            const int c=(z+j)*W+x+i;
            if(claim)cells.insert(c);else cells.erase(c);
        }
    }
    bool slotFree(const Member& m,int cell,int fx,int fz) const {
        const auto found=points.find(m.point);
        if(found==points.end())return true;
        const int W=width(),x=cell%W,z=cell/W;
        for(int j=0;j<fz;++j)for(int i=0;i<fx;++i)if(found->second.cells.count((z+j)*W+x+i))return false;
        return true;
    }
    int nextGroup=1;
    uint64_t structureSignature=0,epoch=0,lastWorldEpoch=~0ull;
    // Plane rebuild work (cells) not yet charged to the per-tick quota.
    uint64_t planeDebt=0;
    int pruneCursor=0;
    Stats stats;
    explicit Impl(World& world):w(world) {}

    int width() const {return w.hW_;}
    int height() const {return w.hH_;}
    bool placementPlane() const {
        return !w.mapPlacementCells_.empty()&&w.mapPlacementCells_.size()==size_t(w.hW_)*w.hH_;
    }

    // ---- static plane -------------------------------------------------
    int planeFor(const UnitType& t) {
        const bool legacy=!placementPlane();
        const int maxDepth=t.canFly||t.domain!=UnitType::Domain::Ground?10000:(t.maxWaterDepth>0?t.maxWaterDepth:20);
        const int minDepth=t.domain==UnitType::Domain::Water?(t.minWaterDepth>0?t.minWaterDepth:13):-10000;
        for(size_t i=0;i<planes.size();++i) {
            const auto& p=planes[i];
            if(p.legacy==legacy&&p.footX==t.footX&&p.footZ==t.footZ&&(legacy||
               (p.maxDepth==maxDepth&&p.minDepth==minDepth&&p.maxSlope==t.maxSlope&&p.maxWaterSlope==t.maxWaterSlope))) {
                return int(i);
            }
        }
        if(planes.size()>=kMaxPlanes) {
            // Evict the least recently used plane no live group refers to.
            int victim=-1;
            for(size_t i=0;i<planes.size();++i) {
                bool used=false;
                for(const auto& [id,g]:groups)if(g.plane==int(i)) {used=true;break;}
                if(!used&&(victim<0||planes[i].lastUse<planes[size_t(victim)].lastUse))victim=int(i);
            }
            if(victim>=0) {
                planes[size_t(victim)]=Plane{};
                auto& p=planes[size_t(victim)];
                p.legacy=legacy;p.footX=t.footX;p.footZ=t.footZ;p.maxDepth=maxDepth;p.minDepth=minDepth;
                p.maxSlope=t.maxSlope;p.maxWaterSlope=t.maxWaterSlope;p.epoch=~0ull;
                return victim;
            }
        }
        Plane p;p.legacy=legacy;p.footX=t.footX;p.footZ=t.footZ;p.maxDepth=maxDepth;p.minDepth=minDepth;
        p.maxSlope=t.maxSlope;p.maxWaterSlope=t.maxWaterSlope;p.epoch=~0ull;
        planes.push_back(std::move(p));
        return int(planes.size()-1);
    }
    // Same cell rules as World::mobilePlacement with the entity slot empty;
    // retailMobilePlacement's per-cell loop makes a footprint legal exactly
    // when every covered cell is (plus the footprint's own bounds test).
    void buildPlane(Plane& p) {
        const int W=width(),H=height();
        p.legal.assign(size_t(std::max(0,W))*std::max(0,H),0);
        ++stats.planeBuilds;
        if(W<=0||H<=0)return;
        if(p.legacy) {
            const auto* grid=legacyGrid(p);
            if(!grid||grid->empty())return;
            const int foot=std::clamp(std::max(p.footX,p.footZ),1,15);
            for(int z=0;z<H;++z)for(int x=0;x<W;++x)
                p.legal[size_t(z)*W+x]=grid->fits(x+foot/2,z+foot/2,foot);
            return;
        }
        std::vector<uint8_t> cell(size_t(W)*H,0);
        auto cellAt=[&](int cx,int cz) {
            const auto& source=w.mapPlacementCells_[size_t(cz)*W+cx];
            RetailPlacementCell result;
            result.low=source.low;
            result.high=std::max({w.heights_[size_t(cz)*W+cx],w.heights_[size_t(cz)*W+cx+1],
                                 w.heights_[size_t(cz+1)*W+cx],w.heights_[size_t(cz+1)*W+cx+1]});
            result.feature=source.feature;
            if(source.feature<0xfffa||source.feature==0xfffe) {
                const int ox=cx-(source.feature==0xfffe?source.backX:0);
                const int oz=cz-(source.feature==0xfffe?source.backZ:0);
                const auto index=w.mapPlacementCells_[size_t(oz)*W+ox].feature;
                const bool blocking=index<w.mapPlacementTypes_.size()&&w.mapPlacementTypes_[index].blocking;
                result.feature=blocking?0:0xffff;
            }
            return result;
        };
        for(int z=0;z+1<H;++z)for(int x=0;x+1<W;++x)
            cell[size_t(z)*W+x]=retailMobilePlacement(x,z,1,1,W,H,w.seaLevel_,p.maxDepth,p.minDepth,
                p.maxSlope,p.maxWaterSlope,0,false,1,cellAt,[](uint16_t){return 0x20u;},
                [](uint16_t){return RetailPlacementEntity{};});
        // Structures are static bodies: stamp them with their yard maps by
        // the mover's rule (World::mobilePlacement): '.' is open, a closable
        // 'c' yard is open while the script holds it open. The yard state is
        // part of the static epoch, so opening or closing one rebuilds.
        for(const auto& u:w.units_) {
            if(!u.alive()||u.embarked()||!u.type||!u.type->isStructure())continue;
            const int ux=footprintOrigin(u.x,u.type->footX),uz=footprintOrigin(u.z,u.type->footZ);
            const bool opened=yardOpen(u.id);
            for(int j=0;j<u.type->footZ;++j)for(int i=0;i<u.type->footX;++i) {
                const int cx=ux+i,cz=uz+j;
                if(cx<0||cz<0||cx>=W||cz>=H)continue;
                if(!u.type->yardMap.empty()) {
                    const char yard=u.type->yardMap[size_t(j)*u.type->footX+i];
                    if(yard=='.'||(opened&&(yard=='c'||yard=='C')))continue;
                }
                cell[size_t(cz)*W+cx]=0;
            }
        }
        // Separable rectangle test: run of legal cells to the right >= footX
        // on footZ consecutive rows, and the footprint's bounds (x+fx<W).
        std::vector<uint16_t> run(size_t(W)*H,0);
        for(int z=0;z<H;++z) {
            int r=0;
            for(int x=W-1;x>=0;--x) {
                r=cell[size_t(z)*W+x]?std::min(r+1,0xffff):0;
                run[size_t(z)*W+x]=uint16_t(r);
            }
        }
        for(int x=0;x<W;++x) {
            int r=0;
            for(int z=H-1;z>=0;--z) {
                r=run[size_t(z)*W+x]>=p.footX?r+1:0;
                p.legal[size_t(z)*W+x]=r>=p.footZ&&x+p.footX<W&&z+p.footZ<H;
            }
        }
    }
    bool yardOpen(int id) const {
        const auto script=w.unitScripts_.find(id);
        return script!=w.unitScripts_.end()&&script->second.yardOpen;
    }
    // Label static components by flood fill in cell order (deterministic).
    void labelPlane(Plane& p) {
        const int W=width(),H=height();
        p.comp.assign(p.legal.size(),-1);
        std::vector<int> queue;int next=0;
        for(int start=0;start<W*H;++start) {
            if(!p.legal[size_t(start)]||p.comp[size_t(start)]>=0)continue;
            p.comp[size_t(start)]=next;queue.assign(1,start);
            for(size_t head=0;head<queue.size();++head) {
                const int x=queue[head]%W,z=queue[head]/W;
                for(const auto& d:kDirections) {
                    if(!step(p,x,z,d[0],d[1]))continue;
                    const int c=(z+d[1])*W+x+d[0];
                    if(p.comp[size_t(c)]<0) {p.comp[size_t(c)]=next;queue.push_back(c);}
                }
            }
            ++next;
        }
    }
    int compAt(const Plane& p,int cell) const {
        return cell>=0&&size_t(cell)<p.comp.size()?p.comp[size_t(cell)]:-1;
    }
    const NavGrid* legacyGrid(const Plane& p) const {
        for(const auto& u:w.units_)if(u.type&&u.type->footX==p.footX&&u.type->footZ==p.footZ&&!u.type->canFly)
            return &w.navFor(u.type);
        return nullptr;
    }
    uint64_t worldEpoch() const {
        uint64_t e=mix(w.placementEpoch_,structureSignature);
        if(!placementPlane())for(const auto& g:w.navClasses_)e=mix(e,g.version());
        if(!placementPlane())e=mix(e,w.nav_.version());
        return e;
    }
    Plane& plane(int index) {
        auto& p=planes[size_t(index)];
        if(p.epoch!=epoch) {p.epoch=epoch;buildPlane(p);labelPlane(p);planeDebt+=2*uint64_t(width())*height();}
        p.lastUse=w.tickCounter_;
        return p;
    }
    bool legal(const Plane& p,int x,int z) const {
        return x>=0&&z>=0&&x<width()&&z<height()&&p.legal[size_t(z)*width()+x];
    }
    // Diagonal steps must clear both orthogonal neighbours (no corner cut).
    bool step(const Plane& p,int x,int z,int dx,int dz) const {
        if(!legal(p,x+dx,z+dz))return false;
        return !dx||!dz||(legal(p,x+dx,z)&&legal(p,x,z+dz));
    }

    // ---- groups and fields -------------------------------------------
    static bool plainMove(const Order& front) {
        return !front.targetId&&!front.load&&!front.unload&&!front.buildType&&!front.reclaimFeat&&
            !front.reclaimArea&&!front.manaBuildArea&&!front.repairTarget&&!front.wait&&!front.waitAttack&&
            !front.attackMove&&!front.patrol&&!front.guard&&!front.autoTarget&&!front.landing&&!front.park&&
            !front.buildRectangle&&!front.flightGoal&&!front.transportPickup&&!front.transportUnloadApproach&&
            !front.transportUnloadReleasePending&&!front.transportUnloadTransferDeferred&&!front.transportPassenger&&
            !front.productionExit;
    }
    bool supports(const Unit& u) const {
        if(!u.alive()||u.embarked()||u.underConstruction||!u.type||u.orders.empty())return false;
        const auto& t=*u.type;
        if(t.canFly||t.isStructure()||t.domain!=UnitType::Domain::Ground||t.footX<1||t.footZ<1||
           t.footX>8||t.footZ>8)return false;
        if(!placementPlane()&&t.footX!=t.footZ)return false;
        if(width()<2||height()<2)return false;
        const auto& leg=u.orders[World::currentLeg(u.orders)];
        if(!leg.groundMission||!leg.goal)return false;
        for(size_t i=0;i<=World::currentLeg(u.orders);++i)if(!plainMove(u.orders[i]))return false;
        return true;
    }
    std::pair<Fixed,Fixed> target(const Order& leg) const {
        return leg.missionTarget.value_or(std::pair{leg.x,leg.z});
    }
    // Nearest statically legal origin to a requested one. Past the near
    // search radius, like Retail, settle for the nearest origin the unit can
    // actually reach (its own static component); -1 only if there is none.
    int nearestLegal(const Plane& p,int x,int z,int reach=-1) const {
        if(legal(p,x,z))return z*width()+x;
        int best=-1;int64_t bestD=0;
        const int far=reach>=0?std::max(width(),height()):kGoalSearchCells;
        for(int r=1;r<=far&&best<0;++r)
            for(int dz=-r;dz<=r;++dz)for(int dx=-r;dx<=r;++dx) {
                if(std::max(std::abs(dx),std::abs(dz))!=r||!legal(p,x+dx,z+dz))continue;
                if(r>kGoalSearchCells&&compAt(p,(z+dz)*width()+x+dx)!=reach)continue;
                const int64_t d=int64_t(dx)*dx+int64_t(dz)*dz;
                const int index=(z+dz)*width()+x+dx;
                if(best<0||d<bestD||(d==bestD&&index<best)) {best=index;bestD=d;}
            }
        return best;
    }
    void leave(int id) {
        auto found=members.find(id);
        if(found==members.end())return;
        if(const auto* u=w.unit(id);u&&u->type&&found->second.slot>=0&&found->second.state!=Arrived)
            slotCells(found->second,u->type->footX,u->type->footZ,false);
        if(auto point=points.find(found->second.point);point!=points.end()&&--point->second.refs<=0)points.erase(point);
        auto group=groups.find(found->second.group);
        if(group!=groups.end()) {
            const auto& m=found->second;
            // A member that completes keeps its slot (it stands there); one
            // cancelled or re-ordered frees it for the rest of the group.
            if(m.slot>=0&&m.state!=Arrived) {
                auto slots=group->second.slots.find(m.requested);
                if(slots!=group->second.slots.end()&&size_t(m.slot)<slots->second.taken.size())
                    slots->second.taken[size_t(m.slot)]=0;
            }
            if(auto shared=group->second.sharing.find(m.requested);shared!=group->second.sharing.end()&&
               --shared->second<=0)group->second.sharing.erase(shared);
            if(--group->second.members<=0)groups.erase(group);
        }
        members.erase(found);
    }
    void registerMove(Unit& u) {
        leave(u.id);
        anchors.erase(u.id);yielding.erase(u.id);
        if(!supports(u))return;
        ++stats.registrations;
        const auto& leg=u.orders[World::currentLeg(u.orders)];
        const int planeIndex=planeFor(*u.type);
        auto& p=plane(planeIndex);
        const auto [tx,tz]=target(leg);
        const int gx=footprintOrigin(tx,u.type->footX),gz=footprintOrigin(tz,u.type->footZ);
        const int sx=footprintOrigin(u.x,u.type->footX),sz=footprintOrigin(u.z,u.type->footZ);
        const int reach=legal(p,sx,sz)?compAt(p,sz*width()+sx):-1;
        Member m;m.controller=leg.controller;m.goal=nearestLegal(p,gx,gz,reach);m.state=Waiting;m.requested=m.goal;
        m.point={u.player,leg.issuedTick,tx.v,tz.v};++points[m.point].refs;
        if(m.goal<0) {members[u.id]=m;return;}   // trapped on first move
        const int x=m.goal%width(),z=m.goal/width();
        const int goalComp=compAt(p,m.goal);
        Group* joined=nullptr;
        for(auto& [id,g]:groups) {
            if(g.player!=u.player||g.issuedTick!=leg.issuedTick||g.plane!=planeIndex)continue;
            // Seeds in different static components never share a field: a
            // member whose goal it cannot reach would descend to a
            // teammate's seed and hold there forever instead of retiring.
            if(g.comp!=goalComp)continue;
            if(x<g.minX-kClusterCells||x>g.maxX+kClusterCells||z<g.minZ-kClusterCells||z>g.maxZ+kClusterCells)continue;
            const bool seeded=std::binary_search(g.seeds.begin(),g.seeds.end(),m.goal);
            // A field in progress or complete is never re-seeded.
            if(g.field&&!seeded)continue;
            joined=&g;break;
        }
        if(!joined) {
            Group g;g.id=nextGroup++;g.player=u.player;g.plane=planeIndex;g.issuedTick=leg.issuedTick;g.comp=goalComp;
            g.minX=g.maxX=x;g.minZ=g.maxZ=z;
            joined=&groups.emplace(g.id,std::move(g)).first->second;
            ++stats.groups;
        }
        auto& g=*joined;
        if(!std::binary_search(g.seeds.begin(),g.seeds.end(),m.goal))
            g.seeds.insert(std::upper_bound(g.seeds.begin(),g.seeds.end(),m.goal),m.goal);
        g.minX=std::min(g.minX,x);g.maxX=std::max(g.maxX,x);g.minZ=std::min(g.minZ,z);g.maxZ=std::max(g.maxZ,z);
        {const int n=++g.sharing[m.goal];int& top=g.peak[m.goal];top=std::max(top,n);}++g.members;g.lastUse=w.tickCounter_;
        m.group=g.id;
        members[u.id]=m;
    }
    size_t liveFields() const {
        size_t n=0;for(const auto& [id,g]:groups)n+=(g.field!=nullptr)+(g.next!=nullptr);return n;
    }
    // At the cap a new field may only displace one that has served its
    // group for a while (oldest build first); otherwise the group waits for
    // a slot. Evicting the least recently used field every tick thrashed:
    // all live groups use theirs every tick.
    bool startField(Group& g) {
        while(liveFields()>=kMaxFields) {
            Group* victim=nullptr;
            for(auto& [id,o]:groups)
                if(o.field&&o.field->done&&w.tickCounter_-o.built>=kFieldTenure&&(!victim||o.built<victim->built))victim=&o;
            if(!victim)return false;
            victim->field.reset();victim->next.reset();victim->stale=false;++stats.fieldEvictions;
        }
        g.built=w.tickCounter_;
        auto f=std::make_unique<Field>();
        f->plane=g.plane;f->epoch=epoch;
        f->potential.assign(size_t(width())*height(),kUnreached);
        for(int s:g.seeds) {f->potential[size_t(s)]=0;f->buckets[0].push_back(s);++f->queued;}
        if(g.field)g.next=std::move(f);else g.field=std::move(f);
        return true;
    }
    // Dial's algorithm: edge costs <= 7 so eight circular buckets suffice.
    uint64_t advance(Field& f,uint64_t budget) {
        const auto& p=planes[size_t(f.plane)];
        const int W=width();uint64_t spent=0;
        while(f.queued&&spent<budget) {
            auto& bucket=f.buckets[f.current&7];
            if(bucket.empty()) {++f.current;continue;}
            const int cell=bucket.back();bucket.pop_back();--f.queued;
            if(f.potential[size_t(cell)]!=f.current)continue;   // stale entry
            const int x=cell%W,z=cell/W;
            for(const auto& d:kDirections) {
                ++spent;
                if(!step(p,x,z,d[0],d[1]))continue;
                const uint32_t next=f.current+(d[0]&&d[1]?kDiagonal:kOrthogonal);
                if(next>=kUnreached)continue;   // saturated: beyond the field's range
                auto& slot=f.potential[size_t((z+d[1])*W+x+d[0])];
                if(next<slot) {slot=uint16_t(next);f.buckets[next&7].push_back((z+d[1])*W+x+d[0]);++f.queued;}
            }
        }
        if(!f.queued) {f.done=true;for(auto& b:f.buckets)std::vector<int>().swap(b);}
        f.work+=spent;
        return spent;
    }
    void tick() {
        // Structures are bodies the mover always refuses: fold their layout
        // into the static epoch so the plane follows construction/death.
        uint64_t sig=0x6c6567696f6e;
        for(const auto& u:w.units_) {
            if(!u.alive()||u.embarked()||!u.type||!u.type->isStructure())continue;
            sig=mix(sig,uint64_t(u.id));sig=mix(sig,uint64_t(uint32_t(u.x.v))<<32|uint32_t(u.z.v));
            sig=mix(sig,yardOpen(u.id));
        }
        structureSignature=sig;
        const uint64_t e=worldEpoch();
        if(e!=lastWorldEpoch) {
            lastWorldEpoch=e;++epoch;
            // Terrain changed. A finished field keeps steering its group
            // (a stale potential can only misdirect, never make a step legal:
            // the plane and commitGroundStep decide legality) until its
            // replacement on the new plane is done. Half-built fields are
            // useless and restart. Arrival slots were proven on the old
            // plane: they are rebuilt (see restaleSlots).
            for(auto& [id,g]:groups) {
                g.next.reset();
                if(g.field&&!g.field->done)g.field.reset();
                g.stale=g.field!=nullptr;
                restaleSlots(g);
            }
        }
        prune();
        // Plane rebuilds and labelling are charged against the same
        // deterministic quota as field relaxations (debt carries over).
        uint64_t budget=kFieldQuota;
        auto settle=[&] {const uint64_t c=std::min(budget,planeDebt);budget-=c;planeDebt-=c;};
        settle();
        auto finish=[&](Group& g,Field& f,uint64_t spent) {
            budget-=std::min(budget,spent);stats.fieldWork+=spent;
            if(f.done) {++stats.fieldsBuilt;if(g.next) {g.field=std::move(g.next);g.stale=false;restaleSlots(g);}}
        };
        for(auto& [id,g]:groups) {
            if(budget==0)break;
            Field* f=g.next?g.next.get():g.field&&!g.field->done?g.field.get():nullptr;
            if(!f)continue;
            plane(g.plane);settle();
            if(budget==0)break;
            finish(g,*f,advance(*f,budget));
        }
        // Groups that need a field (none, or a stale one) start in id order.
        for(auto& [id,g]:groups) {
            if(budget==0)break;
            if((g.field&&!g.stale)||g.next)continue;
            plane(g.plane);settle();
            if(budget==0)break;
            if(!startField(g))break;
            Field& f=g.next?*g.next:*g.field;
            finish(g,f,advance(f,budget));
        }
    }
    // Members whose unit died, embarked, lost its orders or left Legion's
    // plain-move domain (no more move() calls) are dropped here, a bounded
    // number per tick in id order, so their groups, slots and point claims
    // are freed. The death edge also cancels directly.
    void prune() {
        constexpr size_t kPrunePerTick=256;
        if(members.empty()) {pruneCursor=0;return;}
        std::vector<int> ids;
        auto it=members.lower_bound(pruneCursor);
        for(size_t n=0;n<kPrunePerTick&&n<members.size();++n) {
            if(it==members.end())it=members.begin();
            ids.push_back(it->first);++it;
        }
        pruneCursor=it==members.end()?0:it->first;
        for(int id:ids) {
            const Unit* u=w.unit(id);
            if(u&&u->alive()&&!u->embarked()&&!u->orders.empty()&&supports(*u))continue;
            leave(id);
        }
        serviceYields();
    }
    // A settled body that is no longer idle (new order, death) forgets its
    // anchor. Yield steps advance one update per tick in id order: straight
    // toward the target cell, no turning, then a full stop.
    void serviceYields() {
        for(auto it=anchors.begin();it!=anchors.end();) {
            // (The completed leg itself lingers until World retires it.)
            const Unit* u=w.unit(it->first);
            if(!u||!u->alive()||(!u->orders.empty()&&!(u->orders[World::currentLeg(u->orders)].mission.pending&0x500)))
                {yielding.erase(it->first);it=anchors.erase(it);}
            else ++it;
        }
        for(auto it=yielding.begin();it!=yielding.end();) {
            Unit* u=w.unit(it->first);
            if(!u||!u->alive()||!u->orders.empty()) {it=yielding.erase(it);continue;}
            const int W=width(),fx=u->type->footX,fz=u->type->footZ;
            const Fixed tx=centre(it->second.cell%W,fx),tz=centre(it->second.cell/W,fz);
            const int64_t dx=int64_t(tx.v)-u->x.v,dz=int64_t(tz.v)-u->z.v;
            const int64_t length=isqrtFloor(uint64_t(dx*dx+dz*dz));
            if(length==0||++it->second.ticks>45) {u->speed=Fixed();u->turnReqBam=0;it=yielding.erase(it);continue;}
            const int64_t travel=std::min<int64_t>(std::max<int64_t>(1,retailGroundSpeedCap(u->type->maxVel*w.groundTerrainMultiplier(*u),u->groundPitch,0).v),length);
            const Fixed sx=Fixed::raw(int32_t(dx*travel/length)),sz=Fixed::raw(int32_t(dz*travel/length));
            const Fixed bx=u->x,bz=u->z;
            w.commitGroundStep(*u,sx,sz,true);
            u->speed=Fixed();u->turnReqBam=0;
            if(u->x==bx&&u->z==bz) {it=yielding.erase(it);continue;}
            ++stats.slides;++it;
        }
    }
    // Open a lane through settled arrivals: first the cell this member
    // wants, else any neighbouring cell nearer its own goal (in a packed
    // lattice the way through runs BETWEEN goals, where two rows can part).
    // A side cell becomes a committed shuffle while its blockers yield.
    bool yieldLane(const Unit& u,Member& m,const Plane& p,int nx,int nz) {
        if(requestYield(u,nx,nz))return true;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        const int gx=m.goal%W,gz=m.goal/W;
        const int64_t here=int64_t(gx-ox)*(gx-ox)+int64_t(gz-oz)*(gz-oz);
        std::array<std::pair<int64_t,int>,8> options{};int count=0;
        for(int k=0;k<8;++k) {
            const auto& d=kDirections[size_t(k)];
            const int cx=ox+d[0],cz=oz+d[1];
            if((cx==nx&&cz==nz)||!step(p,ox,oz,d[0],d[1]))continue;
            const int64_t v=int64_t(gx-cx)*(gx-cx)+int64_t(gz-cz)*(gz-cz);
            if(v<here)options[size_t(count++)]={v,k};
        }
        std::sort(options.begin(),options.begin()+count);
        for(int i=0;i<count;++i) {
            const auto& d=kDirections[size_t(options[size_t(i)].second)];
            if(!requestYield(u,ox+d[0],oz+d[1]))continue;
            m.detour=(oz+d[1])*W+ox+d[0];m.detourTicks=0;m.detourFace=false;
            return true;
        }
        return false;
    }
    // The cells this member wants are held by settled same-player arrivals:
    // ask each of them to step one cell clear of that footprint, staying
    // within one cell of its own goal origin. All blockers must be able to
    // yield, or none is asked. Returns whether a yield was committed.
    bool requestYield(const Unit& u,int nx,int nz) {
        const int fx=u.type->footX,fz=u.type->footZ,W=width();
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        std::array<int,16> ids{};int count=0;
        for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
            const int cx=nx+i,cz=nz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=w.occ_[size_t(cz)*w.occW_+cx];
            if(!o||o==u.id)continue;
            bool seen=false;for(int k=0;k<count;++k)seen|=ids[size_t(k)]==o;
            if(seen)continue;
            if(count==16)return false;
            ids[size_t(count++)]=o;
        }
        if(!count)return false;
        std::sort(ids.begin(),ids.begin()+count);
        std::array<int,16> cells{};
        for(int k=0;k<count;++k) {
            const int id=ids[size_t(k)];
            const Unit* b=w.unit(id);
            auto anchor=anchors.find(id);
            if(!b||!b->alive()||b->player!=u.player||!b->orders.empty()||b->speed!=Fixed()||
               anchor==anchors.end()||yielding.count(id)||anchor->second.yields>=kMaxYields)return false;
            const int bfx=b->type->footX,bfz=b->type->footZ;
            const int bx=footprintOrigin(b->x,bfx),bz=footprintOrigin(b->z,bfz);
            const int gx=anchor->second.goal%W,gz=anchor->second.goal/W;
            int best=-1;int64_t bestD=0;
            for(const auto& d:kDirections) {
                const int cx=bx+d[0],cz=bz+d[1];
                if(std::abs(cx-gx)>1||std::abs(cz-gz)>1)continue;
                // Clear of the member's wanted footprint and its current one.
                auto overlaps=[&](int ax,int az){return cx<ax+fx&&ax<cx+bfx&&cz<az+fz&&az<cz+bfz;};
                if(overlaps(nx,nz)||overlaps(ox,oz))continue;
                if(!w.mobilePlacement(*b,cx,cz,false))continue;
                if(d[0]&&d[1]&&(!w.mobilePlacement(*b,cx,bz,false)||!w.mobilePlacement(*b,bx,cz,false)))continue;
                const int64_t dd=int64_t(cx-gx)*(cx-gx)+int64_t(cz-gz)*(cz-gz);
                if(best<0||dd<bestD) {best=cz*W+cx;bestD=dd;}
            }
            if(best<0)return false;
            cells[size_t(k)]=best;
        }
        for(int k=0;k<count;++k) {
            ++anchors[ids[size_t(k)]].yields;
            yielding[ids[size_t(k)]]=Yield{cells[size_t(k)],0};
        }
        return true;
    }

    // ---- movement -------------------------------------------------------
    static Fixed centre(int origin,int foot) {return Fixed::fromInt(origin*16+foot*8);}
    // Aim point for a step into an ADJACENT origin cell: move only along the
    // axes whose origin changes (to the new cell's centre line) and keep the
    // other coordinate. Aiming at the cell centre would pull the body back
    // along the unchanged axis -- a visible back-and-forth in a crowd.
    std::pair<Fixed,Fixed> stepAim(const Unit& u,int cell) const {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        const int nx=cell%W,nz=cell/W;
        return {nx!=ox?centre(nx,fx):u.x,nz!=oz?centre(nz,fz):u.z};
    }
    // Every origin the body passes through moving in a straight line from
    // (x0,z0) to (x1,z1) must be legal; an exact corner crossing must clear
    // both side cells. Raw 16.16 coordinates shifted to origin space.
    bool sweep(const Plane& p,const Unit& u,Fixed x0,Fixed z0,Fixed x1,Fixed z1) const {
        const int64_t bx=int64_t(u.type->footX-1)*8*Fixed::kOne,bz=int64_t(u.type->footZ-1)*8*Fixed::kOne;
        const int64_t ax=int64_t(x0.v)-bx,az=int64_t(z0.v)-bz,ex=int64_t(x1.v)-bx,ez=int64_t(z1.v)-bz;
        int cx=int(ax>>20),cz=int(az>>20);const int tx=int(ex>>20),tz=int(ez>>20);
        if(!legal(p,cx,cz))return false;
        const int64_t dx=ex-ax,dz=ez-az;
        const int sx=dx>0?1:-1,sz=dz>0?1:-1;
        for(int guard=0;(cx!=tx||cz!=tz)&&guard<4*kFormationLineCells+8;++guard) {
            const bool canX=cx!=tx,canZ=cz!=tz;
            int64_t nx=0,nz=0;   // distance to the next boundary along each axis
            if(canX)nx=sx>0?(int64_t(cx+1)<<20)-ax:ax-(int64_t(cx)<<20);
            if(canZ)nz=sz>0?(int64_t(cz+1)<<20)-az:az-(int64_t(cz)<<20);
            // Compare nx/|dx| with nz/|dz| without dividing.
            int order=0;   // -1 x first, 1 z first, 0 corner
            if(!canZ)order=-1;else if(!canX)order=1;
            else {
                const __int128 lx=__int128(nx)*(dz<0?-dz:dz),lz=__int128(nz)*(dx<0?-dx:dx);
                order=lx<lz?-1:lx>lz?1:0;
            }
            if(order==0) {
                if(!legal(p,cx+sx,cz)||!legal(p,cx,cz+sz)||!legal(p,cx+sx,cz+sz))return false;
                cx+=sx;cz+=sz;
            } else if(order<0) {cx+=sx;if(!legal(p,cx,cz))return false;}
            else {cz+=sz;if(!legal(p,cx,cz))return false;}
        }
        return cx==tx&&cz==tz;
    }
    // Mobile bodies at an origin (static legality is checked separately).
    bool bodiesFree(const Unit& u,int x,int z) const {
        const int fx=u.type->footX,fz=u.type->footZ;
        if(w.occW_>0) {
            for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
                const int cx=x+i,cz=z+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                const int32_t o=w.occ_[size_t(cz)*w.occW_+cx];
                if(o&&o!=u.id)return false;
            }
        }
        if(placementPlane())return w.mobilePlacement(u,x,z,false);
        const int foot=std::clamp(std::max(fx,fz),1,15);
        return w.cellFree(centre(x,fx),centre(z,fz),u.id,foot);
    }
    bool stepFree(const Unit& u,int ox,int oz,int nx,int nz) const {
        if(nx==ox&&nz==oz)return true;
        if(!bodiesFree(u,nx,nz))return false;
        return nx==ox||nz==oz||(bodiesFree(u,nx,oz)&&bodiesFree(u,ox,nz));
    }
    void hold(Unit& u,Member& m) {
        if(m.state!=Holding)m.nextDetour=8;
        // Holding is a full stop with a constant heading: no creeping, no
        // re-aiming. The body wakes when a candidate cell frees (rechecked
        // against occupancy each update), not on a timer.
        u.speed=Fixed();u.turnReqBam=0;
        if(m.state!=Holding)++stats.holds;
        m.state=Holding;++m.held;
    }
    void complete(Unit& u,Member& m,bool contact) {
        auto& leg=u.orders[World::currentLeg(u.orders)];
        u.speed=Fixed();u.turnReqBam=0;
        leg.mission.pending|=0x500;
        leg.controller=0;leg.navigationExhausted=true;
        if(uint32_t(u.routeStamp)<=w.tickCounter_-6u)u.routeStamp=0;
        ++stats.arrivals;if(contact)++stats.contactArrivals;
        m.state=Arrived;
        anchors[u.id]=Anchor{m.goal,0};
        leave(u.id);
    }
    void trapped(Unit& u,Member& m) {
        // No legal route exists for this footprint: stop at once (zero speed,
        // constant heading, no probing). The order is kept for a grace
        // period so a gate opening or a wall coming down (a new static
        // epoch) resumes it; after that the leg is retired as Retail retires
        // an unreachable goal. Later queued legs proceed.
        if(m.state!=Trapped) {m.state=Trapped;m.trappedSince=w.tickCounter_;m.trappedEpoch=epoch;++stats.trapped;}
        u.speed=Fixed();u.turnReqBam=0;
        if(w.tickCounter_-m.trappedSince<kTrappedRetire)return;
        leave(u.id);
        w.dropLeg(u);
        u.routeStamp=-1;
    }
    // A body that starts on an origin the static plane rejects (inside a
    // yard, or on a cell a structure now covers) leaves it the native way.
    void escape(Unit& u,Fixed maximum,const Order& leg) {
        ++stats.escapes;
        const auto [tx,tz]=target(leg);
        const RetailSteeringPoint start{Fixed::fromInt(u.x.floorInt()),Fixed::fromInt(u.z.floorInt())},end{tx,tz};
        const Bam heading=u.heading;const Fixed x=u.x,z=u.z;
        const auto d=w.steerGround(u,start,end,end,maximum);
        w.commitGroundStep(u,d.s,d.c);
        // A refused escape step is a stop, not a turn in place.
        if(u.x==x&&u.z==z) {u.heading=heading;u.speed=Fixed();u.turnReqBam=0;}
    }
    // Steepest legal descent, measured per unit of path length (an
    // orthogonal drop of 5 and a diagonal drop of 7 are equally steep).
    // Ties -- common on open ground, where a region field is "distance to the
    // nearest goal of anyone" -- go to the neighbour nearest this member's
    // OWN goal, then direction order. Plain first-found tie breaking pulled
    // bodies toward other members' goals and folded formations into a file.
    // Potential strictly decreases, so no step can cycle.
    int descend(const Plane& p,const Field& f,int x,int z,int goal) const {
        const int W=width(),gx=goal%W,gz=goal/W;
        const uint16_t here=f.potential[size_t(z)*W+x];
        int best=-1;int64_t bestSlope=0,bestD=0;
        for(const auto& d:kDirections) {
            if(!step(p,x,z,d[0],d[1]))continue;
            const int cell=(z+d[1])*W+x+d[0];
            const uint16_t v=f.potential[size_t(cell)];
            if(v>=here)continue;
            const int64_t slope=int64_t(here-v)*(d[0]&&d[1]?kOrthogonal:kDiagonal);
            const int64_t dx=gx-x-d[0],dz=gz-z-d[1],dd=dx*dx+dz*dz;
            if(best<0||slope>bestSlope||(slope==bestSlope&&dd<bestD)) {best=cell;bestSlope=slope;bestD=dd;}
        }
        return best;
    }
    // Rebuild a group's arrival slots on its current plane and field,
    // around the cells members and arrivals already hold (those claims live
    // in `points`, so a rebuilt slot is never handed out twice).
    static void restaleSlots(Group& g) {
        for(auto& [seed,slot]:g.slots) {slot.built=false;slot.stale=true;slot.cells.clear();slot.taken.clear();}
    }
    Group::Slots& slotsFor(const Member& m,Group& g,const Plane& p,int seed,int count,int fx,int fz) {
        auto& s=g.slots[seed];
        if(s.built)return s;
        s.built=true;
        const Field& f=*g.field;
        const int W=width(),H=height(),sx=seed%W,sz=seed/W;
        const int foot=std::max(fx,fz);
        for(int radius=int(isqrtFloor(uint64_t(count)))*foot+2*foot+4,attempt=0;attempt<4;++attempt,radius*=2) {
            const int x0=std::max(0,sx-radius),z0=std::max(0,sz-radius);
            const int x1=std::min(W-1,sx+radius),z1=std::min(H-1,sz+radius);
            std::vector<std::pair<uint16_t,int>> order;
            for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
                const int cell=z*W+x;
                if(legal(p,x,z)&&f.potential[size_t(cell)]!=kUnreached&&(!s.stale||slotFree(m,cell,fx,fz)))
                    order.push_back({f.potential[size_t(cell)],cell});
            }
            std::sort(order.begin(),order.end());
            const int bw=x1-x0+1+fx,bh=z1-z0+1+fz;
            std::vector<uint8_t> used(size_t(bw)*bh,0);
            s.cells.clear();
            for(const auto& [potential,cell]:order) {
                const int x=cell%W-x0,z=cell/W-z0;
                bool clear=true;
                for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!used[size_t(z+j)*bw+x+i];
                if(!clear)continue;
                for(int j=0;j<fz;++j)for(int i=0;i<fx;++i)used[size_t(z+j)*bw+x+i]=1;
                s.cells.push_back(cell);s.reach=potential;
                if(int(s.cells.size())>=count)break;
            }
            if(int(s.cells.size())>=count)break;
        }
        s.taken.assign(s.cells.size(),0);
        return s;
    }
    // Members sharing one point claim the innermost free slot once they are
    // close, so arrivals pack from the point outward.
    // Nearest origin to a target centre (px) that this body may take as its
    // slot: statically legal, in the group's component, its footprint clear
    // of every claimed cell of the point, and its centre inside the point's
    // packed area. -1 if none within the search window.
    int formationCell(const Unit& u,const Plane& p,int comp,const Point& pt,int64_t px,int64_t pz,int64_t tx,int64_t tz) const {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int sx=footprintOrigin(Fixed::fromInt(int32_t(tx)),fx),sz=footprintOrigin(Fixed::fromInt(int32_t(tz)),fz);
        int best=-1;int64_t bestD=0;
        for(int r=0;r<=48;++r) {
            if(best>=0&&int64_t(r-1)*16*int64_t(r-1)*16>bestD)break;
            for(int dz=-r;dz<=r;++dz)for(int dx=-r;dx<=r;++dx) {
                if(std::max(std::abs(dx),std::abs(dz))!=r)continue;
                const int x=sx+dx,z=sz+dz;
                if(!legal(p,x,z)||compAt(p,z*W+x)!=comp)continue;
                const int64_t cx=int64_t(x)*16+fx*8,cz=int64_t(z)*16+fz*8;
                if((cx-px)*(cx-px)+(cz-pz)*(cz-pz)>pt.limit*pt.limit)continue;
                const int64_t d=(cx-tx)*(cx-tx)+(cz-tz)*(cz-tz);
                if(best>=0&&(d>bestD||(d==bestD&&z*W+x>best)))continue;
                bool clear=true;
                for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!pt.cells.count((z+j)*W+x+i);
                if(clear) {best=z*W+x;bestD=d;}
            }
        }
        return best;
    }
    // A member walled off from its slot: breadth-first over legal origins
    // whose footprint no other body covers (window of 24 cells), and take the
    // reached free slot cell nearest the point. Reached means a way in
    // exists past the bodies standing now.
    int reachableFormationCell(const Unit& u,const Plane& p,const Point& pt,int64_t px,int64_t pz) const {
        constexpr int R=24,S=2*R+1;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        auto open=[&](int x,int z) {
            if(!legal(p,x,z))return false;
            for(int j=0;j<fz;++j)for(int i=0;i<fx;++i) {
                const int cx=x+i,cz=z+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                const int32_t o=w.occ_[size_t(cz)*w.occW_+cx];
                if(o&&o!=u.id)return false;
            }
            return true;
        };
        std::vector<uint8_t> seen(size_t(S)*S,0);
        std::vector<int> queue;queue.reserve(size_t(S)*S);
        seen[size_t(R*S+R)]=1;queue.push_back(R*S+R);
        int best=-1;int64_t bestD=0;
        for(size_t head=0;head<queue.size();++head) {
            const int local=queue[head],lx=local%S,lz=local/S,x=ox+lx-R,z=oz+lz-R;
            const int64_t cx=int64_t(x)*16+fx*8,cz=int64_t(z)*16+fz*8;
            const int64_t d=(cx-px)*(cx-px)+(cz-pz)*(cz-pz);
            if(d<=pt.limit*pt.limit&&(best<0||d<bestD)) {
                bool clear=true;
                for(int j=0;j<fz&&clear;++j)for(int i=0;i<fx&&clear;++i)clear=!pt.cells.count((z+j)*W+x+i);
                if(clear) {best=z*W+x;bestD=d;}
            }
            for(const auto& dd:kDirections) {
                const int nlx=lx+dd[0],nlz=lz+dd[1];
                if(nlx<0||nlz<0||nlx>=S||nlz>=S||seen[size_t(nlz*S+nlx)])continue;
                if(!step(p,x,z,dd[0],dd[1])||!open(x+dd[0],z+dd[1]))continue;
                if(dd[0]&&dd[1]&&(!open(x+dd[0],z)||!open(x,z+dd[1])))continue;
                seen[size_t(nlz*S+nlx)]=1;queue.push_back(nlz*S+nlx);
            }
        }
        return best;
    }
    bool formationMember(const Member& m) const {
        const auto found=points.find(m.point);
        return found!=points.end()&&found->second.assigned&&found->second.limit>0;
    }
    void takeFormation(Member& m,const Unit& u,int cell) {
        m.goal=cell;m.slot=0;m.lineCell=-1;
        slotCells(m,u.type->footX,u.type->footZ,true);
    }
    // Every member sent to one point in one command gets its slot at once,
    // by formation: its offset from the members' centroid, scaled so the
    // formation's spread matches the packed disc that many bodies occupy,
    // placed around the point. Members are served front first (farthest
    // along the centroid->point direction), so the front of the crowd takes
    // the far side of the area and nobody has to cross a settled body.
    void assignFormation(Point& pt,const std::tuple<int,uint32_t,int32_t,int32_t>& key) {
        pt.assigned=true;
        const int64_t px=int64_t(std::get<2>(key))>>16,pz=int64_t(std::get<3>(key))>>16;
        std::vector<std::pair<int,Member*>> list;
        int64_t sumX=0,sumZ=0,area=0,areaGap=0,firstSide=0;bool mixed=false;
        for(auto& [id,mm]:members) {
            if(mm.point!=key||mm.goal<0||mm.slot>=0)continue;
            const Unit* v=w.unit(id);
            if(!v||!v->type||groups.find(mm.group)==groups.end())continue;
            list.push_back({id,&mm});
            sumX+=v->x.v>>16;sumZ+=v->z.v>>16;
            const int64_t side=std::max(v->type->footX,v->type->footZ);area+=side*side*256;
            areaGap+=(side+1)*(side+1)*256;
            if(!firstSide)firstSide=side;
            mixed|=side!=firstSide;
        }
        // Bodies of different sizes never tile: give each a cell of clearance.
        if(mixed)area=areaGap;
        if(list.size()<2)return;
        const int64_t n=int64_t(list.size());
        pt.centreX=sumX/n;pt.centreZ=sumZ/n;
        const int64_t packed=isqrtFloor(uint64_t(area)*10000/31416);
        pt.limit=packed+80;
        int64_t spread=0;
        for(const auto& [id,mm]:list) {
            const Unit* v=w.unit(id);
            const int64_t ox=(v->x.v>>16)-pt.centreX,oz=(v->z.v>>16)-pt.centreZ;
            spread+=ox*ox+oz*oz;
        }
        // RMS radius of a uniform disc of radius a is a/sqrt(2).
        const int64_t rms2=isqrtFloor(uint64_t(2*spread/n));
        if(rms2>packed) {pt.scaleNum=packed;pt.scaleDen=rms2;} else {pt.scaleNum=pt.scaleDen=1;}
        const int64_t ax=px-pt.centreX,az=pz-pt.centreZ;
        std::vector<std::tuple<int64_t,int,Member*>> order;
        for(const auto& [id,mm]:list) {
            const Unit* v=w.unit(id);
            const int64_t ox=(v->x.v>>16)-pt.centreX,oz=(v->z.v>>16)-pt.centreZ;
            order.push_back({-(ox*ax+oz*az),id,mm});
        }
        std::sort(order.begin(),order.end(),[](const auto& a,const auto& b) {
            return std::get<0>(a)!=std::get<0>(b)?std::get<0>(a)<std::get<0>(b):std::get<1>(a)<std::get<1>(b);});
        for(const auto& [key2,id,mm]:order) {
            const Unit* v=w.unit(id);
            const Group& gg=groups.find(mm->group)->second;
            const Plane& pp=plane(gg.plane);
            const int64_t ox=(v->x.v>>16)-pt.centreX,oz=(v->z.v>>16)-pt.centreZ;
            const int cell=formationCell(*v,pp,gg.comp,pt,px,pz,px+ox*pt.scaleNum/pt.scaleDen,pz+oz*pt.scaleNum/pt.scaleDen);
            if(cell>=0)takeFormation(*mm,*v,cell);
        }
    }
    // Shared points: formation slots (assigned once for the whole point; a
    // later joiner maps its own offset the same way). A member walled off
    // from its slot re-chooses the free cell nearest itself every 20 held
    // updates, inside the same area.
    bool formationSlot(const Unit& u,Member& m,const Group& g,const Plane& p) {
        auto found=points.find(m.point);
        if(found==points.end()||found->second.refs<2)return false;
        auto& pt=found->second;
        if(!pt.assigned)assignFormation(pt,m.point);
        if(!pt.assigned||pt.limit<=0)return true;
        const int64_t px=int64_t(std::get<2>(m.point))>>16,pz=int64_t(std::get<3>(m.point))>>16;
        const int64_t ux=u.x.v>>16,uz=u.z.v>>16;
        if(m.slot>=0) {
            if(m.state!=Holding||m.held<20||m.held%20)return true;
            slotCells(m,u.type->footX,u.type->footZ,false);
            const int cell=reachableFormationCell(u,p,pt,px,pz);
            if(cell>=0)m.goal=cell;
            slotCells(m,u.type->footX,u.type->footZ,true);m.lineCell=-1;
            return true;
        }
        // No free cell was left for this member: it walks to the point and
        // looks again (by reachability) only while it is held.
        if(m.slot==-2) {
            if(m.state!=Holding||m.held<20||m.held%20)return true;
            const int cell=reachableFormationCell(u,p,pt,px,pz);
            if(cell>=0)takeFormation(m,u,cell);
            return true;
        }
        const int64_t ox=ux-pt.centreX,oz=uz-pt.centreZ;
        const int cell=formationCell(u,p,g.comp,pt,px,pz,px+ox*pt.scaleNum/pt.scaleDen,pz+oz*pt.scaleNum/pt.scaleDen);
        if(cell>=0)takeFormation(m,u,cell);else m.slot=-2;
        return true;
    }
    void claimSlot(const Unit& u,Member& m,Group& g,const Plane& p,int here) {
        if(m.requested<0)return;
        if(formationSlot(u,m,g,p))return;
        bool nearest=false;
        if(m.slot>=0) {
            // A claimed slot walled off by bodies that settled first is
            // re-chosen every 20 held updates: take the nearest free slot,
            // which fills the area from the side this member stands on.
            if(m.state!=Holding||m.held<20||m.held%20)return;
            auto found=g.slots.find(m.requested);
            if(found==g.slots.end())return;
            if(size_t(m.slot)<found->second.taken.size())found->second.taken[size_t(m.slot)]=0;
            slotCells(m,u.type->footX,u.type->footZ,false);
            m.slot=-1;nearest=true;
        }
        const auto sharing=g.sharing.find(m.requested);
        if(sharing==g.sharing.end()||sharing->second<2)return;
        auto& s=slotsFor(m,g,p,m.requested,sharing->second,u.type->footX,u.type->footZ);
        const uint16_t potential=g.field->potential[size_t(here)];
        const int foot=std::max(u.type->footX,u.type->footZ);
        if(potential==kUnreached||potential>uint32_t(s.reach)+uint32_t(12*kOrthogonal*foot))return;
        const int W=width(),ux=here%W,uz=here/W;
        // Back-to-front: claim the free slot farthest along this member's
        // own approach direction (ties: nearest the approach axis). Every
        // later arrival then finds the cells between it and its slot still
        // empty, so nobody has to cross a settled body. A member re-claiming
        // after being walled off takes the nearest free slot instead.
        const int seedX=m.requested%W,seedZ=m.requested/W;
        const int64_t ax=seedX-ux,az=seedZ-uz;
        int best=-1;int64_t bestScore=0,bestSide=0;
        for(size_t i=0;i<s.cells.size();++i) {
            if(s.taken[i]||!slotFree(m,s.cells[i],u.type->footX,u.type->footZ))continue;
            // A slot a corpse, feature or structure now covers (or cut off)
            // is never claimed: claiming it would re-register this member
            // and claim it again, forever.
            if(!legal(p,s.cells[i]%W,s.cells[i]/W)||g.field->potential[size_t(s.cells[i])]==kUnreached)continue;
            const int64_t cx=s.cells[i]%W,cz=s.cells[i]/W;
            int64_t score,side;
            if(nearest) {score=-((cx-ux)*(cx-ux)+(cz-uz)*(cz-uz));side=0;}
            else {
                score=(cx-seedX)*ax+(cz-seedZ)*az;
                side=std::abs((cx-seedX)*az-(cz-seedZ)*ax);
            }
            if(best<0||score>bestScore||(score==bestScore&&side<bestSide)) {best=int(i);bestScore=score;bestSide=side;}
        }
        if(best<0)return;
        s.taken[size_t(best)]=1;m.slot=best;m.goal=s.cells[size_t(best)];m.lineCell=-1;
        slotCells(m,u.type->footX,u.type->footZ,true);
    }
    void move(Unit& u,Fixed maximum) {
        auto found=members.find(u.id);
        const auto& leg=u.orders[World::currentLeg(u.orders)];
        if(leg.mission.pending&0x500||!leg.controller) {w.brakeGround(u);return;}
        // A new controller, or terrain that changed since this body was
        // found trapped, means a fresh registration (new goal resolution).
        if(found==members.end()||found->second.controller!=leg.controller||
           (found->second.state==Trapped&&found->second.trappedEpoch!=epoch)) {
            registerMove(u);found=members.find(u.id);
            if(found==members.end()) {w.brakeGround(u);return;}
        }
        auto& m=found->second;
        ++stats.moves;
        if(m.goal<0) {trapped(u,m);return;}
        auto group=groups.find(m.group);
        if(group==groups.end()) {registerMove(u);w.brakeGround(u);return;}
        auto& g=group->second;g.lastUse=w.tickCounter_;
        const auto& p=plane(g.plane);
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        if(!legal(p,ox,oz)) {escape(u,maximum,leg);return;}
        // A goal origin can stop being legal (a building went up on it).
        if(!legal(p,m.goal%W,m.goal/W)) {registerMove(u);w.brakeGround(u);return;}
        const int here=oz*W+ox;
        const Field* f=g.field&&g.field->done?g.field.get():nullptr;
        if(f)claimSlot(u,m,g,p,here);
        if(!m.route.empty()) {
            while(!m.route.empty()&&m.route.front()==here)m.route.erase(m.route.begin());
            if(m.route.empty()||++m.routeTicks>240||(m.state==Holding&&m.held>=30)) {m.route.clear();m.lineCell=-1;}
            else {
                // Route cells are adjacent and were proven clear of still
                // bodies cell by cell: follow them one at a time (a pulled
                // string could clip a body the search went around).
                const int aim=m.route.front();
                // A short way around still bodies is walked without turning
                // the body (a crowd shuffle), not as a U-turn and back.
                const auto [ax,az]=stepAim(u,aim);
                drive(u,m,p,nullptr,maximum,ax,az,false,false,false);
                return;
            }
        }
        if(m.detour>=0) {
            // A committed side-step runs to completion (or a short timeout):
            // no re-deciding every update, so no back-and-forth.
            if(here==m.detour||++m.detourTicks>45||!legal(p,m.detour%W,m.detour/W)) {m.detour=-1;m.lineCell=-1;}
            else {
                // Lateral shuffles keep facing the route: no heading thrash.
                // A keep-right side-step turns the body with it: two units
                // passing each other visibly yield (measured: not turning
                // here gridlocked opposing columns). A flow-around shuffle in
                // a crowd does not turn it (measured: turning there reads as
                // spinning in a held crowd).
                const auto [ax,az]=stepAim(u,m.detour);
                drive(u,m,p,nullptr,maximum,ax,az,false,false,m.detourFace);
                return;
            }
        }
        const int goalX=m.goal%W,goalZ=m.goal/W;
        const Fixed gx=centre(goalX,fx),gz=centre(goalZ,fz);
        // Progress is the integer distance still to go: field potential, or
        // squared cells to the own goal once that is in a straight line.
        {
            const uint32_t left=uint32_t(std::min<int64_t>(0xfffffff,
                f&&f->potential[size_t(here)]!=kUnreached&&f->potential[size_t(here)]>0
                    ? int64_t(f->potential[size_t(here)])*64
                    : (int64_t(goalX-ox)*(goalX-ox)+int64_t(goalZ-oz)*(goalZ-oz))));
            if(left<m.progress) {m.progress=left;m.stalled=0;m.detourCount=0;} else ++m.stalled;
            if(m.stalled>=20&&contactArrival(u,m)) {complete(u,m,true);return;}
        }
        Fixed aimX=gx,aimZ=gz;
        bool direct=false;
        if(here==m.goal)direct=true;
        else {
            if(m.lineCell!=here) {
                m.lineCell=here;
                // A formation member walks its own straight lane from farther out:
                // descending the shared field first funnels the crowd into a file.
                const int reach=m.slot>=0&&formationMember(m)?kFormationLineCells:kLineCells;
                m.line=std::max(std::abs(goalX-ox),std::abs(goalZ-oz))<=reach&&sweep(p,u,u.x,u.z,gx,gz);
            }
            direct=m.line;
        }
        if(!direct) {
            if(!f) {m.state=Waiting;w.brakeGround(u);return;}
            const uint16_t potential=f->potential[size_t(here)];
            if(potential==kUnreached) {trapped(u,m);return;}
            // String-pull along the descent chain: aim at the farthest of the
            // next few cells reachable in a straight legal line.
            int cell=descend(p,*f,ox,oz,m.goal);
            if(cell<0) {
                // Inside the goal region the field is flat (potential 0) but
                // this member's own goal is not in line: walk its seed set by
                // re-entering the goal's neighbourhood through the nearest
                // legal neighbour that reduces octile distance to the goal.
                int best=-1;int64_t bestD=int64_t(goalX-ox)*(goalX-ox)+int64_t(goalZ-oz)*(goalZ-oz);
                for(const auto& d:kDirections) {
                    if(!step(p,ox,oz,d[0],d[1]))continue;
                    const int64_t dd=int64_t(goalX-ox-d[0])*(goalX-ox-d[0])+int64_t(goalZ-oz-d[1])*(goalZ-oz-d[1]);
                    if(dd<bestD) {bestD=dd;best=(oz+d[1])*W+ox+d[0];}
                }
                if(best<0) {hold(u,m);return;}
                cell=best;
            } else {
                int chain=cell;
                for(int k=0;k<3;++k) {
                    const int next=descend(p,*f,chain%W,chain/W,m.goal);
                    if(next<0)break;
                    if(!sweep(p,u,u.x,u.z,centre(next%W,fx),centre(next/W,fz)))break;
                    chain=next;
                }
                cell=chain;
            }
            aimX=centre(cell%W,fx);aimZ=centre(cell/W,fz);
        }
        drive(u,m,p,f,maximum,aimX,aimZ,here==m.goal,direct);
    }
    void drive(Unit& u,Member& m,const Plane& p,const Field* f,Fixed maximum,Fixed aimX,Fixed aimZ,bool final,
               bool towardGoal=false,bool face=true) {
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        int64_t dx=int64_t(aimX.v)-u.x.v,dz=int64_t(aimZ.v)-u.z.v;
        int64_t length=isqrtFloor(uint64_t(dx*dx+dz*dz));
        if(final&&length<=Fixed::kOne/2) {
            // Exactly on the goal centre (sub-pixel): this leg is complete.
            complete(u,m,false);return;
        }
        // Heading: turn toward the travel direction at the authored rate.
        // Motion follows the legal direction exactly; speed is limited while
        // the body still faces away from it, so turning never sweeps the
        // footprint off the proved line (no orbiting, no wall pushing).
        const Fixed multiplier=w.groundTerrainMultiplier(u);
        const auto wanted=retailHeadingToPort(uint16_t(retailDirection(u.x-aimX,u.z-aimZ).v));
        const int32_t diff=retailTurnRequest(wanted,u.heading);
        const int32_t turn=std::max(int32_t(1),int32_t(int64_t(uint16_t(std::max(u.type->turnRate,u.type->turnInPlaceRate)))*multiplier.v/65536));
        Fixed cap=retailGroundSpeedCap(maximum*multiplier,u.groundPitch,0);
        const int32_t facing=std::abs(diff);
        if(facing>12288)cap=Fixed::raw(cap.v/8);
        else if(facing>4096)cap=Fixed::raw(cap.v/2);
        Fixed speed=fxMin(cap,u.speed+u.type->accel*multiplier);
        if(u.speed>cap)speed=fxMax(cap,u.speed-u.type->brake*multiplier);
        if(speed<=Fixed())speed=Fixed::raw(std::max(1,cap.v/8));
        int64_t travel=std::min<int64_t>(speed.v,length);
        auto proposal=[&](int64_t ax,int64_t az,int64_t len,int64_t along) {
            return std::pair{Fixed::raw(int32_t(len?ax*along/len:0)),Fixed::raw(int32_t(len?az*along/len:0))};
        };
        auto [sx,sz]=proposal(dx,dz,length,travel);
        int nx=footprintOrigin(u.x+sx,fx),nz=footprintOrigin(u.z+sz,fz);
        if(!stepFree(u,ox,oz,nx,nz)) {
            // Blocked by a body. Flow around it through any other free cell
            // that is strictly closer to the goal (field descent), committing
            // to that neighbour for this update; otherwise hold still.
            bool moved=false;
            if(f) {
                // Direct-line members measure progress toward their own goal
                // cell; field followers by the group potential.
                const int goalX=m.goal%W,goalZ=m.goal/W;
                auto metric=[&](int x,int z)->int64_t {
                    if(towardGoal)return int64_t(goalX-x)*(goalX-x)+int64_t(goalZ-z)*(goalZ-z);
                    return f->potential[size_t(z*W+x)];
                };
                const int64_t here=metric(ox,oz);
                std::array<std::pair<int64_t,int>,8> options{};int count=0;
                for(int k=0;k<8;++k) {
                    const auto& d=kDirections[size_t(k)];
                    if(!step(p,ox,oz,d[0],d[1]))continue;
                    if(f->potential[size_t((oz+d[1])*W+ox+d[0])]==kUnreached)continue;
                    const int64_t v=metric(ox+d[0],oz+d[1]);
                    if(v<here)options[size_t(count++)]={v,k};
                }
                std::sort(options.begin(),options.begin()+count);
                for(int i=0;i<count&&!moved;++i) {
                    const auto& d=kDirections[size_t(options[size_t(i)].second)];
                    if(!stepFree(u,ox,oz,ox+d[0],oz+d[1]))continue;
                    const auto [cx,cz]=stepAim(u,(oz+d[1])*W+ox+d[0]);
                    dx=int64_t(cx.v)-u.x.v;dz=int64_t(cz.v)-u.z.v;
                    length=isqrtFloor(uint64_t(dx*dx+dz*dz));
                    travel=std::min<int64_t>(speed.v,length);
                    std::tie(sx,sz)=proposal(dx,dz,length,travel);
                    nx=footprintOrigin(u.x+sx,fx);nz=footprintOrigin(u.z+sz,fz);
                    if(stepFree(u,ox,oz,nx,nz)) {
                        // Commit to finishing this step into the cell
                        // (hysteresis): no re-deciding mid-cell, and the body
                        // keeps facing its route while it shuffles.
                        moved=true;++stats.slides;face=false;
                        m.detour=(oz+d[1])*W+ox+d[0];m.detourTicks=0;m.detourFace=false;
                    }
                }
            }
            if(!moved) {
                if(contactArrival(u,m)) {complete(u,m,true);return;}
                if(f&&m.detour<0)sidestep(u,m,p,*f,ox,oz,nx,nz);
                // A body walled in by STILL bodies (settled arrivals, a held
                // queue, idle units) plans a short committed detour around
                // them; moving traffic is waited for, not planned around.
                // A formation member held a long time is in a standing jam of
                // crossing lanes (nobody ahead will move first): it plans too.
                if(f&&m.detour<0&&m.route.empty()&&m.held>=m.nextDetour&&
                   (blockedBySettled(u,nx,nz)||(m.held>=60&&formationMember(m)))) {
                    // Back off geometrically after each attempt: a crowd that
                    // stays jammed stops re-planning instead of shuffling.
                    const uint32_t wait=std::min<uint32_t>(30u<<std::min<uint32_t>(m.detourCount,4u),480u);
                    ++m.detourCount;
                    if(localDetour(u,m,p,f,towardGoal)) {m.nextDetour=wait;u.speed=Fixed();return;}
                    m.nextDetour=m.held+wait;
                }
                if(f&&m.detour<0&&m.route.empty()&&m.held>=12&&yieldLane(u,m,p,nx,nz)) {hold(u,m);m.held=0;return;}
                hold(u,m);return;
            }
        }
        u.speed=speed;
        const Fixed beforeX=u.x,beforeZ=u.z;
        w.commitGroundStep(u,sx,sz,true);
        // The heading only turns with an actual step: a refused step is a
        // hold, and a held body keeps a constant heading.
        if(u.x==beforeX&&u.z==beforeZ) {hold(u,m);return;}
        // A small dead band keeps the body from twitching left and right as
        // its string-pulled aim shifts by a cell in a crowd; motion is exact
        // regardless, and a real course change (> ~5.6 deg) still turns.
        if(face&&std::abs(diff)>1024)u.heading=u.heading+Bam(std::clamp(diff,-turn,turn));
        u.turnReqBam=diff;
        m.state=Moving;m.held=0;
    }
    // Is the cell this body wants held by a body that will not move on its
    // own (idle, arrived, or not a Legion mover)? A queue of members waiting
    // for each other is not: it drains by itself and must not be re-planned.
    bool blockedBySettled(const Unit& u,int nx,int nz) const {
        for(int j=0;j<u.type->footZ;++j)for(int i=0;i<u.type->footX;++i) {
            const int cx=nx+i,cz=nz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=w.occ_[size_t(cz)*w.occW_+cx];
            if(!o||o==u.id)continue;
            const auto peer=members.find(o);
            if(peer==members.end()||peer->second.state==Arrived)return true;
        }
        return false;
    }
    static int z0goal(int local,int S,int R,int ox,int oz,int W) {return (oz+local/S-R)*W+ox+local%S-R;}
    // Bounded breadth-first search (window of radius kDetourCells) over
    // statically legal origins whose footprint touches no STILL body. The
    // target is the own goal if inside, else the reachable cell that most
    // reduces the distance still to go. The route is committed.
    bool localDetour(const Unit& u,Member& m,const Plane& p,const Field* f,bool towardGoal) {
        constexpr int R=kDetourCells,S=2*R+1;
        const int W=width(),fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        const int goalX=m.goal%W,goalZ=m.goal/W;
        auto metric=[&](int x,int z)->int64_t {
            if(!towardGoal&&f) {
                const uint16_t v=f->potential[size_t(z*W+x)];
                return v==kUnreached?INT64_MAX:int64_t(v)*64;
            }
            return int64_t(goalX-x)*(goalX-x)+int64_t(goalZ-z)*(goalZ-z);
        };
        auto still=[&](int cx,int cz) {
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)return false;
            const int32_t o=w.occ_[size_t(cz)*w.occW_+cx];
            if(!o||o==u.id)return false;
            const Unit* other=w.unit(o);
            if(!other||other->speed!=Fixed())return false;
            if(other->orders.empty())return true;
            const auto peer=members.find(o);
            return peer==members.end()||peer->second.state==Holding||peer->second.state==Arrived;
        };
        auto open=[&](int x,int z) {
            if(!legal(p,x,z))return false;
            for(int j=0;j<fz;++j)for(int i=0;i<fx;++i)if(still(x+i,z+j))return false;
            return true;
        };
        std::vector<int16_t> parent(size_t(S)*S,-1);
        std::vector<int> queue;queue.reserve(size_t(S)*S);
        const int start=R*S+R;parent[size_t(start)]=int16_t(start);queue.push_back(start);
        const int64_t current=metric(ox,oz);
        int best=-1;int64_t bestMetric=current;
        for(size_t head=0;head<queue.size();++head) {
            const int local=queue[head];
            const int lx=local%S,lz=local/S,x=ox+lx-R,z=oz+lz-R;
            if(z*W+x==m.goal) {best=local;break;}
            const int64_t v=local==start?current:metric(x,z);
            if(v<bestMetric) {bestMetric=v;best=local;}
            for(const auto& d:kDirections) {
                const int nlx=lx+d[0],nlz=lz+d[1];
                if(nlx<0||nlz<0||nlx>=S||nlz>=S)continue;
                const int next=nlz*S+nlx;
                if(parent[size_t(next)]>=0)continue;
                if(!step(p,x,z,d[0],d[1])||!open(x+d[0],z+d[1]))continue;
                if(d[0]&&d[1]&&(!open(x+d[0],z)||!open(x,z+d[1])))continue;
                parent[size_t(next)]=int16_t(local);queue.push_back(next);
            }
        }
        stats.detourCells+=queue.size();
        if(best<0||best==start)return false;
        // Only a real gain (about two cells) is worth committing to.
        if(z0goal(best,S,R,ox,oz,W)!=m.goal) {
            const bool field=!towardGoal&&f;
            const int64_t gain=field?current-bestMetric
                :int64_t(isqrtFloor(uint64_t(current)))-int64_t(isqrtFloor(uint64_t(bestMetric)));
            if(gain<(field?int64_t(2*kOrthogonal)*64:2))return false;
        }
        std::vector<int> path;
        for(int c=best;c!=start;c=parent[size_t(c)])path.push_back((oz+c/S-R)*W+ox+c%S-R);
        std::reverse(path.begin(),path.end());
        m.route=std::move(path);m.routeTicks=0;++stats.detours;
        m.state=Holding;m.held=0;  // stopped this update; the route starts next
        return true;
    }
    // Opposing traffic: after a short hold, both bodies commit to a lateral
    // step to their own right (keep-right), so head-on pairs pass instead
    // of pushing. Same-direction queues only side-step after a long hold.
    void sidestep(const Unit& u,Member& m,const Plane& p,const Field& f,int ox,int oz,int nx,int nz) {
        if(m.held<6)return;
        const int W=width();
        int blocker=0;
        const int fx=u.type->footX,fz=u.type->footZ;
        for(int j=0;j<fz&&!blocker;++j)for(int i=0;i<fx&&!blocker;++i) {
            const int cx=nx+i,cz=nz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=w.occ_[size_t(cz)*w.occW_+cx];
            if(o&&o!=u.id)blocker=o;
        }
        const Unit* other=blocker?w.unit(blocker):nullptr;
        const bool opposing=other&&!other->orders.empty()&&
            std::abs(retailTurnRequest(other->heading,u.heading))>16384;
        // Same-direction queues never side-step: they drain by themselves.
        // (Purposeful side-steps for them cost opposing-column throughput.)
        if(!opposing)return;
        int dx=nx-ox,dz=nz-oz;
        if(!dx&&!dz)return;
        dx=std::clamp(dx,-1,1);dz=std::clamp(dz,-1,1);
        const uint16_t here=f.potential[size_t(oz*W+ox)];
        const std::array<std::array<int,2>,2> sides{{{-dz,dx},{dz,-dx}}};
        for(int k=0;k<1;++k) {
            const int sx=sides[size_t(k)][0],sz=sides[size_t(k)][1];
            if(!step(p,ox,oz,sx,sz))continue;
            const int cell=(oz+sz)*W+ox+sx;
            if(f.potential[size_t(cell)]>uint32_t(here)+kDiagonal)continue;
            if(!stepFree(u,ox,oz,ox+sx,oz+sz))continue;
            m.detour=cell;m.detourTicks=0;m.detourFace=true;return;
        }
    }
    // A body pressed against settled bodies inside its goal's area has
    // arrived: the destination is an area, and the cells nearer the point
    // are taken. Distinct-goal members use one body width; members sharing a
    // point use the packed disc that many bodies of this size occupy.
    bool contactArrival(const Unit& u,const Member& m) const {
        if(m.stalled<20)return false;
        auto group=groups.find(m.group);
        if(group==groups.end())return false;
        // Shared points are counted by the requested point; a member's own
        // goal is its claimed slot once it has one.
        // The area is sized by everyone the point was given to, including
        // members that already settled there.
        const auto sharing=group->second.peak.find(m.requested);
        const int count=sharing==group->second.peak.end()?1:sharing->second;
        const int body=std::max(u.type->footX,u.type->footZ)*16;
        // Packed disc of `count` bodies: body*sqrt(count/pi), plus a body.
        const int64_t radius=count>1?int64_t(body)+int64_t(body)*isqrtFloor(uint64_t(count)*100000000/31416)/100:body;
        const int W=width();
        const int point=count>1?m.requested:m.goal;
        const Fixed gx=centre(point%W,u.type->footX),gz=centre(point/W,u.type->footZ);
        int64_t dx=(int64_t(u.x.v)-gx.v)>>16,dz=(int64_t(u.z.v)-gz.v)>>16;
        // A member of a shared point held still for ten seconds within one
        // body of the packed disc is at the destination area: its slot is
        // gone (covered, or walled off by bodies that settled first) and
        // waiting longer cannot make one.
        if(const auto pt=points.find(m.point);count>1&&pt!=points.end()&&pt->second.assigned&&pt->second.limit>0) {
            // Formation slots: the area is the whole point's (every class
            // sent there), measured from the requested point itself.
            dx=(int64_t(u.x.v)>>16)-(int64_t(std::get<2>(m.point))>>16);
            dz=(int64_t(u.z.v)>>16)-(int64_t(std::get<3>(m.point))>>16);
            const int64_t limit=pt->second.limit;
            if(dx*dx+dz*dz>limit*limit)return false;
            if(m.stalled>=kAreaSettle)return true;
        } else {
            if(count>1&&m.stalled>=kAreaSettle&&dx*dx+dz*dz<=(radius+body)*(radius+body))return true;
            if(dx*dx+dz*dz>radius*radius)return false;
        }
        if(count==1) {
            // A distinct goal is only "full" if another body stands on it;
            // otherwise settling short could plug the lane a neighbour needs.
            bool taken=false;
            const int gx0=m.goal%W,gz0=m.goal/W;
            for(int j=0;j<u.type->footZ&&!taken;++j)for(int i=0;i<u.type->footX&&!taken;++i) {
                const int cx=gx0+i,cz=gz0+j;
                if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
                const int32_t o=w.occ_[size_t(cz)*w.occW_+cx];
                taken=o&&o!=u.id;
            }
            if(!taken)return false;
        }
        // Only settled neighbours (idle, or already arrived) make an area full.
        const int fx=u.type->footX,fz=u.type->footZ;
        const int ox=footprintOrigin(u.x,fx),oz=footprintOrigin(u.z,fz);
        bool settled=false;
        for(int j=-1;j<=fz&&!settled;++j)for(int i=-1;i<=fx&&!settled;++i) {
            const int cx=ox+i,cz=oz+j;
            if(cx<0||cz<0||cx>=w.occW_||cz>=w.occH_)continue;
            const int32_t o=w.occ_[size_t(cz)*w.occW_+cx];
            if(!o||o==u.id)continue;
            const Unit* other=w.unit(o);
            if(!other||other->player!=u.player)continue;
            if(other->orders.empty()||other->orders[World::currentLeg(other->orders)].mission.pending&0x500)settled=true;
            // Members of a shared point that are themselves pressed still
            // inside the area count as its filled part.
            else if(count>1) {
                const auto peer=members.find(o);
                settled=peer!=members.end()&&peer->second.group==m.group&&peer->second.stalled>=20;
            }
        }
        return settled;
    }
    uint64_t checksum() const {
        uint64_t h=mix(0x4c4547494f4eull,epoch);
        h=mix(h,planeDebt);h=mix(h,uint64_t(pruneCursor));
        h=mix(h,uint64_t(nextGroup));
        for(const auto& [id,g]:groups) {
            h=mix(h,uint64_t(id));h=mix(h,uint64_t(g.player));h=mix(h,g.issuedTick);
            h=mix(h,uint64_t(uint32_t(g.minX))<<32|uint32_t(g.minZ));h=mix(h,uint64_t(uint32_t(g.maxX))<<32|uint32_t(g.maxZ));
            h=mix(h,uint64_t(uint32_t(g.comp)));h=mix(h,g.stale);
            for(const auto& [seed,count]:g.sharing) {h=mix(h,uint64_t(seed));h=mix(h,uint64_t(count));}
            if(g.next) {h=mix(h,g.next->work);h=mix(h,g.next->done);h=mix(h,g.next->epoch);}
            h=mix(h,uint64_t(g.members));h=mix(h,uint64_t(g.plane));
            for(int s:g.seeds)h=mix(h,uint64_t(s));
            for(const auto& [seed,n]:g.peak) {h=mix(h,uint64_t(seed));h=mix(h,uint64_t(n));}
            h=mix(h,g.lastUse);
            h=mix(h,g.built);
            if(g.field) {h=mix(h,g.field->work);h=mix(h,g.field->done);h=mix(h,g.field->epoch);}
            for(const auto& [seed,slot]:g.slots) {
                h=mix(h,uint64_t(seed));h=mix(h,slot.built);h=mix(h,slot.reach);h=mix(h,slot.stale);
                for(size_t i=0;i<slot.cells.size();++i)h=mix(h,uint64_t(slot.cells[i])<<1|slot.taken[i]);
            }
        }
        for(const auto& [key,point]:points) {
            h=mix(h,uint64_t(std::get<0>(key)));h=mix(h,std::get<1>(key));
            h=mix(h,uint32_t(std::get<2>(key)));h=mix(h,uint32_t(std::get<3>(key)));h=mix(h,uint64_t(point.refs));
            h=mix(h,point.assigned);h=mix(h,uint64_t(point.centreX));h=mix(h,uint64_t(point.centreZ));
            h=mix(h,uint64_t(point.scaleNum));h=mix(h,uint64_t(point.scaleDen));h=mix(h,uint64_t(point.limit));
            for(int c:point.cells)h=mix(h,uint64_t(c));
        }
        for(const auto& [id,a]:anchors) {h=mix(h,uint64_t(id));h=mix(h,uint64_t(a.goal));h=mix(h,a.yields);}
        for(const auto& [id,y]:yielding) {h=mix(h,uint64_t(id));h=mix(h,uint64_t(y.cell));h=mix(h,y.ticks);}
        for(const auto& [id,m]:members) {
            h=mix(h,uint64_t(id));h=mix(h,m.controller);h=mix(h,uint64_t(m.group));h=mix(h,uint64_t(m.goal));
            h=mix(h,uint64_t(m.lineCell));h=mix(h,m.line);h=mix(h,m.state);h=mix(h,m.best);
            h=mix(h,m.held);h=mix(h,m.stalled);h=mix(h,m.progress);h=mix(h,uint64_t(m.requested));
            h=mix(h,uint64_t(m.slot));h=mix(h,uint64_t(m.detour));h=mix(h,m.detourTicks);h=mix(h,m.detourFace);
            h=mix(h,m.trappedSince);h=mix(h,m.trappedEpoch);
            h=mix(h,uint64_t(std::get<0>(m.point)));h=mix(h,std::get<1>(m.point));
            h=mix(h,uint32_t(std::get<2>(m.point)));h=mix(h,uint32_t(std::get<3>(m.point)));
            h=mix(h,m.routeTicks);h=mix(h,m.nextDetour);h=mix(h,m.detourCount);for(int c:m.route)h=mix(h,uint64_t(c));
        }
        return h;
    }
};

LegionNavigator::LegionNavigator(World& w):impl_(std::make_unique<Impl>(w)) {}
LegionNavigator::~LegionNavigator()=default;
bool LegionNavigator::supports(const Unit& u) const {return impl_->supports(u);}
void LegionNavigator::registerMove(Unit& u) {impl_->registerMove(u);}
void LegionNavigator::cancel(int id) {impl_->leave(id);}
void LegionNavigator::tick() {impl_->tick();}
void LegionNavigator::move(Unit& u,Fixed maximum) {impl_->move(u,maximum);}
uint64_t LegionNavigator::checksum() const {return impl_->checksum();}
LegionNavigator::Stats LegionNavigator::stats() const {
    auto s=impl_->stats;
    s.bytes=0;
    for(const auto& p:impl_->planes)s.bytes+=p.legal.capacity();
    for(const auto& [id,g]:impl_->groups) {
        s.bytes+=g.seeds.capacity()*sizeof(int);
        if(g.field) {
            s.bytes+=g.field->potential.capacity()*sizeof(uint16_t);
            for(const auto& b:g.field->buckets)s.bytes+=b.capacity()*sizeof(int);
        }
    }
    s.bytes+=impl_->members.size()*(sizeof(Impl::Member)+48);
    s.liveGroups=impl_->groups.size();s.liveMembers=impl_->members.size();
    s.livePoints=impl_->points.size();s.liveFields=impl_->liveFields();
    return s;
}
bool LegionNavigator::staticLegal(const Unit& u,int x,int z) {
    if(!u.type)return false;
    const int index=impl_->planeFor(*u.type);
    return impl_->legal(impl_->plane(index),x,z);
}
int LegionNavigator::unitState(int id) const {
    const auto found=impl_->members.find(id);
    return found==impl_->members.end()?0:int(found->second.state);
}
int LegionNavigator::unitGroup(int id) const {
    const auto found=impl_->members.find(id);
    return found==impl_->members.end()?0:found->second.group;
}
}
namespace tak::sim {
int LegionNavigator::fieldPotential(int id,int x,int z) const {
    const auto found=impl_->members.find(id);
    if(found==impl_->members.end())return -1;
    const auto group=impl_->groups.find(found->second.group);
    if(group==impl_->groups.end()||!group->second.field||!group->second.field->done)return -1;
    if(x<0||z<0||x>=impl_->width()||z>=impl_->height())return -1;
    return group->second.field->potential[size_t(z)*impl_->width()+x];
}
}
