#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace tak::sim {

// Retail group records (player+0x84, 99 records of 0xc4 bytes). 51b890
// rebuilds the aggregates every tick before 51d3e0 runs missions and movers.
// Members split into three classes: ground, boat (0x80000 with a positive
// minimum water depth) and flyer (type flag 0x800). "all" covers every
// completed member with a mover; "moving" only those whose current mission
// has 0x1000000 and not 0x4000000. Centres are integer means of the integer
// world positions (the high words of unit+0x68/0x6c/0x70); "area" sums the
// footprint products unit+0x78 * unit+0x7a.
enum class RetailGroupClass : uint8_t { Ground=0, Boat=1, Flyer=2 };

struct RetailGroupPoint { int32_t x=0, y=0, z=0; };

struct RetailGroupAggregate {
    int32_t count=0, area=0;
    RetailGroupPoint centre;   // retained when a later tick has no members
};

struct RetailGroupRecord {
    std::array<RetailGroupAggregate,3> all{}, moving{};
    bool active=false, changed=false;     // record +0x2c / +0xb0
    int32_t groundSpeed=0, boatSpeed=0;   // record +0x58 / +0x84 (16.16)
};

// 535e90: octagonal distance, max + min/4 with arithmetic shift.
constexpr int32_t retailOctDistance(int32_t dx,int32_t dz) {
    const int32_t x=dx<0?-dx:dx, z=dz<0?-dz:dz;
    return x>z ? x+(z>>2) : z+(x>>2);
}

struct RetailGroupMember {
    RetailGroupClass kind=RetailGroupClass::Ground;
    RetailGroupPoint position;   // integer world position
    int32_t area=0;              // footprint X * Z
    bool mover=true;
};

namespace detail {
// Centres are stored as 16.16 vectors and read back through their high words.
constexpr int32_t retailGroupWord(int32_t value) { return int16_t(uint16_t(uint32_t(value))); }
constexpr RetailGroupPoint retailGroupMean(const RetailGroupAggregate& g,const RetailGroupPoint& p,bool sameY) {
    const int32_t n=g.count;
    const int32_t x=(n*retailGroupWord(g.centre.x)+p.x)/(n+1);
    const int32_t z=(n*retailGroupWord(g.centre.z)+p.z)/(n+1);
    const int32_t y=sameY ? x : (n*retailGroupWord(g.centre.y)+p.y)/(n+1);
    return {x,y,z};
}
}

// 51c700(unit, includeSelf, moving): the point a member holds formation on.
// It is the group centre for the member's class, not a per-member slot. The
// ground variant's self-inclusive mean copies x into y (retail 51cdcd); that
// y is never read by the slot test.
constexpr RetailGroupPoint retailGroupCentre(const RetailGroupRecord& g,const RetailGroupMember& u,
                                             bool includeSelf,bool moving) {
    if (!u.mover) return {};
    const auto own=[&](RetailGroupClass c) {
        const auto& all=g.all[size_t(c)];const auto& part=g.moving[size_t(c)];
        if (!moving || part.count<=0) return all.centre;
        if (!includeSelf) return part.centre;
        return detail::retailGroupMean(part,u.position,c==RetailGroupClass::Ground);
    };
    if (u.kind!=RetailGroupClass::Flyer) return own(u.kind);
    constexpr int32_t far=99999999;
    const auto& boat=g.all[size_t(RetailGroupClass::Boat)];
    const auto& ground=g.all[size_t(RetailGroupClass::Ground)];
    const auto distance=[&](const RetailGroupAggregate& a) {
        return retailOctDistance(u.position.x-detail::retailGroupWord(a.centre.x),
                                 u.position.z-detail::retailGroupWord(a.centre.z));
    };
    const int32_t boatDistance=boat.count>0 ? distance(boat) : far;
    const int32_t groundDistance=ground.count>0 ? distance(ground) : far;
    if (groundDistance>=far && boatDistance>=far) {
        const auto& all=g.all[size_t(RetailGroupClass::Flyer)];
        const auto& part=g.moving[size_t(RetailGroupClass::Flyer)];
        if (!moving || part.count<=0) return all.centre;
        return includeSelf ? detail::retailGroupMean(part,u.position,false) : part.centre;
    }
    // A flyer formates on the nearer surface group (ties choose ground),
    // always counting itself as one more member.
    return detail::retailGroupMean(boatDistance<groundDistance ? boat : ground,u.position,false);
}

// 51ce40(unit, includeSelf, moving): the footprint area the slot test scales.
constexpr int32_t retailGroupRadius(const RetailGroupRecord& g,const RetailGroupMember& u,
                                    bool includeSelf,bool moving) {
    if (!u.mover) return 1;
    const auto own=[&](RetailGroupClass c) {
        const auto& part=g.moving[size_t(c)];
        if (!moving || part.count<=0) return g.all[size_t(c)].area;
        return includeSelf ? part.area+u.area : part.area;
    };
    if (u.kind!=RetailGroupClass::Flyer) return own(u.kind);
    constexpr int32_t far=99999999;
    const auto& boat=g.all[size_t(RetailGroupClass::Boat)];
    const auto& ground=g.all[size_t(RetailGroupClass::Ground)];
    const auto distance=[&](const RetailGroupAggregate& a) {
        return retailOctDistance(u.position.x-detail::retailGroupWord(a.centre.x),
                                 u.position.z-detail::retailGroupWord(a.centre.z));
    };
    const int32_t flyers=g.all[size_t(RetailGroupClass::Flyer)].area;
    const int32_t boatDistance=boat.count>0 ? distance(boat) : far;
    const int32_t groundDistance=ground.count>0 ? distance(ground) : far;
    if (groundDistance<far || boatDistance<far) {
        const int32_t value=u.area+(boatDistance<groundDistance ? boat.area : ground.area);
        return value>flyers ? value : flyers;
    }
    return own(RetailGroupClass::Flyer);
}

// 51d1e0(unit, moving, level): is the member outside its formation radius?
// Level 1..5 scales the area by 1/4, 1/2, 1, 2, 4; with `moving`, an empty
// moving subset relaxes the level by one. missionAll is the current mission's
// 0x4000000 flag, which makes the moving centre and area include the member.
constexpr bool retailGroupOutOfSlot(const RetailGroupRecord& g,const RetailGroupMember& u,
                                    bool moving,int level,bool missionAll) {
    if (!u.mover || !g.active || level<1 || level>=6) return false;
    if (moving) {
        const bool empty=u.kind==RetailGroupClass::Flyer
            ? g.all[size_t(RetailGroupClass::Boat)].count==0 && g.all[size_t(RetailGroupClass::Ground)].count==0 &&
              g.moving[size_t(RetailGroupClass::Flyer)].count==0
            : g.moving[size_t(u.kind)].count==0;
        if (empty) ++level;
    }
    if (level==6) return false;
    const bool includeSelf=moving && missionAll;
    const auto centre=retailGroupCentre(g,u,includeSelf,moving);
    int32_t radius=retailGroupRadius(g,u,includeSelf,moving);
    const int32_t distance=retailOctDistance(u.position.x-detail::retailGroupWord(centre.x),
                                             u.position.z-detail::retailGroupWord(centre.z))/16;
    switch (level) {
    case 5: radius*=4; break;
    case 4: radius*=2; break;
    case 2: radius/=2; break;
    case 1: radius/=4; break;
    default: break;
    }
    radius=std::max(radius,2);
    if (u.kind==RetailGroupClass::Flyer) radius*=2;
    return distance*distance/4>radius;
}

// 51b890's straggler pass: each paced member out of slot at level 4 whose
// terrain-scaled type maximum is below 1.5x the group speed slows the whole
// group to 0xaa7e/65536 of it. Several stragglers compound.
constexpr int32_t retailGroupStragglerSpeed(int32_t speed,int32_t typeMaximum) {
    const int32_t limit=int32_t((int64_t(speed)*0x18000)>>16);
    return typeMaximum<limit ? int32_t((int64_t(speed)*0xaa7e)>>16) : speed;
}

// One unit as 51b890 sees it. eligible = alive, completed, with a mover;
// typeMaximum is the terrain-scaled type maximum (type+0x162 x road/water).
struct RetailGroupUnit {
    int group=0;
    bool eligible=false;
    RetailGroupMember member;
    uint32_t missionFlags=0;   // 0 when there is no current mission
    int32_t typeMaximum=0;
};

// 51b890 for one player: groups with members (record+8) are rebuilt from
// their eligible units in unit order, then paced members out of slot at
// level 4 apply the straggler slowdown. paced receives each unit's
// navigator formation bit (vt+0x30). Returns whether any record's active
// flag or speed changed (the record+0xb0 "changed" flag, 50b7a0/50bbc0/50bfe0).
template<class Units,class Members>
bool retailGroupTick(const Units& units,const Members& hasMembers,
                     std::array<RetailGroupRecord,100>& records,std::vector<uint8_t>& paced) {
    struct Sum { int64_t x=0,y=0,z=0; int32_t count=0,area=0; };
    std::array<std::array<Sum,3>,100> all{},moving{};
    std::array<int32_t,100> ground{},boat{};
    paced.assign(units.size(),0);
    bool any=false;
    for (size_t g=1;g<100;++g) any|=bool(hasMembers[g]);
    if (any) for (size_t i=0;i<units.size();++i) {
        const auto& u=units[i];
        if (!u.eligible || u.group<1 || u.group>=100 || !hasMembers[size_t(u.group)]) continue;
        const size_t g=size_t(u.group);const auto& m=u.member;
        paced[i]=(u.missionFlags&0x2000000u)!=0;
        const auto add=[&](Sum& s) { ++s.count;s.area+=m.area;s.x+=m.position.x;s.y+=m.position.y;s.z+=m.position.z; };
        add(all[g][size_t(m.kind)]);
        if (!(u.missionFlags&0x1000000u)) continue;
        if (!(u.missionFlags&0x4000000u)) add(moving[g][size_t(m.kind)]);
        if (m.kind==RetailGroupClass::Flyer) continue;
        auto& speed=m.kind==RetailGroupClass::Boat ? boat[g] : ground[g];
        if (speed==0 || u.typeMaximum<speed) speed=u.typeMaximum;
    }
    bool changed=false;
    for (size_t g=1;g<100;++g) {
        auto& record=records[g];
        bool active=false;
        if (hasMembers[g]) {
            for (size_t c=0;c<3;++c) {
                const auto write=[](RetailGroupAggregate& a,const Sum& s) {
                    a.count=s.count;a.area=s.area;
                    // 16.16 words: the integer means are stored as their low 16 bits.
                    if (s.count>0) a.centre={int32_t(int16_t(s.x/s.count)),int32_t(int16_t(s.y/s.count)),
                                             int32_t(int16_t(s.z/s.count))};
                };
                write(record.all[c],all[g][c]);write(record.moving[c],moving[g][c]);
                active|=all[g][c].count>0;
            }
        }
        if (record.active!=active) record.changed=changed=true;
        record.active=active;
        if (active) {
            if (record.groundSpeed!=ground[g]||record.boatSpeed!=boat[g]) record.changed=changed=true;
            record.groundSpeed=ground[g];record.boatSpeed=boat[g];
        }
    }
    if (any) for (size_t i=0;i<units.size();++i) {
        const auto& u=units[i];
        if (!u.eligible || !paced[i] || u.member.kind==RetailGroupClass::Flyer ||
            u.group<1 || u.group>=100 || !hasMembers[size_t(u.group)]) continue;
        auto& record=records[size_t(u.group)];
        if (!record.active) continue;
        if (!retailGroupOutOfSlot(record,u.member,true,4,(u.missionFlags&0x4000000u)!=0)) continue;
        auto& speed=u.member.kind==RetailGroupClass::Boat ? record.boatSpeed : record.groundSpeed;
        const int32_t slowed=retailGroupStragglerSpeed(speed,u.typeMaximum);
        if (slowed!=speed) record.changed=changed=true;
        speed=slowed;
    }
    return changed;
}

// 51c612..51c6e1: when any record is flagged changed, every eligible unit
// in a changed group is notified (navigator vt+0x38) and all flags clear.
// Retail runs this only when game+0x3070 bit 0 is set, for owner kinds 1/2.
template<class Units>
std::vector<size_t> retailGroupNotify(const Units& units,std::array<RetailGroupRecord,100>& records) {
    std::vector<size_t> notified;
    if (std::none_of(records.begin()+1,records.end(),[](const RetailGroupRecord& r){return r.changed;}))
        return notified;
    for (size_t i=0;i<units.size();++i) {
        const auto& u=units[i];
        if (u.eligible && u.group>=1 && u.group<100 && records[size_t(u.group)].changed) notified.push_back(i);
    }
    for (size_t g=1;g<100;++g) records[g].changed=false;
    return notified;
}

// 50c480: take a member out of its group's moving aggregate for the rest of
// the tick. With fewer than two moving members only a flyer subset clears.
inline void retailGroupLeave(RetailGroupRecord& record,const RetailGroupMember& m) {
    if (!m.mover || !record.active) return;
    auto& part=record.moving[size_t(m.kind)];
    const int32_t n=part.count;
    if (n<2) {
        if (m.kind==RetailGroupClass::Flyer) part.count=part.area=0,part.centre={};
        return;
    }
    const int32_t area=part.area-m.area;
    if (area<=0) return;
    const auto word=[](int32_t v){return detail::retailGroupWord(v);};
    part.centre={int32_t(int16_t((word(part.centre.x)*n-m.position.x)/(n-1))),
                 int32_t(int16_t((word(part.centre.y)*n-m.position.y)/(n-1))),
                 int32_t(int16_t((word(part.centre.z)*n-m.position.z)/(n-1)))};
    part.count=n-1;part.area=area;
}

// 402b00..402cd8: Move_Ground's two group checks before its own stages.
// Positions are 16.16; the comparison sums the high words of the squared
// component differences. Returns 0 (none), -2 (re-form on the moving
// centre), 4 (wait), or 1 (cancel: 50c480 then 4d6a50(unit,0)).
struct RetailGroupPoint16 { int32_t x=0, y=0, z=0; };
template<class Random>
int retailGroundGroupCheck(const RetailGroupRecord& g,const RetailGroupMember& u,uint32_t& flags,
                           RetailGroupPoint16 unit,RetailGroupPoint16 goal,Random random) {
    if (retailGroupOutOfSlot(g,u,true,3,(flags&0x4000000u)!=0)) {
        const auto c=retailGroupCentre(g,u,false,true);
        const auto square=[](int32_t d) { return int32_t((int64_t(d)*d)>>32); };
        const auto wide=[](int32_t v) { return int32_t(uint32_t(detail::retailGroupWord(v))<<16); };
        const int32_t toCentre=square(goal.x-wide(c.x))+square(goal.y-wide(c.y))+square(goal.z-wide(c.z));
        const int32_t toMember=square(goal.x-unit.x)+square(goal.y-unit.y)+square(goal.z-unit.z);
        if (toCentre>toMember) {
            if ((flags&0x8000000u) && random(4)==0) return 1;
            flags|=0x8000000u;
        }
        return -2;
    }
    return retailGroupOutOfSlot(g,u,false,4,false) ? 4 : 0;
}

// 4d95f0 (4d976a..4d9a2a): the maximum for a paced member. own is the
// terrain-scaled unit maximum (unit+0x12b), typeMaximum the terrain-scaled
// type maximum (type+0x162), modeTypeMaximum that value after the speed-mode
// factor. Returns 0 when the group leaves the member at its own maximum.
// The capped value skips the speed-mode factor; the pitch limit still applies.
inline int32_t retailGroupSpeedCap(int32_t group,int32_t own,int32_t typeMaximum,int32_t modeTypeMaximum) {
    if (group<=0) return 0;
    const int32_t floor=int32_t(0.25*double(typeMaximum));
    if (group<floor) group=floor;
    if (group>=modeTypeMaximum || typeMaximum<=0) return 0;
    const int32_t ratio=int32_t(double(own)/65536.0/(double(typeMaximum)/65536.0)*65536.0);
    return int32_t((int64_t(ratio)*group)>>16);
}

} // namespace tak::sim
