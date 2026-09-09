#pragma once

// Persistent worker pool for data-parallel loops (e.g. per-unit animation VM
// ticks). parallelFor splits [0,count) into chunks pulled off an atomic counter
// and blocks until all are done; the calling thread participates as one worker.
// Extracted from client/main.cpp; kept at global scope (and header-only, since
// parallelFor is a template) so its unqualified use sites there are unchanged.

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool {
public:
    ThreadPool() {
        unsigned n = std::thread::hardware_concurrency();
        n_ = n ? n : 1;
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

    // minParallel: run serially below this count. Dispatch (wake + join across all
    // workers) measures ~30us flat, so a callsite whose per-item work is tiny must
    // set this high -- e.g. the COB VM tick at ~0.2us/item only breaks even around
    // ~2000 items; heavy per-item work (model projection) keeps the low default.
    template <class F>
    void parallelFor(size_t count, F&& f, size_t minParallel = 32) {
        if (count == 0) return;
        if (n_ == 1 || count < minParallel) { f(size_t(0), count); return; }  // not worth it
        std::function<void(size_t, size_t)> fn =
            [&f](size_t b, size_t e) { f(b, e); };
        {
            std::lock_guard<std::mutex> lk(m_);
            fn_ = &fn;
            count_ = count;
            next_.store(0, std::memory_order_relaxed);
            active_ = n_;
            ++gen_;
        }
        cv_.notify_all();
        runChunks();
        std::unique_lock<std::mutex> lk(m_);
        doneCv_.wait(lk, [this] { return active_ == 0; });
        fn_ = nullptr;
    }

private:
    void runChunks() {
        constexpr size_t kChunk = 8;
        for (;;) {
            size_t b = next_.fetch_add(kChunk, std::memory_order_relaxed);
            if (b >= count_) break;
            size_t e = std::min(b + kChunk, count_);
            (*fn_)(b, e);
        }
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
            runChunks();
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
};
