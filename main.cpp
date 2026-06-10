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


using namespace std;

int main(int argc, char* argv[])
{
    // Parse startup mode
    // Usage: ./HTTP_Server [start|reverse]
    //   start   -> static file serving mode (default)
    //   reverse -> reverse proxy mode
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
    General_error err(false,"this is testing error",ERROR);
    config.load("config.txt");
    Connection_manager connection_manager(io_context, config);
    io_context.run();
    global_thread_pool.join();

    return 0;
}
