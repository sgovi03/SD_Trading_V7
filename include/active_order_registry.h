// ============================================================
// ACTIVE ORDER REGISTRY - Live Trading Order Tracking
// Thread-safe registry of open orders keyed by OrderTag
// ============================================================

#ifndef ACTIVE_ORDER_REGISTRY_H
#define ACTIVE_ORDER_REGISTRY_H

#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <vector>

/**
 * Represents a single open order in the live trading system.
 * Populated at entry and removed when the position is closed.
 */
struct ActiveOrder {
    std::string order_tag;       // 16-char unique tag (SDT format)
    std::string symbol;          // Trading symbol (e.g. "NIFTY25APRFUT")
    std::string side;            // "LONG" or "SHORT"
    int         lots;            // Position size in lots
    double      entry_price;     // Entry price
    double      stop_loss;       // Stop loss level
    int         zone_id;         // Zone that triggered the entry
    long long   entry_time_epoch; // Unix timestamp at entry (seconds)
};

/**
 * Thread-safe registry of all open orders, keyed by OrderTag.
 * Obtain the singleton via get_order_registry().
 */
class ActiveOrderRegistry {
public:
    /** Register a newly submitted order. */
    void register_order(const ActiveOrder& order) {
        std::lock_guard<std::mutex> lk(mutex_);
        map_[order.order_tag] = order;
    }

    /** Look up an order by tag. Returns std::nullopt if not found. */
    std::optional<ActiveOrder> get(const std::string& tag) const {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = map_.find(tag);
        if (it == map_.end()) return std::nullopt;
        return it->second;
    }

    /** Remove an order (call after confirmed close). */
    void remove(const std::string& tag) {
        std::lock_guard<std::mutex> lk(mutex_);
        map_.erase(tag);
    }

    /** Return true if the tag is currently registered. */
    bool has(const std::string& tag) const {
        std::lock_guard<std::mutex> lk(mutex_);
        return map_.count(tag) > 0;
    }

    /** Number of currently open orders. */
    size_t active_count() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return map_.size();
    }

    /** All registered tags — for diagnostics only. */
    std::vector<std::string> all_tags() const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<std::string> tags;
        tags.reserve(map_.size());
        for (const auto& kv : map_) {
            tags.push_back(kv.first);
        }
        return tags;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ActiveOrder> map_;
};

/** Process-wide singleton accessor. */
inline ActiveOrderRegistry& get_order_registry() {
    static ActiveOrderRegistry instance;
    return instance;
}

#endif // ACTIVE_ORDER_REGISTRY_H
