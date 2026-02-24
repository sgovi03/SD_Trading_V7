// ============================================================
// FYERS API INTEGRATION IMPLEMENTATION
// ============================================================

#include "fyers_integration.h"
#include "sd_engine_core.h"  // For Bar, Zone, BacktestConfig definitions
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <libwebsockets.h>

using namespace std;

// ============================================================
// FYERS REST CLIENT IMPLEMENTATION
// ============================================================

size_t FyersRestClient::write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

bool FyersRestClient::validate_token() {
    cout << "🔑 Validating Fyers access token..." << endl;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "❌ Failed to initialize CURL" << endl;
        return false;
    }

    string url = base_url_ + "/profile";  // Changed from /validate-authcode
    cout << "📡 Validation URL: " << url << endl;
    string response;

    struct curl_slist* headers = NULL;
    string auth_header = "Authorization: " + app_id_ + ":" + access_token_;
    headers = curl_slist_append(headers, auth_header.c_str());
    
    // Debug: Show token format (first 20 chars)
    if (access_token_.length() > 20) {
        string token_preview = access_token_.substr(0, 20);
        cout << "🔐 Token preview: " << token_preview << "..." << endl;
    } else {
        cout << "🔐 Token length: " << access_token_.length() << " chars" << endl;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // SSL and timeout options for Windows
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    
    // CRITICAL: SSL and timeout options for Windows
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    cout << "⏳ Making API request..." << endl;

    CURLcode res = curl_easy_perform(curl);
    
    cout << "✅ API request completed" << endl;
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        cerr << "❌ CURL Error: " << curl_easy_strerror(res) << endl;
        cerr << "Error code: " << res << endl;
        return false;
    }
    
    cout << "📊 HTTP Response Code: " << http_code << endl;
    cout << "📄 Response length: " << response.length() << " bytes" << endl;
    
    if (response.length() < 2000) {
        cout << "📄 Response: " << response << endl;
    }

    // Parse JSON response
    Json::Value root;
    Json::CharReaderBuilder reader;
    string errors;

    istringstream sstream(response);
    if (!Json::parseFromStream(reader, sstream, &root, &errors)) {
        cerr << "❌ JSON Parse Error: " << errors << endl;
        cerr << "Raw response: " << response.substr(0, 500) << endl;
        return false;
    }

    // Check response status
    string status = root["s"].asString();
    cout << "✔️ API Status: " << status << endl;
    
    if (status == "ok") {
        // Print user info if available
        if (root.isMember("data")) {
            Json::Value data = root["data"];
            if (data.isMember("name")) {
                cout << "👤 User: " << data["name"].asString() << endl;
            }
            if (data.isMember("email_id")) {
                cout << "📧 Email: " << data["email_id"].asString() << endl;
            }
        }
        cout << "✅ Token validation successful!" << endl;
        return true;
    } else {
        cerr << "❌ Token validation failed!" << endl;
        if (root.isMember("message")) {
            cerr << "Message: " << root["message"].asString() << endl;
        }
        if (root.isMember("code")) {
            cerr << "Code: " << root["code"].asInt() << endl;
        }
        return false;
    }
}


vector<FyersCandle> FyersRestClient::get_historical_data(
    const string& symbol,
    const string& resolution,
    const string& date_from,
    const string& date_to,
    int range_from,
    int range_to
) {
    vector<FyersCandle> candles;

    CURL* curl = curl_easy_init();
    if (!curl) return candles;

    // Build URL with parameters
    stringstream url_stream;
    url_stream << base_url_ << "/data/history"
        << "?symbol=" << symbol
        << "&resolution=" << resolution;

    if (range_from > 0 && range_to > 0) {
        url_stream << "&range_from=" << range_from
            << "&range_to=" << range_to;
    }
    else {
        url_stream << "&date_format=1"
            << "&range_from=" << date_from
            << "&range_to=" << date_to;
    }

    string url = url_stream.str();
    string response;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("Authorization: " + app_id_ + ":" + access_token_).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        cerr << "❌ Historical Data CURL Error: " << curl_easy_strerror(res) << endl;
        return candles;
    }

    // Parse JSON response
    Json::Value root;
    Json::CharReaderBuilder reader;
    string errors;

    istringstream sstream(response);
    if (!Json::parseFromStream(reader, sstream, &root, &errors)) {
        cerr << "❌ Historical Data JSON Parse Error: " << errors << endl;
        return candles;
    }

    if (root["s"].asString() != "ok") {
        cerr << "❌ Fyers API Error: " << root["message"].asString() << endl;
        return candles;
    }

    // Parse candles array
    // Fyers format: [[timestamp, open, high, low, close, volume], ...]
    Json::Value candles_array = root["candles"];

    for (const auto& candle_data : candles_array) {
        FyersCandle candle;

        long long timestamp = candle_data[0].asInt64();
        candle.datetime = FyersDataAdapter::timestamp_to_datetime(timestamp);
        candle.open = candle_data[1].asDouble();
        candle.high = candle_data[2].asDouble();
        candle.low = candle_data[3].asDouble();
        candle.close = candle_data[4].asDouble();
        candle.volume = candle_data[5].asDouble();
        candle.oi = 0;  // Not available in historical data

        candles.push_back(candle);
    }

    cout << "✅ Loaded " << candles.size() << " historical candles from Fyers" << endl;

    return candles;
}

FyersTickData FyersRestClient::get_quote(const string& symbol) {
    FyersTickData tick;

    CURL* curl = curl_easy_init();
    if (!curl) return tick;

    string url = base_url_ + "/data/quotes/?symbols=" + symbol;
    string response;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token_).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return tick;

    // Parse JSON
    Json::Value root;
    Json::CharReaderBuilder reader;
    string errors;

    istringstream sstream(response);
    if (Json::parseFromStream(reader, sstream, &root, &errors)) {
        if (root["s"].asString() == "ok" && root["d"].isArray() && root["d"].size() > 0) {
            Json::Value quote = root["d"][0]["v"];

            tick.symbol = symbol;
            tick.ltp = quote["lp"].asDouble();
            tick.open_price = quote["open_price"].asDouble();
            tick.high_price = quote["high_price"].asDouble();
            tick.low_price = quote["low_price"].asDouble();
            tick.close_price = quote["prev_close_price"].asDouble();
            tick.volume = quote["volume"].asDouble();
            tick.oi = quote["oi"].asDouble();
            tick.timestamp = quote["tt"].asInt64();
        }
    }

    return tick;
}

// ============================================================
// FYERS WEBSOCKET CLIENT IMPLEMENTATION
// ============================================================

struct WebSocketContext {
    FyersWebSocketClient* client;
    lws* wsi;
    bool connected;
    string pending_message;
};

static int websocket_callback(struct lws* wsi, enum lws_callback_reasons reason,
    void* user, void* in, size_t len) {
    WebSocketContext* ctx = (WebSocketContext*)user;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        cout << "✅ WebSocket connected to Fyers" << endl;
        ctx->connected = true;
        // Subscribe to symbol
        lws_callback_on_writable(wsi);
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        string message((char*)in, len);
        FyersWebSocketClient::on_message(message, ctx->client);
        break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        if (!ctx->pending_message.empty()) {
            size_t msg_len = ctx->pending_message.length();
            vector<unsigned char> buf_vec(LWS_PRE + msg_len);
            unsigned char* buf = buf_vec.data();
            memcpy(&buf[LWS_PRE], ctx->pending_message.c_str(), msg_len);
            lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
            ctx->pending_message.clear();
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        cerr << "❌ WebSocket connection error" << endl;
        FyersWebSocketClient::on_error("Connection error", ctx->client);
        break;

    case LWS_CALLBACK_CLOSED:
        cout << "⚠️ WebSocket connection closed" << endl;
        FyersWebSocketClient::on_close(ctx->client);
        break;

    default:
        break;
    }

    return 0;
}





// ============================================================
// FYERS WEBSOCKET CLIENT - CONSTRUCTOR AND DESTRUCTOR
// ============================================================

//FyersWebSocketClient::FyersWebSocketClient(const std::string& access_token, const std::string& symbol, int timeframe_minutes)
//    : access_token_(access_token),
//    symbol_(symbol),
//    timeframe_minutes_(timeframe_minutes),
//    candle_interval_seconds_(timeframe_minutes * 60),
//    ws_context_(nullptr),
//    ws_connection_(nullptr),
//    running_(false),
//    connected_(false),
//    candle_start_time_(0) {
//
//    std::cout << "🔧 FyersWebSocketClient initialized for " << symbol
//        << " (" << timeframe_minutes << " min candles)" << std::endl;
//}

//FyersWebSocketClient::~FyersWebSocketClient() {
//    stop();
//    std::cout << "🔧 FyersWebSocketClient destroyed" << std::endl;
//}

//void FyersWebSocketClient::set_candle_callback(std::function<void(const FyersCandle&)> callback) {
//    on_candle_callback_ = callback;
//}



bool FyersWebSocketClient::start() {
    // Initialize WebSocket connection
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    /*struct lws_protocols* protocols = new struct lws_protocols[2];
    info.protocols = protocols;

    protocols[0] = {
        "fyers-data-protocol",
        websocket_callback,
        sizeof(WebSocketContext),
        4096,
        0, NULL, 0
    };
    protocols[1] = { NULL, NULL, 0, 0, 0, NULL, 0 };*/

    static struct lws_protocols protocols[] = {
      {
          "fyers-data-protocol",        // name
          websocket_callback,           // callback
          sizeof(WebSocketContext),     // per_session_data_size
          4096,                         // rx_buffer_size
          0,                            // id
          nullptr                       // user
      },
      {
          nullptr, nullptr, 0, 0, 0, nullptr  // terminator (REQUIRED)
      }
    };


    struct lws_context* context = lws_create_context(&info);
    if (!context) {
        cerr << "❌ Failed to create WebSocket context" << endl;
        return false;
    }

    // Connect to Fyers WebSocket
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));

    ccinfo.context = context;
    ccinfo.address = "api-t1.fyers.in";
    ccinfo.port = 443;
    ccinfo.path = "/socket/v2/dataSock";
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = info.protocols[0].name;
    ccinfo.ssl_connection = LCCSCF_USE_SSL;

    WebSocketContext* ctx = new WebSocketContext();
    ctx->client = this;
    ctx->connected = false;

    // Add access token to connection
    ccinfo.userdata = ctx;

    struct lws* wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi) {
        cerr << "❌ Failed to connect to Fyers WebSocket" << endl;
        lws_context_destroy(context);
        delete ctx;
        return false;
    }

    ctx->wsi = wsi;
    ws_connection_ = context;
    running_ = true;

    // Subscribe to symbol
    Json::Value subscribe_msg;
    subscribe_msg["T"] = "SUB_L2";
    subscribe_msg["SLIST"] = Json::arrayValue;
    subscribe_msg["SLIST"].append(symbol_);
    subscribe_msg["SUB_T"] = 1;  // 1 for depth data

    Json::StreamWriterBuilder writer;
    ctx->pending_message = Json::writeString(writer, subscribe_msg);

    // Service WebSocket in separate thread
    thread([context]() {
        while (lws_service(context, 50) >= 0) {
            // Keep servicing
        }
        }).detach();

    return true;
}

void FyersWebSocketClient::stop() {
    running_ = false;
    if (ws_connection_) {
        lws_context_destroy((struct lws_context*)ws_connection_);
        ws_connection_ = nullptr;
    }
}

void FyersWebSocketClient::on_message(const string& message, void* user_data) {
    FyersWebSocketClient* client = (FyersWebSocketClient*)user_data;

    // Parse Fyers tick data
    Json::Value root;
    Json::CharReaderBuilder reader;
    string errors;

    istringstream sstream(message);
    if (!Json::parseFromStream(reader, sstream, &root, &errors)) {
        return;  // Skip malformed messages
    }

    // Fyers WebSocket message format
    if (root["T"].asString() == "DF") {  // Depth Full
        FyersTickData tick;

        tick.symbol = root["symbol"].asString();
        tick.timestamp = root["tt"].asInt64();
        tick.ltp = root["ltp"].asDouble();
        tick.open_price = root["open_price"].asDouble();
        tick.high_price = root["high_price"].asDouble();
        tick.low_price = root["low_price"].asDouble();
        tick.close_price = root["prev_close_price"].asDouble();
        tick.volume = root["volume"].asDouble();
        tick.oi = root["oi"].asDouble();

        client->process_tick(tick);
    }
}

void FyersWebSocketClient::on_error(const string& error, void* user_data) {
    cerr << "❌ WebSocket Error: " << error << endl;
}

void FyersWebSocketClient::on_close(void* user_data) {
    FyersWebSocketClient* client = (FyersWebSocketClient*)user_data;
    client->running_ = false;
}

void FyersWebSocketClient::process_tick(const FyersTickData& tick) {
    lock_guard<mutex> lock(queue_mutex_);

    // Update current candle
    update_candle(tick);
}

void FyersWebSocketClient::update_candle(const FyersTickData& tick) {
    long long tick_time_seconds = tick.timestamp / 1000;

    // Initialize first candle
    if (candle_start_time_ == 0) {
        candle_start_time_ = (tick_time_seconds / candle_interval_seconds_) * candle_interval_seconds_;
        current_candle_.open = tick.ltp;
        current_candle_.high = tick.ltp;
        current_candle_.low = tick.ltp;
        current_candle_.close = tick.ltp;
        current_candle_.volume = tick.volume;
        current_candle_.oi = tick.oi;
        return;
    }

    // Check if candle should be completed
    long long expected_end = candle_start_time_ + candle_interval_seconds_;
    if (tick_time_seconds >= expected_end) {
        // Complete current candle
        complete_candle();

        // Start new candle
        candle_start_time_ = (tick_time_seconds / candle_interval_seconds_) * candle_interval_seconds_;
        current_candle_.open = tick.ltp;
        current_candle_.high = tick.ltp;
        current_candle_.low = tick.ltp;
        current_candle_.close = tick.ltp;
        current_candle_.volume = tick.volume;
        current_candle_.oi = tick.oi;
    }
    else {
        // Update current candle
        current_candle_.high = max(current_candle_.high, tick.high_price);
        current_candle_.low = min(current_candle_.low, tick.low_price);
        current_candle_.close = tick.ltp;
        current_candle_.volume = tick.volume;
        current_candle_.oi = tick.oi;
    }
}

void FyersWebSocketClient::complete_candle() {
    // Set datetime
    current_candle_.datetime = FyersDataAdapter::timestamp_to_datetime(candle_start_time_ * 1000);

    // Call callback if set
    if (on_candle_callback_) {
        on_candle_callback_(current_candle_);
    }

    // Notify waiting threads
    data_ready_.notify_all();
}

bool FyersWebSocketClient::get_next_candle(FyersCandle& candle, int timeout_ms) {
    unique_lock<mutex> lock(queue_mutex_);

    auto now = chrono::steady_clock::now();
    auto timeout_time = now + chrono::milliseconds(timeout_ms);

    if (data_ready_.wait_until(lock, timeout_time) == cv_status::timeout) {
        return false;
    }

    candle = current_candle_;
    return true;
}

bool FyersWebSocketClient::has_data() const {
    return candle_start_time_ > 0;
}

// ============================================================
// DATA ADAPTER IMPLEMENTATION
// ============================================================

Bar FyersDataAdapter::fyers_to_bar(const FyersCandle& fyers_candle) {
    Bar bar;
    bar.datetime = fyers_candle.datetime;
    bar.open = fyers_candle.open;
    bar.high = fyers_candle.high;
    bar.low = fyers_candle.low;
    bar.close = fyers_candle.close;
    bar.volume = fyers_candle.volume;
    bar.oi = fyers_candle.oi;
    return bar;
}

string FyersDataAdapter::timestamp_to_datetime(long long timestamp) {
    time_t time = timestamp / 1000;
    struct tm* timeinfo = localtime(&time);

    stringstream ss;
    ss << put_time(timeinfo, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

string FyersDataAdapter::format_symbol(const string& symbol) {
    // Convert user format to Fyers format
    // Example: NIFTY-FUT -> NSE:NIFTY24DECFUT
    // This needs to be customized based on your symbol conventions
    return symbol;
}

// ============================================================
// REAL-TIME TRADING ENGINE IMPLEMENTATION
// ============================================================

bool RealTimeTradingEngine::initialize(const string& symbol, int lookback_bars) {
    cout << "🔄 Initializing Real-Time Engine..." << endl;

    // Validate token
    if (!rest_client_->validate_token()) {
        cerr << "❌ Invalid Fyers access token!" << endl;
        return false;
    }

    cout << "✅ Fyers access token validated" << endl;

    // Get historical data for context
    auto now = chrono::system_clock::now();
    auto days_ago = now - chrono::hours(lookback_bars / 75 * 24);  // Approx days needed

    time_t now_t = chrono::system_clock::to_time_t(now);
    time_t days_ago_t = chrono::system_clock::to_time_t(days_ago);

    stringstream date_from, date_to;
    date_from << put_time(localtime(&days_ago_t), "%Y-%m-%d");
    date_to << put_time(localtime(&now_t), "%Y-%m-%d");

    vector<FyersCandle> historical = rest_client_->get_historical_data(
        symbol, "5", date_from.str(), date_to.str()
    );

    if (historical.empty()) {
        cerr << "❌ Failed to load historical data!" << endl;
        return false;
    }

    // Convert to Bar format
    for (const auto& candle : historical) {
        historical_bars_.push_back(FyersDataAdapter::fyers_to_bar(candle));
    }

    cout << "✅ Loaded " << historical_bars_.size() << " historical bars for context" << endl;

    return true;
}

bool RealTimeTradingEngine::start_realtime(const string& symbol) {
    cout << "🚀 Starting Real-Time Data Stream..." << endl;

    ws_client_ = new FyersWebSocketClient(
        rest_client_->get_access_token(),
        symbol,
        5  // 5-minute candles
    );

    // Set callback for new candles
    ws_client_->set_candle_callback([this](const FyersCandle& candle) {
        this->on_new_candle(candle);
        });

    if (!ws_client_->start()) {
        cerr << "❌ Failed to start WebSocket client!" << endl;
        return false;
    }

    cout << "✅ Real-Time data stream active" << endl;
    return true;
}

void RealTimeTradingEngine::on_new_candle(const FyersCandle& candle) {
    Bar bar = FyersDataAdapter::fyers_to_bar(candle);

    cout << "📊 New Candle: " << bar.datetime
        << " | O:" << bar.open << " H:" << bar.high
        << " L:" << bar.low << " C:" << bar.close << endl;

    process_bar(bar);
}

void RealTimeTradingEngine::process_bar(const Bar& bar) {
    // Add to historical context
    historical_bars_.push_back(bar);

    // Keep only last N bars for lookback
    if (historical_bars_.size() > config_.lookback_for_zones + 100) {
        historical_bars_.erase(historical_bars_.begin());
    }

    // HERE: Call your V3.4 zone detection and trading logic
    // This would integrate with your existing functions:
    // - detect_zones()
    // - calculate_zone_score()
    // - make_entry_decision()
    // - manage_trailing_stop()
    // etc.

    // Example structure:
    // 1. Update existing zones
    // 2. Detect new zones
    // 3. If in trade: check exit conditions
    // 4. If not in trade: check entry signals

    cout << "🔍 Processing bar for zones and signals..." << endl;
}

void RealTimeTradingEngine::stop() {
    if (ws_client_) {
        ws_client_->stop();
    }
    cout << "⏹️ Real-Time Engine stopped" << endl;
}