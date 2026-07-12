#ifndef GLOBAL_HPP
#define GLOBAL_HPP

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include "../Configuration/config_parser.hpp"
#include "../Configuration/server_config.hpp"
#include "../Configuration/site_config.hpp"
#include "../Thread/thread.hpp"

// Legacy server operating mode (kept for backward compatibility)
enum class ServerMode
{
    STATIC,        // Serve local static files (default)
    REVERSE_PROXY  // Forward requests to configured backend(s)
};

extern boost::asio::io_context io_context;
extern ThreadPool global_thread_pool;
extern Config config;              // Legacy config (config.txt) — kept for compat
extern ServerMode server_mode;

#endif // GLOBAL_HPP
