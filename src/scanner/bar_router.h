// ============================================================
// SD TRADING V8 - BAR ROUTER
// src/scanner/bar_router.h
//
// PURPOSE:
//   Routes validated bars from EventBus to the correct
//   SymbolContext (and hence PipeBroker → LiveEngine).
//
// DESIGN:
//   Single responsibility — routing only.
//   Zero trading logic. Zero zone logic. Zero portfolio logic.
//
//   Subscribes to BarValidatedEvent on EventBus (SYNC).
//   Looks up symbol in context map.
//   Calls context.push_bar(bar).
// ============================================================

#ifndef SDTRADING_BAR_ROUTER_H
#define SDTRADING_BAR_ROUTER_H

#include "scanner/symbol_context.h"
#include "events/event_bus.h"
#include "events/event_types.h"
#include "utils/logger.h"
#include <unordered_map>
#include <string>
#include <memory>

namespace SDTrading {
namespace Scanner {

class BarRouter {
public:
    explicit BarRouter(Events::EventBus& bus);

    // Register a SymbolContext. Must be called before bus.start().
    void register_symbol(SymbolContext& ctx);

    // Subscribe to BarValidatedEvent on EventBus.
    void subscribe(Events::EventBus& bus);

    // Accessors
    int bars_routed()  const { return bars_routed_.load(); }
    int bars_dropped() const { return bars_dropped_.load(); }

private:
    // symbol → SymbolContext* (non-owning, contexts outlive router)
    std::unordered_map<std::string, SymbolContext*> contexts_;
    Events::EventBus&                               bus_;

    std::atomic<int>  bars_routed_  { 0 };
    std::atomic<int>  bars_dropped_ { 0 };

    void on_bar_validated(const Events::BarValidatedEvent& evt);
};

} // namespace Scanner
} // namespace SDTrading

#endif // SDTRADING_BAR_ROUTER_H
