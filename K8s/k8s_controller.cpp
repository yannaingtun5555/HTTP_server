#include "k8s_controller.hpp"
#include <curl/curl.h>
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>

// Global instance
K8sController k8s_controller;

// ─────────────────────────────────────────────────────────────
//  libcurl write callback
// ─────────────────────────────────────────────────────────────
static size_t curlWriteCb(char* ptr, size_t size, size_t nmemb, std::string* out)
{
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// ─────────────────────────────────────────────────────────────
void K8sController::init(const std::string& api_server,
                          const std::string& kubeconfig_path)
{
    api_server_  = api_server;
    kubeconfig_  = kubeconfig_path;
    parseKubeconfig();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::cout << "[K8sController] Initialized for " << api_server_ << "\n";
}

// ─────────────────────────────────────────────────────────────
//  parseKubeconfig — extracts Bearer token and CA cert path
//  from a standard kubeadm-generated kubeconfig YAML.
//  We use a simple line-by-line parser (avoids a YAML dep).
// ─────────────────────────────────────────────────────────────
void K8sController::parseKubeconfig()
{
    std::ifstream f(kubeconfig_);
    if (!f.is_open())
    {
        std::cerr << "[K8sController] Cannot open kubeconfig: " << kubeconfig_ << "\n";
        return;
    }
    std::string line;
    while (std::getline(f, line))
    {
        auto trim = [](std::string s) {
            auto b = s.find_first_not_of(" \t");
            return (b == std::string::npos) ? "" : s.substr(b);
        };
        std::string t = trim(line);
        if (t.rfind("token:", 0) == 0)
            token_ = trim(t.substr(6));
        else if (t.rfind("certificate-authority:", 0) == 0)
            ca_cert_path_ = trim(t.substr(22));
    }
}

// ─────────────────────────────────────────────────────────────
//  libcurl helper — GET
// ─────────────────────────────────────────────────────────────
std::string K8sController::curlGet(const std::string& url)
{
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    std::string auth = "Authorization: Bearer " + token_;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if (!ca_cert_path_.empty())
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert_path_.c_str());
    else
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

// ─────────────────────────────────────────────────────────────
//  libcurl helper — POST (application/json)
// ─────────────────────────────────────────────────────────────
std::string K8sController::curlPost(const std::string& url,
                                     const std::string& body)
{
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    std::string auth = "Authorization: Bearer " + token_;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if (!ca_cert_path_.empty())
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert_path_.c_str());
    else
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

// ─────────────────────────────────────────────────────────────
//  libcurl helper — DELETE
// ─────────────────────────────────────────────────────────────
std::string K8sController::curlDelete(const std::string& url)
{
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    std::string auth = "Authorization: Bearer " + token_;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    if (!ca_cert_path_.empty())
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert_path_.c_str());
    else
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

// ─────────────────────────────────────────────────────────────
//  ensureNamespace
// ─────────────────────────────────────────────────────────────
bool K8sController::ensureNamespace(const std::string& ns)
{
    // Check if already exists
    std::string check = curlGet(api_server_ + "/api/v1/namespaces/" + ns);
    if (check.find("\"name\":\"" + ns + "\"") != std::string::npos)
        return true;

    std::string body = R"({"apiVersion":"v1","kind":"Namespace","metadata":{"name":")" + ns + R"("}})";
    std::string resp = curlPost(api_server_ + "/api/v1/namespaces", body);
    bool ok = (resp.find("\"name\":\"" + ns + "\"") != std::string::npos);
    std::cout << "[K8sController] Namespace " << ns << (ok ? " created" : " failed") << "\n";
    return ok;
}

// ─────────────────────────────────────────────────────────────
//  DB image / port / env-var mapping
// ─────────────────────────────────────────────────────────────
std::string K8sController::dbImage(const std::string& db_type)
{
    if (db_type == "postgres") return "postgres:15-alpine";
    if (db_type == "mysql")    return "mysql:8";
    if (db_type == "mongo")    return "mongo:7";
    if (db_type == "redis")    return "redis:7-alpine";
    return "postgres:15-alpine";
}

int K8sController::dbPort(const std::string& db_type)
{
    if (db_type == "postgres") return 5432;
    if (db_type == "mysql")    return 3306;
    if (db_type == "mongo")    return 27017;
    if (db_type == "redis")    return 6379;
    return 5432;
}

std::string K8sController::dbEnvVar(const std::string& alias)
{
    // Converts alias "main_db" → "MAIN_DB_URL"
    std::string r = alias;
    for (auto& c : r) c = std::toupper(c);
    return r + "_URL";
}

// ─────────────────────────────────────────────────────────────
//  buildDbDeploymentJson — minimal Deployment JSON for a DB pod
// ─────────────────────────────────────────────────────────────
std::string K8sController::buildDbDeploymentJson(const std::string& ns,
                                                   const std::string& name,
                                                   const DbEntry& db,
                                                   const std::string& db_user,
                                                   const std::string& db_password)
{
    int port = dbPort(db.type);
    std::string image = dbImage(db.type);

    // Build env vars depending on DB type
    std::string env_json;
    if (db.type == "postgres")
    {
        env_json = R"([
            {"name":"POSTGRES_USER","value":")" + db_user + R"("},
            {"name":"POSTGRES_PASSWORD","value":")" + db_password + R"("},
            {"name":"POSTGRES_DB","value":")" + db.db_name + R"("}
        ])";
    }
    else if (db.type == "mysql")
    {
        env_json = R"([
            {"name":"MYSQL_ROOT_PASSWORD","value":")" + db_password + R"("},
            {"name":"MYSQL_DATABASE","value":")" + db.db_name + R"("},
            {"name":"MYSQL_USER","value":")" + db_user + R"("},
            {"name":"MYSQL_PASSWORD","value":")" + db_password + R"("}
        ])";
    }
    else if (db.type == "mongo")
    {
        env_json = R"([
            {"name":"MONGO_INITDB_ROOT_USERNAME","value":")" + db_user + R"("},
            {"name":"MONGO_INITDB_ROOT_PASSWORD","value":")" + db_password + R"("},
            {"name":"MONGO_INITDB_DATABASE","value":")" + db.db_name + R"("}
        ])";
    }
    else
    {
        env_json = "[]";
    }

    return R"({
    "apiVersion": "apps/v1",
    "kind": "Deployment",
    "metadata": {"name": ")" + name + R"(", "namespace": ")" + ns + R"("},
    "spec": {
        "replicas": 1,
        "selector": {"matchLabels": {"app": ")" + name + R"("}},
        "template": {
            "metadata": {"labels": {"app": ")" + name + R"("}},
            "spec": {
                "containers": [{
                    "name": "db",
                    "image": ")" + image + R"(",
                    "ports": [{"containerPort": )" + std::to_string(port) + R"(}],
                    "env": )" + env_json + R"(
                }]
            }
        }
    }
})";
}

// ─────────────────────────────────────────────────────────────
//  buildServiceJson — ClusterIP Service JSON
// ─────────────────────────────────────────────────────────────
std::string K8sController::buildServiceJson(const std::string& ns,
                                             const std::string& name,
                                             int port,
                                             const std::string& selector_app)
{
    return R"({
    "apiVersion": "v1",
    "kind": "Service",
    "metadata": {"name": ")" + name + R"(", "namespace": ")" + ns + R"("},
    "spec": {
        "selector": {"app": ")" + selector_app + R"("},
        "ports": [{"protocol":"TCP","port":)" + std::to_string(port) +
               R"(,"targetPort":)" + std::to_string(port) + R"(}],
        "type": "ClusterIP"
    }
})";
}

// ─────────────────────────────────────────────────────────────
//  parseClusterIp
// ─────────────────────────────────────────────────────────────
std::string K8sController::parseClusterIp(const std::string& service_json)
{
    // Simple search: find "clusterIP":"<ip>"
    auto pos = service_json.find("\"clusterIP\"");
    if (pos == std::string::npos) return "";
    pos = service_json.find(":", pos);
    if (pos == std::string::npos) return "";
    pos = service_json.find("\"", pos);
    if (pos == std::string::npos) return "";
    pos++;
    auto end = service_json.find("\"", pos);
    return service_json.substr(pos, end - pos);
}

// ─────────────────────────────────────────────────────────────
//  waitForPodRunning — polls pod list until one is Running
// ─────────────────────────────────────────────────────────────
std::string K8sController::waitForPodRunning(const std::string& ns,
                                              const std::string& deployment_name,
                                              int timeout_secs)
{
    using namespace std::chrono;
    auto deadline = steady_clock::now() + seconds(timeout_secs);

    while (steady_clock::now() < deadline)
    {
        std::string url = api_server_ + "/api/v1/namespaces/" + ns +
                          "/pods?labelSelector=app%3D" + deployment_name;
        std::string resp = curlGet(url);

        // Look for "phase":"Running"
        if (resp.find("\"phase\":\"Running\"") != std::string::npos)
        {
            // Extract pod name
            auto np = resp.find("\"name\":\"");
            if (np != std::string::npos)
            {
                np += 8;
                auto ne = resp.find("\"", np);
                return resp.substr(np, ne - np);
            }
        }
        std::this_thread::sleep_for(seconds(3));
    }
    return "";
}

// ─────────────────────────────────────────────────────────────
//  spinUpDbContainer
// ─────────────────────────────────────────────────────────────
std::string K8sController::spinUpDbContainer(const std::string& domain,
                                              const DbEntry& db,
                                              const std::string& db_user,
                                              const std::string& db_password,
                                              DbContainerRecord& record_out,
                                              int timeout_secs)
{
    // Sanitise domain for K8s naming (replace dots with dashes)
    std::string safe_domain = domain;
    for (auto& c : safe_domain) if (c == '.') c = '-';

    std::string ns   = "site-" + safe_domain;
    std::string name = "db-" + safe_domain + "-" + db.alias;
    int port = dbPort(db.type);

    ensureNamespace(ns);

    // Create Deployment
    std::string dep_json = buildDbDeploymentJson(ns, name, db, db_user, db_password);
    std::string dep_url  = api_server_ + "/apis/apps/v1/namespaces/" + ns + "/deployments";
    std::string dep_resp = curlPost(dep_url, dep_json);
    std::cout << "[K8sController] Created DB deployment " << name << "\n";

    // Create Service
    std::string svc_json = buildServiceJson(ns, name, port, name);
    std::string svc_url  = api_server_ + "/api/v1/namespaces/" + ns + "/services";
    std::string svc_resp = curlPost(svc_url, svc_json);

    // Get ClusterIP
    std::string svc_get  = curlGet(api_server_ + "/api/v1/namespaces/" + ns + "/services/" + name);
    std::string cluster_ip = parseClusterIp(svc_get);

    // Wait for pod Running
    std::string pod_name = waitForPodRunning(ns, name, timeout_secs);

    record_out.k8s_namespace  = ns;
    record_out.k8s_deployment = name;
    record_out.k8s_service    = name;
    record_out.cluster_ip     = cluster_ip;
    record_out.port           = port;

    if (pod_name.empty())
    {
        record_out.status    = "error";
        record_out.error_msg = "Timeout waiting for DB pod to be Running";
        std::cerr << "[K8sController] " << record_out.error_msg << "\n";
        return "";
    }

    // Build connection string
    std::string conn_str;
    if (db.type == "postgres")
        conn_str = "postgresql://" + db_user + ":" + db_password + "@" +
                   cluster_ip + ":" + std::to_string(port) + "/" + db.db_name;
    else if (db.type == "mysql")
        conn_str = "mysql://" + db_user + ":" + db_password + "@" +
                   cluster_ip + ":" + std::to_string(port) + "/" + db.db_name;
    else if (db.type == "mongo")
        conn_str = "mongodb://" + db_user + ":" + db_password + "@" +
                   cluster_ip + ":" + std::to_string(port) + "/" + db.db_name;
    else if (db.type == "redis")
        conn_str = "redis://" + cluster_ip + ":" + std::to_string(port);

    record_out.connection_str = conn_str;
    record_out.status         = "running";
    std::cout << "[K8sController] DB pod " << pod_name << " running. Conn: " << conn_str << "\n";
    return conn_str;
}

// ─────────────────────────────────────────────────────────────
//  buildBeDeploymentJson
// ─────────────────────────────────────────────────────────────
std::string K8sController::buildBeDeploymentJson(const SiteEntry& site,
                                                  const std::string& ns,
                                                  const std::string& name,
                                                  const std::map<std::string, std::string>& env)
{
    // Choose base image based on be_type
    std::string image;
    if      (site.be_type == "node")   image = "node:20-alpine";
    else if (site.be_type == "python") image = "python:3.11-alpine";
    else if (site.be_type == "go")     image = "golang:1.22-alpine";
    else if (site.be_type == "java")   image = "eclipse-temurin:17-alpine";
    else if (site.be_type == "php")    image = "php:8.2-cli-alpine";
    else                               image = "ubuntu:22.04";

    // Build env array JSON
    std::string env_arr = "[";
    bool first = true;
    for (const auto& [k, v] : env)
    {
        if (!first) env_arr += ",";
        // Escape quotes in value
        std::string escaped_v = v;
        for (std::size_t i = 0; i < escaped_v.size(); ++i)
            if (escaped_v[i] == '"') { escaped_v.insert(i, "\\"); ++i; }
        env_arr += R"({"name":")" + k + R"(","value":")" + escaped_v + R"("})";
        first = false;
    }
    env_arr += "]";

    std::string run_cmd_json = R"(["/bin/sh","-c",")" + site.run_cmd + R"("])";

    return R"({
    "apiVersion": "apps/v1",
    "kind": "Deployment",
    "metadata": {"name": ")" + name + R"(", "namespace": ")" + ns + R"("},
    "spec": {
        "replicas": 1,
        "selector": {"matchLabels": {"app": ")" + name + R"("}},
        "template": {
            "metadata": {"labels": {"app": ")" + name + R"("}},
            "spec": {
                "containers": [{
                    "name": "be",
                    "image": ")" + image + R"(",
                    "command": )" + run_cmd_json + R"(,
                    "ports": [{"containerPort": )" + std::to_string(site.be_port) + R"(}],
                    "env": )" + env_arr + R"(,
                    "volumeMounts": [{
                        "name": "be-src",
                        "mountPath": "/app"
                    }]
                }],
                "volumes": [{
                    "name": "be-src",
                    "hostPath": {"path": ")" + site.be_folder + R"(", "type": "Directory"}
                }]
            }
        }
    }
})";
}

// ─────────────────────────────────────────────────────────────
//  spinUpBeContainer
// ─────────────────────────────────────────────────────────────
std::string K8sController::spinUpBeContainer(const SiteEntry& site,
                                              const std::map<std::string, std::string>& conn_env,
                                              BeContainerRecord& record_out,
                                              int timeout_secs)
{
    std::string safe_domain = site.domain;
    for (auto& c : safe_domain) if (c == '.') c = '-';

    std::string ns   = "site-" + safe_domain;
    std::string name = "be-" + safe_domain;

    ensureNamespace(ns);

    // Combine conn_env with working directory env var
    auto env = conn_env;
    env["APP_DIR"] = "/app";

    std::string dep_json = buildBeDeploymentJson(site, ns, name, env);
    std::string dep_url  = api_server_ + "/apis/apps/v1/namespaces/" + ns + "/deployments";
    curlPost(dep_url, dep_json);
    std::cout << "[K8sController] Created BE deployment " << name << "\n";

    // Create Service
    std::string svc_json = buildServiceJson(ns, name, site.be_port, name);
    std::string svc_url  = api_server_ + "/api/v1/namespaces/" + ns + "/services";
    std::string svc_resp = curlPost(svc_url, svc_json);

    // Get ClusterIP
    std::string svc_get    = curlGet(api_server_ + "/api/v1/namespaces/" + ns + "/services/" + name);
    std::string cluster_ip = parseClusterIp(svc_get);

    // Wait for pod Running
    std::string pod_name = waitForPodRunning(ns, name, timeout_secs);

    record_out.k8s_namespace  = ns;
    record_out.k8s_deployment = name;
    record_out.k8s_service    = name;
    record_out.be_host        = cluster_ip;
    record_out.be_port        = static_cast<int>(site.be_port);

    if (pod_name.empty())
    {
        // Collect logs from crashed pod if any
        record_out.status    = "error";
        record_out.error_log = getPodLogs(ns, name, 100);
        std::cerr << "[K8sController] BE pod failed to start for " << site.domain << "\n";
        return "";
    }

    record_out.status = "running";
    std::cout << "[K8sController] BE pod " << pod_name << " running at "
              << cluster_ip << ":" << site.be_port << "\n";
    return pod_name;
}

// ─────────────────────────────────────────────────────────────
//  deleteNamespace
// ─────────────────────────────────────────────────────────────
bool K8sController::deleteNamespace(const std::string& ns)
{
    std::string resp = curlDelete(api_server_ + "/api/v1/namespaces/" + ns);
    bool ok = (resp.find("\"status\":\"Terminating\"") != std::string::npos ||
               resp.find("\"name\":\"" + ns + "\"") != std::string::npos);
    std::cout << "[K8sController] Delete namespace " << ns << (ok ? " OK" : " FAIL") << "\n";
    return ok;
}

// ─────────────────────────────────────────────────────────────
//  getPodLogs
// ─────────────────────────────────────────────────────────────
std::string K8sController::getPodLogs(const std::string& ns,
                                       const std::string& deployment_name,
                                       int tail_lines)
{
    // Find pod name first
    std::string pods_resp = curlGet(
        api_server_ + "/api/v1/namespaces/" + ns +
        "/pods?labelSelector=app%3D" + deployment_name);

    auto np = pods_resp.find("\"name\":\"");
    if (np == std::string::npos) return "No pod found";
    np += 8;
    auto ne = pods_resp.find("\"", np);
    std::string pod_name = pods_resp.substr(np, ne - np);

    std::string url = api_server_ + "/api/v1/namespaces/" + ns +
                      "/pods/" + pod_name + "/log?tailLines=" +
                      std::to_string(tail_lines);
    return curlGet(url);
}
