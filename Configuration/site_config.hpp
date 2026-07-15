#ifndef SITE_CONFIG_HPP
#define SITE_CONFIG_HPP

#include <string>
#include <vector>
#include <map>
#include <shared_mutex>

// ─────────────────────────────────────────────────────────────
//  DbEntry — one database per site (sites can have 1..N)
// ─────────────────────────────────────────────────────────────
struct DbEntry
{
    std::string alias;      // logical name, e.g. "main_db"
    std::string type;       // postgres | mysql | mongo | redis
    std::string db_name;    // database / schema name
};

// ─────────────────────────────────────────────────────────────
//  SiteEntry — one managed website
// ─────────────────────────────────────────────────────────────
struct SiteEntry
{
    std::string domain;
    std::string fe_folder;
    std::string be_folder;
    std::string user;
    std::string be_type;    // node | python | go | java | php | static
    std::string run_cmd;
    unsigned int be_port  = 3000;
    std::string fe_build; // npm | vite | next | hugo | none
    unsigned int db_count = 0;
    std::vector<DbEntry> dbs;

    // Resource tier (PaaS upgrade)
    std::string db_max_cpu    = "250m";
    std::string db_max_memory = "256Mi";
    int         db_storage_gb = 5;
    std::string be_max_cpu    = "500m";
    std::string be_max_memory = "512Mi";

    // Runtime-populated after K8s deployment
    std::string be_cluster_host;
    unsigned int be_cluster_port = 0;
};

// ─────────────────────────────────────────────────────────────
//  SiteConfig — loads sites.conf; supports hot-reload
// ─────────────────────────────────────────────────────────────
class SiteConfig
{
public:
    SiteConfig() = default;

    // Load (or reload) from file. Thread-safe.
    void load(const std::string& file_path);

    // Return a snapshot of all sites (thread-safe copy)
    std::vector<SiteEntry> sites() const;

    // Find a site by domain name (returns nullptr if not found)
    // Caller must hold no lock; returns copy.
    bool findSite(const std::string& domain, SiteEntry& out) const;

    // Persist a new or updated SiteEntry to the file
    void writeSite(const std::string& file_path, const SiteEntry& site);

private:
    mutable std::shared_mutex mutex_;
    std::vector<SiteEntry> sites_;
};

extern SiteConfig site_config;

#endif // SITE_CONFIG_HPP
