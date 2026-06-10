#include "global.hpp"


#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "session.hpp"


boost::asio::io_context io_context;
ThreadPool global_thread_pool(std::thread::hardware_concurrency());

Config config;
ServerMode server_mode = ServerMode::STATIC;
