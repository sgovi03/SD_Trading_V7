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
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef CREATE_NEW
#undef CREATE_NEW
#endif
#ifdef CRITICAL
#undef CRITICAL
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef WARN
#undef WARN
#endif
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
    // Use pipe_symbol_override for the pipe name if set,
    // otherwise use symbol_. Internal symbol_ always stays as canonical name.
    const std::string& pipe_sym = pipe_cfg_.pipe_symbol_override.empty()
                                  ? symbol_
                                  : pipe_cfg_.pipe_symbol_override;
    pipe_path_ = build_pipe_path(pipe_cfg_.pipe_base_path, pipe_sym, timeframe_);
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
    LOG_INFO("[PipeListener:" << symbol_ << "] Creating pipe server: " << pipe_path_);
    LOG_INFO("[PipeListener:" << symbol_ << "] Load AFL DataBridge in AmiBroker to connect.");

    bool first_bar_logged = false;

#ifdef _WIN32
    // Open the pipe SERVER once — handle is reused across all bars.
    // read_line() handles the DisconnectNamedPipe/ConnectNamedPipe cycle
    // after each bar (AFL does fopen/fclose per bar).
    HANDLE hPipe = open_pipe();
    if (hPipe == INVALID_HANDLE_VALUE) {
        LOG_ERROR("[PipeListener:" << symbol_ << "] Failed to create pipe server."
                  << " Cannot receive data from AmiBroker.");
        publish_alert(Events::AlertSeverity::CRITICAL,
            "Failed to create named pipe: " + pipe_path_);
        return;
    }

    // ── Outer loop: one iteration per AFL connection ─────────
    // AFL opens pipe, writes ALL bars for a scan run, then closes.
    // When read_line() returns "" (pipe closed), we disconnect
    // and wait for the next AFL connection (next AR run).
    while (running_.load()) {

        // ── Inner loop: read all lines from current connection ──
        bool got_any_line = false;
        while (running_.load()) {
            std::string line = read_line(hPipe);

            if (!running_.load()) break;

            if (line.empty()) {
                // AFL closed connection (fclose after all bars sent)
                // Break inner loop → outer loop reconnects
                if (got_any_line) {
                    LOG_INFO("[PipeListener:" << symbol_ << "] AFL disconnected."
                             << " Bars received this session: " << bars_received_.load()
                             << ". Waiting for next connection...");
                }
                break;
            }

            got_any_line = true;
            ++bars_received_;

            if (!first_bar_logged) {
                LOG_INFO("[PipeListener:" << symbol_ << "] First bar received: " << line);
                first_bar_logged = true;
            }

            // ── Parse wire format ─────────────────────────────
            RawBar raw;
            if (!parse_wire(line, raw)) {
                LOG_WARN("[PipeListener:" << symbol_ << "] Malformed wire: " << line);
                Events::BarRejectedEvent evt;
                evt.symbol           = symbol_;
                evt.timeframe        = timeframe_;
                evt.raw_wire_data    = line;
                evt.rejection_tier   = 1;
                evt.rejection_reason = "Malformed wire format";
                bus_.publish(evt);
                continue;
            }

            // Override wire symbol with canonical symbol_ from registry.
            // Wire carries "NIFTY-FUT" but DB uses canonical "NIFTY".
            raw.symbol = symbol_;

            // ── Validate ─────────────────────────────────────
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
                         << result.tier << "): " << result.reason);

                if (validator_.is_alert_threshold_hit()) {
                    publish_alert(Events::AlertSeverity::WARN,
                        std::to_string(validator_.get_consecutive_rejects())
                        + " consecutive rejections on " + symbol_
                        + ". Check AmiBroker data feed quality.");
                }
            }
        }

        if (!running_.load()) break;

        // ── Reconnect: wait for next AFL scan run ─────────────
        DisconnectNamedPipe(hPipe);

        OVERLAPPED ov = {};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) break;

        LOG_DEBUG("[PipeListener:" << symbol_ << "] Waiting for next AFL connection...");
        BOOL reconnected = ConnectNamedPipe(hPipe, &ov);
        if (!reconnected && GetLastError() == ERROR_IO_PENDING) {
            while (running_.load()) {
                DWORD w = WaitForSingleObject(ov.hEvent, 500);
                if (w == WAIT_OBJECT_0) { reconnected = TRUE; break; }
            }
        }
        CloseHandle(ov.hEvent);

        if (!running_.load()) break;
        if (reconnected || GetLastError() == ERROR_PIPE_CONNECTED) {
            LOG_INFO("[PipeListener:" << symbol_ << "] AmiBroker reconnected.");
            // Switch back to blocking mode
            DWORD mode = PIPE_READMODE_BYTE | PIPE_WAIT;
            SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);
        }
    }

    close_pipe(hPipe);

#else
    // Linux FIFO path — unchanged
    while (running_.load()) {
        int fd = open_pipe();
        if (fd < 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(pipe_cfg_.reconnect_wait_ms));
            continue;
        }

        while (running_.load()) {
            std::string line = read_line(fd);
            if (line.empty()) break;

            ++bars_received_;
            RawBar raw;
            if (!parse_wire(line, raw)) continue;

            auto result = validator_.validate(raw);
            if (result.passed) {
                bus_.publish(validator_.to_validated_event(raw));
            } else {
                bus_.publish(validator_.to_rejected_event(raw, result));
            }
        }
        close_pipe(fd);
    }
#endif

    LOG_INFO("[PipeListener:" << symbol_ << "] Reader loop exited."
             << " Bars received=" << bars_received_.load()
             << " Validated=" << validator_.get_bars_validated()
             << " Rejected=" << validator_.get_bars_rejected());
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
    // C++ is the PIPE SERVER. AFL (fopen "w") is the CLIENT.
    // Uses FILE_FLAG_OVERLAPPED so ConnectNamedPipe is non-blocking
    // and can be cancelled cleanly when stop() sets running_=false.

    HANDLE hPipe = CreateNamedPipeA(
        pipe_path_.c_str(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,  // OVERLAPPED for cancellable wait
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,       // Max instances
        4096,    // Out buffer
        4096,    // In buffer
        0,       // Default timeout
        nullptr  // Default security
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        LOG_ERROR("[PipeListener:" << symbol_ << "] CreateNamedPipe failed."
                  << " Error=" << err << " Path=" << pipe_path_);
        return INVALID_HANDLE_VALUE;
    }

    LOG_INFO("[PipeListener:" << symbol_ << "] Pipe server created: "
             << pipe_path_ << " — waiting for AmiBroker to connect...");

    // Use OVERLAPPED ConnectNamedPipe so we can cancel on shutdown
    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        CloseHandle(hPipe);
        return INVALID_HANDLE_VALUE;
    }

    BOOL connected = ConnectNamedPipe(hPipe, &ov);
    DWORD err = GetLastError();

    if (!connected) {
        if (err == ERROR_IO_PENDING) {
            // Wait in 500ms chunks so stop() is responsive
            while (running_.load()) {
                DWORD wait = WaitForSingleObject(ov.hEvent, 500);
                if (wait == WAIT_OBJECT_0) {
                    // Client connected
                    DWORD bytes = 0;
                    GetOverlappedResult(hPipe, &ov, &bytes, FALSE);
                    break;
                }
                // WAIT_TIMEOUT → check running_ again
            }
            if (!running_.load()) {
                // Shutdown during wait — cancel and clean up
                CancelIo(hPipe);
                CloseHandle(ov.hEvent);
                CloseHandle(hPipe);
                return INVALID_HANDLE_VALUE;
            }
        } else if (err != ERROR_PIPE_CONNECTED) {
            LOG_WARN("[PipeListener:" << symbol_ << "] ConnectNamedPipe failed."
                     << " Error=" << err);
            CloseHandle(ov.hEvent);
            CloseHandle(hPipe);
            return INVALID_HANDLE_VALUE;
        }
    }

    CloseHandle(ov.hEvent);
    LOG_INFO("[PipeListener:" << symbol_ << "] AmiBroker connected.");

    // Switch back to blocking mode for ReadFile calls in read_line()
    DWORD mode = PIPE_READMODE_BYTE | PIPE_WAIT;
    SetNamedPipeHandleState(hPipe, &mode, nullptr, nullptr);

    return hPipe;
}

std::string PipeListener::read_line(HANDLE hPipe) {
    // AFL now opens pipe once, writes ALL bars, then closes (fclose).
    // read_line() reads one line at a time from the open connection.
    // When AFL closes (ERROR_BROKEN_PIPE), returns "" to signal
    // reader_loop() to DisconnectNamedPipe and wait for next connection.
    std::string line;
    char ch;
    DWORD bytesRead;

    while (running_.load()) {
        OVERLAPPED ov = {};
        ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return "";

        BOOL ok = ReadFile(hPipe, &ch, 1, &bytesRead, &ov);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                while (running_.load()) {
                    DWORD wait = WaitForSingleObject(ov.hEvent, 500);
                    if (wait == WAIT_OBJECT_0) {
                        if (!GetOverlappedResult(hPipe, &ov, &bytesRead, FALSE)) {
                            err = GetLastError();
                            ok = FALSE;
                        } else {
                            ok = TRUE;
                        }
                        break;
                    }
                }
                if (!running_.load()) {
                    CancelIo(hPipe);
                    CloseHandle(ov.hEvent);
                    return "";
                }
            }

            if (!ok) {
                CloseHandle(ov.hEvent);
                // Pipe closed by AFL (fclose) or disconnected.
                // Return "" — reader_loop will handle reconnect.
                return "";
            }
        }

        CloseHandle(ov.hEvent);
        if (bytesRead == 0) break;
        if (ch == '\n') break;
        if (ch != '\r') line += ch;
    }

    return line;
}

void PipeListener::close_pipe(HANDLE& handle) {
    if (handle != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(handle);
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