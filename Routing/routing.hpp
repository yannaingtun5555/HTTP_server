#ifndef ROUTING_HPP
#define ROUTING_HPP

#include <string>
#include <memory>
#include <boost/beast/http.hpp>
#include "../File_Management/file_handler.hpp"
#include "../Core/response_generator.hpp"

class Session;

// ─────────────────────────────────────────────────────────────
//  Routing — domain-aware static file routing
//  Serves files from: <sites_root>/<domain>/public/<path>
// ─────────────────────────────────────────────────────────────
class Routing
{
public:
    Routing();

    // Process a static-file request for a given domain.
    // `domain` is extracted from the Host: header by the request handler.
    void processRequest(bool request_valid,
                        const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                        std::shared_ptr<Session> session,
                        const std::string& domain = "");

private:
    FileManager   file_manager;
    ResponseGenerator response;

    // Resolve the public path for a domain + URL path
    std::string resolvePath(const std::string& domain,
                            const std::string& url_path) const;
};

#endif // ROUTING_HPP