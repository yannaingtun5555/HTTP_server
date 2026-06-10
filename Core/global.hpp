#ifndef GLOBAL_HPP
#define GLOBAL_HPP

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include "../Configuration/config_parser.hpp"
#include "../Thread/thread.hpp"

// Server operating mode — set once at startup via argv[1]
enum class ServerMode
{
    STATIC,        // Serve local static files (default)
    REVERSE_PROXY  // Forward requests to configured backend(s)
};

extern boost::asio::io_context io_context;
extern ThreadPool global_thread_pool;
extern Config config;
extern ServerMode server_mode;

#endif // GLOBAL_HPP
