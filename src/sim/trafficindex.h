#pragma once
#include <cstddef>
#include <vector>

namespace tak::sim::flow {
// Direct id lookup for traffic records held in a std::map. The map remains
// the sole authority for membership, ordered iteration, cursors and
// checksums; this only caches pointers to its stable nodes, so it never
// decides an order and cannot change behavior. Ids outside the dense window
// fall back to the map. The owner must set/clear an entry exactly where it
// emplaces/erases the map node. Copying would duplicate node pointers into
// another map, so owners become move-only.
template<class Slot>
class IdIndex {
public:
    static constexpr int dense=1<<18;
    IdIndex()=default;
    IdIndex(const IdIndex&)=delete;
    IdIndex& operator=(const IdIndex&)=delete;
    IdIndex(IdIndex&& other) noexcept:slots_(std::move(other.slots_)),cleared_(other.cleared_) {
        other.slots_.clear();other.cleared_=0;
    }
    IdIndex& operator=(IdIndex&& other) noexcept {
        if(this!=&other){slots_=std::move(other.slots_);cleared_=other.cleared_;other.slots_.clear();other.cleared_=0;}
        return *this;
    }
    // Null when absent; callers then consult the map for out-of-window ids.
    const Slot* find(int id) const {
        return id>=0&&size_t(id)<slots_.size()&&slots_[size_t(id)].record?&slots_[size_t(id)]:nullptr;
    }
    static bool covers(int id) {return id>=0&&id<dense;}
    void set(int id,const Slot& slot) {
        if(!covers(id))return;
        if(size_t(id)>=slots_.size())slots_.resize(size_t(id)+1);
        slots_[size_t(id)]=slot;
    }
    // Trim the window back to the highest live id, releasing storage once it
    // is mostly unused, so a long game's climbing ids do not pin the
    // high-water size for a few live movers. A trim runs only after size/8
    // erases, which pays for its scan and for any regrowth (amortized O(1)).
    void clear(int id) {
        if(id<0||size_t(id)>=slots_.size())return;
        slots_[size_t(id)]=Slot{};
        if(++cleared_<slots_.size()/8)return;
        cleared_=0;
        while(!slots_.empty()&&!slots_.back().record)slots_.pop_back();
        if(slots_.capacity()>4096&&slots_.size()*4<=slots_.capacity())slots_.shrink_to_fit();
    }
    // Owners release storage once their map is empty, so idle tables own none.
    void release() {std::vector<Slot>().swap(slots_);cleared_=0;}
    size_t bytes() const {return slots_.capacity()*sizeof(Slot);}
private:
    std::vector<Slot> slots_;
    size_t cleared_=0;
};
}
