#include "request_handler.hpp"
#include "../Routing/routing.hpp"
#include "../Proxy/proxy_handler.hpp"
#include "../Thread/thread.hpp"
#include "../Monitoring/monitoring.hpp"
#include "global.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

std::mutex session_mutex;
std::mutex routing_mutex;

Request_handler::Request_handler(std::shared_ptr<Session> session)
    : session(session) {}

void Request_handler::handleRequest(std::shared_ptr<boost::beast::http::request<boost::beast::http::dynamic_body>> req)
{     
    {
        std::lock_guard<std::mutex> lock(session_mutex); 
        endpoint = session->socket().remote_endpoint();
    }  

    if (server_mode == ServerMode::REVERSE_PROXY)
    {
        // Reverse proxy mode: initiate async forwarding on the io_context.
        // ProxyHandler takes over the response; do not write from here.
        auto client_session = session;
        boost::asio::post(io_context, [req, client_session]()
        {
            ProxyHandler handler;
            handler.forwardRequest(req, client_session);
        });
        return;
    }
    else
    {
        // Static mode: validate then route to file handler
        if (isValidRequest(*req))
        {
            std::lock_guard<std::mutex> lock(routing_mutex);
            routing.processRequest(true, *req, session);
        }
        else
        {
            std::lock_guard<std::mutex> lock(routing_mutex);
            routing.processRequest(false, *req, session);
        }
    }
}

bool Request_handler::isValidRequest(const boost::beast::http::request<boost::beast::http::dynamic_body>& req_)
{
    std::string target(req_.target().data(), req_.target().size());
    if (target.empty() || target == "/")
    {
        return false; 
    }
    return true;
}
