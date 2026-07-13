#include "site_config.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <map>
#include <mutex>
#include <shared_mutex>

// Global instance
SiteConfig site_config;

// ─────────────────────────────────────────────────────────────
static std::string trim(const std::string& s)
{
    auto b = s.find_first_not_of(" \t\r\n");
    auto e = s.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
}

// ─────────────────────────────────────────────────────────────
void SiteConfig::load(const std::string& file_path)
{
    std::ifstream f(file_path);
    if (!f.is_open())
    {
        std::cerr << "[SiteConfig] Cannot open " << file_path << "\n";
        return;
    }

    // Temporary accumulator: domain -> field -> value
    std::map<std::string, std::map<std::string, std::string>> raw;
    // domain -> db_index -> field -> value
    std::map<std::string, std::map<int, std::map<std::string, std::string>>> raw_dbs;

    std::string line;
    while (std::getline(f, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        // Expected key pattern: site.<domain>.<field>
        //                    or site.<domain>.db.<N>.<field>
        if (key.rfind("site.", 0) != 0) continue;
        std::string rest = key.substr(5); // strip "site."

        auto db_pos = rest.rfind(".db.");
        if (db_pos != std::string::npos)
        {
            // site.<domain>.db.<N>.<field>
            std::string domain = rest.substr(0, db_pos);
            std::string db_rest = rest.substr(db_pos + 4); // strip ".db."
            auto d2 = db_rest.find('.');
            if (d2 == std::string::npos) continue;
            int idx = std::stoi(db_rest.substr(0, d2));
            std::string db_field = db_rest.substr(d2 + 1);
            raw_dbs[domain][idx][db_field] = val;
        }
        else
        {
            auto d1 = rest.rfind('.');
            if (d1 == std::string::npos) continue;
            std::string domain = rest.substr(0, d1);
            std::string remainder = rest.substr(d1 + 1);
            raw[domain][remainder] = val;
        }
    }

    // Build SiteEntry list
    std::vector<SiteEntry> new_sites;
    for (auto& [domain, fields] : raw)
    {
        SiteEntry s;
        s.domain = domain;
        if (fields.count("fe_folder")) s.fe_folder = fields["fe_folder"];
        if (fields.count("be_folder")) s.be_folder = fields["be_folder"];
        if (fields.count("user"))      s.user      = fields["user"];
        if (fields.count("domain"))    s.domain    = fields["domain"];
        if (fields.count("be_type"))   s.be_type   = fields["be_type"];
        if (fields.count("run_cmd"))   s.run_cmd   = fields["run_cmd"];
        if (fields.count("be_port"))   s.be_port   = std::stoi(fields["be_port"]);
        if (fields.count("fe_build"))  s.fe_build  = fields["fe_build"];
        if (fields.count("db_count"))  s.db_count  = std::stoul(fields["db_count"]);
        if (fields.count("be_cluster_host")) s.be_cluster_host = fields["be_cluster_host"];
        if (fields.count("be_cluster_port"))  s.be_cluster_port = std::stoul(fields["be_cluster_port"]);

        // Attach DB entries
        if (raw_dbs.count(domain))
        {
            for (auto& [idx, db_fields] : raw_dbs[domain])
            {
                DbEntry db;
                if (db_fields.count("name"))    db.alias   = db_fields["name"];
                if (db_fields.count("type"))    db.type    = db_fields["type"];
                if (db_fields.count("db_name")) db.db_name = db_fields["db_name"];
                s.dbs.push_back(db);
            }
        }

        new_sites.push_back(std::move(s));
    }

    {
        std::unique_lock lock(mutex_);
        sites_ = std::move(new_sites);
    }
    std::cout << "[SiteConfig] Loaded " << sites_.size() << " site(s) from " << file_path << "\n";
}

// ─────────────────────────────────────────────────────────────
std::vector<SiteEntry> SiteConfig::sites() const
{
    std::shared_lock lock(mutex_);
    return sites_;
}

// ─────────────────────────────────────────────────────────────
bool SiteConfig::findSite(const std::string& domain, SiteEntry& out) const
{
    std::shared_lock lock(mutex_);
    for (const auto& s : sites_)
    {
        if (s.domain == domain) { out = s; return true; }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
void SiteConfig::writeSite(const std::string& file_path, const SiteEntry& site)
{
    std::vector<SiteEntry> snapshot;
    {
        std::shared_lock lock(mutex_);
        snapshot = sites_;
    }

    bool replaced = false;
    for (auto& s : snapshot)
    {
        if (s.domain == site.domain)
        {
            s = site;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        snapshot.push_back(site);

    std::ofstream f(file_path, std::ios::trunc);
    if (!f.is_open())
    {
        std::cerr << "[SiteConfig] Cannot write to " << file_path << "\n";
        return;
    }

    f << "# Auto-generated by server\n";
    for (const auto& s : snapshot)
    {
        f << "\nsite." << s.domain << ".fe_folder=" << s.fe_folder << "\n";
        f << "site." << s.domain << ".be_folder=" << s.be_folder << "\n";
        f << "site." << s.domain << ".user="      << s.user      << "\n";
        f << "site." << s.domain << ".domain="    << s.domain    << "\n";
        f << "site." << s.domain << ".be_type="   << s.be_type   << "\n";
        f << "site." << s.domain << ".run_cmd="   << s.run_cmd   << "\n";
        f << "site." << s.domain << ".be_port="   << s.be_port   << "\n";
        f << "site." << s.domain << ".fe_build="  << s.fe_build  << "\n";
        f << "site." << s.domain << ".db_count="  << s.dbs.size() << "\n";
        if (!s.be_cluster_host.empty())
            f << "site." << s.domain << ".be_cluster_host=" << s.be_cluster_host << "\n";
        if (s.be_cluster_port != 0)
            f << "site." << s.domain << ".be_cluster_port=" << s.be_cluster_port << "\n";

        for (std::size_t i = 0; i < s.dbs.size(); ++i)
        {
            const auto& db = s.dbs[i];
            f << "site." << s.domain << ".db." << i << ".name="    << db.alias   << "\n";
            f << "site." << s.domain << ".db." << i << ".type="    << db.type    << "\n";
            f << "site." << s.domain << ".db." << i << ".db_name=" << db.db_name << "\n";
        }
    }

    // Update in-memory list
    std::unique_lock lock(mutex_);
    sites_ = std::move(snapshot);
}
