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

Request_handler::Request_handler(std::shared_ptr<Session> session)
    : session(session) {}

void Request_handler::handleRequest(std::shared_ptr<boost::beast::http::request<boost::beast::http::dynamic_body>> req)
{     
    try {
        endpoint = session->socket().remote_endpoint();
    } catch (...) {
        return; // Socket already closed
    }

    if (server_mode == ServerMode::REVERSE_PROXY)
    {
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
            routing.processRequest(true, *req, session);
        }
        else
        {
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
