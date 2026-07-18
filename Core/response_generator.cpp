#include "response_generator.hpp"
#include "../Monitoring/monitoring.hpp"
#include "../Thread/thread.hpp"
#include "global.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <thread>
#include <iostream>
#include <mutex>
#include <cctype>

ResponseGenerator::ResponseGenerator() {}

static std::string mimeTypeForTarget(const std::string& target)
{
    auto path_end = target.find_first_of("?#");
    std::string path = (path_end == std::string::npos) ? target : target.substr(0, path_end);

    auto dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return "text/html";

    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css")  return "text/css";
    if (ext == "js")   return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "txt")  return "text/plain";
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")  return "image/gif";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "ico")  return "image/x-icon";
    return "application/octet-stream";
}

void ResponseGenerator::generateAndSendResponse(const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                                               std::shared_ptr<Session> session, 
                                               const std::string& content, unsigned short status_code) 
{
    // Already running in the thread pool — write response directly.
    // No need to post again (was causing thread pool exhaustion).
    try
    {
        boost::beast::http::response<boost::beast::http::string_body> res;

        switch (status_code) 
        {
            case 200:
                res.result(boost::beast::http::status::ok);
                break;
            case 201:
                res.result(boost::beast::http::status::created);
                break;
            case 400:
                res.result(boost::beast::http::status::bad_request);
                break;
            case 404:
                res.result(boost::beast::http::status::not_found);
                break;
            case 405:
                res.result(boost::beast::http::status::method_not_allowed);
                break;
            case 500:
                res.result(boost::beast::http::status::internal_server_error);
                break;
            default:
                res.result(boost::beast::http::status::internal_server_error);
                break;
        }

        res.version(req.version());
        setResponseHeaders(res, req);
        res.body() = content;
        res.prepare_payload();

        boost::beast::http::write(session->socket(), res);

    } 
    catch (const std::exception& e) 
    {
        // Silently handle — don't use cerr on hot path
    }
}

void ResponseGenerator::setResponseHeaders(boost::beast::http::response<boost::beast::http::string_body>& res, 
                                           const boost::beast::http::request<boost::beast::http::dynamic_body>& req) 
{
    res.set(boost::beast::http::field::server, "HTTP_Server");

    std::string target(req.target().data(), req.target().size());
    res.set(boost::beast::http::field::content_type, mimeTypeForTarget(target));
    res.keep_alive(req.keep_alive());
}
