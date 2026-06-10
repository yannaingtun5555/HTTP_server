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
};

#endif // PROXY_HANDLER_HPP
