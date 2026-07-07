#include <boost/asio.hpp>
#include "Core/connection.hpp"
#include "Configuration/config_parser.hpp"
#include "File_Management/file_handler.hpp"
#include "Error_handling/error_handling.hpp"
#include "Thread/thread.hpp"
#include "Monitoring/monitoring.hpp"
#include "Core/global.hpp"
#include <iostream>
#include <json/json.h> 
#include <string>
#include <vector>
#include <thread>

using namespace std;

int main(int argc, char* argv[])
{
    // Parse startup mode
    std::string mode_arg = (argc >= 2) ? std::string(argv[1]) : "start";

    if (mode_arg == "reverse")
    {
        server_mode = ServerMode::REVERSE_PROXY;
        std::cout << "[HTTP_Server] Starting in REVERSE PROXY mode" << std::endl;
    }
    else
    {
        server_mode = ServerMode::STATIC;
        std::cout << "[HTTP_Server] Starting in STATIC file serving mode" << std::endl;
    }

    Monitoring::init("logs/");
    Error error_log;
    error_log.start_log();
    config.load("config.txt");
    Connection_manager connection_manager(io_context, config);

    // Run io_context on multiple threads for parallel async I/O
    unsigned int io_threads = std::thread::hardware_concurrency();
    if (io_threads < 2) io_threads = 2;

    std::vector<std::thread> threads;
    for (unsigned int i = 1; i < io_threads; ++i)
    {
        threads.emplace_back([&]() {
            io_context.run();
        });
    }

    std::cout << "[HTTP_Server] Running with " << io_threads 
              << " I/O threads" << std::endl;

    // Main thread also runs io_context
    io_context.run();

    // Wait for worker threads
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    global_thread_pool.join();

    return 0;
}
