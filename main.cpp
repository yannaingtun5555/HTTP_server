#include <boost/asio.hpp>
#include "Core/connection.hpp"
#include "Core/global.hpp"
#include "Configuration/config_parser.hpp"
#include "Configuration/server_config.hpp"
#include "Configuration/site_config.hpp"
#include "File_Management/file_handler.hpp"
#include "Error_handling/error_handling.hpp"
#include "Thread/thread.hpp"
#include "Monitoring/monitoring.hpp"
#include "DB/server_db.hpp"
#include "K8s/k8s_controller.hpp"
#include "FrontEnd/fe_builder.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <filesystem>

namespace fs = std::filesystem;

using namespace std;

int main(int argc, char* argv[])
{
    // ── 1. Logging ──────────────────────────────────────────────
    Monitoring::init("logs/");
    Error error_log;
    error_log.start_log();

    // ── 2. Load configs ─────────────────────────────────────────
    server_config.load("server.conf");
    site_config.load("sites.conf");

    // Legacy config.txt still loaded for backward-compat with
    // existing ProxyHandler which reads `config.backends`.
    config.load("config.txt");

    // Determine run mode (legacy start/reverse for backward compat)
    std::string mode_arg = (argc >= 2) ? std::string(argv[1]) : "start";
    if (mode_arg == "reverse")
    {
        server_mode = ServerMode::REVERSE_PROXY;
        std::cout << "[HTTP_Server] Legacy REVERSE PROXY mode\n";
    }
    else
    {
        server_mode = ServerMode::STATIC;
        std::cout << "[HTTP_Server] Platform mode (virtual-host static + K8s backends)\n";
    }

    // ── 3. Connect to server DB ─────────────────────────────────
    bool db_ok = server_db.connect(
        server_config.pgConnString(),
        "DB/schema.sql");
    if (!db_ok)
    {
        std::cerr << "[HTTP_Server] WARNING: Server DB unavailable. "
                  << "Deploy and status features disabled.\n";
    }

    // ── 4. Init K8s controller ──────────────────────────────────
    k8s_controller.init(server_config.k8s_api_server,
                        server_config.k8s_kubeconfig);

    // ── 5. Ensure sites_root exists ─────────────────────────────
    fs::create_directories(server_config.sites_root);
    // Default site (admin panel static FE)
    fs::create_directories(server_config.sites_root + "/default/public");

    // ── 6. For each site: build FE + spin up DB + BE containers ─
    //       Run in thread-pool threads so server starts accepting
    //       connections while deployments proceed.
    auto sites = site_config.sites();
    for (const auto& site : sites)
    {
        global_thread_pool.post([site]()
        {
            std::cout << "[Startup] Processing site: " << site.domain << "\n";

            // Build FE
            std::string public_dir = server_config.sites_root
                                   + "/" + site.domain + "/public";
            std::string fe_error;
            bool fe_ok = FrontendBuilder::build(
                site.domain, site.fe_folder, public_dir,
                site.fe_build, fe_error);

            if (!fe_ok)
            {
                std::cerr << "[Startup][" << site.domain << "] FE build failed: "
                          << fe_error << "\n";
                return;
            }

            // Spin up DB containers and collect connection strings
            // (credentials not stored in sites.conf — already running
            //  sites must have been deployed via admin UI and are thus
            //  already in K8s; skip K8s spin-up on plain restart)
            std::cout << "[Startup][" << site.domain << "] FE ready at "
                      << public_dir << "\n";
        });
    }

    // ── 7. IO thread pool ───────────────────────────────────────
    unsigned int io_threads = std::thread::hardware_concurrency();
    if (io_threads < 2) io_threads = 2;

    // Start accepting connections
    Connection_manager connection_manager(io_context, config);

    std::vector<std::thread> threads;
    for (unsigned int i = 1; i < io_threads; ++i)
    {
        threads.emplace_back([&]() { io_context.run(); });
    }

    std::cout << "[HTTP_Server] Listening on "
              << server_config.host << ":" << server_config.port
              << " with " << io_threads << " I/O threads\n";

    io_context.run();

    for (auto& t : threads)
        if (t.joinable()) t.join();

    global_thread_pool.join();
    server_db.disconnect();

    return 0;
}
