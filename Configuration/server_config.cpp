#include "server_config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

// Global instance
ServerConfig server_config;

// ─────────────────────────────────────────────────────────────
static std::string trim(const std::string& s)
{
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

// ─────────────────────────────────────────────────────────────
void ServerConfig::load(const std::string& file_path)
{
    std::ifstream f(file_path);
    if (!f.is_open())
    {
        std::cerr << "[ServerConfig] Cannot open " << file_path
                  << " — using defaults\n";
        return;
    }

    std::string line;
    while (std::getline(f, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if      (key == "server_host")                  host                       = val;
        else if (key == "server_port")                  port                       = std::stoi(val);
        else if (key == "thread_pool_size")             thread_pool_size           = std::stoi(val);
        else if (key == "backend_connect_timeout")      backend_connect_timeout    = std::stoi(val);
        else if (key == "backend_response_timeout")     backend_response_timeout   = std::stoi(val);
        else if (key == "sites_root")                   sites_root                 = val;
        else if (key == "server_db_host")               db_host                    = val;
        else if (key == "server_db_port")               db_port                    = std::stoi(val);
        else if (key == "server_db_name")               db_name                    = val;
        else if (key == "server_db_user")               db_user                    = val;
        else if (key == "server_db_password")           db_password                = val;
        else if (key == "admin_backend_host")           admin_backend_host         = val;
        else if (key == "admin_backend_port")           admin_backend_port         = std::stoi(val);
        else if (key == "admin_backend_path_prefix")    admin_backend_path_prefix  = val;
        else if (key == "internal_api_path_prefix")     internal_api_path_prefix   = val;
        else if (key == "internal_api_secret")          internal_api_secret        = val;
        else if (key == "k8s_kubeconfig")               k8s_kubeconfig             = val;
        else if (key == "k8s_api_server")               k8s_api_server             = val;
        else if (key == "ssl_enabled")                  ssl_enabled                = (val == "true" || val == "1");
        else if (key == "ssl_cert_path")                ssl_cert_path              = val;
        else if (key == "ssl_key_path")                 ssl_key_path               = val;
    }

    std::cout << "[ServerConfig] Loaded from " << file_path << "\n";
}

// ─────────────────────────────────────────────────────────────
std::string ServerConfig::pgConnString() const
{
    return "host="     + db_host +
           " port="    + std::to_string(db_port) +
           " dbname="  + db_name +
           " user="    + db_user +
           " password="+ db_password +
           " connect_timeout=10";
}
