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
    std::string static_domain;

    SiteEntry matched_site;
    if (!host.empty() && site_config.findSite(host, matched_site))
    {
        static_domain = matched_site.domain;
    }

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
        BackendConfig admin_backend;
        admin_backend.name = "admin";
        admin_backend.host = server_config.admin_backend_host;
        admin_backend.port = server_config.admin_backend_port;
        admin_backend.path_prefix = admin_prefix;
        boost::asio::post(io_context, [req, client_session, admin_backend]()
        {
            try {
                ProxyHandler handler;
                handler.forwardRequest(req, client_session, admin_backend);
            } catch (...) {
                // Backend unavailable — socket already handled inside ProxySession
            }
        });
        return;
    }

    // ── 3. Site API path (/api/...) → proxy to the site's BE pod ────
    if (target.rfind("/api/", 0) == 0)
    {
        if (static_domain.empty() || matched_site.be_cluster_host.empty() || matched_site.be_cluster_port == 0)
        {
            auto res = boost::beast::http::response<boost::beast::http::string_body>();
            res.version(req->version());
            res.result(boost::beast::http::status::service_unavailable);
            res.set(boost::beast::http::field::server, "HTTP_Server");
            res.set(boost::beast::http::field::content_type, "text/plain");
            res.keep_alive(req->keep_alive());
            res.body() = "503 Service Unavailable: backend is not deployed";
            res.prepare_payload();
            boost::beast::http::write(session->socket(), res);
            if (req->keep_alive()) session->do_read();
            return;
        }

        auto client_session = session;
        BackendConfig backend;
        backend.name = matched_site.domain;
        backend.host = matched_site.be_cluster_host;
        backend.port = matched_site.be_cluster_port;
        backend.path_prefix = "/api";
        boost::asio::post(io_context, [req, client_session, backend]()
        {
            try {
                ProxyHandler handler;
                handler.forwardRequest(req, client_session, backend);
            } catch (...) {}
        });
        return;
    }

    // ── 4. Static file serving (virtual-host aware) ───────────
    // Route to the domain's public folder
    if (isValidRequest(*req))
    {
        routing.processRequest(true, *req, session, static_domain);
    }
    else
    {
        // Invalid path (e.g. bare "/") → serve index.html for the domain
        routing.processRequest(false, *req, session, static_domain);
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
