// ============================================================
// SD TRADING V8 - CPU THREAD POOL BACKEND IMPLEMENTATION
// src/scanner/cpu_backend.cpp
// Milestone M4.2
// ============================================================

#include "cpu_backend.h"

namespace SDTrading {
namespace Scanner {

CpuBackend::CpuBackend(int num_workers)
    : num_workers_(num_workers) {
    LOG_INFO("[CpuBackend] Starting " << num_workers_ << " worker threads.");
    for (int i = 0; i < num_workers_; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
}

CpuBackend::~CpuBackend() {
    shutdown_ = true;
    queue_cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    LOG_INFO("[CpuBackend] All workers stopped.");
}

void CpuBackend::worker_loop() {
    while (!shutdown_.load()) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]{
                return !task_queue_.empty() || shutdown_.load();
            });
            if (shutdown_.load() && task_queue_.empty()) return;
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        task();
    }
}

// ============================================================
// score_symbols — parallel dispatch, collect results
// ============================================================

std::vector<SignalResult> CpuBackend::score_symbols(
    std::vector<SymbolEngineContext*>& contexts,
    const std::vector<Core::Bar>& bars)
{
    if (contexts.size() != bars.size()) {
        LOG_ERROR("[CpuBackend] contexts/bars size mismatch: "
                  << contexts.size() << " vs " << bars.size());
        return {};
    }

    const size_t n = contexts.size();
    std::vector<std::future<SignalResult>> futures;
    futures.reserve(n);

    // Submit one task per symbol to the thread pool
    for (size_t i = 0; i < n; ++i) {
        SymbolEngineContext* ctx = contexts[i];
        Core::Bar bar = bars[i];

        futures.push_back(submit([ctx, bar]() -> SignalResult {
            return ctx->on_bar(bar);
        }));
    }

    // Collect results — blocks until all done
    std::vector<SignalResult> results;
    results.reserve(n);
    for (auto& f : futures) {
        results.push_back(f.get());
    }

    return results;
}

} // namespace Scanner
} // namespace SDTrading
