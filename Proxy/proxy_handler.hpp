#ifndef PROXY_HANDLER_HPP
#define PROXY_HANDLER_HPP

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <memory>
#include <string>

#include "../Core/session.hpp"
#include "../Configuration/config_parser.hpp"

/**
 * Asynchronous reverse-proxy entry point.
 *
 * forwardRequest() returns immediately after scheduling the first async
 * operation.  All state lives in an internal ProxySession (shared_ptr).
 */
class ProxyHandler
{
public:
    ProxyHandler() = default;

    /**
     * Begin forwarding a client request to the matched backend.
     * Must be invoked on (or posted to) the server's io_context thread.
     * The caller must not write a response afterward.
     */
    void forwardRequest(
        std::shared_ptr<boost::beast::http::request<boost::beast::http::dynamic_body>> req,
        std::shared_ptr<Session> session
    );

    void forwardRequest(
        std::shared_ptr<boost::beast::http::request<boost::beast::http::dynamic_body>> req,
        std::shared_ptr<Session> session,
        const BackendConfig& backend
    );

    /**
     * Perform a health check on a target backend (host:port).
     * Returns true if the TCP connection succeeds and responds within timeout.
     */
    static bool checkBackendHealth(const std::string& host, unsigned port, int timeout_secs = 5);

    /**
     * Rate limiter (Token Bucket): check if client IP has exceeded request quota.
     * Returns true if allowed, false if rate limited (429 Too Many Requests).
     */
    static bool allowClientRequest(const std::string& client_ip, int max_burst = 60, int fill_rate_per_sec = 10);
};

#endif // PROXY_HANDLER_HPP
