#ifndef FYERS_INTEGRATION_H
#define FYERS_INTEGRATION_H

// Windows-specific includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <queue>
#include "sd_engine_core.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <curl/curl.h>
#include <json/json.h>

// Forward declarations
struct lws;
struct lws_context;

// ============================================================
// FYERS DATA STRUCTURES
// ============================================================

struct FyersCandle {
    std::string datetime;
    double open;
    double high;
    double low;
    double close;
    double volume;
    double oi;  // Open Interest

    FyersCandle() : open(0), high(0), low(0), close(0), volume(0), oi(0) {}
};

struct FyersTickData {
    std::string symbol;
    double ltp;          // Last Traded Price
    double open_price;
    double high_price;
    double low_price;
    double close_price;
    double volume;
    double oi;           // Open Interest
    long long timestamp;

    FyersTickData() : ltp(0), open_price(0), high_price(0), low_price(0),
        close_price(0), volume(0), oi(0), timestamp(0) {
    }
};

// ============================================================
// FYERS REST API CLIENT
// ============================================================

class FyersRestClient {
public:
    FyersRestClient(const std::string& app_id,
        const std::string& access_token,
        const std::string& base_url = "https://api-t1.fyers.in/api/v3")
        : app_id_(app_id), access_token_(access_token), base_url_(base_url) {
    }

    // Validate the access token
    bool validate_token();

    // Get historical candle data (6 parameters)
    std::vector<FyersCandle> get_historical_data(
        const std::string& symbol,
        const std::string& resolution,
        const std::string& date_from = "",
        const std::string& date_to = "",
        int range_from = 0,
        int range_to = 0
    );

    // Get current quote/tick data
    FyersTickData get_quote(const std::string& symbol);

    // Get access token
    std::string get_access_token() const { return access_token_; }

private:
    std::string app_id_;
    std::string access_token_;
    std::string base_url_;

    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);
};

// ============================================================
// FYERS WEBSOCKET CLIENT
// ============================================================




class FyersWebSocketClient {
public:
    //FyersWebSocketClient(const std::string& access_token, const std::string& symbol, int timeframe_minutes = 5);

    FyersWebSocketClient(const std::string& access_token,
        const std::string& symbol,
        int candle_interval_minutes = 5)
        : access_token_(access_token),
        symbol_(symbol),
        running_(false),
        candle_start_time_(0),
        candle_interval_seconds_(candle_interval_minutes * 60),
        candle_initialized_(false),
        ws_context_(nullptr),
        wsi_(nullptr),
        connected_(false) {

#ifdef _WIN32
        // Initialize Winsock on Windows
        WSAStartup(MAKEWORD(2, 2), &wsa_data_);
#endif
    }


    ~FyersWebSocketClient() {
        stop();

#ifdef _WIN32
        WSACleanup();
#endif
    }
    // added for windows by shan ends here

    // Start/stop WebSocket connection
    bool start();
    void stop();

    // Set callback for new candles
    void set_candle_callback(std::function<void(const FyersCandle&)> callback) {
        on_candle_callback_ = callback;
    }


    // Get next candle (blocking with timeout)
    bool get_next_candle(FyersCandle& candle, int timeout_ms = 30000);

    // Check if data is available
    bool has_data() const;

    // Check if connected
    bool is_connected() const { return connected_; }

    // WebSocket event handlers (STATIC for C callback compatibility)
    static void on_connected();
    static void on_message(const std::string& message, void* user_data);
    static void on_error(const std::string& error, void* user_data);
    static void on_close(void* user_data);

private:
    std::string access_token_;
    std::string symbol_;
    int timeframe_minutes_;
    long long candle_interval_seconds_;  // Candle interval in seconds

    void* ws_context_;      // struct lws_context*
    void* ws_connection_;   // struct lws*
    bool running_;
    bool connected_;

    //lws_context* ws_context_; //added for windows by shan
    lws* wsi_;  //added for windows by shan

    std::queue<FyersCandle> candle_queue_; //added for windows by shan
    std::thread service_thread_;  // added for windows by shan
    bool candle_initialized_; //added for windows by shan

#ifdef _WIN32
    WSADATA wsa_data_;
#endif
    //added for windows by shan ends here


    // Candle building
    FyersCandle current_candle_;
    long long candle_start_time_;  // Start time of current candle (seconds)

    // Thread synchronization
    std::mutex queue_mutex_;
    std::condition_variable data_ready_;

    // Callback for completed candles
    std::function<void(const FyersCandle&)> on_candle_callback_;

    // Process incoming tick data
    void process_tick(const FyersTickData& tick);

    // Update current candle with tick
    void update_candle(const FyersTickData& tick);

    // Complete current candle and emit
    void complete_candle();

    // Service thread function
    void service_websocket();

    // WebSocket callbacks (static for C compatibility)
    /*static int websocket_callback(struct lws* wsi, int reason,
        void* user, void* in, size_t len);*/
};

// ============================================================
// DATA ADAPTER
// ============================================================

class FyersDataAdapter {
public:

    // Convert Fyers candle to Bar
    static Bar fyers_to_bar(const FyersCandle& candle);

    // Convert timestamp to datetime string
    static std::string timestamp_to_datetime(long long timestamp);

    // Format symbol for Fyers API
    static std::string format_symbol(const std::string& symbol);
};

// ============================================================
// REAL-TIME TRADING ENGINE
// ============================================================

class RealTimeTradingEngine {
public:
    RealTimeTradingEngine(const std::string& app_id,
        const std::string& access_token,
        const BacktestConfig& config)
        : config_(config),
        rest_client_(new FyersRestClient(app_id, access_token)),
        ws_client_(nullptr) {
    }

    ~RealTimeTradingEngine() {
        if (ws_client_) delete ws_client_;
        if (rest_client_) delete rest_client_;
    }

    // Initialize with historical data
    bool initialize(const std::string& symbol, int lookback_bars = 100);

    // Start real-time trading
    bool start_realtime(const std::string& symbol);

    // Stop trading
    void stop();

    // Process new bar
    void process_bar(const Bar& bar);

private:
    BacktestConfig config_;

    FyersRestClient* rest_client_;
    FyersWebSocketClient* ws_client_;

    std::vector<Bar> historical_bars_;
    std::vector<Zone> zones_;

    // Callback for new candles
    void on_new_candle(const FyersCandle& candle);
};

#endif // FYERS_INTEGRATION_H