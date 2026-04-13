// ============================================================
// SD TRADING V8 - IN-PROCESS EVENT BUS
// src/events/event_bus.h
// Milestone M2.4
//
// Design:
//   Single in-process pub/sub bus. No external broker needed.
//   Each event type has its own subscriber list.
//
// Dispatch modes:
//   SYNC  — called on publisher's thread. Zero latency.
//           Used for engine scoring (bar → signal in same thread).
//   ASYNC — dispatched to a dedicated worker thread.
//           Used for DB writes, alerts (slow I/O never blocks engine).
//
// Threading:
//   Subscriber registration: call before any threads start (startup only).
//   publish() is thread-safe — multiple pipe listener threads can publish.
//   ASYNC queue is drained by a single dedicated worker thread.
//   SYNC callbacks execute on the calling thread — keep them fast.
//
// Usage:
//   EventBus bus;
//   bus.subscribe_sync<BarValidatedEvent>("engine",
//       [&scanner](const BarValidatedEvent& e){ scanner.on_bar(e); });
//   bus.subscribe_async<BarValidatedEvent>("db_writer",
//       [&writer](const BarValidatedEvent& e){ writer.write_bar(e); });
//   bus.start();
//   bus.publish(myBarEvent);   // engine called sync, db_writer async
// ============================================================

#ifndef SDTRADING_EVENT_BUS_H
#define SDTRADING_EVENT_BUS_H

#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <atomic>
#include <any>
#include <typeindex>
#include <unordered_map>
#include <stdexcept>
#include "../utils/logger.h"

namespace SDTrading {
namespace Events {

// ============================================================
// ASYNC WORK ITEM
// Type-erased wrapper stored in the async queue.
// ============================================================
struct AsyncWorkItem {
    std::function<void()> fn;  // captures event by value
};

// ============================================================
// EVENT BUS
// ============================================================

class EventBus {
public:

    EventBus() : running_(false) {}
    ~EventBus() { stop(); }

    // Non-copyable, non-movable
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // --------------------------------------------------------
    // Start async worker thread.
    // Call AFTER all subscriptions are registered.
    // --------------------------------------------------------
    void start() {
        if (running_.exchange(true)) return;  // already running
        worker_ = std::thread([this]() { run_async_worker(); });
        LOG_INFO("[EventBus] Started. Async worker running.");
    }

    // --------------------------------------------------------
    // Stop async worker and drain remaining items.
    // --------------------------------------------------------
    void stop() {
        if (!running_.exchange(false)) return;
        async_cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        LOG_INFO("[EventBus] Stopped.");
    }

    // --------------------------------------------------------
    // subscribe_sync<EventType>
    // Callback runs on the publisher's thread — must be fast.
    // Use for: engine scoring, signal ranking.
    // --------------------------------------------------------
    template<typename EventType>
    void subscribe_sync(const std::string& name,
                        std::function<void(const EventType&)> callback) {
        auto key = std::type_index(typeid(EventType));
        std::lock_guard<std::mutex> lock(sub_mutex_);
        sync_subs_[key].push_back({ name, [callback](const std::any& a) {
            callback(std::any_cast<const EventType&>(a));
        }});
        LOG_INFO("[EventBus] SYNC subscriber '" << name
                 << "' registered for " << typeid(EventType).name());
    }

    // --------------------------------------------------------
    // subscribe_async<EventType>
    // Callback runs on the dedicated async worker thread.
    // Use for: DB writes, HTTP calls, file I/O, alerts.
    // --------------------------------------------------------
    template<typename EventType>
    void subscribe_async(const std::string& name,
                         std::function<void(const EventType&)> callback) {
        auto key = std::type_index(typeid(EventType));
        std::lock_guard<std::mutex> lock(sub_mutex_);
        async_subs_[key].push_back({ name, [callback](const std::any& a) {
            callback(std::any_cast<const EventType&>(a));
        }});
        LOG_INFO("[EventBus] ASYNC subscriber '" << name
                 << "' registered for " << typeid(EventType).name());
    }

    // --------------------------------------------------------
    // publish<EventType>
    // Thread-safe. Event copied into async queue for ASYNC subs;
    // called inline for SYNC subs.
    // --------------------------------------------------------
    template<typename EventType>
    void publish(const EventType& event) {
        auto key = std::type_index(typeid(EventType));

        // SYNC subscribers — called immediately on this thread
        {
            std::lock_guard<std::mutex> lock(sub_mutex_);
            auto sit = sync_subs_.find(key);
            if (sit != sync_subs_.end()) {
                std::any wrapped = event;
                for (auto& sub : sit->second) {
                    try {
                        sub.fn(wrapped);
                    } catch (const std::exception& e) {
                        LOG_ERROR("[EventBus] SYNC subscriber '" << sub.name
                                  << "' threw: " << e.what());
                    }
                }
            }
        }

        // ASYNC subscribers — enqueue; worker thread dispatches
        {
            std::lock_guard<std::mutex> lock(sub_mutex_);
            auto ait = async_subs_.find(key);
            if (ait == async_subs_.end()) return;
            if (ait->second.empty()) return;

            // Copy event for each async subscriber
            for (auto& sub : ait->second) {
                auto event_copy = event;            // value copy
                std::string sub_name = sub.name;
                auto fn_copy = sub.fn;

                std::lock_guard<std::mutex> qlock(queue_mutex_);
                async_queue_.push({ [event_copy, sub_name, fn_copy]() {
                    std::any wrapped = event_copy;
                    try {
                        fn_copy(wrapped);
                    } catch (const std::exception& e) {
                        LOG_ERROR("[EventBus] ASYNC subscriber '" << sub_name
                                  << "' threw: " << e.what());
                    }
                }});
            }
            async_cv_.notify_one();
        }
    }

    // Queue depth (for monitoring)
    size_t async_queue_depth() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return async_queue_.size();
    }

private:

    struct Subscriber {
        std::string name;
        std::function<void(const std::any&)> fn;
    };

    // Subscriber maps (key = event type)
    std::unordered_map<std::type_index, std::vector<Subscriber>> sync_subs_;
    std::unordered_map<std::type_index, std::vector<Subscriber>> async_subs_;
    mutable std::mutex sub_mutex_;

    // Async worker
    std::queue<AsyncWorkItem>  async_queue_;
    mutable std::mutex         queue_mutex_;
    std::condition_variable    async_cv_;
    std::thread                worker_;
    std::atomic<bool>          running_;

    void run_async_worker() {
        LOG_INFO("[EventBus] Async worker started (tid=" 
                 << std::this_thread::get_id() << ")");
        while (true) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            async_cv_.wait(lock, [this]{
                return !async_queue_.empty() || !running_.load();
            });

            // Drain queue before checking stop
            while (!async_queue_.empty()) {
                auto item = std::move(async_queue_.front());
                async_queue_.pop();
                lock.unlock();
                item.fn();
                lock.lock();
            }

            if (!running_.load() && async_queue_.empty()) break;
        }
        LOG_INFO("[EventBus] Async worker stopped.");
    }
};

} // namespace Events
} // namespace SDTrading

#endif // SDTRADING_EVENT_BUS_H
