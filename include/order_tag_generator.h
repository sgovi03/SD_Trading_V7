// ============================================================
// ORDER TAG GENERATOR - Live Trading Order Identification
// Format: SDT + MMDDHHMMSS + [L/S] + CC  (always 16 chars)
// ============================================================

#ifndef ORDER_TAG_GENERATOR_H
#define ORDER_TAG_GENERATOR_H

#include <string>
#include <atomic>
#include <ctime>
#include <cstdio>
#include <cassert>
#include <cctype>

/**
 * Generates unique 16-character alphanumeric order tags.
 *
 * Format: SDT<MMDDHHMMSS><L|S><CC>
 *   SDT        - fixed system prefix (3 chars)
 *   MMDDHHMMSS - local timestamp at generation time (10 chars)
 *   L or S     - L for BUY/LONG, S for SELL/SHORT (1 char)
 *   CC         - global atomic counter 01-99, resets on process restart (2 chars)
 *
 * Example: SDT0331093512L01
 *
 * Counter is global across all instruments and wraps 99 -> 1.
 */
class OrderTagGenerator {
public:
    /**
     * Generate a unique 16-char order tag.
     * @param side "BUY", "LONG", "SELL", or "SHORT"
     * @return 16-char alphanumeric OrderTag string
     */
    static std::string generate(const std::string& side) {
        // Timestamp: MMDDHHMMSS (Windows-compatible)
        std::time_t now = std::time(nullptr);
        std::tm tm_info{};
        localtime_s(&tm_info, &now);

        char ts[11];
        std::snprintf(ts, sizeof(ts), "%02d%02d%02d%02d%02d",
            tm_info.tm_mon + 1,  // month: 01-12
            tm_info.tm_mday,     // day:   01-31
            tm_info.tm_hour,     // hour:  00-23
            tm_info.tm_min,      // min:   00-59
            tm_info.tm_sec);     // sec:   00-59

        // Direction character
        char dir = (side == "BUY" || side == "LONG") ? 'L' : 'S';

        // Global counter 01-99, wraps back to 01
        int raw = counter_.fetch_add(1, std::memory_order_relaxed);
        int cc  = ((raw - 1) % 99) + 1;  // maps raw=1..99->1..99, raw=100->1, etc.

        char tag[17];
        std::snprintf(tag, sizeof(tag), "SDT%s%c%02d", ts, dir, cc);

        std::string result(tag);

        assert(result.length() == 16 && "OrderTag must be exactly 16 chars");
        for (unsigned char c : result) {
            assert(std::isalnum(c) && "OrderTag must be alphanumeric only");
        }

        return result;
    }

private:
    inline static std::atomic<int> counter_{1};
};

#endif // ORDER_TAG_GENERATOR_H
