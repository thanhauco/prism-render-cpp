#pragma once

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace prism {

// Runs fn(i) for i in [0, count) across all hardware threads, handing out indices
// dynamically so uneven work (e.g. dense tiles) balances itself.
template <typename Fn>
void parallelFor(int count, Fn&& fn) {
    if (count <= 0) return;
    int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, count);
    std::atomic<int> next{0};
    auto worker = [&] {
        for (int i; (i = next.fetch_add(1, std::memory_order_relaxed)) < count;) fn(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(threads - 1);
    for (int t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
}

} // namespace prism
