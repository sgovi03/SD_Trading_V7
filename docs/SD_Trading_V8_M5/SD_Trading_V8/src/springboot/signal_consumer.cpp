// ============================================================
// SD TRADING V8 - SIGNAL CONSUMER IMPLEMENTATION
// src/springboot/signal_consumer.cpp
// Milestone M5
// ============================================================

#include "signal_consumer.h"

namespace SDTrading {
namespace SpringBoot {

// ============================================================
// Constructor
// ============================================================

SignalConsumer::SignalConsumer(
    const SignalConsumerConfig&     cfg,
    const Live::OrderSubmitConfig&  order_cfg,
    Events::EventBus&               bus)
    : cfg_(cfg)
    , order_submitter_(order_cfg)
    , bus_(bus)
{
    LOG_INFO("[SignalConsumer] Created."
             << " dry_run=" << (cfg_.dry_run ? "YES" : "NO")
             << " max_positions=" << cfg_.max_open_positions
             << " enabled=" << (cfg_.enabled ? "YES" : "NO"));

    if (!order_submitter_.initialize()) {
        LOG_WARN("[SignalConsumer] OrderSubmitter init failed — "
                 "Spring Boot service may not be running.");
    }
}

// ============================================================
// subscribe — register ASYNC EventBus handler
// ============================================================

void SignalConsumer::subscribe(Events::EventBus& bus) {
    bus.subscribe_async<Events::SignalEvent>("signal_consumer",
        [this](const Events::SignalEvent& e) { on_signal(e); });
    LOG_INFO("[SignalConsumer] Subscribed to SignalEvent (ASYNC).");
}

// ============================================================
// on_signal — main handler
// ============================================================

void SignalConsumer::on_signal(const Events::SignalEvent& evt) {
    ++signals_received_;

    LOG_INFO("[SignalConsumer] Signal received:"
             << " " << evt.symbol
             << " " << evt.direction
             << " score=" << evt.score
             << " @" << evt.entry_price
             << " tag=" << evt.order_tag);

    // ── Guard: consumer disabled ─────────────────────────────
    if (!cfg_.enabled) {
        ++orders_skipped_;
        LOG_INFO("[SignalConsumer] Skipped (disabled): " << evt.order_tag);
        return;
    }

    // ── Guard: portfolio position limit ──────────────────────
    if (open_positions_.load() >= cfg_.max_open_positions) {
        ++orders_skipped_;
        LOG_INFO("[SignalConsumer] Skipped (portfolio full "
                 << open_positions_.load() << "/"
                 << cfg_.max_open_positions << "): " << evt.symbol);
        return;
    }

    // ── Guard: duplicate order tag ────────────────────────────
    {
        std::lock_guard<std::mutex> lock(tags_mutex_);
        if (submitted_tags_.count(evt.order_tag)) {
            ++orders_skipped_;
            LOG_WARN("[SignalConsumer] Duplicate tag rejected: " << evt.order_tag);
            return;
        }
    }

    // ── Dry run mode — log but don't submit ──────────────────
    if (cfg_.dry_run) {
        ++orders_submitted_;
        {
            std::lock_guard<std::mutex> lock(tags_mutex_);
            submitted_tags_.insert(evt.order_tag);
        }
        LOG_INFO("[SignalConsumer] [DRY-RUN] Would submit:"
                 << " " << evt.direction
                 << " " << evt.lot_size << " lot(s)"
                 << " " << evt.symbol
                 << " @" << evt.entry_price
                 << " SL=" << evt.stop_loss
                 << " TP=" << evt.target_1
                 << " tag=" << evt.order_tag);
        // Still publish TradeOpenEvent so DB records the signal
        Live::OrderSubmitResult fake_result;
        fake_result.success = true;
        fake_result.http_status = 200;
        fake_result.response_body = "{\"s\":\"ok\",\"message\":\"DRY_RUN\"}";
        publish_trade_open(evt, fake_result);
        return;
    }

    // ── Submit order to Spring Boot ───────────────────────────
    LOG_INFO("[SignalConsumer] Submitting to Spring Boot:"
             << " " << evt.direction
             << " " << evt.lot_size << " lot(s)"
             << " tag=" << evt.order_tag);

    Live::OrderSubmitResult result = order_submitter_.submit_order(
        evt.direction,
        evt.lot_size,
        evt.zone_id,
        evt.score,
        evt.entry_price,
        evt.stop_loss,
        evt.target_1,
        evt.order_tag
    );

    if (result.success) {
        ++orders_submitted_;
        ++open_positions_;
        {
            std::lock_guard<std::mutex> lock(tags_mutex_);
            submitted_tags_.insert(evt.order_tag);
        }
        LOG_INFO("[SignalConsumer] Order accepted by Spring Boot:"
                 << " tag=" << evt.order_tag
                 << " http=" << result.http_status);
        publish_trade_open(evt, result);
    } else {
        ++orders_failed_;
        LOG_ERROR("[SignalConsumer] Order REJECTED by Spring Boot:"
                  << " tag=" << evt.order_tag
                  << " http=" << result.http_status
                  << " error=" << result.error_message);
        publish_alert(Events::AlertSeverity::ERROR,
            "Order rejected: " + evt.order_tag
            + " | " + result.error_message,
            evt.symbol);
    }
}

// ============================================================
// publish_trade_open
// ============================================================

void SignalConsumer::publish_trade_open(
    const Events::SignalEvent&       signal,
    const Live::OrderSubmitResult&   result)
{
    Events::TradeOpenEvent evt;
    evt.order_tag   = signal.order_tag;
    evt.symbol      = signal.symbol;
    evt.direction   = signal.direction;
    evt.entry_time  = signal.signal_time;
    evt.entry_price = signal.entry_price;
    evt.stop_loss   = signal.stop_loss;
    evt.take_profit = signal.target_1;
    evt.lots        = signal.lot_size;
    evt.zone_id     = signal.zone_id;
    // broker_order_id from Spring Boot response (if parseable)
    if (!result.response_body.empty()) {
        evt.broker_order_id = result.response_body;
    }
    bus_.publish(evt);
}

// ============================================================
// publish_alert
// ============================================================

void SignalConsumer::publish_alert(Events::AlertSeverity sev,
                                   const std::string& msg,
                                   const std::string& symbol)
{
    Events::SystemAlertEvent alert;
    alert.severity = sev;
    alert.source   = "SignalConsumer";
    alert.symbol   = symbol;
    alert.message  = msg;
    bus_.publish(alert);
}

// ============================================================
// Position tracking
// ============================================================

void SignalConsumer::on_trade_opened(const std::string& symbol) {
    ++open_positions_;
    LOG_DEBUG("[SignalConsumer] Trade opened: " << symbol
              << " open=" << open_positions_.load());
}

void SignalConsumer::on_trade_closed(const std::string& symbol) {
    if (open_positions_.load() > 0) --open_positions_;
    LOG_INFO("[SignalConsumer] Trade closed: " << symbol
             << " open=" << open_positions_.load());
}

} // namespace SpringBoot
} // namespace SDTrading
