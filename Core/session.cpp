#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>
#include <json/json.h>

#include "session.hpp"
#include "global.hpp"
#include "request_handler.hpp"
#include "../Thread/thread.hpp"
#include "../Monitoring/monitoring.hpp"
#include <iostream>
#include <thread>

Session::Session(boost::asio::ip::tcp::socket socket)
    : socket_(std::move(socket))
{
}

void Session::start()
{
    try {
        boost::asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint();
        client_ip = remote_ep.address().to_string();
    } catch (const std::exception& e) {
        std::cerr << "Error getting remote endpoint: " << e.what() << std::endl;
        return;
    }
    do_read();
}

void Session::do_read()
{
    auto self = shared_from_this();

    auto buffer = std::make_shared<boost::beast::flat_buffer>();
    auto req = std::make_shared<boost::beast::http::request<boost::beast::http::dynamic_body>>();

    boost::beast::http::async_read(
        socket_, *buffer, *req,
        [this, self, buffer, req](boost::system::error_code ec, std::size_t bytes_transferred)
        {
            if (ec)
            {
                if (ec == boost::asio::error::eof ||
                    ec == boost::beast::http::error::end_of_stream)
                {
                    // Client closed connection gracefully
                }
                else if (ec != boost::asio::error::operation_aborted)
                {
                    std::cerr << "Read error: " << ec.message() << std::endl;
                }
                return;
            }

            // Post request handling to thread pool — keep io_context free
            // for accepting new connections and reading data
            global_thread_pool.post([this, self, req]()
            {
                // Log user activity in background (non-blocking)
                std::string user_agent = "unknown";
                auto ua_it = req->find(boost::beast::http::field::user_agent);
                if (ua_it != req->end()) {
                    user_agent = std::string(ua_it->value());
                }
                std::string http_target(req->target().data(), req->target().size());

                // Fire-and-forget: log in a separate thread pool task
                std::string ip = client_ip;
                std::string ua = user_agent;
                std::string tgt = http_target;
                global_thread_pool.post([ip, ua, tgt]() {
                    Monitoring::logUserActivity(ip, ua, tgt, Json::Value());
                });

                // Handle request immediately (don't wait for logging)
                auto handler = std::make_shared<Request_handler>(self);
                handler->handleRequest(req);
            });
        });
}
