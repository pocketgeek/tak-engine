#pragma once
// legion_observe.h -- one const observer for group-motion scenarios (Legion
// and Retail alike), header-only. It reads `const World&` and nothing else:
// it never mutates the world, never calls a lazy cache that could, and uses
// integer math only (positions are 16.16 Fixed, distances integer square
// roots), so every compiler reports the same numbers. Members are iterated in
// the order the groups list them; the pair counts that need two members run
// once, at report time.
//
//   Observer obs(config);                 // groups, gates, lines, probes
//   obs.setStaticBlocked(mask,W,H);       // optional: wall/clearance metrics
//   for(t...) {world.tick(...); obs.work("fieldWork",v); obs.sample(world,t);}
//   auto keys=obs.report();               // ordered key -> int64
//   std::puts(Observer::json(keys).c_str());
//
// Call sample() once per tick, after the tick. The cheap parts (progress,
// age classes, motion, gates, lines, flyers) run every call; the decision-
// stability and spacing parts run when tick % decisionEvery == 0.
//
// Metric definitions (docs: the observer section of the Legion plan, T8):
// - Progress. A member has ARRIVED when its orders are empty, its speed is 0
//   and its centre lies inside the packed-disc limit of its group's click
//   point: 16*(foot+1)*sqrt(N/pi)*1.25 px (foot: the group's largest
//   footprint side, N: the group size). t50/t90/done are the first ticks
//   half/90%/all of the group had arrived; left_behind is the rest at the end.
//   orders_done is the first tick every live member's orders were empty.
// - Completion (AR-08). When a member's orders empty, its distance from the
//   click (whole cells) is recorded: complete_outside_radius counts those
//   beyond the packed-disc limit; complete_dist_median/max summarise them.
// - Retail age classes. A member with orders that has not moved for 10-150
//   ticks is WAITING, longer is PARKED (retailgrade.h's recent/stale stamps).
//   Each splits into held_by_design (Legion's member state is Holding,
//   Waiting for its field, Arrived or Trapped: a deliberate stand) and
//   no_progress (Legion says Moving, or the unit is not a Legion member).
// - Motion. spins: heading changed without the position changing.
//   reversals: return to a 16 px cell left less than 90 ticks earlier
//   (A -> B -> A over the last 6 cells). stop_go: a start after a stop while
//   the orders last. sideways/backward: a real step (> 1/4 px) more than
//   45/90 degrees off the facing. walk_in_place: ticks a ground member with
//   orders keeps its walk (speed > 0, not legionStill) after 6 or more ticks
//   without headway (a step under baseSpeed/4 and no turn). statue_ticks: a
//   ground member with orders steps at no more than cap/8 (maxVel/8, 1/8
//   slack for rounding) without turning. back: a decision sample whose last
//   step opposes the member's 10-sample mean direction (cos < -0.3; the
//   pinwheel2/lanes2 'back').
// - Decision stability (every decisionEvery ticks, from observed state only).
//   flips: a member's moving/still class changed between two samples while it
//   has orders; flip_rate_per30_permille = flips per 30 ordered member-ticks,
//   x1000. aim_reversals: the side of the member's step relative to the way
//   to its goal (sin > 1/6) flips. engagement_on_flowing: after 30 or more
//   free samples (a step of at least half its base speed) the member stops
//   (under a quarter) or deviates by more than 45 degrees. crawl_samples: a
//   sample's step is positive but no more than cap/8 per tick.
//   follow_chain_max/mean: the longest run of same-group movers each within
//   two body widths behind the next (inside 45 degrees of its own motion),
//   at exactly the same speed.
// - Lanes. A Gate is a strip of centre cells: files = distinct lateral bands
//   (band cells wide) among members inside it, averaged over ticks with at
//   least minCount inside; spread = lateral extent. crossings (pw.py): the
//   first lateral cell at each end window of the strip (edge cells deep) is
//   recorded per member; two same-group members that traversed it the same
//   way, entering within pairWindow ticks, cross when their lateral order
//   flips by at least flipCells (default: the group's body width) at both
//   ends. LaneOrder (the MV-13 S-bend probe): the same flip test between the
//   first entries into a line before a vertex and a line after it. Hug: a
//   member is hugging a vertex when its lateral cell on entering the vertex's
//   line is within `cells` of the tip; carry is the members that hug both
//   vertices (kept separate from crossings).
// - Spacing (every decisionEvery ticks, ordered ground members). Every live
//   ground body (landed flyers included) is stamped into an epoch-stamped id
//   grid; the ring of cells round a member's footprint is walked:
//   contact_own (a same-group member with orders), contact_other (a body
//   with orders outside the group), contact_settled (a body without orders).
//   Permille of member samples. No pair loop.
// - Walls (when a static mask or legality provider is set). Per footprint
//   class, a two-pass chessboard chamfer distance transform over the legal
//   origins; clearance = cells of free ground between the footprint and the
//   nearest wall (distance - 1), capped at 11. wall_touch_permille: moving
//   ordered ground samples with clearance 0; the _near variant and the
//   clearance mean/p10 count only samples within 10 cells of a wall (the
//   lanes2 'touch'/'wallgap').
// - Pair proximity (the aware case's contacts_permille). Every decision
//   sample, pairs = live ordered members of A x those of B; contact when
//   their centre cells are within `cells` on both axes (a count grid, no
//   pair loop). permille_x100.
// - Flyers (canFly group members). away_max/mean: px from the group's ground
//   centroid while its ground members have orders, from 150 ticks in.
//   takeoffs (landed -> airborne), relifts (a takeoff after landing during
//   the run), go_arounds (a landing abandoned while airborne), hover_max
//   (consecutive airborne ticks under 1/4 px/tick, not landing, from 60 ticks
//   in), land_delay_max (orders empty while airborne to the start of the
//   descent; 9999 if it never began), illegal_overlap_ticks (a landed flyer's
//   footprint on a non-flyer ground body).
// - Side lines: members whose centre cell lies at or beyond `at` on the axis;
//   t90 is the first tick 90% of the observed members were there.
// - Work: any per-tick counters the caller passes (work() for per-tick
//   values, workCumulative() for running totals); max, p99 and total.
#include "sim/sim.h"
#include "sim/footprint.h"
#include "sim/retailmotion.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tak::legion_observe {

using Keys=std::vector<std::pair<std::string,int64_t>>;

// Inclusive rectangle of body centre cells (position >> 20: 16 px cells).
struct Rect {
    int x0=0,z0=0,x1=-1,z1=-1;
    bool contains(int x,int z) const {return x>=x0&&x<=x1&&z>=z0&&z<=z1;}
};
struct Group {
    std::string name;
    std::vector<int> ids;
    int clickX=0,clickZ=0;   // px
};
struct Gate {
    std::string name;
    Rect region;
    int lateral=1;           // lateral axis: 0 x, 1 z (motion runs along the other)
    int band=2;              // file width in cells
    int minCount=3;          // members inside for a files sample
    int edge=2;              // depth in cells of each end window (crossings)
    int pairWindow=600;      // ticks between two members' entries for a pair
    int flipCells=0;         // 0: the group's body width
};
struct Line {
    Rect region;
    int lateral=1;           // 0 x, 1 z
    int sign=1;              // orients the lateral cell (sign*cell is recorded)
};
struct LaneOrder {
    std::string name;
    Line before,after;
    int window=300;
    int minCells=2;
};
struct Hug {
    std::string name;
    Line first,second;            // regions and lateral axes (sign unused here)
    int tipFirst=0,tipSecond=0;   // lateral cell of each vertex's tip (unsigned)
    int cells=2;
};
struct Side {
    std::string name;
    int axis=0;              // 0 x, 1 z
    int at=0;                // centre cell; >= at is the far side
};
struct Pair {
    std::string a,b;         // group names
    int cells=2;
};

struct Config {
    std::vector<Group> groups;
    std::vector<Gate> gates;
    std::vector<LaneOrder> laneOrders;
    std::vector<Hug> hugs;
    std::vector<Side> sides;
    std::vector<Pair> pairs;
    int decisionEvery=10;
    // Selftest only: 1 makes the crossing flip test strict (> instead of >=
    // one body width), which legion_observe_selftest must detect.
    int mutation=0;
};

namespace detail {
inline int64_t isqrt(int64_t v) {return v<=0?0:int64_t(sim::isqrt64(uint64_t(v)));}
inline int64_t permille(int64_t n,int64_t d) {return d>0?(n*1000+d/2)/d:0;}
inline int centreCell(sim::Fixed p) {return int(p.v>>20);}
// Packed-disc radius squared, px^2: (16*(f+1)*1.25)^2*N/pi with pi = 355/113.
inline int64_t discRadius2(int foot,int n) {return int64_t(400)*(foot+1)*(foot+1)*n*113/355;}
}

class Observer {
public:
    explicit Observer(Config c):cfg_(std::move(c)) {
        for(size_t g=0;g<cfg_.groups.size();++g) {
            for(int id:cfg_.groups[g].ids) {
                bool dup=false;for(const auto& m:m_)dup|=m.id==id;
                if(dup)continue;
                Member m;m.id=id;m.group=int(g);m_.push_back(m);
            }
        }
        groupStats_.resize(cfg_.groups.size());
        gateStats_.resize(cfg_.gates.size());
        sideStats_.resize(cfg_.sides.size());
        pairStats_.resize(cfg_.pairs.size());
        for(size_t p=0;p<cfg_.pairs.size();++p) {
            pairStats_[p].a=groupIndex(cfg_.pairs[p].a);pairStats_[p].b=groupIndex(cfg_.pairs[p].b);
        }
        for(auto& m:m_) {
            m.gate.assign(cfg_.gates.size()*2,Mark{});
            m.lane.assign(cfg_.laneOrders.size()*2,Mark{});
            m.hug.assign(cfg_.hugs.size()*2,Mark{});
        }
    }

    // Walls: 1x1 blocked cells (nonzero = blocked), the map's cell size. A
    // footprint origin is legal when every cell under it is inside the map
    // and open. Use setStaticLegal for terrain/plane legality instead.
    void setStaticBlocked(std::vector<uint8_t> blocked,int w,int h) {
        blocked_=std::move(blocked);bw_=w;bh_=h;legalFn_=nullptr;staticChanged();
    }
    // Any legality source (e.g. a test holding a mutable World may pass
    // LegionNavigator::staticLegal). Called only from staticChanged() rebuilds.
    void setStaticLegal(std::function<bool(const sim::Unit&,int,int)> fn,int w,int h) {
        legalFn_=std::move(fn);bw_=w;bh_=h;blocked_.clear();staticChanged();
    }
    void staticChanged() {dt_.clear();}

    void work(std::string_view name,uint64_t value) {workSeries(name).pending+=value;}
    void workCumulative(std::string_view name,uint64_t total) {
        auto& s=workSeries(name);
        s.pending+=total>=s.lastTotal?total-s.lastTotal:0;s.lastTotal=total;
    }

    void sample(const sim::World& w,int64_t tick) {
        if(start_<0)start_=tick;
        ++samples_;
        const bool decide=cfg_.decisionEvery>0&&tick%cfg_.decisionEvery==0;
        // Read-only: legionNavigator() is a plain accessor; unitState is const.
        const auto* nav=const_cast<sim::World&>(w).legionNavigator();
        const bool anyFlyer=hasFlyers(w);
        if(anyFlyer||decide)buildGrid(w,anyFlyer);
        for(auto& m:m_)perTick(w,m,tick,nav);
        progress(w,tick);
        gates();
        sides(tick);
        if(anyFlyer)flyerAway(w,tick);
        if(decide) {
            for(auto& m:m_)decision(w,m);
            followChains();
            spacing(w);
            pairProximity(w);
        }
        for(auto& s:work_) {s.values.push_back(s.pending);s.pending=0;}
    }

    Keys report() const {
        Keys k;
        auto put=[&](std::string key,int64_t v) {k.emplace_back(std::move(key),v);};
        put("members",int64_t(m_.size()));put("samples",samples_);
        for(size_t g=0;g<cfg_.groups.size();++g) {
            const auto& gs=groupStats_[g];const std::string p="g."+cfg_.groups[g].name+".";
            int n=0,flyers=0,landed=0;std::vector<int64_t> dists;int64_t outside=0;
            int64_t takeoffs=0,takeoffsMax=0,relifts=0,reliftsMax=0,goArounds=0,hoverMax=0,landDelayMax=0;
            for(const auto& m:m_) {
                if(m.group!=int(g))continue;
                ++n;
                if(m.completeDist>=0) {dists.push_back(m.completeDist);outside+=m.completeOutside;}
                if(!m.flyer)continue;
                ++flyers;landed+=m.mode==1;
                takeoffs+=m.takeoffs;takeoffsMax=std::max<int64_t>(takeoffsMax,m.takeoffs);
                relifts+=m.relifts;reliftsMax=std::max<int64_t>(reliftsMax,m.relifts);
                goArounds+=m.goArounds;hoverMax=std::max<int64_t>(hoverMax,m.hoverMax);
                landDelayMax=std::max<int64_t>(landDelayMax,m.idleAt>=0?9999:m.landDelayMax);
            }
            std::sort(dists.begin(),dists.end());
            put(p+"n",n);put(p+"radius_px",detail::isqrt(gs.radius2));
            put(p+"arrived",gs.arrived);put(p+"t50",gs.t50);put(p+"t90",gs.t90);put(p+"done",gs.done);
            put(p+"left_behind",n-gs.arrived-gs.dead);put(p+"dead",gs.dead);put(p+"orders_done",gs.ordersDone);
            put(p+"complete_n",int64_t(dists.size()));put(p+"complete_outside_radius",outside);
            put(p+"complete_dist_median",dists.empty()?-1:dists[(dists.size()-1)/2]);
            put(p+"complete_dist_max",dists.empty()?-1:dists.back());
            if(flyers) {
                put(p+"flyers",flyers);put(p+"flyers_landed",landed);
                put(p+"away_max",gs.awayMax);put(p+"away_mean",gs.awaySamples?gs.awaySum/gs.awaySamples:0);
                put(p+"takeoffs",takeoffs);put(p+"takeoffs_max",takeoffsMax);
                put(p+"relifts",relifts);put(p+"relifts_max",reliftsMax);put(p+"go_arounds",goArounds);
                put(p+"hover_max",hoverMax);put(p+"land_delay_max",landDelayMax);
                put(p+"illegal_overlap_ticks",gs.illegalOverlap);
            }
        }
        put("spins",spins_);put("reversals",reversals_);put("stop_go",stopGo_);
        put("sideways",sideways_);put("backward",backward_);put("back",back_);
        put("walk_in_place",walkInPlace_);put("statue_ticks",statue_);put("crawl_samples",crawl_);
        put("waiting_held",waitingHeld_);put("waiting_no_progress",waitingNoProgress_);
        put("parked_held",parkedHeld_);put("parked_no_progress",parkedNoProgress_);
        put("decision_samples",ordered_);put("stopped_permille",detail::permille(stopped_,ordered_));
        put("flips",flips_);
        put("flip_rate_per30_permille",flipSamples_?(flips_*30*1000+flipSamples_*cfg_.decisionEvery/2)/(flipSamples_*cfg_.decisionEvery):0);
        put("aim_reversals",aimReversals_);put("engagement_on_flowing",engagement_);
        put("follow_chain_max",followMax_);
        put("follow_chain_mean_x100",followSamples_?(followSum_*100+followSamples_/2)/followSamples_:0);
        int64_t detourSum=0,detourN=0;
        for(const auto& m:m_) {
            if(!m.seen)continue;
            const int64_t dx=(int64_t(m.x)-m.startX)>>8,dz=(int64_t(m.z)-m.startZ)>>8;
            const int64_t straight=detail::isqrt(dx*dx+dz*dz);   // 1/256 px
            if(straight>64*256) {detourSum+=(m.travelled>>8)*1000/straight;++detourN;}
        }
        put("detour_permille",detourN?detourSum/detourN-1000:0);
        put("spacing_samples",spacingSamples_);
        put("contact_own_permille",detail::permille(contactOwn_,spacingSamples_));
        put("contact_other_permille",detail::permille(contactOther_,spacingSamples_));
        put("contact_settled_permille",detail::permille(contactSettled_,spacingSamples_));
        if(bw_>0) {
            put("wall_samples",wallSamples_);put("wall_touch_permille",detail::permille(wallTouch_,wallSamples_));
            put("wall_near_samples",int64_t(near_.size()));
            int64_t touching=0,sum=0;for(int g:near_) {touching+=g<=0;sum+=g;}
            put("wall_touch_near_permille",detail::permille(touching,int64_t(near_.size())));
            put("clearance_mean_x100",near_.empty()?-1:(sum*100+int64_t(near_.size())/2)/int64_t(near_.size()));
            auto sorted=near_;std::sort(sorted.begin(),sorted.end());
            put("clearance_p10_x100",sorted.empty()?-1:int64_t(sorted[sorted.size()/10])*100);
        }
        for(size_t i=0;i<cfg_.gates.size();++i) {
            const auto& s=gateStats_[i];const std::string p="gate."+cfg_.gates[i].name+".";
            int64_t pairs=0,cross=0;gateCrossings(i,pairs,cross);
            put(p+"samples",s.samples);
            put(p+"files_x100",s.samples?(s.files*100+s.samples/2)/s.samples:0);
            put(p+"spread_x100",s.samples?(s.spread*100+s.samples/2)/s.samples:0);
            put(p+"pairs",pairs);put(p+"crossings",cross);
        }
        for(size_t i=0;i<cfg_.laneOrders.size();++i) {
            const std::string p="lane."+cfg_.laneOrders[i].name+".";
            int64_t pairs=0,swaps=0;laneSwaps(i,pairs,swaps);
            put(p+"pairs",pairs);put(p+"swaps",swaps);put(p+"swaps_permille",detail::permille(swaps,pairs));
        }
        for(size_t i=0;i<cfg_.hugs.size();++i) {
            const auto& h=cfg_.hugs[i];const std::string p="hug."+h.name+".";
            int64_t first=0,second=0,carry=0;
            for(const auto& m:m_) {
                const auto& a=m.hug[i*2];const auto& b=m.hug[i*2+1];
                const bool ha=a.tick>=0&&std::abs(a.lat-h.tipFirst)<=h.cells;
                const bool hb=b.tick>=0&&std::abs(b.lat-h.tipSecond)<=h.cells;
                first+=ha;second+=hb;carry+=ha&&hb;
            }
            put(p+"first",first);put(p+"second",second);put(p+"carry",carry);
        }
        for(size_t i=0;i<cfg_.sides.size();++i) {
            const auto& s=sideStats_[i];const std::string p="side."+cfg_.sides[i].name+".";
            put(p+"high",s.high);put(p+"low",s.low);put(p+"t90",s.t90);
        }
        for(size_t i=0;i<cfg_.pairs.size();++i) {
            const auto& s=pairStats_[i];const std::string p="pair."+cfg_.pairs[i].a+"."+cfg_.pairs[i].b+".";
            put(p+"pairs",s.pairs);put(p+"contacts",s.contacts);
            put(p+"permille_x100",s.pairs?(s.contacts*100000+s.pairs/2)/s.pairs:0);
        }
        for(const auto& s:work_) {
            const std::string p="work."+s.name+".";
            auto v=s.values;uint64_t total=0,max=0;for(auto x:v) {total+=x;max=std::max(max,x);}
            uint64_t p99=0;
            if(!v.empty()) {
                const size_t at=(v.size()*99+99)/100-1;
                std::nth_element(v.begin(),v.begin()+ptrdiff_t(at),v.end());p99=v[at];
            }
            put(p+"max",int64_t(max));put(p+"p99",int64_t(p99));put(p+"total",int64_t(total));
        }
        return k;
    }

    static std::string json(const Keys& keys) {
        std::string s="{";
        for(size_t i=0;i<keys.size();++i) {
            if(i)s+=",";
            s+="\""+keys[i].first+"\":"+std::to_string(keys[i].second);
        }
        return s+"}";
    }
    static int64_t get(const Keys& keys,std::string_view key,int64_t fallback=INT64_MIN) {
        for(const auto& [k,v]:keys)if(k==key)return v;
        return fallback;
    }

private:
    struct Mark {int lat=0;int64_t tick=-1;};
    struct Member {
        int id=0,group=0;
        bool seen=false,flyer=false,alive=false;
        int foot=1,fx=1,fz=1;
        int32_t x=0,z=0,startX=0,startZ=0;uint16_t heading=0;
        int64_t travelled=0;   // raw px (1/65536) along the per-tick steps
        // legacy Motion: the last cells visited and when.
        std::vector<std::pair<int64_t,int64_t>> cells;
        bool moved=false,everMoved=false,hadOrders=false;
        int stillRun=0,noHeadRun=0;
        int completeDist=-1;bool completeOutside=false;
        // decision samples: positions at the last 11 samples (10 steps).
        std::vector<std::pair<int32_t,int32_t>> ring;
        int lastClass=-1,aimSign=0,freeRun=0;
        int64_t speed=0;bool chainMover=false;int64_t stepX=0,stepZ=0;
        // flyers
        int mode=-1;bool landing=false,landedOnce=false;
        int takeoffs=0,relifts=0,goArounds=0,hoverRun=0,hoverMax=0;
        int64_t idleAt=-1;int64_t landDelayMax=0;
        std::vector<Mark> gate,lane,hug;   // [probe*2 + end]
    };
    struct GroupStats {
        int64_t radius2=0;int foot=0;
        int arrived=0,dead=0;int64_t t50=-1,t90=-1,done=-1,ordersDone=-1;
        int64_t awayMax=0,awaySum=0,awaySamples=0,illegalOverlap=0;
    };
    struct GateStats {int64_t samples=0,files=0,spread=0;};
    struct SideStats {int64_t high=0,low=0,t90=-1;};
    struct PairStats {int a=-1,b=-1;int64_t pairs=0,contacts=0;};
    struct WorkSeries {std::string name;uint64_t pending=0,lastTotal=0;std::vector<uint64_t> values;};

    int groupIndex(const std::string& name) const {
        for(size_t g=0;g<cfg_.groups.size();++g)if(cfg_.groups[g].name==name)return int(g);
        return -1;
    }
    WorkSeries& workSeries(std::string_view name) {
        for(auto& s:work_)if(s.name==name)return s;
        WorkSeries s;s.name=std::string(name);s.values.assign(size_t(samples_),0);
        work_.push_back(std::move(s));return work_.back();
    }
    bool hasFlyers(const sim::World& w) const {
        for(const auto& m:m_)if(const auto* u=w.unit(m.id);u&&u->type&&u->type->canFly)return true;
        return false;
    }
    static bool groundBody(const sim::Unit& u) {
        return u.alive()&&!u.embarked()&&u.type&&!u.type->isStructure()&&!(u.type->canFly&&u.flightGroundMode!=1);
    }

    // ---- id grid: every live ground body's footprint, epoch-stamped ----
    void buildGrid(const sim::World& w,bool flyerCheck) {
        gw_=w.mapW();gh_=w.mapH();
        const size_t cells=size_t(std::max(gw_,0))*size_t(std::max(gh_,0));
        if(gridEpoch_.size()!=cells) {gridEpoch_.assign(cells,0);gridId_.assign(cells,0);epoch_=0;}
        ++epoch_;
        auto stamp=[&](const sim::Unit& u) {
            const int ox=sim::footprintOrigin(u.x,u.type->footX),oz=sim::footprintOrigin(u.z,u.type->footZ);
            for(int j=0;j<u.type->footZ;++j)for(int i=0;i<u.type->footX;++i) {
                const int x=ox+i,z=oz+j;
                if(x<0||z<0||x>=gw_||z>=gh_)continue;
                gridEpoch_[size_t(z)*size_t(gw_)+size_t(x)]=epoch_;gridId_[size_t(z)*size_t(gw_)+size_t(x)]=u.id;
            }
        };
        // Non-flyer bodies first, so a landed flyer can be checked against them.
        for(const auto& u:w.units())if(groundBody(u)&&!u.type->canFly)stamp(u);
        if(flyerCheck) {
            for(const auto& m:m_) {
                const auto* u=w.unit(m.id);
                if(!u||!u->alive()||!u->type||!u->type->canFly||u->flightGroundMode!=1)continue;
                if(overlapsBody(*u))++groupStats_[size_t(m.group)].illegalOverlap;
            }
        }
        for(const auto& u:w.units())if(groundBody(u)&&u.type->canFly)stamp(u);
    }
    int bodyAt(int x,int z) const {
        if(x<0||z<0||x>=gw_||z>=gh_)return 0;
        const size_t i=size_t(z)*size_t(gw_)+size_t(x);
        return gridEpoch_[i]==epoch_?gridId_[i]:0;
    }
    bool overlapsBody(const sim::Unit& u) const {
        const int ox=sim::footprintOrigin(u.x,u.type->footX),oz=sim::footprintOrigin(u.z,u.type->footZ);
        for(int j=0;j<u.type->footZ;++j)for(int i=0;i<u.type->footX;++i) {
            const int id=bodyAt(ox+i,oz+j);
            if(id&&id!=u.id)return true;
        }
        return false;
    }

    // ---- per tick, per member ----
    void perTick(const sim::World& w,Member& m,int64_t tick,const sim::LegionNavigator* nav) {
        const auto* u=w.unit(m.id);
        if(!u||!u->alive()||!u->type) {m.moved=false;m.alive=false;return;}
        m.alive=true;
        const bool fresh=!m.seen;
        if(fresh) {
            m.seen=true;m.flyer=u->type->canFly;m.fx=u->type->footX;m.fz=u->type->footZ;m.foot=std::max(m.fx,m.fz);
            m.x=m.startX=u->x.v;m.z=m.startZ=u->z.v;m.heading=uint16_t(u->heading.v);m.mode=u->flightGroundMode;
            m.landing=u->landing.has_value();
        }
        const bool orders=!u->orders.empty();
        const int64_t dx=int64_t(u->x.v)-m.x,dz=int64_t(u->z.v)-m.z;
        const bool moved=!fresh&&(dx||dz);
        const bool turned=!fresh&&uint16_t(u->heading.v)!=m.heading;
        const int64_t step2=dx*dx+dz*dz;
        if(moved)m.travelled+=detail::isqrt(step2);
        // spins and cell reversals (the legacy Motion observer)
        if(!fresh&&!moved&&turned)++spins_;
        const int64_t cell=(int64_t(u->z.v>>20)<<32)|uint32_t(u->x.v>>20);
        if(m.cells.empty()||m.cells.back().first!=cell) {
            for(size_t i=0;i+1<m.cells.size();++i)
                if(m.cells[i].first==cell&&tick-m.cells[i].second<90) {++reversals_;break;}
            m.cells.push_back({cell,tick});
            if(m.cells.size()>6)m.cells.erase(m.cells.begin());
        }
        if(orders) {
            if(moved&&!m.moved&&m.everMoved)++stopGo_;
            // sideways / backward against the facing (legion_group_motion)
            if(moved&&step2>int64_t(16384)*16384) {
                const auto way=sim::retailHeadingToPort(uint16_t(sim::retailDirection(sim::Fixed::raw(int32_t(-dx)),sim::Fixed::raw(int32_t(-dz))).v));
                const int32_t off=std::abs(sim::retailTurnRequest(way,u->heading));
                sideways_+=off>8192;backward_+=off>16384;
            }
            // age classes
            m.stillRun=moved?0:m.stillRun+1;
            if(m.stillRun>=10) {
                const int state=nav?nav->unitState(m.id):0;
                const bool held=state>=2;
                if(m.stillRun<=150)(held?waitingHeld_:waitingNoProgress_)++;
                else (held?parkedHeld_:parkedNoProgress_)++;
            }
            if(!m.flyer) {
                const int64_t pace=std::max<int64_t>(1,u->baseSpeed.v/4);
                m.noHeadRun=(step2>=pace*pace||turned)?0:m.noHeadRun+1;
                if(m.noHeadRun>=6&&u->speed.v>0&&!u->legionStill)++walkInPlace_;
                const int64_t cap=int64_t(u->type->maxVel.v)*9/64;   // cap/8 plus 1/8 slack
                if(moved&&!turned&&step2<=cap*cap)++statue_;
            }
        } else {
            m.stillRun=0;m.noHeadRun=0;
        }
        // completion (AR-08)
        if(orders)m.hadOrders=true;
        else if(m.hadOrders&&m.completeDist<0) {
            const auto& g=cfg_.groups[size_t(m.group)];
            const int64_t cx=(int64_t(u->x.v)>>16)-g.clickX,cz=(int64_t(u->z.v)>>16)-g.clickZ;
            m.completeDist=int(detail::isqrt(cx*cx+cz*cz)/16);
            m.completeOutside=cx*cx+cz*cz>groupRadius2(size_t(m.group));
        }
        if(moved)m.everMoved=true;
        m.moved=moved;
        // gates, lane-order lines, hug lines: first entries
        const int cx=detail::centreCell(u->x),cz=detail::centreCell(u->z);
        for(size_t i=0;i<cfg_.gates.size();++i) {
            const auto& g=cfg_.gates[i];
            if(!g.region.contains(cx,cz))continue;
            const int along=g.lateral?cx:cz,lat=g.lateral?cz:cx;
            const int lo=g.lateral?g.region.x0:g.region.z0,hi=g.lateral?g.region.x1:g.region.z1;
            if(along<lo+g.edge&&m.gate[i*2].tick<0)m.gate[i*2]={lat,tick};
            if(along>hi-g.edge&&m.gate[i*2+1].tick<0)m.gate[i*2+1]={lat,tick};
        }
        auto line=[&](const Line& l,Mark& mark) {
            if(mark.tick<0&&l.region.contains(cx,cz))mark={l.sign*(l.lateral?cz:cx),tick};
        };
        for(size_t i=0;i<cfg_.laneOrders.size();++i) {line(cfg_.laneOrders[i].before,m.lane[i*2]);line(cfg_.laneOrders[i].after,m.lane[i*2+1]);}
        for(size_t i=0;i<cfg_.hugs.size();++i) {
            const auto& h=cfg_.hugs[i];
            if(m.hug[i*2].tick<0&&h.first.region.contains(cx,cz))m.hug[i*2]={h.first.lateral?cz:cx,tick};
            if(m.hug[i*2+1].tick<0&&h.second.region.contains(cx,cz))m.hug[i*2+1]={h.second.lateral?cz:cx,tick};
        }
        // flyers
        if(m.flyer) {
            const int mode=u->flightGroundMode;
            if(m.mode==1&&mode==2) {++m.takeoffs;if(m.landedOnce)++m.relifts;}
            if(m.mode==2&&mode==1)m.landedOnce=true;
            const bool landing=u->landing.has_value();
            if(m.landing&&!landing&&mode==2)++m.goArounds;
            const bool hover=mode==2&&u->speed.v<16384&&!landing&&tick-start_>=60;
            m.hoverRun=hover?m.hoverRun+1:0;m.hoverMax=std::max(m.hoverMax,m.hoverRun);
            if(!orders&&mode==2&&m.idleAt<0)m.idleAt=tick;
            if(orders)m.idleAt=-1;
            if(m.idleAt>=0&&(landing||mode==1)) {m.landDelayMax=std::max(m.landDelayMax,tick-m.idleAt);m.idleAt=-1;}
            m.mode=mode;m.landing=landing;
        }
        m.x=u->x.v;m.z=u->z.v;m.heading=uint16_t(u->heading.v);
    }

    int64_t groupRadius2(size_t g) {
        auto& gs=groupStats_[g];
        if(!gs.radius2) {
            int foot=1,n=0;
            for(const auto& m:m_)if(m.group==int(g)) {++n;foot=std::max(foot,m.foot);}
            gs.foot=foot;gs.radius2=detail::discRadius2(foot,std::max(n,1));
        }
        return gs.radius2;
    }

    void progress(const sim::World& w,int64_t tick) {
        for(size_t g=0;g<cfg_.groups.size();++g) {
            auto& gs=groupStats_[g];
            const auto& grp=cfg_.groups[g];
            const int64_t r2=groupRadius2(g);
            int n=0,arrived=0,dead=0;bool ordersLeft=false;
            for(const auto& m:m_) {
                if(m.group!=int(g))continue;
                ++n;
                const auto* u=w.unit(m.id);
                if(!u||!u->alive()) {++dead;continue;}
                if(!u->orders.empty()) {ordersLeft=true;continue;}
                if(u->speed.v!=0)continue;
                const int64_t dx=(int64_t(u->x.v)>>16)-grp.clickX,dz=(int64_t(u->z.v)>>16)-grp.clickZ;
                arrived+=dx*dx+dz*dz<=r2;
            }
            gs.arrived=arrived;gs.dead=dead;
            const int64_t at=tick-start_;
            if(gs.t50<0&&n&&arrived*2>=n)gs.t50=at;
            if(gs.t90<0&&n&&arrived*10>=n*9)gs.t90=at;
            if(gs.done<0&&n&&arrived==n)gs.done=at;
            if(gs.ordersDone<0&&n&&!ordersLeft)gs.ordersDone=at;
        }
    }

    void gates() {
        for(size_t i=0;i<cfg_.gates.size();++i) {
            const auto& g=cfg_.gates[i];
            bands_.clear();int count=0,lo=INT_MAX,hi=INT_MIN;
            for(const auto& m:m_) {
                if(!m.seen||!m.alive)continue;
                const int cx=int(m.x>>20),cz=int(m.z>>20);
                if(!g.region.contains(cx,cz))continue;
                const int lat=g.lateral?cz:cx;
                bands_.push_back(lat>=0?lat/std::max(g.band,1):-1-(-1-lat)/std::max(g.band,1));
                lo=std::min(lo,lat);hi=std::max(hi,lat);++count;
            }
            if(count<g.minCount)continue;
            std::sort(bands_.begin(),bands_.end());
            auto& s=gateStats_[i];
            ++s.samples;s.files+=std::unique(bands_.begin(),bands_.end())-bands_.begin();s.spread+=hi-lo;
        }
    }
    void gateCrossings(size_t i,int64_t& pairs,int64_t& cross) const {
        const auto& g=cfg_.gates[i];
        struct Pass {int group,dir,entry,exit,foot;int64_t tick;};
        std::vector<Pass> passes;
        for(const auto& m:m_) {
            const auto& a=m.gate[i*2];const auto& b=m.gate[i*2+1];
            if(a.tick<0||b.tick<0)continue;
            const bool up=a.tick<b.tick;
            passes.push_back({m.group,up?1:-1,up?a.lat:b.lat,up?b.lat:a.lat,m.foot,std::min(a.tick,b.tick)});
        }
        for(size_t a=0;a<passes.size();++a)for(size_t b=a+1;b<passes.size();++b) {
            const auto& p=passes[a];const auto& q=passes[b];
            if(p.group!=q.group||p.dir!=q.dir)continue;
            if(std::abs(p.tick-q.tick)>g.pairWindow)continue;
            ++pairs;
            const int flip=g.flipCells>0?g.flipCells:std::max(p.foot,q.foot);
            const int da=p.entry-q.entry,db=p.exit-q.exit;
            const bool far=cfg_.mutation==1?std::abs(da)>flip&&std::abs(db)>flip:std::abs(da)>=flip&&std::abs(db)>=flip;
            if(int64_t(da)*db<0&&far)++cross;
        }
    }
    void laneSwaps(size_t i,int64_t& pairs,int64_t& swaps) const {
        const auto& l=cfg_.laneOrders[i];
        std::vector<const Member*> both;
        for(const auto& m:m_)if(m.lane[i*2].tick>=0&&m.lane[i*2+1].tick>=0)both.push_back(&m);
        for(size_t a=0;a<both.size();++a)for(size_t b=a+1;b<both.size();++b) {
            const auto& p=*both[a];const auto& q=*both[b];
            if(p.group!=q.group)continue;
            if(std::abs(p.lane[i*2].tick-q.lane[i*2].tick)>l.window)continue;
            ++pairs;
            const int da=p.lane[i*2].lat-q.lane[i*2].lat,db=p.lane[i*2+1].lat-q.lane[i*2+1].lat;
            if(int64_t(da)*db<0&&std::abs(da)>=l.minCells&&std::abs(db)>=l.minCells)++swaps;
        }
    }
    void sides(int64_t tick) {
        for(size_t i=0;i<cfg_.sides.size();++i) {
            const auto& s=cfg_.sides[i];auto& st=sideStats_[i];
            int64_t high=0,low=0;
            for(const auto& m:m_) {
                if(!m.seen||!m.alive)continue;
                const int c=s.axis?int(m.z>>20):int(m.x>>20);
                (c>=s.at?high:low)++;
            }
            st.high=high;st.low=low;
            if(st.t90<0&&!m_.empty()&&high*10>=int64_t(m_.size())*9)st.t90=tick-start_;
        }
    }
    void flyerAway(const sim::World& w,int64_t tick) {
        for(size_t g=0;g<cfg_.groups.size();++g) {
            int64_t sx=0,sz=0,n=0;bool busy=false;
            for(const auto& m:m_) {
                if(m.group!=int(g)||m.flyer)continue;
                const auto* u=w.unit(m.id);
                if(!u||!u->alive())continue;
                sx+=u->x.v;sz+=u->z.v;++n;busy|=!u->orders.empty();
            }
            if(!n||!busy||tick-start_<150)continue;
            auto& gs=groupStats_[g];
            for(const auto& m:m_) {
                if(m.group!=int(g)||!m.flyer)continue;
                const auto* u=w.unit(m.id);
                if(!u||!u->alive())continue;
                const int64_t dx=(int64_t(u->x.v)-sx/n)>>16,dz=(int64_t(u->z.v)-sz/n)>>16;
                const int64_t d=detail::isqrt(dx*dx+dz*dz);
                gs.awayMax=std::max(gs.awayMax,d);gs.awaySum+=d;++gs.awaySamples;
            }
        }
    }

    // ---- decision samples ----
    void decision(const sim::World& w,Member& m) {
        m.chainMover=false;
        const auto* u=w.unit(m.id);
        if(!u||!u->alive()||!u->type||!m.seen) {m.alive=false;return;}
        m.alive=true;
        const bool orders=!u->orders.empty();
        const int every=std::max(cfg_.decisionEvery,1);
        int64_t sx=0,sz=0;bool moved=false;
        if(!m.ring.empty()) {sx=int64_t(u->x.v)-m.ring.back().first;sz=int64_t(u->z.v)-m.ring.back().second;moved=sx||sz;}
        const bool hasPrev=!m.ring.empty();
        const int64_t step=detail::isqrt(sx*sx+sz*sz);   // raw px over `every` ticks
        const int64_t base=std::max<int64_t>(1,u->baseSpeed.v);
        if(orders) {++ordered_;stopped_+=u->speed.v<=0;}
        if(orders&&hasPrev) {
            ++flipSamples_;
            const int cls=moved?1:0;
            if(m.lastClass>=0&&cls!=m.lastClass)++flips_;
            m.lastClass=cls;
            // crawl: moving, at no more than cap/8 per tick on average
            const int64_t cap=int64_t(u->type->maxVel.v)*9/64*every;
            if(moved&&!m.flyer&&step<=cap)++crawl_;
            // back: the last step against the 10-sample mean direction
            if(moved&&m.ring.size()>=10) {
                const auto& old=m.ring[m.ring.size()-10];
                const int64_t mx=(int64_t(u->x.v)-old.first)>>12,mz=(int64_t(u->z.v)-old.second)>>12;
                const int64_t bx=sx>>12,bz=sz>>12;
                const int64_t lm2=mx*mx+mz*mz,lb2=bx*bx+bz*bz,dot=mx*bx+mz*bz;
                if(lm2>128*128&&lb2>32*32&&dot<0&&100*dot*dot>9*lm2*lb2)++back_;
            }
            // aim reversals: the side of the step relative to the way to the goal
            if(moved) {
                const auto& goal=u->orders.back();
                const int64_t gx=(int64_t(goal.x.v)-u->x.v)>>16,gz=(int64_t(goal.z.v)-u->z.v)>>16;
                const int64_t ax=sx>>8,az=sz>>8;
                const int64_t g2=gx*gx+gz*gz,a2=ax*ax+az*az,crs=ax*gz-az*gx;
                if(g2>=32*32&&a2>0&&crs*crs*36>a2*g2) {
                    const int sign=crs>0?1:-1;
                    if(m.aimSign&&sign!=m.aimSign)++aimReversals_;
                    m.aimSign=sign;
                }
            }
            // engagement on flowing: a long free run, then a stop or a >45 degree turn
            const bool freeStep=moved&&step*2>=base*every;
            bool engaged=false;
            if(m.freeRun>=30) {
                const bool stop=step*4<base*every;
                bool bend=false;
                if(!stop&&(m.stepX||m.stepZ)) {
                    const int64_t ax=sx>>8,az=sz>>8,bx=m.stepX>>8,bz=m.stepZ>>8;
                    const int64_t dot=ax*bx+az*bz,a2=ax*ax+az*az,b2=bx*bx+bz*bz;
                    bend=dot<=0||2*dot*dot<a2*b2;
                }
                if(stop||bend) {++engagement_;engaged=true;}
            }
            m.freeRun=engaged?0:freeStep?m.freeRun+1:0;
            if(moved&&!m.flyer) {m.chainMover=true;m.speed=u->speed.v;}
        } else if(!orders) {
            m.lastClass=-1;m.aimSign=0;m.freeRun=0;
        }
        m.stepX=sx;m.stepZ=sz;
        m.ring.push_back({u->x.v,u->z.v});
        if(m.ring.size()>11)m.ring.erase(m.ring.begin());
        // walls
        if(orders&&moved&&!m.flyer&&bw_>0) {
            const int g=clearance(*u);
            if(g>=0) {
                ++wallSamples_;wallTouch_+=g==0;
                if(g<kClearanceCap)near_.push_back(g);
            }
        }
    }

    // Follow chains: a mover's leader is the nearest same-group mover ahead of
    // it (inside 45 degrees of its motion) within two body widths, at exactly
    // its speed. Candidates come from a sorted cell index, not a pair loop.
    void followChains() {
        std::vector<std::pair<int64_t,int>> index;   // (cell key, member)
        for(size_t i=0;i<m_.size();++i) {
            const auto& m=m_[i];
            if(!m.chainMover)continue;
            index.push_back({cellKey(m.group,int(m.x>>20),int(m.z>>20)),int(i)});
        }
        if(index.empty())return;
        std::sort(index.begin(),index.end());
        std::vector<int> leader(m_.size(),-1);
        for(const auto& [key,i]:index) {
            const auto& m=m_[size_t(i)];
            const int reach=2*m.foot;   // cells: two body widths
            const int64_t reachPx=int64_t(reach)*16;
            const int cx=int(m.x>>20),cz=int(m.z>>20);
            int best=-1;int64_t bestD=INT64_MAX;
            for(int z=cz-reach;z<=cz+reach;++z) {
                const int64_t k0=cellKey(m.group,cx-reach,z),k1=cellKey(m.group,cx+reach,z);
                for(auto it=std::lower_bound(index.begin(),index.end(),std::pair<int64_t,int>{k0,INT_MIN});it!=index.end()&&it->first<=k1;++it) {
                    const int j=it->second;if(j==i)continue;
                    const auto& o=m_[size_t(j)];
                    if(o.speed!=m.speed)continue;
                    const int64_t dx=(int64_t(o.x)-m.x)>>12,dz=(int64_t(o.z)-m.z)>>12;   // 1/16 px
                    const int64_t d2=dx*dx+dz*dz;
                    if(d2>reachPx*reachPx*256)continue;
                    const int64_t ax=m.stepX>>8,az=m.stepZ>>8;
                    const int64_t dot=ax*dx+az*dz;
                    if(dot<=0||2*dot*dot<(ax*ax+az*az)*d2)continue;
                    if(d2<bestD||(d2==bestD&&o.id<m_[size_t(best)].id)) {bestD=d2;best=j;}
                }
            }
            leader[size_t(i)]=best;
        }
        std::vector<int> depth(m_.size(),0);
        int sampleMax=0;
        for(const auto& [key,i]:index) {
            // Walk to the head (or a node already measured), then fill back.
            std::vector<int> path;int at=i;
            while(at>=0&&depth[size_t(at)]==0&&int(path.size())<=int(m_.size())) {
                depth[size_t(at)]=-1;path.push_back(at);at=leader[size_t(at)];
            }
            int d=at>=0&&depth[size_t(at)]>0?depth[size_t(at)]:0;   // -1: a cycle, counted from here
            for(auto it=path.rbegin();it!=path.rend();++it)depth[size_t(*it)]=++d;
            sampleMax=std::max(sampleMax,depth[size_t(i)]);
        }
        followMax_=std::max<int64_t>(followMax_,sampleMax);followSum_+=sampleMax;++followSamples_;
    }
    static int64_t cellKey(int group,int x,int z) {
        return (int64_t(group)<<40)|(int64_t(uint32_t(z+0x8000)&0xfffff)<<20)|int64_t(uint32_t(x+0x8000)&0xfffff);
    }

    void spacing(const sim::World& w) {
        for(const auto& m:m_) {
            const auto* u=w.unit(m.id);
            if(!u||!u->alive()||!u->type||u->orders.empty()||m.flyer)continue;
            ++spacingSamples_;
            const int ox=sim::footprintOrigin(u->x,m.fx),oz=sim::footprintOrigin(u->z,m.fz);
            bool own=false,other=false,settled=false;
            auto look=[&](int x,int z) {
                const int id=bodyAt(x,z);
                if(!id||id==m.id)return;
                const auto* v=w.unit(id);
                if(!v)return;
                if(v->orders.empty())settled=true;
                else if(memberGroup(id)==m.group)own=true;
                else other=true;
            };
            for(int i=-1;i<=m.fx;++i) {look(ox+i,oz-1);look(ox+i,oz+m.fz);}
            for(int j=0;j<m.fz;++j) {look(ox-1,oz+j);look(ox+m.fx,oz+j);}
            contactOwn_+=own;contactOther_+=other;contactSettled_+=settled;
        }
    }
    int memberGroup(int id) const {
        // Members are few next to the map; a sorted id index keeps this O(log n).
        if(memberIndex_.size()!=m_.size()) {
            memberIndex_.clear();
            for(const auto& m:m_)memberIndex_.push_back({m.id,m.group});
            std::sort(memberIndex_.begin(),memberIndex_.end());
        }
        const auto it=std::lower_bound(memberIndex_.begin(),memberIndex_.end(),std::pair<int,int>{id,INT_MIN});
        return it!=memberIndex_.end()&&it->first==id?it->second:-1;
    }

    void pairProximity(const sim::World& w) {
        for(auto& p:pairStats_) {
            if(p.a<0||p.b<0)continue;
            const int cells=cfg_.pairs[size_t(&p-pairStats_.data())].cells;
            if(countGrid_.size()!=size_t(std::max(gw_,0))*size_t(std::max(gh_,0))) {countGrid_.assign(size_t(gw_)*size_t(gh_),0);countEpoch_.assign(countGrid_.size(),0);cepoch_=0;}
            ++cepoch_;
            int64_t na=0,nb=0;
            auto live=[&](const Member& m)->const sim::Unit* {
                const auto* u=w.unit(m.id);
                return u&&u->alive()&&!u->orders.empty()?u:nullptr;
            };
            for(const auto& m:m_) {
                if(m.group!=p.b)continue;
                const auto* u=live(m);if(!u)continue;
                ++nb;
                const int x=detail::centreCell(u->x),z=detail::centreCell(u->z);
                if(x<0||z<0||x>=gw_||z>=gh_)continue;
                const size_t i=size_t(z)*size_t(gw_)+size_t(x);
                if(countEpoch_[i]!=cepoch_) {countEpoch_[i]=cepoch_;countGrid_[i]=0;}
                ++countGrid_[i];
            }
            for(const auto& m:m_) {
                if(m.group!=p.a)continue;
                const auto* u=live(m);if(!u)continue;
                ++na;
                const int x=detail::centreCell(u->x),z=detail::centreCell(u->z);
                for(int j=-cells;j<=cells;++j)for(int i=-cells;i<=cells;++i) {
                    const int cx=x+i,cz=z+j;
                    if(cx<0||cz<0||cx>=gw_||cz>=gh_)continue;
                    const size_t c=size_t(cz)*size_t(gw_)+size_t(cx);
                    if(countEpoch_[c]==cepoch_)p.contacts+=countGrid_[c];
                }
            }
            p.pairs+=na*nb;
        }
    }

    // ---- walls: chessboard chamfer distance over legal origins ----
    static constexpr int kClearanceCap=11;
    int clearance(const sim::Unit& u) {
        const int key=u.type->footX*64+u.type->footZ;
        auto it=std::find_if(dt_.begin(),dt_.end(),[&](const auto& e){return e.first==key;});
        if(it==dt_.end()) {dt_.push_back({key,buildDistance(u)});it=dt_.end()-1;}
        const int ox=sim::footprintOrigin(u.x,u.type->footX),oz=sim::footprintOrigin(u.z,u.type->footZ);
        if(ox<0||oz<0||ox>=bw_||oz>=bh_)return -1;
        const int d=it->second[size_t(oz)*size_t(bw_)+size_t(ox)];
        return d<=0?0:std::min(d-1,kClearanceCap);
    }
    std::vector<uint16_t> buildDistance(const sim::Unit& u) const {
        const int fx=u.type->footX,fz=u.type->footZ;
        std::vector<uint16_t> d(size_t(bw_)*size_t(bh_),0);
        for(int z=0;z<bh_;++z)for(int x=0;x<bw_;++x) {
            bool legal=x+fx<=bw_&&z+fz<=bh_;
            if(legal&&legalFn_)legal=legalFn_(u,x,z);
            else if(legal)for(int j=0;j<fz&&legal;++j)for(int i=0;i<fx&&legal;++i)legal=!blocked_[size_t(z+j)*size_t(bw_)+size_t(x+i)];
            d[size_t(z)*size_t(bw_)+size_t(x)]=legal?uint16_t(0xffff):0;
        }
        // Outside the map counts as illegal: distance 1 at the border.
        auto at=[&](int x,int z)->int {return x<0||z<0||x>=bw_||z>=bh_?0:d[size_t(z)*size_t(bw_)+size_t(x)];};
        for(int z=0;z<bh_;++z)for(int x=0;x<bw_;++x) {
            auto& v=d[size_t(z)*size_t(bw_)+size_t(x)];
            if(!v)continue;
            const int m=std::min({at(x-1,z),at(x-1,z-1),at(x,z-1),at(x+1,z-1)});
            v=uint16_t(std::min<int>(v,m+1));
        }
        for(int z=bh_-1;z>=0;--z)for(int x=bw_-1;x>=0;--x) {
            auto& v=d[size_t(z)*size_t(bw_)+size_t(x)];
            if(!v)continue;
            const int m=std::min({at(x+1,z),at(x+1,z+1),at(x,z+1),at(x-1,z+1)});
            v=uint16_t(std::min<int>(v,m+1));
        }
        return d;
    }

    Config cfg_;
    std::vector<Member> m_;
    mutable std::vector<std::pair<int,int>> memberIndex_;
    std::vector<GroupStats> groupStats_;
    std::vector<GateStats> gateStats_;
    std::vector<SideStats> sideStats_;
    std::vector<PairStats> pairStats_;
    std::vector<WorkSeries> work_;
    std::vector<int> bands_;
    int64_t start_=-1,samples_=0;
    int64_t spins_=0,reversals_=0,stopGo_=0,sideways_=0,backward_=0,back_=0,walkInPlace_=0,statue_=0,crawl_=0;
    int64_t waitingHeld_=0,waitingNoProgress_=0,parkedHeld_=0,parkedNoProgress_=0;
    int64_t ordered_=0,stopped_=0,flipSamples_=0,flips_=0,aimReversals_=0,engagement_=0,followMax_=0,followSum_=0,followSamples_=0;
    int64_t spacingSamples_=0,contactOwn_=0,contactOther_=0,contactSettled_=0;
    int64_t wallSamples_=0,wallTouch_=0;std::vector<int> near_;
    // id grid and the pair count grid
    int gw_=0,gh_=0;uint32_t epoch_=0,cepoch_=0;
    std::vector<uint32_t> gridEpoch_,countEpoch_;std::vector<int> gridId_;std::vector<int64_t> countGrid_;
    // walls
    std::vector<uint8_t> blocked_;int bw_=0,bh_=0;
    std::function<bool(const sim::Unit&,int,int)> legalFn_;
    std::vector<std::pair<int,std::vector<uint16_t>>> dt_;
};

// The Work adaptor: every LegionNavigator::Stats counter, as per-tick deltas,
// into an Observer's Work section (work.<snake_name>.max/p99/total), plus
// work.legion_total, the per-tick sum of the work classes below. Call
// sample() once per tick, after World::tick and before Observer::sample. It
// reads World::legionStats() (a const copy) only. A counter enters the report
// the first tick it moves (earlier ticks count 0), so a counter that never
// moves has no keys: read a missing key as 0. Gauges (bytes, live_*) and the
// running max completion_dist_max are not per-tick work and are skipped.
// Retail worlds have no navigator: nothing is fed.
//
// Total Legion work sums the classes that count cells, iterations or probes,
// each once: subsets are left out of the sum (field_work_* split field_work;
// sched_group_visits and join_iterations are inside group_loop_iters;
// formation_ring_cells and rechoice_bfs_cells inside slot_search_cells;
// line_sweeps are counted by trace_cells, one plus the cells stepped).
class NavWork {
public:
    static bool totalClass(std::string_view n) {
        static constexpr std::string_view k[]={
            "field_work","trace_cells","pass_scan_cells","slot_search_cells","group_loop_iters",
            "share_scan_iters","aware_pairs","held_rechecks","lift_members_walked","still_units_processed",
            "crowd_window_ring_cells","crowd_settle_visits","detour_cells","softowner_lookups"};
        for(auto c:k)if(c==n)return true;
        return false;
    }
    void sample(Observer& obs,const sim::World& w) {
        if(!const_cast<sim::World&>(w).legionNavigator())return;   // a plain accessor
        const auto s=w.legionStats();
        size_t i=0;uint64_t total=0;
        sim::LegionNavigator::forEachStat(s,[&](const char* name,uint64_t v) {
            const std::string_view n(name);
            if(i>=last_.size())last_.push_back(0);
            const uint64_t d=v>=last_[i]?v-last_[i]:0;last_[i++]=v;
            if(n=="bytes"||n.substr(0,5)=="live_"||n=="completion_dist_max")return;
            if(d)obs.work(n,d);
            if(totalClass(n))total+=d;
        });
        obs.work("legion_total",total);
    }
private:
    std::vector<uint64_t> last_;
};

}
