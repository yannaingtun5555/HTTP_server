#include "global.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "session.hpp"

boost::asio::io_context io_context;
Config config;
// Thread pool is resized in main() after config load via reinitThreadPool().
// Default: max(hardware_concurrency, 8) ensures reasonable concurrency even
// before config is applied (e.g. during static initialisation of other TUs).
ThreadPool global_thread_pool(
    std::thread::hardware_concurrency() > 8
        ? std::thread::hardware_concurrency()
        : 8);
ServerMode server_mode = ServerMode::STATIC;
