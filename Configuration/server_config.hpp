#ifndef SERVER_CONFIG_HPP
#define SERVER_CONFIG_HPP

#include <string>

// ─────────────────────────────────────────────────────────────
//  ServerConfig — loaded from server.conf
//  Contains only server-level settings (no per-site data).
// ─────────────────────────────────────────────────────────────
struct ServerConfig
{
    // ── Network ──────────────────────────────────────────────
    std::string host        = "0.0.0.0";
    unsigned int port       = 80;
    bool ssl_enabled        = false;
    std::string ssl_cert_path = "./server.crt";
    std::string ssl_key_path  = "./server.key";

    // ── Performance ──────────────────────────────────────────
    int thread_pool_size    = 0;   // 0 = auto (hardware_concurrency)

    // ── Proxy timeouts (seconds) ─────────────────────────────
    int backend_connect_timeout  = 30;
    int backend_response_timeout = 60;

    // ── Sites root ───────────────────────────────────────────
    std::string sites_root  = "./sites";

    // ── Server-side PostgreSQL ────────────────────────────────
    std::string db_host     = "127.0.0.1";
    unsigned int db_port    = 5432;
    std::string db_name     = "server_db";
    std::string db_user     = "admin";
    std::string db_password = "changeme";

    // ── Admin panel (Django pod) ──────────────────────────────
    std::string admin_backend_host         = "127.0.0.1";
    unsigned int admin_backend_port        = 8080;
    std::string admin_backend_path_prefix  = "/_admin";

    // ── Internal API ─────────────────────────────────────────
    std::string internal_api_path_prefix   = "/_api/internal";
    std::string internal_api_secret        = "changeme-secret";

    // ── Kubernetes ───────────────────────────────────────────
    std::string k8s_kubeconfig  = "/etc/kubernetes/admin.conf";
    std::string k8s_api_server  = "https://127.0.0.1:6443";

    // ── Methods ──────────────────────────────────────────────
    void load(const std::string& file_path);

    // Convenience: build libpqxx connection string
    std::string pgConnString() const;
};

extern ServerConfig server_config;

#endif // SERVER_CONFIG_HPP
