// ============================================================
// HTTP CLIENT - IMPLEMENTATION
// ============================================================

#include "http_client.h"
#include "../logger.h"
#include <sstream>
#include <cstring>

namespace SDTrading {
namespace Utils {
namespace Http {

// Static callback for libcurl to capture response data
size_t HttpClient::write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* response_body = static_cast<std::string*>(userp);
    response_body->append(static_cast<char*>(contents), total_size);
    return total_size;
}

std::string HttpClient::url_encode(const std::string& value) {
    if (!curl_handle_) return value;
    
    char* encoded = curl_easy_escape(curl_handle_, value.c_str(), static_cast<int>(value.length()));
    if (!encoded) return value;
    
    std::string result(encoded);
    curl_free(encoded);
    return result;
}

std::string HttpClient::build_form_data(const std::map<std::string, std::string>& params) {
    std::ostringstream oss;
    bool first = true;
    
    for (const auto& [key, value] : params) {
        if (!first) oss << "&";
        oss << url_encode(key) << "=" << url_encode(value);
        first = false;
    }
    
    return oss.str();
}

HttpClient::HttpClient(long timeout_seconds, bool verbose)
    : curl_handle_(nullptr), timeout_seconds_(timeout_seconds), verbose_(verbose) {
    
    // Initialize libcurl globally (once per process)
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized = true;
    }
    
    // Create reusable curl handle
    curl_handle_ = curl_easy_init();
    if (!curl_handle_) {
        LOG_ERROR("Failed to initialize libcurl handle");
    } else {
        LOG_INFO("HttpClient initialized (timeout: " << timeout_seconds_ << "s)");
    }
}

HttpClient::~HttpClient() {
    if (curl_handle_) {
        curl_easy_cleanup(curl_handle_);
        curl_handle_ = nullptr;
    }
}

HttpResponse HttpClient::post(const std::string& url, 
                              const std::map<std::string, std::string>& form_params) {
    HttpResponse response;
    
    if (!curl_handle_) {
        response.error_message = "CURL handle not initialized";
        LOG_ERROR(response.error_message);
        return response;
    }
    
    // Build URL-encoded form data
    std::string post_data = build_form_data(form_params);
    
    LOG_INFO("POST " << url);
    LOG_DEBUG("Form data: " << post_data);
    
    // Reset curl handle for new request
    curl_easy_reset(curl_handle_);
    
    // Configure request
    curl_easy_setopt(curl_handle_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_handle_, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl_handle_, CURLOPT_TIMEOUT, timeout_seconds_);
    curl_easy_setopt(curl_handle_, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle_, CURLOPT_WRITEDATA, &response.body);
    
    // Optional: verbose output
    if (verbose_) {
        curl_easy_setopt(curl_handle_, CURLOPT_VERBOSE, 1L);
    }
    
    // Set content type header
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl_handle_, CURLOPT_HTTPHEADER, headers);
    
    // Execute request
    CURLcode res = curl_easy_perform(curl_handle_);
    
    // Cleanup headers
    curl_slist_free_all(headers);
    
    // Check for errors
    if (res != CURLE_OK) {
        response.success = false;
        response.error_message = curl_easy_strerror(res);
        LOG_ERROR("HTTP POST failed: " << response.error_message);
        return response;
    }
    
    // Get HTTP status code
    long http_code = 0;
    curl_easy_getinfo(curl_handle_, CURLINFO_RESPONSE_CODE, &http_code);
    response.status_code = static_cast<int>(http_code);
    
    // Determine success based on HTTP status
    response.success = (http_code >= 200 && http_code < 300);
    
    if (response.success) {
        LOG_INFO("HTTP POST succeeded - Status: " << response.status_code);
        LOG_DEBUG("Response: " << response.body.substr(0, 200) 
                  << (response.body.length() > 200 ? "..." : ""));
    } else {
        LOG_WARN("HTTP POST returned status: " << response.status_code);
        LOG_WARN("Response: " << response.body);
    }
    
    return response;
}

HttpResponse HttpClient::post_json(const std::string& url, const std::string& json_body) {
    HttpResponse response;
    
    if (!curl_handle_) {
        response.error_message = "CURL handle not initialized";
        LOG_ERROR(response.error_message);
        return response;
    }
    
    LOG_INFO("POST " << url);
    LOG_DEBUG("JSON body: " << json_body);
    
    curl_easy_reset(curl_handle_);
    
    curl_easy_setopt(curl_handle_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_handle_, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl_handle_, CURLOPT_TIMEOUT, timeout_seconds_);
    curl_easy_setopt(curl_handle_, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle_, CURLOPT_WRITEDATA, &response.body);
    
    if (verbose_) {
        curl_easy_setopt(curl_handle_, CURLOPT_VERBOSE, 1L);
    }
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl_handle_, CURLOPT_HTTPHEADER, headers);
    
    CURLcode res = curl_easy_perform(curl_handle_);
    
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        response.success = false;
        response.error_message = curl_easy_strerror(res);
        LOG_ERROR("HTTP POST failed: " << response.error_message);
        return response;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl_handle_, CURLINFO_RESPONSE_CODE, &http_code);
    response.status_code = static_cast<int>(http_code);
    response.success = (http_code >= 200 && http_code < 300);
    
    if (response.success) {
        LOG_INFO("HTTP POST succeeded - Status: " << response.status_code);
        LOG_DEBUG("Response: " << response.body.substr(0, 200) 
                  << (response.body.length() > 200 ? "..." : ""));
    } else {
        LOG_WARN("HTTP POST returned status: " << response.status_code);
        LOG_WARN("Response: " << response.body);
    }
    
    return response;
}

HttpResponse HttpClient::get(const std::string& url) {
    HttpResponse response;
    
    if (!curl_handle_) {
        response.error_message = "CURL handle not initialized";
        LOG_ERROR(response.error_message);
        return response;
    }
    
    LOG_INFO("GET " << url);
    
    // Reset curl handle
    curl_easy_reset(curl_handle_);
    
    // Configure request
    curl_easy_setopt(curl_handle_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_handle_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_handle_, CURLOPT_TIMEOUT, timeout_seconds_);
    curl_easy_setopt(curl_handle_, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle_, CURLOPT_WRITEDATA, &response.body);
    
    if (verbose_) {
        curl_easy_setopt(curl_handle_, CURLOPT_VERBOSE, 1L);
    }
    
    // Execute request
    CURLcode res = curl_easy_perform(curl_handle_);
    
    if (res != CURLE_OK) {
        response.success = false;
        response.error_message = curl_easy_strerror(res);
        LOG_ERROR("HTTP GET failed: " << response.error_message);
        return response;
    }
    
    // Get HTTP status code
    long http_code = 0;
    curl_easy_getinfo(curl_handle_, CURLINFO_RESPONSE_CODE, &http_code);
    response.status_code = static_cast<int>(http_code);
    response.success = (http_code >= 200 && http_code < 300);
    
    LOG_INFO("HTTP GET completed - Status: " << response.status_code);
    
    return response;
}

} // namespace Http
} // namespace Utils
} // namespace SDTrading
