#ifndef SESSION_HPP
#define SESSION_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>

#include <memory>
#include <string>

using boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket);
    void start();
    void do_read();
    tcp::socket& socket() { return socket_; }

private:
    tcp::socket socket_;
    enum { max_length = 1024 };
    char data_[max_length];
    boost::asio::ip::tcp::endpoint endpoint;
    std::string client_ip;
};

#endif // SESSION_HPP
