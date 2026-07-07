#include "routing.hpp"
#include "../Error_handling/error_handling.hpp"
#include "../File_Management/file_handler.hpp"
#include "../Configuration/config_parser.hpp"
#include "../Core/response_generator.hpp"
#include "../Monitoring/monitoring.hpp"
#include "../Thread/thread.hpp"
#include "../Core/global.hpp"
#include <boost/beast/http.hpp>
#include <iostream>
#include <string>
#include <thread>

Routing::Routing() {}

void Routing::processRequest(bool request_valid,const boost::beast::http::request<boost::beast::http::dynamic_body>& req,
                             std::shared_ptr<Session> session) 
{
    try {
        std::string content;
        std::string target = std::string(req.target());

        if(!request_valid)
        {
            content = file_manager.readFile("/error/400.html");
            response.generateAndSendResponse(req, session, content, 400);
            if (req.keep_alive()) session->do_read();
            return;
        }

        if (req.method_string() == "GET")
        {
            if (file_manager.fileExists(target))
            {
                content = file_manager.readFile(target);
                response.generateAndSendResponse(req, session, content, 200); 
            }
            else
            {
                content = file_manager.readFile("/error/404.html");
                response.generateAndSendResponse(req, session, content, 404); 
            }
        }
        else if (req.method_string() == "POST")
        {
            std::string request_body = boost::beast::buffers_to_string(req.body().data());
            try
            {
                file_manager.writeFile(target, request_body); 
                response.generateAndSendResponse(req, session, content, 201); 
            }
            catch (const std::exception& e)
            {
                content = file_manager.readFile("/error/500.html");
                response.generateAndSendResponse(req, session, content, 500); 
            }
        }
        else
        {
            content = "Method Not Allowed";
            response.generateAndSendResponse(req, session, content, 405); 
        }

        // Keep-alive: read the next request on this connection
        if (req.keep_alive()) {
            session->do_read();
        }

    } catch (const std::exception& e) {
        // Don't log to cerr on hot path — too slow
    }
}