#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <stdexcept>

// A simple bounded, thread-safe FIFO queue for single producer/single consumer
// Template parameter T is the data type (e.g., RawBar)
template<typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t max_size) : max_size_(max_size) {}

    // Push an item into the queue. Blocks if full.
    void push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_not_full_.wait(lock, [this] { return queue_.size() < max_size_; });
        queue_.push(item);
        cond_not_empty_.notify_one();
    }

    // Pop an item from the queue. Blocks if empty.
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_not_empty_.wait(lock, [this] { return !queue_.empty(); });
        T item = queue_.front();
        queue_.pop();
        cond_not_full_.notify_one();
        return item;
    }

    // Try to pop, returns false if empty
    bool try_pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = queue_.front();
        queue_.pop();
        cond_not_full_.notify_one();
        return true;
    }

    // Check if empty
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // Get current size
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cond_not_empty_;
    std::condition_variable cond_not_full_;
    std::queue<T> queue_;
    size_t max_size_;
};
