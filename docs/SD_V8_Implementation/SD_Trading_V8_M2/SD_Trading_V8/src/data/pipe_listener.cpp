// ============================================================
// SD TRADING V8 - NAMED PIPE LISTENER IMPLEMENTATION
// src/data/pipe_listener.cpp
// Milestone M2.2
// ============================================================

#include "pipe_listener.h"
#include <sstream>
#include <stdexcept>
#include <chrono>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <cstring>
#endif

namespace SDTrading {
namespace Data {

// ============================================================
// Constructor / Destructor
// ============================================================

PipeListener::PipeListener(const std::string&       symbol,
                           const std::string&       timeframe,
                           const ValidatorConfig&   validator_cfg,
                           const PipeListenerConfig& pipe_cfg,
                           Events::EventBus&        bus)
    : symbol_(symbol)
    , timeframe_(timeframe)
    , pipe_cfg_(pipe_cfg)
    , validator_(symbol, validator_cfg)
    , bus_(bus)
{
    pipe_path_ = build_pipe_path(pipe_cfg_.pipe_base_path, symbol_, timeframe_);
    LOG_INFO("[PipeListener:" << symbol_ << "] Created. Pipe: " << pipe_path_);
}

PipeListener::~PipeListener() {
    stop();
}

// ============================================================
// Static helper
// ============================================================

std::string PipeListener::build_pipe_path(const std::string& base,
                                           const std::string& symbol,
                                           const std::string& timeframe) {
#ifdef _WIN32
    return base + symbol + "_" + timeframe;
    // e.g. \\.\pipe\SD_NIFTY_5min
#else
    // Linux FIFO: /tmp/SD_NIFTY_5min
    return "/tmp/SD_" + symbol + "_" + timeframe;
#endif
}

// ============================================================
// start / stop
// ============================================================

void PipeListener::start() {
    if (running_.exchange(true)) return;
    reader_thread_ = std::thread([this]() { reader_loop(); });
    LOG_INFO("[PipeListener:" << symbol_ << "] Reader thread started.");
}

void PipeListener::stop() {
    if (!running_.exchange(false)) return;
    // Reader thread will exit naturally when it detects running_=false
    // and the pipe read times out or is interrupted.
    if (reader_thread_.joinable()) reader_thread_.join();
    LOG_INFO("[PipeListener:" << symbol_ << "] Stopped. "
             << "Received=" << bars_received_.load()
             << " Validated=" << validator_.get_bars_validated()
             << " Rejected=" << validator_.get_bars_rejected());
}

// ============================================================
// reader_loop — runs on the reader thread
// ============================================================

void PipeListener::reader_loop() {
    LOG_INFO("[PipeListener:" << symbol_ << "] Reader loop started.");

    int  reconnect_count = 0;
    bool first_bar_logged = false;

    while (running_.load()) {
        // ── Open/connect to pipe ──────────────────────────
#ifdef _WIN32
        HANDLE hPipe = open_pipe();
        if (hPipe == INVALID_HANDLE_VALUE) {
#else
        int fd = open_pipe();
        if (fd < 0) {
#endif
            ++reconnect_count;
            if (reconnect_count > pipe_cfg_.reconnect_attempts) {
                publish_alert(Events::AlertSeverity::CRITICAL,
                    "Pipe '" + pipe_path_ + "' unavailable after "
                    + std::to_string(pipe_cfg_.reconnect_attempts)
                    + " attempts. AmiBroker may not be running.");
                // Back off longer before retrying
                std::this_thread::sleep_for(std::chrono::seconds(10));
                reconnect_count = 0;
            } else {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(pipe_cfg_.reconnect_wait_ms));
            }
            continue;
        }

        reconnect_count = 0;
        LOG_INFO("[PipeListener:" << symbol_ << "] Pipe connected: " << pipe_path_);

        // ── Read loop ─────────────────────────────────────
        while (running_.load()) {
#ifdef _WIN32
            std::string line = read_line(hPipe);
#else
            std::string line = read_line(fd);
#endif
            if (line.empty()) {
                // Disconnected or timeout
                break;
            }

            ++bars_received_;

            if (!first_bar_logged) {
                LOG_INFO("[PipeListener:" << symbol_ << "] First bar received: " << line);
                first_bar_logged = true;
            }

            // ── Parse wire format ─────────────────────────
            RawBar raw;
            if (!parse_wire(line, raw)) {
                LOG_WARN("[PipeListener:" << symbol_ << "] Malformed wire: " << line);
                // Publish as Tier-1 rejection
                Events::BarRejectedEvent evt;
                evt.symbol          = symbol_;
                evt.timeframe       = timeframe_;
                evt.raw_wire_data   = line;
                evt.rejection_tier  = 1;
                evt.rejection_reason = "Malformed wire format";
                bus_.publish(evt);
                continue;
            }

            // ── Validate ──────────────────────────────────
            auto result = validator_.validate(raw);

            if (result.passed) {
                auto validated_evt = validator_.to_validated_event(raw);
                bus_.publish(validated_evt);
                LOG_DEBUG("[PipeListener:" << symbol_ << "] BAR OK: "
                          << raw.timestamp << " C=" << raw.close
                          << " V=" << raw.volume << " OI=" << raw.open_interest);
            } else {
                auto rejected_evt = validator_.to_rejected_event(raw, result);
                bus_.publish(rejected_evt);
                LOG_WARN("[PipeListener:" << symbol_ << "] BAR REJECTED (Tier "
                         << result.tier << "): " << result.reason
                         << " | ts=" << raw.timestamp);

                if (validator_.is_alert_threshold_hit()) {
                    publish_alert(Events::AlertSeverity::WARN,
                        std::to_string(validator_.get_consecutive_rejects())
                        + " consecutive rejections on " + symbol_
                        + ". Check AmiBroker data feed quality.");
                }
            }
        }

        // ── Disconnected — close and retry ────────────────
#ifdef _WIN32
        close_pipe(hPipe);
#else
        close_pipe(fd);
#endif
        LOG_WARN("[PipeListener:" << symbol_ << "] Pipe disconnected. "
                 << "Attempting reconnect...");
        std::this_thread::sleep_for(
            std::chrono::milliseconds(pipe_cfg_.reconnect_wait_ms));
    }

    LOG_INFO("[PipeListener:" << symbol_ << "] Reader loop exited.");
}

// ============================================================
// parse_wire()
// Format: SOURCE|SYMBOL|TIMEFRAME|TIMESTAMP|O|H|L|C|V|OI
// ============================================================

bool PipeListener::parse_wire(const std::string& line, RawBar& out) const {
    // Trim trailing whitespace/newlines
    std::string trimmed = line;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r'
           || trimmed.back() == ' ')) {
        trimmed.pop_back();
    }

    if (trimmed.empty()) return false;

    std::istringstream ss(trimmed);
    std::string field;
    std::vector<std::string> fields;

    while (std::getline(ss, field, '|')) {
        fields.push_back(field);
    }

    // Expect exactly 10 fields
    if (fields.size() < 10) {
        LOG_DEBUG("[PipeListener:" << symbol_ << "] Wire has "
                  << fields.size() << " fields (need 10): " << trimmed);
        return false;
    }

    try {
        out.source         = fields[0];   // AMIBROKER
        out.symbol         = fields[1];   // NIFTY
        out.timeframe      = fields[2];   // 5min
        out.timestamp      = fields[3];   // ISO8601
        out.open           = std::stod(fields[4]);
        out.high           = std::stod(fields[5]);
        out.low            = std::stod(fields[6]);
        out.close          = std::stod(fields[7]);
        out.volume         = std::stod(fields[8]);
        out.open_interest  = std::stod(fields[9]);
    } catch (const std::exception& e) {
        LOG_WARN("[PipeListener:" << symbol_ << "] Parse error: " << e.what()
                 << " | line: " << trimmed);
        return false;
    }

    return out.is_structurally_complete();
}

// ============================================================
// Publish alert
// ============================================================

void PipeListener::publish_alert(Events::AlertSeverity severity,
                                  const std::string& message) const {
    Events::SystemAlertEvent alert;
    alert.severity = severity;
    alert.source   = "PipeListener:" + symbol_;
    alert.symbol   = symbol_;
    alert.message  = message;
    bus_.publish(alert);
}

// ============================================================
// Platform implementations — Windows
// ============================================================

#ifdef _WIN32

HANDLE PipeListener::open_pipe() {
    // Try to connect to the pipe (server mode — wait for AFL to create)
    // AFL creates the pipe as server; C++ connects as client
    HANDLE hPipe = CreateFileA(
        pipe_path_.c_str(),
        GENERIC_READ,                   // Read-only (AFL writes, C++ reads)
        0,                              // No sharing
        nullptr,                        // Default security
        OPEN_EXISTING,                  // Must already exist (AFL creates it)
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            // AFL not running or hasn't created pipe yet — silent, expected
        } else if (err == ERROR_PIPE_BUSY) {
            // Pipe exists but busy — wait and retry
            WaitNamedPipeA(pipe_path_.c_str(), pipe_cfg_.reconnect_wait_ms);
        } else {
            LOG_DEBUG("[PipeListener:" << symbol_ << "] CreateFile error: " << err);
        }
        return INVALID_HANDLE_VALUE;
    }

    // Set read timeout
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadTotalTimeoutConstant = pipe_cfg_.read_timeout_ms;
    SetCommTimeouts(hPipe, &timeouts);

    return hPipe;
}

std::string PipeListener::read_line(HANDLE hPipe) {
    std::string line;
    char ch;
    DWORD bytesRead;

    while (running_.load()) {
        BOOL ok = ReadFile(hPipe, &ch, 1, &bytesRead, nullptr);
        if (!ok || bytesRead == 0) {
            // Disconnected or timeout
            return "";
        }
        if (ch == '\n') break;
        if (ch != '\r') line += ch;
    }

    return line;
}

void PipeListener::close_pipe(HANDLE& handle) {
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

// ============================================================
// Platform implementations — Linux (FIFO)
// ============================================================

#else

int PipeListener::open_pipe() {
    // Create FIFO if it doesn't exist
    mkfifo(pipe_path_.c_str(), 0666);

    // Open in non-blocking mode first to avoid hanging if AFL not running
    int fd = ::open(pipe_path_.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }

    // Switch to blocking mode for reads
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    return fd;
}

std::string PipeListener::read_line(int fd) {
    std::string line;
    char ch;

    while (running_.load()) {
        ssize_t n = ::read(fd, &ch, 1);
        if (n <= 0) return "";
        if (ch == '\n') break;
        if (ch != '\r') line += ch;
    }

    return line;
}

void PipeListener::close_pipe(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

#endif  // _WIN32

} // namespace Data
} // namespace SDTrading
