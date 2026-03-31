// ============================================================
// HTTP CLIENT - REST API Communication Module
// Handles HTTP POST requests to Java Spring Boot microservices
// ============================================================

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <curl/curl.h>

namespace SDTrading {
namespace Utils {
namespace Http {

/**
 * @struct HttpResponse
 * @brief Encapsulates HTTP response data
 */
struct HttpResponse {
    int status_code;                    // HTTP status code (200, 404, 500, etc.)
    std::string body;                   // Response body content
    std::map<std::string, std::string> headers;  // Response headers
    bool success;                       // True if request succeeded
    std::string error_message;          // Error description if failed
    
    HttpResponse() : status_code(0), success(false) {}
};

/**
 * @class HttpClient
 * @brief Thread-safe HTTP client using libcurl
 * 
 * Features:
 * - POST requests with form data
 * - Configurable timeouts
 * - Connection pooling via CURL handles
 * - Error handling and logging
 * - Support for URL-encoded form data
 * 
 * Example:
 *   HttpClient client;
 *   std::map<std::string, std::string> params;
 *   params["quantity"] = "1";
 *   params["strategyNumber"] = "13";
 *   HttpResponse response = client.post("http://localhost:8080/multipleOrderSubmit", params);
 */
class HttpClient {
private:
    CURL* curl_handle_;                 // libcurl handle (reusable)
    long timeout_seconds_;              // Request timeout
    bool verbose_;                      // Enable verbose logging
    
    /**
     * Callback for libcurl to write response data
     */
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);
    
    /**
     * URL-encode a string for form data
     */
    std::string url_encode(const std::string& value);
    
    /**
     * Build URL-encoded form data from parameters
     */
    std::string build_form_data(const std::map<std::string, std::string>& params);

public:
    /**
     * Constructor
     * @param timeout_seconds Request timeout (default: 10 seconds)
     * @param verbose Enable verbose curl logging (default: false)
     */
    explicit HttpClient(long timeout_seconds = 10, bool verbose = false);
    
    /**
     * Destructor - cleanup curl handle
     */
    ~HttpClient();
    
    // Prevent copying (curl handles are not copyable)
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    
    /**
     * Execute HTTP POST request with form data
     * @param url Target URL
     * @param form_params Form parameters (key-value pairs)
     * @return HttpResponse with status and body
     */
    HttpResponse post(const std::string& url, 
                     const std::map<std::string, std::string>& form_params);

    /**
     * Execute HTTP POST request with JSON body
     * @param url Target URL
     * @param json_body JSON payload as a string
     * @return HttpResponse with status and body
     */
    HttpResponse post_json(const std::string& url, const std::string& json_body);
    
    /**
     * Execute HTTP GET request
     * @param url Target URL
     * @return HttpResponse with status and body
     */
    HttpResponse get(const std::string& url);
    
    /**
     * Set request timeout
     * @param seconds Timeout in seconds
     */
    void set_timeout(long seconds) { timeout_seconds_ = seconds; }
    
    /**
     * Enable/disable verbose logging
     * @param verbose True to enable curl verbose output
     */
    void set_verbose(bool verbose) { verbose_ = verbose; }
};

} // namespace Http
} // namespace Utils
} // namespace SDTrading

#endif // HTTP_CLIENT_H
