// ============================================================
// SD TRADING V8 - CPU THREAD POOL BACKEND
// src/scanner/cpu_backend.h
// Milestone M4.2
//
// Primary compute backend. Uses std::thread pool (8 workers
// on Ryzen 9 8945HS). Each symbol scored on its own thread.
// No CUDA dependency. Always available.
// ============================================================

#ifndef SDTRADING_CPU_BACKEND_H
#define SDTRADING_CPU_BACKEND_H

#include "compute_backend.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <atomic>
#include "../utils/logger.h"

namespace SDTrading {
namespace Scanner {

class CpuBackend : public IComputeBackend {
public:
    explicit CpuBackend(int num_workers = 8);
    ~CpuBackend() override;

    // Process one bar per context in parallel.
    // Blocks until all symbols have been scored.
    std::vector<SignalResult> score_symbols(
        std::vector<SymbolEngineContext*>& contexts,
        const std::vector<Core::Bar>& bars) override;

    std::string name()         const override { return "CPU_THREADPOOL"; }
    bool        is_available() const override { return true; }

    int  worker_count() const { return num_workers_; }

private:
    int                          num_workers_;
    std::vector<std::thread>     workers_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex                   queue_mutex_;
    std::condition_variable      queue_cv_;
    std::atomic<bool>            shutdown_ { false };

    void worker_loop();

    // Submit a task and get a future for the result
    template<typename F>
    auto submit(F&& f) -> std::future<decltype(f())> {
        using RetType = decltype(f());
        auto task = std::make_shared<std::packaged_task<RetType()>>(
            std::forward<F>(f));
        auto future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            task_queue_.push([task]() { (*task)(); });
        }
        queue_cv_.notify_one();
        return future;
    }
};

} // namespace Scanner
} // namespace SDTrading

#endif // SDTRADING_CPU_BACKEND_H
