#pragma once

// Persistent worker pool for data-parallel loops (e.g. per-unit animation VM
// ticks). parallelFor splits [0,count) into chunks pulled off an atomic counter
// and blocks until all are done; the calling thread participates as one worker.
// Extracted from client/main.cpp; kept at global scope (and header-only, since
// parallelFor is a template) so its unqualified use sites there are unchanged.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool {
public:
    ThreadPool() : ThreadPool(std::thread::hardware_concurrency()) {}
    // A pool of `threads` workers in all, the calling thread included (so threads-1 are
    // spawned). A pool is never shared between two callers that may run at once.
    explicit ThreadPool(unsigned threads) {
        n_ = threads ? threads : 1;
        for (unsigned i = 1; i < n_; ++i)
            workers_.emplace_back([this] { workerLoop(); });
    }
    ~ThreadPool() {
        { std::lock_guard<std::mutex> lk(m_); stop_ = true; }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Optional dispatch telemetry (render diagnostics). Null = off: parallelFor
    // then reads no clocks. The caller-thread fields are written by the thread
    // calling parallelFor; worker busy/wake time arrives through atomics and is
    // folded in before parallelFor returns. Changes timing only, never the
    // chunking or the results.
    struct Stats {
        uint64_t dispatches = 0, serial = 0, items = 0;
        uint64_t dispatchNs = 0;   // lock + wake of the workers
        uint64_t waitNs = 0;       // caller idle after its own chunks until all finish
        uint64_t busyNs = 0;       // every participant's time running chunks (summed)
        uint64_t wakeNs = 0;       // dispatch -> each worker starting (summed over workers)
        uint64_t wakes = 0;
    };
    void setStats(Stats* stats) { stats_ = stats; }
    unsigned threads() const { return n_; }

    // minParallel: run serially below this count. Dispatch (wake + join across all
    // workers) measures ~30us flat, so a callsite whose per-item work is tiny must
    // set this high -- e.g. the COB VM tick at ~0.2us/item only breaks even around
    // ~2000 items; heavy per-item work (model projection) keeps the low default.
    template <class F>
    void parallelFor(size_t count, F&& f, size_t minParallel = 32) {
        if (count == 0) return;
        Stats* const stats = stats_;
        if (n_ == 1 || count < minParallel) {   // not worth it
            if (stats) { ++stats->serial; stats->items += count; }
            f(size_t(0), count);
            return;
        }
        std::function<void(size_t, size_t)> fn =
            [&f](size_t b, size_t e) { f(b, e); };
        const int64_t start = stats ? nowNs() : 0;
        {
            std::lock_guard<std::mutex> lk(m_);
            fn_ = &fn;
            count_ = count;
            next_.store(0, std::memory_order_relaxed);
            active_ = n_;
            timed_ = stats != nullptr;
            dispatchedNs_ = start;
            ++gen_;
        }
        cv_.notify_all();
        if (stats) { stats->dispatchNs += uint64_t(nowNs() - start); ++stats->dispatches; stats->items += count; }
        runChunks(false);
        const int64_t own = stats ? nowNs() : 0;
        std::unique_lock<std::mutex> lk(m_);
        doneCv_.wait(lk, [this] { return active_ == 0; });
        fn_ = nullptr;
        if (stats) {
            stats->waitNs += uint64_t(nowNs() - own);
            stats->busyNs += busyNs_.exchange(0, std::memory_order_relaxed);
            stats->wakeNs += wakeNs_.exchange(0, std::memory_order_relaxed);
            stats->wakes += wakes_.exchange(0, std::memory_order_relaxed);
        }
    }

private:
    static int64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    // timed_/dispatchedNs_ are written under m_ before the generation bump, so a
    // worker that saw the new generation reads this dispatch's values.
    void runChunks(bool worker) {
        constexpr size_t kChunk = 8;
        const bool timed = timed_;
        const int64_t start = timed ? nowNs() : 0;
        if (timed && worker) {
            wakeNs_.fetch_add(uint64_t(std::max<int64_t>(0, start - dispatchedNs_)), std::memory_order_relaxed);
            wakes_.fetch_add(1, std::memory_order_relaxed);
        }
        for (;;) {
            size_t b = next_.fetch_add(kChunk, std::memory_order_relaxed);
            if (b >= count_) break;
            size_t e = std::min(b + kChunk, count_);
            (*fn_)(b, e);
        }
        if (timed) busyNs_.fetch_add(uint64_t(nowNs() - start), std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(m_);
        if (--active_ == 0) doneCv_.notify_one();
    }
    void workerLoop() {
        unsigned seen = 0;
        for (;;) {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this, &seen] { return stop_ || gen_ != seen; });
            if (stop_) return;
            seen = gen_;
            lk.unlock();
            runChunks(true);
        }
    }
    unsigned n_ = 1;
    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable cv_, doneCv_;
    std::function<void(size_t, size_t)>* fn_ = nullptr;
    size_t count_ = 0;
    std::atomic<size_t> next_{0};
    unsigned active_ = 0, gen_ = 0;
    bool stop_ = false;
    Stats* stats_ = nullptr;
    bool timed_ = false;
    int64_t dispatchedNs_ = 0;
    std::atomic<uint64_t> busyNs_{0}, wakeNs_{0}, wakes_{0};
};
