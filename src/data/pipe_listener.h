// ============================================================
// SD TRADING V8 - NAMED PIPE LISTENER
// src/data/pipe_listener.h
// Milestone M2.2
//
// One PipeListener instance per active symbol.
// Each instance runs one persistent reader thread that blocks
// on a Windows named pipe written by the AFL DataBridge.
//
// Pipe naming convention:
//   \\.\pipe\SD_{SYMBOL}_{TIMEFRAME}
//   e.g.: \\.\pipe\SD_NIFTY_5min
//         \\.\pipe\SD_BANKNIFTY_5min
//
// Wire format (pipe message, newline-terminated):
//   SOURCE|SYMBOL|TIMEFRAME|TIMESTAMP|O|H|L|C|V|OI
//   e.g.: AMIBROKER|NIFTY|5min|2026-04-09T09:30:00+05:30|
//         22950.00|23010.50|22940.00|22985.00|142500|9875400
//
// On each message:
//   1. Parse wire format → RawBar
//   2. Pass to BarValidator
//   3. If PASS: publish BarValidatedEvent on EventBus
//   4. If FAIL: publish BarRejectedEvent on EventBus
//
// Reconnect policy:
//   On pipe disconnection: wait 500ms, retry up to 10 times.
//   After 10 failures: publish SystemAlertEvent (CRITICAL).
// ============================================================

#ifndef SDTRADING_PIPE_LISTENER_H
#define SDTRADING_PIPE_LISTENER_H

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include "bar_validator.h"
#include "../events/event_bus.h"
#include "../events/event_types.h"
#include "../utils/logger.h"

// Windows named pipes
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace SDTrading {
namespace Data {

// ============================================================
// PIPE LISTENER CONFIG
// ============================================================
struct PipeListenerConfig {
    std::string pipe_base_path      = "\\\\.\\pipe\\SD_";  // Prefix
    std::string pipe_symbol_override = "";   // If set, overrides symbol in pipe name.
                                             // e.g. symbol="NIFTY", override="NIFTY-FUT"
                                             // → pipe: \\.\pipe\SD_NIFTY-FUT_5min
                                             // Internal symbol_ stays "NIFTY" for DB/signals.
    int         reconnect_attempts  = 10;
    int         reconnect_wait_ms   = 500;
    int         read_timeout_ms     = 30000;  // 30s: if no bar in 30s, warn
};

// ============================================================
// PIPE LISTENER
// One instance per symbol. Owns a single reader thread.
// ============================================================
class PipeListener {
public:
    PipeListener(const std::string&      symbol,
                 const std::string&      timeframe,
                 const ValidatorConfig&  validator_cfg,
                 const PipeListenerConfig& pipe_cfg,
                 Events::EventBus&       bus);

    ~PipeListener();

    // Non-copyable
    PipeListener(const PipeListener&) = delete;
    PipeListener& operator=(const PipeListener&) = delete;

    // Start the reader thread
    void start();

    // Stop the reader thread (graceful)
    void stop();

    bool is_running() const { return running_.load(); }

    // Statistics
    int get_bars_received()  const { return bars_received_; }
    int get_bars_validated() const { return validator_.get_bars_validated(); }
    int get_bars_rejected()  const { return validator_.get_bars_rejected(); }

    // Build full pipe path from symbol + timeframe
    static std::string build_pipe_path(const std::string& base,
                                       const std::string& symbol,
                                       const std::string& timeframe);

private:
    std::string          symbol_;
    std::string          timeframe_;
    std::string          pipe_path_;
    PipeListenerConfig   pipe_cfg_;
    BarValidator         validator_;
    Events::EventBus&    bus_;

    std::thread          reader_thread_;
    std::atomic<bool>    running_     { false };
    std::atomic<int>     bars_received_ { 0 };

    // Reader thread entry
    void reader_loop();

    // Open the named pipe (blocking connect with retry)
    // Returns platform handle or INVALID_HANDLE_VALUE
#ifdef _WIN32
    HANDLE open_pipe();
    std::string read_line(HANDLE pipe_handle);
    void close_pipe(HANDLE& handle);
#else
    int  open_pipe();
    std::string read_line(int fd);
    void close_pipe(int& fd);
#endif

    // Parse wire format → RawBar
    // Returns false if format is malformed (fields missing/unparseable)
    bool parse_wire(const std::string& line, RawBar& out) const;

    // Publish alert
    void publish_alert(Events::AlertSeverity severity,
                       const std::string& message) const;
};

} // namespace Data
} // namespace SDTrading

#endif // SDTRADING_PIPE_LISTENER_H