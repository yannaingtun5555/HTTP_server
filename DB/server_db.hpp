#ifndef SERVER_DB_HPP
#define SERVER_DB_HPP

#include <string>
#include <vector>
#include <memory>
#include <pqxx/pqxx>

// Forward-declare the record structs to avoid including site_config.hpp
// in every TU that just needs DB access.
struct DomainRecord
{
    int         id          = 0;
    std::string domain;
    std::string user_owner;
    std::string fe_folder;
    std::string be_folder;
    std::string be_type;
    std::string run_cmd;
    int         be_port     = 0;
    std::string fe_build;
    int         db_count    = 0;
    std::string status;     // pending | building | running | error
    std::string error_msg;
};

struct DbContainerRecord
{
    int         id          = 0;
    int         domain_id   = 0;
    std::string db_alias;
    std::string db_type;
    std::string db_name;
    std::string k8s_namespace;
    std::string k8s_deployment;
    std::string k8s_service;
    std::string cluster_ip;
    int         port        = 0;
    std::string connection_str;
    std::string status;
    std::string error_msg;
};

struct BeContainerRecord
{
    int         id          = 0;
    int         domain_id   = 0;
    std::string image_tag;
    std::string k8s_namespace;
    std::string k8s_deployment;
    std::string k8s_service;
    std::string be_host;
    int         be_port     = 0;
    std::string status;
    std::string error_log;
};

// ─────────────────────────────────────────────────────────────
//  ServerDB — wraps a libpqxx connection; provides CRUD
// ─────────────────────────────────────────────────────────────
class ServerDB
{
public:
    // Connect and run schema.sql if tables are missing
    bool connect(const std::string& conn_string, const std::string& schema_sql_path);
    void disconnect();

    // ── domains ──────────────────────────────────────────────
    int  insertDomain(const DomainRecord& d);
    bool updateDomainStatus(int id, const std::string& status, const std::string& error = "");
    bool getDomain(const std::string& domain, DomainRecord& out);
    std::vector<DomainRecord> allDomains();
    bool deleteDomain(int id);

    // ── db_containers ─────────────────────────────────────────
    int  insertDbContainer(const DbContainerRecord& r);
    bool updateDbContainer(const DbContainerRecord& r);
    std::vector<DbContainerRecord> dbContainersForDomain(int domain_id);

    // ── be_containers ─────────────────────────────────────────
    int  insertBeContainer(const BeContainerRecord& r);
    bool updateBeContainer(const BeContainerRecord& r);
    bool getBeContainer(int domain_id, BeContainerRecord& out);

private:
    std::unique_ptr<pqxx::connection> conn_;

    void runSchemaFile(const std::string& path);
    std::string readFile(const std::string& path);
};

extern ServerDB server_db;

#endif // SERVER_DB_HPP
