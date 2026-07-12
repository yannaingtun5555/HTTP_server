#include "internal_api.hpp"
#include "../../Configuration/server_config.hpp"
#include "../../Configuration/site_config.hpp"
#include "../../DB/server_db.hpp"
#include "../../K8s/k8s_controller.hpp"
#include "../../FrontEnd/fe_builder.hpp"
#include "../../Thread/thread.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <json/json.h>
#include <iostream>
#include <filesystem>
#include <map>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace fs    = std::filesystem;

// ─────────────────────────────────────────────────────────────
//  makeJson helper
// ─────────────────────────────────────────────────────────────
http::response<http::string_body>
InternalApiHandler::makeJson(unsigned status, const std::string& body,
                              unsigned http_version, bool keep_alive)
{
    http::response<http::string_body> res;
    res.version(http_version);
    res.result(status);
    res.set(http::field::server, "HTTP_Server/InternalAPI");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(keep_alive);
    res.body() = body;
    res.prepare_payload();
    return res;
}

// ─────────────────────────────────────────────────────────────
//  handle — dispatch entry point
// ─────────────────────────────────────────────────────────────
http::response<http::string_body>
InternalApiHandler::handle(const http::request<http::dynamic_body>& req,
                            const std::string& path_suffix,
                            const std::string& expected_secret)
{
    unsigned ver  = req.version();
    bool keep     = req.keep_alive();

    // ── Auth check ───────────────────────────────────────────
    auto secret_it = req.find("X-Internal-Secret");
    if (secret_it == req.end() ||
        std::string(secret_it->value()) != expected_secret)
    {
        return makeJson(403, R"({"success":false,"error":"Forbidden"})", ver, keep);
    }

    // ── Route ─────────────────────────────────────────────────
    std::string method(req.method_string());

    if (path_suffix == "/deploy" && method == "POST")
        return handleDeploy(req, ver, keep);

    if (path_suffix.rfind("/status/", 0) == 0 && method == "GET")
        return handleStatus(path_suffix.substr(8), ver, keep);

    if (path_suffix.rfind("/delete/", 0) == 0 && method == "DELETE")
        return handleDelete(path_suffix.substr(8), ver, keep);

    if (path_suffix == "/reload" && method == "POST")
        return handleReload(ver, keep);

    return makeJson(404, R"({"success":false,"error":"Not found"})", ver, keep);
}

// ─────────────────────────────────────────────────────────────
//  handleDeploy
//  Expected JSON body:
//  {
//    "domain": "example.com",
//    "user_owner": "alice",
//    "fe_folder": "/path/to/fe",
//    "be_folder": "/path/to/be",
//    "be_type": "node",
//    "run_cmd": "node index.js",
//    "be_port": 3000,
//    "fe_build": "npm",
//    "dbs": [
//      {"db_alias":"main","db_type":"postgres","db_name":"mydb",
//       "db_user":"u","db_password":"p"}
//    ]
//  }
// ─────────────────────────────────────────────────────────────
http::response<http::string_body>
InternalApiHandler::handleDeploy(const http::request<http::dynamic_body>& req,
                                  unsigned http_version, bool keep_alive)
{
    // Parse JSON body
    std::string body_str = beast::buffers_to_string(req.body().data());
    Json::Value  root;
    Json::Reader reader;
    if (!reader.parse(body_str, root))
    {
        return makeJson(400,
            R"({"success":false,"error":"Invalid JSON body"})",
            http_version, keep_alive);
    }

    // Build SiteEntry
    SiteEntry site;
    site.domain    = root["domain"].asString();
    site.user      = root.get("user_owner", "").asString();
    site.fe_folder = root["fe_folder"].asString();
    site.be_folder = root["be_folder"].asString();
    site.be_type   = root["be_type"].asString();
    site.run_cmd   = root.get("run_cmd", "").asString();
    site.be_port   = root.get("be_port", 3000).asUInt();
    site.fe_build  = root.get("fe_build", "none").asString();

    // Build DB entries and credential map
    std::vector<std::pair<std::string,std::string>> db_creds; // user, password per DB
    const Json::Value& dbs_json = root["dbs"];
    for (const auto& d : dbs_json)
    {
        DbEntry db;
        db.alias   = d.get("db_alias", "").asString();
        db.type    = d.get("db_type", "postgres").asString();
        db.db_name = d.get("db_name", "").asString();
        site.dbs.push_back(db);
        db_creds.push_back({
            d.get("db_user", "admin").asString(),
            d.get("db_password", "changeme").asString()
        });
    }

    std::string error_out;
    std::string conn_json;
    bool ok = runDeploy(site, db_creds, error_out, conn_json);

    if (ok)
    {
        return makeJson(200,
            R"({"success":true,"connection_strings":)" + conn_json + R"(})",
            http_version, keep_alive);
    }
    else
    {
        // Escape error string for JSON
        Json::Value err_val(error_out);
        Json::FastWriter w;
        std::string err_json = w.write(err_val);
        err_json.erase(std::remove(err_json.begin(), err_json.end(), '\n'), err_json.end());
        return makeJson(500,
            R"({"success":false,"error":)" + err_json + R"(})",
            http_version, keep_alive);
    }
}

// ─────────────────────────────────────────────────────────────
//  runDeploy — full deployment pipeline
// ─────────────────────────────────────────────────────────────
bool InternalApiHandler::runDeploy(
    const SiteEntry& site,
    const std::vector<std::pair<std::string,std::string>>& db_creds,
    std::string& error_out,
    std::string& conn_strings_json_out)
{
    // 1) Insert domain into server DB
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
    dr.status     = "building";

    int domain_id = server_db.insertDomain(dr);
    if (domain_id < 0)
    {
        error_out = "Failed to insert domain into server DB";
        return false;
    }

    // 2) Build FE (background thread, but we wait via a future since
    //    the deploy response needs to know the result)
    std::string public_dir = server_config.sites_root + "/" + site.domain + "/public";
    std::string fe_error;
    bool fe_ok = FrontendBuilder::build(
        site.domain, site.fe_folder, public_dir, site.fe_build, fe_error);
    if (!fe_ok)
    {
        server_db.updateDomainStatus(domain_id, "error", "FE build failed: " + fe_error);
        error_out = "FE build failed: " + fe_error;
        return false;
    }

    // 3) Spin up DB containers one by one
    std::map<std::string, std::string> conn_env; // env-var-name → conn string
    Json::Value conn_arr(Json::arrayValue);

    for (std::size_t i = 0; i < site.dbs.size(); ++i)
    {
        const DbEntry& db   = site.dbs[i];
        const auto& [u, p]  = db_creds[i];

        DbContainerRecord dbc;
        dbc.domain_id  = domain_id;
        dbc.db_alias   = db.alias;
        dbc.db_type    = db.type;
        dbc.db_name    = db.db_name;
        dbc.status     = "pending";
        dbc.id         = server_db.insertDbContainer(dbc);

        std::string conn = k8s_controller.spinUpDbContainer(
            site.domain, db, u, p, dbc);

        if (conn.empty())
        {
            server_db.updateDbContainer(dbc);
            server_db.updateDomainStatus(domain_id, "error",
                "DB container failed: " + db.alias);
            error_out = "DB container failed to start: " + db.alias +
                        " — " + dbc.error_msg;
            return false;
        }

        server_db.updateDbContainer(dbc);

        // Map to env var: e.g. "main_db" → "MAIN_DB_URL"
        std::string env_key = db.alias;
        for (auto& c : env_key) c = std::toupper(c);
        env_key += "_URL";
        conn_env[env_key] = conn;

        Json::Value entry;
        entry["alias"]      = db.alias;
        entry["conn_str"]   = conn;
        conn_arr.append(entry);
    }

    // 4) Spin up BE container
    BeContainerRecord bec;
    bec.domain_id = domain_id;
    bec.be_port   = static_cast<int>(site.be_port);
    bec.status    = "pending";
    bec.id        = server_db.insertBeContainer(bec);

    std::string pod_name = k8s_controller.spinUpBeContainer(site, conn_env, bec);
    if (pod_name.empty())
    {
        server_db.updateBeContainer(bec);
        server_db.updateDomainStatus(domain_id, "error",
            "BE container failed: " + bec.error_log);
        error_out = "BE container failed to start:\n" + bec.error_log;
        return false;
    }

    server_db.updateBeContainer(bec);

    // 5) Update in-memory site routing (set cluster BE host/port)
    SiteEntry updated = site;
    updated.be_cluster_host = bec.be_host;
    updated.be_cluster_port = static_cast<unsigned int>(bec.be_port);
    site_config.writeSite("sites.conf", updated);

    // 6) Mark domain running
    server_db.updateDomainStatus(domain_id, "running");

    // 7) Write connection strings JSON output
    Json::FastWriter w;
    conn_strings_json_out = w.write(conn_arr);
    conn_strings_json_out.erase(
        std::remove(conn_strings_json_out.begin(), conn_strings_json_out.end(), '\n'),
        conn_strings_json_out.end());

    return true;
}

// ─────────────────────────────────────────────────────────────
//  handleStatus
// ─────────────────────────────────────────────────────────────
http::response<http::string_body>
InternalApiHandler::handleStatus(const std::string& domain,
                                  unsigned http_version, bool keep_alive)
{
    DomainRecord dr;
    if (!server_db.getDomain(domain, dr))
    {
        return makeJson(404, R"({"error":"domain not found"})", http_version, keep_alive);
    }

    BeContainerRecord bec;
    server_db.getBeContainer(dr.id, bec);
    auto db_records = server_db.dbContainersForDomain(dr.id);

    Json::Value root;
    root["status"]    = dr.status;
    root["error_msg"] = dr.error_msg;
    root["be"]["status"]    = bec.status;
    root["be"]["error_log"] = bec.error_log;
    root["be"]["host"]      = bec.be_host;
    root["be"]["port"]      = bec.be_port;

    Json::Value dbs(Json::arrayValue);
    for (const auto& d : db_records)
    {
        Json::Value entry;
        entry["alias"]      = d.db_alias;
        entry["status"]     = d.status;
        entry["conn_str"]   = d.connection_str;
        dbs.append(entry);
    }
    root["dbs"] = dbs;

    Json::FastWriter w;
    std::string out = w.write(root);
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    return makeJson(200, out, http_version, keep_alive);
}

// ─────────────────────────────────────────────────────────────
//  handleDelete
// ─────────────────────────────────────────────────────────────
http::response<http::string_body>
InternalApiHandler::handleDelete(const std::string& domain,
                                  unsigned http_version, bool keep_alive)
{
    DomainRecord dr;
    if (!server_db.getDomain(domain, dr))
    {
        return makeJson(404, R"({"success":false,"error":"domain not found"})",
                        http_version, keep_alive);
    }

    // Build K8s namespace name
    std::string safe_domain = domain;
    for (auto& c : safe_domain) if (c == '.') c = '-';
    std::string ns = "site-" + safe_domain;

    k8s_controller.deleteNamespace(ns);
    server_db.deleteDomain(dr.id);

    return makeJson(200, R"({"success":true})", http_version, keep_alive);
}

// ─────────────────────────────────────────────────────────────
//  handleReload
// ─────────────────────────────────────────────────────────────
http::response<http::string_body>
InternalApiHandler::handleReload(unsigned http_version, bool keep_alive)
{
    site_config.load("sites.conf");
    std::cout << "[InternalAPI] sites.conf hot-reloaded\n";
    return makeJson(200, R"({"success":true,"message":"sites.conf reloaded"})",
                    http_version, keep_alive);
}
