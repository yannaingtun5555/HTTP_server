#include "server_db.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

// Global instance
ServerDB server_db;

// ─────────────────────────────────────────────────────────────
std::string ServerDB::readFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void ServerDB::runSchemaFile(const std::string& path)
{
    std::string sql = readFile(path);
    if (sql.empty())
    {
        std::cerr << "[ServerDB] Schema file not found: " << path << "\n";
        return;
    }
    try
    {
        pqxx::work txn(*conn_);
        txn.exec(sql);
        txn.commit();
        std::cout << "[ServerDB] Schema applied from " << path << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] Schema error: " << e.what() << "\n";
    }
}

// ─────────────────────────────────────────────────────────────
bool ServerDB::connect(const std::string& conn_string,
                       const std::string& schema_sql_path)
{
    try
    {
        conn_ = std::make_unique<pqxx::connection>(conn_string);
        if (!conn_->is_open())
        {
            std::cerr << "[ServerDB] Connection failed\n";
            return false;
        }
        std::cout << "[ServerDB] Connected to " << conn_->dbname() << "\n";
        runSchemaFile(schema_sql_path);
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] connect() error: " << e.what() << "\n";
        return false;
    }
}

void ServerDB::disconnect()
{
    conn_.reset();
}

// ─────────────────────────────────────────────────────────────
//  domains
// ─────────────────────────────────────────────────────────────
int ServerDB::insertDomain(const DomainRecord& d)
{
    try
    {
        pqxx::work txn(*conn_);
        auto r = txn.exec_params1(
            "INSERT INTO domains(domain,user_owner,fe_folder,be_folder,"
            "be_type,run_cmd,be_port,fe_build,db_count,status)"
            " VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10) RETURNING id",
            d.domain, d.user_owner, d.fe_folder, d.be_folder,
            d.be_type, d.run_cmd, d.be_port, d.fe_build,
            d.db_count, d.status.empty() ? "pending" : d.status);
        txn.commit();
        return r[0].as<int>();
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] insertDomain: " << e.what() << "\n";
        return -1;
    }
}

bool ServerDB::updateDomainStatus(int id, const std::string& status,
                                  const std::string& error)
{
    try
    {
        pqxx::work txn(*conn_);
        txn.exec_params(
            "UPDATE domains SET status=$1, error_msg=$2 WHERE id=$3",
            status, error, id);
        txn.commit();
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] updateDomainStatus: " << e.what() << "\n";
        return false;
    }
}

bool ServerDB::getDomain(const std::string& domain, DomainRecord& out)
{
    try
    {
        pqxx::work txn(*conn_);
        auto r = txn.exec_params(
            "SELECT id,domain,user_owner,fe_folder,be_folder,be_type,"
            "run_cmd,be_port,fe_build,db_count,status,error_msg"
            " FROM domains WHERE domain=$1", domain);
        txn.commit();
        if (r.empty()) return false;
        const auto& row = r[0];
        out.id         = row[0].as<int>();
        out.domain     = row[1].as<std::string>();
        out.user_owner = row[2].is_null() ? "" : row[2].as<std::string>();
        out.fe_folder  = row[3].is_null() ? "" : row[3].as<std::string>();
        out.be_folder  = row[4].is_null() ? "" : row[4].as<std::string>();
        out.be_type    = row[5].is_null() ? "" : row[5].as<std::string>();
        out.run_cmd    = row[6].is_null() ? "" : row[6].as<std::string>();
        out.be_port    = row[7].is_null() ? 0  : row[7].as<int>();
        out.fe_build   = row[8].is_null() ? "" : row[8].as<std::string>();
        out.db_count   = row[9].is_null() ? 0  : row[9].as<int>();
        out.status     = row[10].is_null()? "" : row[10].as<std::string>();
        out.error_msg  = row[11].is_null()? "" : row[11].as<std::string>();
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] getDomain: " << e.what() << "\n";
        return false;
    }
}

std::vector<DomainRecord> ServerDB::allDomains()
{
    std::vector<DomainRecord> result;
    try
    {
        pqxx::work txn(*conn_);
        auto rows = txn.exec(
            "SELECT id,domain,user_owner,fe_folder,be_folder,be_type,"
            "run_cmd,be_port,fe_build,db_count,status,error_msg"
            " FROM domains ORDER BY id");
        txn.commit();
        for (const auto& row : rows)
        {
            DomainRecord d;
            d.id         = row[0].as<int>();
            d.domain     = row[1].as<std::string>();
            d.user_owner = row[2].is_null() ? "" : row[2].as<std::string>();
            d.fe_folder  = row[3].is_null() ? "" : row[3].as<std::string>();
            d.be_folder  = row[4].is_null() ? "" : row[4].as<std::string>();
            d.be_type    = row[5].is_null() ? "" : row[5].as<std::string>();
            d.run_cmd    = row[6].is_null() ? "" : row[6].as<std::string>();
            d.be_port    = row[7].is_null() ? 0  : row[7].as<int>();
            d.fe_build   = row[8].is_null() ? "" : row[8].as<std::string>();
            d.db_count   = row[9].is_null() ? 0  : row[9].as<int>();
            d.status     = row[10].is_null()? "" : row[10].as<std::string>();
            d.error_msg  = row[11].is_null()? "" : row[11].as<std::string>();
            result.push_back(d);
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] allDomains: " << e.what() << "\n";
    }
    return result;
}

bool ServerDB::deleteDomain(int id)
{
    try
    {
        pqxx::work txn(*conn_);
        txn.exec_params("DELETE FROM domains WHERE id=$1", id);
        txn.commit();
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] deleteDomain: " << e.what() << "\n";
        return false;
    }
}

// ─────────────────────────────────────────────────────────────
//  db_containers
// ─────────────────────────────────────────────────────────────
int ServerDB::insertDbContainer(const DbContainerRecord& r)
{
    try
    {
        pqxx::work txn(*conn_);
        auto row = txn.exec_params1(
            "INSERT INTO db_containers(domain_id,db_alias,db_type,db_name,"
            "k8s_namespace,k8s_deployment,k8s_service,status)"
            " VALUES($1,$2,$3,$4,$5,$6,$7,$8) RETURNING id",
            r.domain_id, r.db_alias, r.db_type, r.db_name,
            r.k8s_namespace, r.k8s_deployment, r.k8s_service,
            r.status.empty() ? "pending" : r.status);
        txn.commit();
        return row[0].as<int>();
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] insertDbContainer: " << e.what() << "\n";
        return -1;
    }
}

bool ServerDB::updateDbContainer(const DbContainerRecord& r)
{
    try
    {
        pqxx::work txn(*conn_);
        txn.exec_params(
            "UPDATE db_containers SET cluster_ip=$1, port=$2,"
            " connection_str=$3, status=$4, error_msg=$5,"
            " k8s_service=$6 WHERE id=$7",
            r.cluster_ip, r.port, r.connection_str,
            r.status, r.error_msg, r.k8s_service, r.id);
        txn.commit();
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] updateDbContainer: " << e.what() << "\n";
        return false;
    }
}

std::vector<DbContainerRecord> ServerDB::dbContainersForDomain(int domain_id)
{
    std::vector<DbContainerRecord> result;
    try
    {
        pqxx::work txn(*conn_);
        auto rows = txn.exec_params(
            "SELECT id,domain_id,db_alias,db_type,db_name,k8s_namespace,"
            "k8s_deployment,k8s_service,cluster_ip,port,connection_str,status,error_msg"
            " FROM db_containers WHERE domain_id=$1", domain_id);
        txn.commit();
        for (const auto& row : rows)
        {
            DbContainerRecord r;
            r.id             = row[0].as<int>();
            r.domain_id      = row[1].as<int>();
            r.db_alias       = row[2].is_null() ? "" : row[2].as<std::string>();
            r.db_type        = row[3].is_null() ? "" : row[3].as<std::string>();
            r.db_name        = row[4].is_null() ? "" : row[4].as<std::string>();
            r.k8s_namespace  = row[5].is_null() ? "" : row[5].as<std::string>();
            r.k8s_deployment = row[6].is_null() ? "" : row[6].as<std::string>();
            r.k8s_service    = row[7].is_null() ? "" : row[7].as<std::string>();
            r.cluster_ip     = row[8].is_null() ? "" : row[8].as<std::string>();
            r.port           = row[9].is_null() ? 0  : row[9].as<int>();
            r.connection_str = row[10].is_null()? "" : row[10].as<std::string>();
            r.status         = row[11].is_null()? "" : row[11].as<std::string>();
            r.error_msg      = row[12].is_null()? "" : row[12].as<std::string>();
            result.push_back(r);
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] dbContainersForDomain: " << e.what() << "\n";
    }
    return result;
}

// ─────────────────────────────────────────────────────────────
//  be_containers
// ─────────────────────────────────────────────────────────────
int ServerDB::insertBeContainer(const BeContainerRecord& r)
{
    try
    {
        pqxx::work txn(*conn_);
        auto row = txn.exec_params1(
            "INSERT INTO be_containers(domain_id,image_tag,k8s_namespace,"
            "k8s_deployment,k8s_service,be_port,status)"
            " VALUES($1,$2,$3,$4,$5,$6,$7) RETURNING id",
            r.domain_id, r.image_tag, r.k8s_namespace,
            r.k8s_deployment, r.k8s_service, r.be_port,
            r.status.empty() ? "pending" : r.status);
        txn.commit();
        return row[0].as<int>();
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] insertBeContainer: " << e.what() << "\n";
        return -1;
    }
}

bool ServerDB::updateBeContainer(const BeContainerRecord& r)
{
    try
    {
        pqxx::work txn(*conn_);
        txn.exec_params(
            "UPDATE be_containers SET be_host=$1, be_port=$2, status=$3,"
            " error_log=$4, deployed_at=NOW(), k8s_service=$5 WHERE id=$6",
            r.be_host, r.be_port, r.status, r.error_log, r.k8s_service, r.id);
        txn.commit();
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] updateBeContainer: " << e.what() << "\n";
        return false;
    }
}

bool ServerDB::getBeContainer(int domain_id, BeContainerRecord& out)
{
    try
    {
        pqxx::work txn(*conn_);
        auto rows = txn.exec_params(
            "SELECT id,domain_id,image_tag,k8s_namespace,k8s_deployment,"
            "k8s_service,be_host,be_port,status,error_log"
            " FROM be_containers WHERE domain_id=$1"
            " ORDER BY id DESC LIMIT 1", domain_id);
        txn.commit();
        if (rows.empty()) return false;
        const auto& row = rows[0];
        out.id             = row[0].as<int>();
        out.domain_id      = row[1].as<int>();
        out.image_tag      = row[2].is_null() ? "" : row[2].as<std::string>();
        out.k8s_namespace  = row[3].is_null() ? "" : row[3].as<std::string>();
        out.k8s_deployment = row[4].is_null() ? "" : row[4].as<std::string>();
        out.k8s_service    = row[5].is_null() ? "" : row[5].as<std::string>();
        out.be_host        = row[6].is_null() ? "" : row[6].as<std::string>();
        out.be_port        = row[7].is_null() ? 0  : row[7].as<int>();
        out.status         = row[8].is_null() ? "" : row[8].as<std::string>();
        out.error_log      = row[9].is_null() ? "" : row[9].as<std::string>();
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ServerDB] getBeContainer: " << e.what() << "\n";
        return false;
    }
}
