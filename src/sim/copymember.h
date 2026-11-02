#pragma once
// Members that let an owner keep a defaulted copy (World::clone, the Legion navigator).
#include <atomic>
#include <utility>

namespace tak::sim {

// A member a defaulted copy leaves empty, for the owner to fill by hand afterwards:
// an owning pointer whose target needs its own clone, a table of owned arrays, a
// cache that has to be rebuilt round its copies.
template<class T> struct LeftEmptyOnCopy : T {
    using T::T;
    LeftEmptyOnCopy()=default;
    LeftEmptyOnCopy(T&& value):T(std::move(value)) {}
    LeftEmptyOnCopy(const LeftEmptyOnCopy&) noexcept:T() {}
    LeftEmptyOnCopy(LeftEmptyOnCopy&&)=default;
    LeftEmptyOnCopy& operator=(const LeftEmptyOnCopy&) {static_cast<T&>(*this)=T();return *this;}
    LeftEmptyOnCopy& operator=(LeftEmptyOnCopy&&)=default;
};

// An atomic a copy carries by value. Copying is not synchronised: the worlds are
// copied between ticks, when nothing else writes them.
template<class T> struct CopyableAtomic : std::atomic<T> {
    CopyableAtomic(T value=T()) noexcept:std::atomic<T>(value) {}
    CopyableAtomic(const CopyableAtomic& o) noexcept:std::atomic<T>(o.load(std::memory_order_relaxed)) {}
    CopyableAtomic& operator=(const CopyableAtomic& o) noexcept {this->store(o.load(std::memory_order_relaxed));return *this;}
    using std::atomic<T>::operator=;
};

}  // namespace tak::sim
