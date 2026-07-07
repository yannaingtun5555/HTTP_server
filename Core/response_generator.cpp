#include "response_generator.hpp"
#include "../Monitoring/monitoring.hpp"
#include "../Thread/thread.hpp"
#include "global.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <thread>
#include <iostream>
#include <mutex>

ResponseGenerator::ResponseGenerator() {}

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
   
    if (req.find(boost::beast::http::field::content_type) != req.end()) 
    {
        std::string content_type = std::string(req[boost::beast::http::field::content_type]);

        if (content_type == "application/json")
        {
            res.set(boost::beast::http::field::content_type, "application/json");
        }
        else if (content_type == "text/html") 
        {
            res.set(boost::beast::http::field::content_type, "text/html");
        }
        else if (content_type == "text/plain") 
        {
            res.set(boost::beast::http::field::content_type, "text/plain");
        }
        else if (content_type == "image/png") 
        {
            res.set(boost::beast::http::field::content_type, "image/png");
        }
        else 
        {
            res.result(boost::beast::http::status::unsupported_media_type);
            res.set(boost::beast::http::field::content_type, "text/plain");
        }
    }
    else
    {
        // Default to text/html for GET responses (serving files)
        res.set(boost::beast::http::field::content_type, "text/html");
    }
    res.keep_alive(req.keep_alive());
}
