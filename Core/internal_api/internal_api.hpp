#ifndef INTERNAL_API_HPP
#define INTERNAL_API_HPP

#include <string>
#include <functional>
#include <boost/beast/http.hpp>
#include "../Configuration/site_config.hpp"

namespace beast = boost::beast;
namespace http  = beast::http;

// ─────────────────────────────────────────────────────────────
//  InternalApiHandler
//  Processes requests that match the internal API path prefix
//  (e.g. /_api/internal/...) coming from the admin panel or CLI.
//
//  All endpoints require the X-Internal-Secret header to match
//  the configured secret.
//
//  Endpoints:
//    POST  /deploy         — deploy a new site (JSON body)
//    GET   /status/<domain>— get deploy status
//    DELETE/delete/<domain>— tear down a site
//    POST  /reload         — hot-reload sites.conf + routing
// ─────────────────────────────────────────────────────────────
class InternalApiHandler
{
public:
    // Returns an HTTP response string for the given request.
    // `path_suffix` is the part of the path after the prefix,
    // e.g. "/deploy", "/status/example.com".
    static http::response<http::string_body>
    handle(const http::request<http::dynamic_body>& req,
           const std::string& path_suffix,
           const std::string& expected_secret);

private:
    // Helpers
    static http::response<http::string_body>
    makeJson(unsigned status, const std::string& body,
             unsigned http_version, bool keep_alive);

    static http::response<http::string_body>
    handleDeploy(const http::request<http::dynamic_body>& req,
                 unsigned http_version, bool keep_alive);

    static http::response<http::string_body>
    handleStatus(const std::string& domain,
                 unsigned http_version, bool keep_alive);

    static http::response<http::string_body>
    handleDelete(const std::string& domain,
                 unsigned http_version, bool keep_alive);

    static http::response<http::string_body>
    handleReload(unsigned http_version, bool keep_alive);

    // Deploy sequence (runs synchronously in the calling thread
    // which is a thread-pool thread — not the io_context thread).
    static bool runDeploy(const SiteEntry& site,
                          const std::vector<std::pair<std::string,std::string>>& db_creds,
                          std::string& error_out,
                          std::string& conn_strings_json_out);
};

#endif // INTERNAL_API_HPP
