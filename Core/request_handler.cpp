#include "request_handler.hpp"
#include "../Routing/routing.hpp"
#include "../Proxy/proxy_handler.hpp"
#include "../Thread/thread.hpp"
#include "../Monitoring/monitoring.hpp"
#include "../Configuration/server_config.hpp"
#include "../Configuration/site_config.hpp"
#include "../Core/internal_api/internal_api.hpp"
#include "global.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

Request_handler::Request_handler(std::shared_ptr<Session> session)
    : session(session) {}

// ─────────────────────────────────────────────────────────────
//  extractHost — parse the Host: header, strip port if present
// ─────────────────────────────────────────────────────────────
static std::string extractHost(
    const boost::beast::http::request<boost::beast::http::dynamic_body>& req)
{
    auto it = req.find(boost::beast::http::field::host);
    if (it == req.end()) return "";
    std::string h(it->value());
    auto colon = h.rfind(':');
    if (colon != std::string::npos) h = h.substr(0, colon);
    return h;
}

// ─────────────────────────────────────────────────────────────
//  handleRequest — virtual-host dispatch
// ─────────────────────────────────────────────────────────────
void Request_handler::handleRequest(
    std::shared_ptr<boost::beast::http::request<boost::beast::http::dynamic_body>> req)
{
    try {
        endpoint = session->socket().remote_endpoint();
    } catch (...) {
        return; // Socket already closed
    }

    std::string target(req->target().data(), req->target().size());
    std::string host = extractHost(*req);

    // ── 1. Internal API (/_api/internal/...) ─────────────────
    const std::string& int_prefix = server_config.internal_api_path_prefix;
    if (target.rfind(int_prefix, 0) == 0)
    {
        std::string suffix = target.substr(int_prefix.size());
        auto resp = InternalApiHandler::handle(
            *req, suffix, server_config.internal_api_secret);

        try {
            boost::beast::http::write(session->socket(), resp);
        } catch (...) {}

        if (req->keep_alive()) session->do_read();
        return;
    }

    // ── 2. Admin panel (/_admin/...) → proxy to Django pod ───
    const std::string& admin_prefix = server_config.admin_backend_path_prefix;
    if (target.rfind(admin_prefix, 0) == 0)
    {
        auto client_session = session;
        boost::asio::post(io_context, [req, client_session]()
        {
            // Reuse ProxyHandler but point at admin backend
            ProxyHandler handler;
            handler.forwardRequest(req, client_session);
        });
        return;
    }

    // ── 3. API path (/api/<domain>/...) → proxy to BE pod ────
    if (target.rfind("/api/", 0) == 0)
    {
        // The ProxyHandler uses config.backends (legacy) for now.
        // In the new design, the C++ server sets backend entries
        // dynamically when a site is deployed. ProxyHandler reads
        // `config.backends` which gets updated by the deploy pipeline.
        auto client_session = session;
        boost::asio::post(io_context, [req, client_session]()
        {
            ProxyHandler handler;
            handler.forwardRequest(req, client_session);
        });
        return;
    }

    // ── 4. Static file serving (virtual-host aware) ───────────
    // Route to the domain's public folder
    if (isValidRequest(*req))
    {
        routing.processRequest(true, *req, session, host);
    }
    else
    {
        // Invalid path (e.g. bare "/") → serve index.html for the domain
        routing.processRequest(false, *req, session, host);
    }
}

// ─────────────────────────────────────────────────────────────
bool Request_handler::isValidRequest(
    const boost::beast::http::request<boost::beast::http::dynamic_body>& req_)
{
    std::string target(req_.target().data(), req_.target().size());
    // Allow "/" — routing layer will serve index.html
    if (target.empty()) return false;
    return true;
}
