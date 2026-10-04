#include "flowworkers.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace tak::sim::flow {
namespace {
// One bounded process pool, shared by server rooms. Constructed only on first
// flowfield worker use; Retail games neither create nor submit to it.
class Workers {
public:
    Workers() {
        try {for(unsigned i=0;i<4;++i)threads_.emplace_back([this]{run();});}
        catch(...) {
            {std::lock_guard lock(mutex_);stop_=true;}cv_.notify_all();
            for(auto& thread:threads_)thread.join();
            // Thread creation is a capability, not a simulation rule. Retain
            // the same fixed work schedule and execute jobs inline if the OS
            // cannot provide this process-wide pool.
            threads_.clear();
        }
    }
    ~Workers() {
        {std::lock_guard lock(mutex_);stop_=true;}cv_.notify_all();
        for(auto& thread:threads_)thread.join();
    }
    std::future<size_t> submit(std::function<size_t()> fn) {
        auto job=std::make_shared<std::packaged_task<size_t()>>(std::move(fn));auto result=job->get_future();
        bool inlineWork=false;
        {std::lock_guard lock(mutex_);if(stop_||queue_.size()>=64)inlineWork=true;else queue_.push_back(job);}
        if(inlineWork)(*job)();else cv_.notify_one();
        return result;
    }
private:
    void run() {
        for(;;) {
            std::shared_ptr<std::packaged_task<size_t()>> job;
            {std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stop_||!queue_.empty();});
                if(stop_&&queue_.empty())return;
                job=queue_.front();queue_.pop_front();}
            (*job)();
        }
    }
    std::mutex mutex_;std::condition_variable cv_;bool stop_=false;
    std::deque<std::shared_ptr<std::packaged_task<size_t()>>> queue_;
    std::vector<std::thread> threads_;
};
}
std::future<size_t> submitWork(std::function<size_t()> work) {static Workers pool;return pool.submit(std::move(work));}
}
