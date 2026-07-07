#include "global.hpp"


#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "session.hpp"


boost::asio::io_context io_context;
Config config;
ThreadPool global_thread_pool(config.thread_pool_size > 0 ? config.thread_pool_size : std::thread::hardware_concurrency());
ServerMode server_mode = ServerMode::STATIC;
