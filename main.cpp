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
#include <fstream>
#include <sstream>
#include <cstdint>

namespace fs = std::filesystem;

using namespace std;

namespace {

std::string fnv1a64Hex(const std::string& data)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : data)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}

void syncPagesForDomain(int domain_id, const fs::path& public_dir)
{
    if (!server_db.clearPagesForDomain(domain_id))
        return;

    if (!fs::exists(public_dir) || !fs::is_directory(public_dir))
        return;

    for (const auto& entry : fs::recursive_directory_iterator(public_dir))
    {
        if (!entry.is_regular_file())
            continue;

        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.is_open())
            continue;

        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        PageRecord p;
        p.domain_id    = domain_id;
        p.path         = "/" + fs::relative(entry.path(), public_dir).generic_string();
        p.content_hash = fnv1a64Hex(content);
        p.size_bytes   = static_cast<long long>(content.size());
        server_db.upsertPage(p);
    }
}

}

int main(int argc, char* argv[])
{
    // ── 1. Logging ──────────────────────────────────────────────
    Monitoring::init("logs/");
    Error error_log;
    error_log.start_log();

    // ── 2. Load configs ─────────────────────────────────────────
    server_config.load("server.conf");
    site_config.load("sites.conf");
    config.host = server_config.host;
    config.port = server_config.port;
    config.backend_connect_timeout = server_config.backend_connect_timeout;
    config.backend_response_timeout = server_config.backend_response_timeout;

    // Determine run mode (keep start/reverse for compatibility,
    // but the active architecture is site-config + K8s managed)
    std::string mode_arg = (argc >= 2) ? std::string(argv[1]) : "start";
    if (mode_arg == "reverse")
    {
        server_mode = ServerMode::REVERSE_PROXY;
        std::cout << "[HTTP_Server] Compatibility reverse-proxy mode\n";
    }
    else
    {
        server_mode = ServerMode::STATIC;
        std::cout << "[HTTP_Server] Site platform mode (static FE + K8s BE/DB)\n";
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

    // ── 6. For each site: build FE and sync DB pages ────────────
    //       Backend/DB containers are deployed via the admin panel,
    //       not automatically at boot.
    auto sites = site_config.sites();
    for (const auto& site : sites)
    {
        std::cout << "[Startup] Processing site: " << site.domain << "\n";

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
        }
        else
        {
            std::cout << "[Startup][" << site.domain << "] FE ready at "
                      << public_dir << "\n";
        }

        if (db_ok)
        {
            DomainRecord dr;
            dr.domain     = site.domain;
            dr.user_owner = site.user;
            dr.fe_folder  = site.fe_folder;
            dr.be_folder  = site.be_folder;
            dr.be_type    = site.be_type;
            dr.run_cmd    = site.run_cmd;
            dr.be_port    = static_cast<int>(site.be_port);
            dr.fe_build   = site.fe_build;
            dr.db_count   = static_cast<int>(site.dbs.size());
            dr.status     = (site.be_type == "static" || !site.be_cluster_host.empty())
                            ? "running"
                            : "pending";
            dr.error_msg  = "";
            server_db.upsertDomain(dr);

            DomainRecord stored;
            if (server_db.getDomain(site.domain, stored))
            {
                syncPagesForDomain(stored.id, public_dir);
            }
        }
    }

    // ── 7. IO thread pool ───────────────────────────────────────
    unsigned int io_threads = std::thread::hardware_concurrency();
    if (io_threads < 2) io_threads = 2;

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
