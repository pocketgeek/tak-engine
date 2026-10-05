#include "cooperativemovement.h"
#include <algorithm>

namespace tak::sim::cooperative {
bool MovementBatch::add(const Attempt& a) {
    if (a.id<=0 || a.player>=16 || a.footX<1 || a.footX>64 || a.footZ<1 || a.footZ>64 ||
        a.blockerCount>maxBlockers || (a.fromX==a.toX && a.fromZ==a.toZ)) {
        ++pending_.invalid;return false;
    }
    for (size_t i=0;i<a.blockerCount;++i) if (a.blockers[i]<=0) {
        ++pending_.invalid;return false;
    }
    if (entries_.size()==maxAttempts) {++pending_.deferred;return false;}
    if (entries_.capacity()==0) {
        entries_.reserve(maxAttempts);edges_.reserve(maxAttempts*maxBlockers);
        ready_.reserve(maxAttempts);index_.reserve(indexSlots);
    }
    entries_.push_back(Entry{a});++pending_.queued;return true;
}

MovementBatch::Stats MovementBatch::flush(uint32_t tick,
        const std::function<Outcome(const Attempt&)>& commit) {
    Stats out=pending_;
    if (entries_.empty()) {clear();return out;}
    const auto byId=[](const Entry& a,const Entry& b) {
        return a.attempt.id<b.attempt.id;
    };
    if (!std::is_sorted(entries_.begin(),entries_.end(),byId))
        std::sort(entries_.begin(),entries_.end(),byId);
    // Two native proposals for one body cannot both be valid. Reject both,
    // rather than letting insertion order select an extra movement step.
    for (size_t i=0;i<entries_.size();) {
        size_t end=i+1;
        while (end<entries_.size() && entries_[end].attempt.id==entries_[i].attempt.id) ++end;
        if (end>i+1) for (size_t j=i;j<end;++j) {
            entries_[j].failed=entries_[j].finished=true;++out.invalid;
        }
        i=end;
    }
    // This derived index stores entry+1, not another copy of each ID. Limit
    // collisions to64 probes; the sorted entries remain the exact fallback,
    // including large/sparse IDs and adversarial insertion overflow.
    index_.assign(indexSlots,0);
    constexpr size_t indexMask=indexSlots-1;
    const auto slotOf=[](int id) {return size_t((uint32_t(id)*2654435761u)>>17);};
    bool overflow=false;
    for (size_t i=0;i<entries_.size();++i) {
        const int id=entries_[i].attempt.id;size_t slot=slotOf(id);bool inserted=false;
        for (size_t probe=0;probe<64;++probe,slot=(slot+1)&indexMask) {
            auto& stored=index_[slot];
            if (!stored) {stored=uint16_t(i+1);inserted=true;break;}
            if (entries_[stored-1].attempt.id==id) {inserted=true;break;}
        }
        overflow|=!inserted;
    }
    const auto lookup=[&](int id) {
        size_t slot=slotOf(id);
        for (size_t probe=0;probe<64;++probe,slot=(slot+1)&indexMask) {
            const auto stored=index_[slot];
            // A sparse slot proves absence only if every ID was indexed.
            if (!stored) {if (!overflow) return entries_.end();break;}
            if (entries_[stored-1].attempt.id==id) return entries_.begin()+stored-1;
        }
        return std::lower_bound(entries_.begin(),entries_.end(),id,
            [](const Entry& candidate,int wanted){return candidate.attempt.id<wanted;});
    };
    for (size_t i=0;i<entries_.size();++i) {
        auto& e=entries_[i];if (e.finished) continue;
        for (size_t j=0;j<e.attempt.blockerCount;++j) {
            const int id=e.attempt.blockers[j];bool repeated=false;
            for (size_t k=0;k<j;++k) repeated|=e.attempt.blockers[k]==id;
            if (repeated) continue;
            const auto it=lookup(id);
            if (it==entries_.end() || it->attempt.id!=id) continue;
            if (it->finished) {e.failed=true;continue;}
            edges_.push_back({it->first,uint16_t(i)});
            it->first=uint32_t(edges_.size()-1);++e.remaining;
        }
    }
    // Rotate independent ready work without retained cursor state. Dependencies
    // still run first. For a stable cohort, each ready ID becomes first priority
    // within maxAttempts consecutive ticks even under full quota pressure.
    const size_t start=tick%entries_.size();
    auto later=[&](uint16_t a,uint16_t b) {
        if ((a<start)!=(b<start)) return a<start;
        return a>b;
    };
    for (size_t i=0;i<entries_.size();++i)
        if (!entries_[i].finished && entries_[i].remaining==0) ready_.push_back(uint16_t(i));
    std::make_heap(ready_.begin(),ready_.end(),later);
    size_t remaining=probeBudget;
    while (!ready_.empty()) {
        std::pop_heap(ready_.begin(),ready_.end(),later);
        const auto index=ready_.back();ready_.pop_back();auto& e=entries_[index];
        const size_t charge=4*size_t(e.attempt.footX)*e.attempt.footZ;
        bool moved=false;
        if (e.failed || charge>remaining) ++out.deferred;
        else {
            remaining-=charge;out.probes+=charge;++out.attempted;
            const Outcome result=commit(e.attempt);
            if (result==Outcome::Moved) {moved=true;++out.moved;}
            else if (result==Outcome::Invalid) ++out.invalid;
            else ++out.blocked;
        }
        e.finished=true;
        for (uint32_t edge=e.first;edge!=UINT32_MAX;edge=edges_[edge].next) {
            auto& follower=entries_[edges_[edge].follower];
            follower.failed|=!moved;
            if (--follower.remaining==0) {
                ready_.push_back(edges_[edge].follower);
                std::push_heap(ready_.begin(),ready_.end(),later);
            }
        }
    }
    // This also counts followers of a closed cycle. None receive a speculative
    // callback: an actually occupied cycle cannot create its own vacancy.
    for (const auto& e:entries_) if (!e.finished) ++out.cycle;
    clear();return out;
}

void MovementBatch::clear() {
    entries_.clear();edges_.clear();ready_.clear();index_.clear();pending_={};
}
size_t MovementBatch::bytes() const {
    return sizeof(*this)+entries_.capacity()*sizeof(Entry)+edges_.capacity()*sizeof(Edge)+
        (ready_.capacity()+index_.capacity())*sizeof(uint16_t);
}
}
