// ============================================================
// SD TRADING V8 - BAR ROUTER IMPLEMENTATION
// src/scanner/bar_router.cpp
// ============================================================

#include "scanner/bar_router.h"

namespace SDTrading {
namespace Scanner {

using namespace Utils;

BarRouter::BarRouter(Events::EventBus& bus)
    : bus_(bus)
{}

void BarRouter::register_symbol(SymbolContext& ctx) {
    contexts_[ctx.symbol()] = &ctx;
    LOG_INFO("[BarRouter] Registered symbol: " << ctx.symbol());
}

void BarRouter::subscribe(Events::EventBus& bus) {
    bus.subscribe_sync<Events::BarValidatedEvent>(
        "bar_router",
        [this](const Events::BarValidatedEvent& evt) {
            on_bar_validated(evt);
        });
    LOG_INFO("[BarRouter] Subscribed to BarValidatedEvent (SYNC).");
}

void BarRouter::on_bar_validated(const Events::BarValidatedEvent& evt) {
    // Symbol in BarValidatedEvent matches SymbolContext key directly.
    // Both use "NIFTY-FUT" / "BANKNIFTY-FUT" — no stripping needed.
    const std::string& canonical = evt.symbol;

    auto it = contexts_.find(canonical);
    if (it == contexts_.end()) {
        LOG_WARN("[BarRouter] No context for symbol: " << evt.symbol
                 << " (canonical=" << canonical << ") — bar dropped.");
        ++bars_dropped_;
        return;
    }

    // Convert event to Core::Bar
    Core::Bar bar;
    bar.datetime     = evt.timestamp;
    bar.open         = evt.open;
    bar.high         = evt.high;
    bar.low          = evt.low;
    bar.close        = evt.close;
    bar.volume       = static_cast<long>(evt.volume);
    bar.oi           = static_cast<double>(evt.open_interest);
    bar.source       = evt.source;   // "HISTORICAL" gates trading; "AMIBROKER" enables it

    it->second->push_bar(bar);
    ++bars_routed_;

    LOG_DEBUG("[BarRouter] Routed bar: " << evt.symbol
              << " " << evt.timestamp
              << " C=" << evt.close);
}

} // namespace Scanner
} // namespace SDTrading