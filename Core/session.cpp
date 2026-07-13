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
        return;
    }
    do_read();
}

void Session::do_read()
{
    auto self = shared_from_this();

    // Buffers live on heap via shared_ptr — safe across async boundary
    auto buffer = std::make_shared<boost::beast::flat_buffer>();
    auto req    = std::make_shared<boost::beast::http::request<boost::beast::http::dynamic_body>>();

    boost::beast::http::async_read(
        socket_, *buffer, *req,
        [this, self, buffer, req](boost::system::error_code ec, std::size_t /*bytes*/)
        {
            if (ec)
            {
                // EOF / closed — not an error
                if (ec != boost::asio::error::eof &&
                    ec != boost::beast::http::error::end_of_stream &&
                    ec != boost::asio::error::operation_aborted)
                {
                    // Suppress noisy read errors (broken pipe, connection reset)
                }
                return;
            }

            // Post to thread pool — frees the io_context thread for new accepts
            global_thread_pool.post([this, self, req]()
            {
                // Inline logging (no nested post — avoids unbounded task chains)
                try {
                    std::string ip  = client_ip;
                    std::string tgt(req->target().data(), req->target().size());
                    auto ua_it = req->find(boost::beast::http::field::user_agent);
                    std::string ua  = (ua_it != req->end())
                                      ? std::string(ua_it->value())
                                      : "unknown";
                    Monitoring::logUserActivity(ip, ua, tgt, Json::Value());
                } catch (...) {}

                // Handle the request
                try {
                    auto handler = std::make_shared<Request_handler>(self);
                    handler->handleRequest(req);
                } catch (const std::bad_alloc& e) {
                    // OOM: close the socket gracefully, don't crash
                    boost::system::error_code ignored;
                    self->socket_.shutdown(
                        boost::asio::ip::tcp::socket::shutdown_both, ignored);
                } catch (...) {}
            });
        });
}
