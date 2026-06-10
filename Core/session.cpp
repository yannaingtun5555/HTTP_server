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
    boost::asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint();
    client_ip = remote_ep.address().to_string();
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
            if (!ec)
            {
                std::string user_agent(req->at(boost::beast::http::field::user_agent).data(), req->at(boost::beast::http::field::user_agent).size());
                std::string http_method(req->method_string().data(), req->method_string().size());
                std::string http_target(req->target().data(), req->target().size());

                Json::Value headers_json;
                for (const auto& field : req->base())
                {
                    //headers_json[field.name_string().to_string()] = field.value().to_string();
                }

                Json::Value json_payload;
                std::string body_data = boost::beast::buffers_to_string(req->body().data());
                if (!body_data.empty())
                {
                   
                    Json::CharReaderBuilder builder;
                    std::istringstream s(body_data);
                    std::string errs;
                    if (Json::parseFromStream(builder, s, &json_payload, &errs))
                    {
                        Monitoring::logUserActivity(client_ip, user_agent, http_target,json_payload);
                    }
                    else
                    {
                        Monitoring::log_error(client_ip,"JSON parsing failed");
                        Monitoring::logUserActivity(client_ip, user_agent, http_target, Json::Value());
                    }
                }
                else
                {
                    Monitoring::logUserActivity(client_ip, user_agent, http_target, Json::Value());
                }
                global_thread_pool.post([self, req]()
                {
                    auto handler = std::make_shared<Request_handler>(self);
                    handler->handleRequest(req);
                });

            }
            else
            {
                if (ec == boost::asio::error::eof)
                {
                    std::cout << "Client disconnected: " << ec.message() << std::endl;
                }
                else
                {
                    std::cerr << "Error during read: " << ec.message() << std::endl;
                    General_error err(false,"Error during read" + ec.message(),ERROR);
                }
                // Handle error and possibly close the session
            }
        });
}
