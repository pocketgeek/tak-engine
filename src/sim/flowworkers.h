#pragma once
#include <cstddef>
#include <functional>
#include <future>

namespace tak::sim::flow {
// Lazy process-wide executor shared by Flowfield matches and preparation stages.
// Four workers, at most 64 queued tasks; saturation/creation failure executes
// inline. Callers select deterministic work first and join EVERY future before
// publication, mutation of shared inputs, cancellation, or destruction.
// Jobs must own sealed inputs and may never capture live World/Unit state.
std::future<size_t> submitWork(std::function<size_t()> work);
}
