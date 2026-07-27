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
        else if (t.rfind("certificate-authority-data:", 0) == 0)
            ca_cert_path_ = ""; // Fallback to insecure for inline certs
    }
}

// ─────────────────────────────────────────────────────────────
//  libcurl helper — GET
// ─────────────────────────────────────────────────────────────
K8sController::HttpResponse K8sController::curlGet(const std::string& url)
{
    HttpResponse res;
    CURL* curl = curl_easy_init();
    if (!curl) return res;

    std::string auth = "Authorization: Bearer " + token_;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    if (!ca_cert_path_.empty())
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert_path_.c_str());
    else
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res;
}

// ─────────────────────────────────────────────────────────────
//  libcurl helper — POST (application/json)
// ─────────────────────────────────────────────────────────────
K8sController::HttpResponse K8sController::curlPost(const std::string& url,
                                     const std::string& body)
{
    HttpResponse res;
    CURL* curl = curl_easy_init();
    if (!curl) return res;

    std::string auth = "Authorization: Bearer " + token_;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    if (!ca_cert_path_.empty())
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert_path_.c_str());
    else
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res;
}

// ─────────────────────────────────────────────────────────────
//  libcurl helper — DELETE
// ─────────────────────────────────────────────────────────────
K8sController::HttpResponse K8sController::curlDelete(const std::string& url)
{
    HttpResponse res;
    CURL* curl = curl_easy_init();
    if (!curl) return res;

    std::string auth = "Authorization: Bearer " + token_;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    if (!ca_cert_path_.empty())
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_cert_path_.c_str());
    else
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return res;
}

// ─────────────────────────────────────────────────────────────
//  checkApiResponse — validates K8s API JSON response
//  Returns true if the response indicates success; logs
//  detailed error messages with context on failure.
// ─────────────────────────────────────────────────────────────
bool K8sController::checkApiResponse(const K8sController::HttpResponse& response,
                                      const std::string& context)
{
    if (response.code >= 200 && response.code < 300) {
        return true;
    }
    
    // 409 Conflict (AlreadyExists) is not a fatal error for idempotent creates
    if (response.code == 409) {
        std::cout << "[K8sController] " << context
                  << ": resource already exists (idempotent OK)\n";
        return true;
    }
    
    std::cerr << "[K8sController] " << context
              << ": API error " << response.code << " - " 
              << response.body.substr(0, std::min<size_t>(response.body.size(), 300)) << "\n";
    return false;
}

// ─────────────────────────────────────────────────────────────
//  lookupResourceTier — fetches per-tenant resource specs
//  First tries the master database, falls back to sites.conf,
//  returns defaults if neither has custom values.
// ─────────────────────────────────────────────────────────────
ResourceTier K8sController::lookupResourceTier(const std::string& domain)
{
    ResourceTier tier;

    // Try master database first
    DomainRecord dr;
    if (server_db.isConnected() && server_db.getDomain(domain, dr))
    {
        tier.db_max_cpu    = dr.db_max_cpu;
        tier.db_max_memory = dr.db_max_memory;
        tier.db_storage_gb = dr.db_storage_gb;
        tier.be_max_cpu    = dr.be_max_cpu;
        tier.be_max_memory = dr.be_max_memory;
        std::cout << "[K8sController] Resource tier from DB for " << domain
                  << ": db=" << tier.db_max_cpu << "/" << tier.db_max_memory
                  << " be=" << tier.be_max_cpu << "/" << tier.be_max_memory
                  << " storage=" << tier.db_storage_gb << "Gi\n";
        return tier;
    }

    // Fall back to sites.conf
    SiteEntry site;
    if (site_config.findSite(domain, site))
    {
        tier.db_max_cpu    = site.db_max_cpu;
        tier.db_max_memory = site.db_max_memory;
        tier.db_storage_gb = site.db_storage_gb;
        tier.be_max_cpu    = site.be_max_cpu;
        tier.be_max_memory = site.be_max_memory;
        std::cout << "[K8sController] Resource tier from config for " << domain
                  << ": db=" << tier.db_max_cpu << "/" << tier.db_max_memory
                  << " be=" << tier.be_max_cpu << "/" << tier.be_max_memory
                  << " storage=" << tier.db_storage_gb << "Gi\n";
        return tier;
    }

    // Return defaults
    std::cout << "[K8sController] Using default resource tier for " << domain << "\n";
    return tier;
}

// ─────────────────────────────────────────────────────────────
//  ensureNamespace
// ─────────────────────────────────────────────────────────────
bool K8sController::ensureNamespace(const std::string& ns)
{
    // Check if already exists
    HttpResponse check = curlGet(api_server_ + "/api/v1/namespaces/" + ns);
    if (check.code == 200 && check.body.find("\"name\":\"" + ns + "\"") != std::string::npos)
        return true;

    std::string body = R"({"apiVersion":"v1","kind":"Namespace","metadata":{"name":")" + ns + R"("}})";
    HttpResponse resp = curlPost(api_server_ + "/api/v1/namespaces", body);
    bool ok = checkApiResponse(resp, "ensureNamespace(" + ns + ")");
    std::cout << "[K8sController] Namespace " << ns << (ok ? " created" : " failed") << "\n";
    return ok;
}

// ─────────────────────────────────────────────────────────────
//  DB image / port / env-var / mount-path mapping
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

std::string K8sController::dbMountPath(const std::string& db_type)
{
    if (db_type == "postgres") return "/var/lib/postgresql/data";
    if (db_type == "mysql")    return "/var/lib/mysql";
    if (db_type == "mongo")    return "/data/db";
    if (db_type == "redis")    return "/data";
    return "/var/lib/postgresql/data";
}

// ─────────────────────────────────────────────────────────────
//  buildPersistentVolumeClaimJson — PVC via K3s local-path
// ─────────────────────────────────────────────────────────────
std::string K8sController::buildPersistentVolumeClaimJson(const std::string& ns,
                                                           const std::string& name,
                                                           int size_gb)
{
    Json::Value root;
    root["apiVersion"] = "v1";
    root["kind"] = "PersistentVolumeClaim";
    root["metadata"]["name"] = name;
    root["metadata"]["namespace"] = ns;
    root["spec"]["accessModes"].append("ReadWriteOnce");
    root["spec"]["storageClassName"] = "local-path";
    root["spec"]["resources"]["requests"]["storage"] = std::to_string(size_gb) + "Gi";
    
    Json::FastWriter writer;
    std::string out = writer.write(root);
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    return out;
}

// ─────────────────────────────────────────────────────────────
//  buildCertificateJson — cert-manager.io TLS Certificate
// ─────────────────────────────────────────────────────────────
std::string K8sController::buildCertificateJson(const std::string& ns,
                                                 const std::string& name,
                                                 const std::string& domain,
                                                 const std::string& secret_name)
{
    Json::Value root;
    root["apiVersion"] = "cert-manager.io/v1";
    root["kind"] = "Certificate";
    root["metadata"]["name"] = name;
    root["metadata"]["namespace"] = ns;
    root["spec"]["secretName"] = secret_name;
    root["spec"]["dnsNames"].append(domain);
    root["spec"]["issuerRef"]["name"] = "letsencrypt-prod";
    root["spec"]["issuerRef"]["kind"] = "ClusterIssuer";

    Json::FastWriter writer;
    std::string out = writer.write(root);
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    return out;
}

// ─────────────────────────────────────────────────────────────
//  buildIngressJson — Traefik host-based routing
//  Generates a networking.k8s.io/v1 Ingress resource that maps
//  an external HTTP domain to an internal ClusterIP Service.
// ─────────────────────────────────────────────────────────────
std::string K8sController::buildIngressJson(const std::string& ns,
                                             const std::string& name,
                                             const std::string& domain,
                                             const std::string& backend_service_name,
                                             int backend_port,
                                             const std::string& tls_secret_name)
{
    Json::Value root;
    root["apiVersion"] = "networking.k8s.io/v1";
    root["kind"] = "Ingress";
    root["metadata"]["name"] = name;
    root["metadata"]["namespace"] = ns;
    root["metadata"]["annotations"]["kubernetes.io/ingress.class"] = "traefik";

    if (!tls_secret_name.empty())
    {
        root["metadata"]["annotations"]["cert-manager.io/cluster-issuer"] = "letsencrypt-prod";
        Json::Value tlsItem;
        tlsItem["hosts"].append(domain);
        tlsItem["secretName"] = tls_secret_name;
        root["spec"]["tls"].append(tlsItem);
    }
    
    Json::Value path;
    path["path"] = "/";
    path["pathType"] = "Prefix";
    path["backend"]["service"]["name"] = backend_service_name;
    path["backend"]["service"]["port"]["number"] = backend_port;
    
    Json::Value http;
    http["paths"].append(path);
    
    Json::Value rule;
    rule["host"] = domain;
    rule["http"] = http;
    
    root["spec"]["rules"].append(rule);
    
    Json::FastWriter writer;
    std::string out = writer.write(root);
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    return out;
}

// ─────────────────────────────────────────────────────────────
//  buildDbDeploymentJson — Deployment with PVC + resource limits
// ─────────────────────────────────────────────────────────────
std::string K8sController::buildDbDeploymentJson(const std::string& ns,
                                                   const std::string& name,
                                                   const DbEntry& db,
                                                   const std::string& db_user,
                                                   const std::string& db_password,
                                                   const std::string& pvc_name,
                                                   const std::string& max_cpu,
                                                   const std::string& max_memory)
{
    int port = dbPort(db.type);
    std::string image = dbImage(db.type);
    std::string mount_path = dbMountPath(db.type);

    Json::Value root;
    root["apiVersion"] = "apps/v1";
    root["kind"] = "Deployment";
    root["metadata"]["name"] = name;
    root["metadata"]["namespace"] = ns;
    
    Json::Value& spec = root["spec"];
    spec["replicas"] = 1;
    spec["strategy"]["type"] = "Recreate";
    spec["selector"]["matchLabels"]["app"] = name;
    
    Json::Value& template_ = spec["template"];
    template_["metadata"]["labels"]["app"] = name;
    
    Json::Value& pod_spec = template_["spec"];
    
    Json::Value container;
    container["name"] = "db";
    container["image"] = image;
    
    Json::Value p;
    p["containerPort"] = port;
    container["ports"].append(p);
    
    Json::Value env(Json::arrayValue);
    if (db.type == "postgres") {
        Json::Value e1, e2, e3;
        e1["name"] = "POSTGRES_USER"; e1["value"] = db_user;
        e2["name"] = "POSTGRES_PASSWORD"; e2["value"] = db_password;
        e3["name"] = "POSTGRES_DB"; e3["value"] = db.db_name;
        env.append(e1); env.append(e2); env.append(e3);
    } else if (db.type == "mysql") {
        Json::Value e1, e2, e3, e4;
        e1["name"] = "MYSQL_ROOT_PASSWORD"; e1["value"] = db_password;
        e2["name"] = "MYSQL_DATABASE"; e2["value"] = db.db_name;
        e3["name"] = "MYSQL_USER"; e3["value"] = db_user;
        e4["name"] = "MYSQL_PASSWORD"; e4["value"] = db_password;
        env.append(e1); env.append(e2); env.append(e3); env.append(e4);
    } else if (db.type == "mongo") {
        Json::Value e1, e2, e3;
        e1["name"] = "MONGO_INITDB_ROOT_USERNAME"; e1["value"] = db_user;
        e2["name"] = "MONGO_INITDB_ROOT_PASSWORD"; e2["value"] = db_password;
        e3["name"] = "MONGO_INITDB_DATABASE"; e3["value"] = db.db_name;
        env.append(e1); env.append(e2); env.append(e3);
    }
    container["env"] = env;
    
    Json::Value resources;
    resources["limits"]["cpu"] = max_cpu;
    resources["limits"]["memory"] = max_memory;
    resources["requests"]["cpu"] = max_cpu;
    resources["requests"]["memory"] = max_memory;
    container["resources"] = resources;
    
    Json::Value volumeMount;
    volumeMount["name"] = "db-data";
    volumeMount["mountPath"] = mount_path;
    container["volumeMounts"].append(volumeMount);
    
    pod_spec["containers"].append(container);
    
    Json::Value volume;
    volume["name"] = "db-data";
    volume["persistentVolumeClaim"]["claimName"] = pvc_name;
    pod_spec["volumes"].append(volume);
    
    Json::FastWriter writer;
    std::string out = writer.write(root);
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    return out;
}

// ─────────────────────────────────────────────────────────────
//  buildServiceJson — ClusterIP Service JSON
// ─────────────────────────────────────────────────────────────
std::string K8sController::buildServiceJson(const std::string& ns,
                                             const std::string& name,
                                             int port,
                                             const std::string& selector_app)
{
    Json::Value root;
    root["apiVersion"] = "v1";
    root["kind"] = "Service";
    root["metadata"]["name"] = name;
    root["metadata"]["namespace"] = ns;
    
    Json::Value& spec = root["spec"];
    spec["selector"]["app"] = selector_app;
    spec["type"] = "ClusterIP";
    
    Json::Value p;
    p["protocol"] = "TCP";
    p["port"] = port;
    p["targetPort"] = port;
    spec["ports"].append(p);
    
    Json::FastWriter writer;
    std::string out = writer.write(root);
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    return out;
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
        HttpResponse resp = curlGet(url);

        // Look for "phase":"Running"
        if (resp.body.find("\"phase\":\"Running\"") != std::string::npos)
        {
            // Extract pod name
            auto np = resp.body.find("\"name\":\"");
            if (np != std::string::npos)
            {
                np += 8;
                auto ne = resp.body.find("\"", np);
                return resp.body.substr(np, ne - np);
            }
        }
        std::this_thread::sleep_for(seconds(3));
    }
    return "";
}

// ─────────────────────────────────────────────────────────────
//  spinUpDbContainer — production pipeline:
//  1. Lookup resource tier for the domain
//  2. Create namespace
//  3. Deploy PVC (validate)
//  4. Deploy DB Deployment with PVC + resource limits (validate)
//  5. Deploy Service (validate)
//  6. Wait for pod Running
//  7. Build connection string
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

    std::string ns       = "site-" + safe_domain;
    std::string name     = "db-" + safe_domain + "-" + db.alias;
    std::string pvc_name = "pvc-" + name;
    int port = dbPort(db.type);

    // 1. Lookup resource tier
    ResourceTier tier = lookupResourceTier(domain);

    // 2. Create namespace
    if (!ensureNamespace(ns))
    {
        record_out.status    = "error";
        record_out.error_msg = "Failed to create namespace " + ns;
        std::cerr << "[K8sController] " << record_out.error_msg << "\n";
        return "";
    }

    // 3. Deploy PVC
    std::string pvc_json = buildPersistentVolumeClaimJson(ns, pvc_name, tier.db_storage_gb);
    std::string pvc_url  = api_server_ + "/api/v1/namespaces/" + ns + "/persistentvolumeclaims";
    HttpResponse pvc_resp = curlPost(pvc_url, pvc_json);
    if (!checkApiResponse(pvc_resp, "PVC " + pvc_name))
    {
        record_out.status    = "error";
        record_out.error_msg = "Failed to create PVC " + pvc_name;
        std::cerr << "[K8sController] " << record_out.error_msg << "\n";
        return "";
    }
    std::cout << "[K8sController] PVC " << pvc_name << " created ("
              << tier.db_storage_gb << "Gi)\n";

    // 4. Create Deployment with PVC mount + resource limits
    std::string dep_json = buildDbDeploymentJson(ns, name, db, db_user, db_password,
                                                  pvc_name, tier.db_max_cpu, tier.db_max_memory);
    std::string dep_url  = api_server_ + "/apis/apps/v1/namespaces/" + ns + "/deployments";
    HttpResponse dep_resp = curlPost(dep_url, dep_json);
    if (!checkApiResponse(dep_resp, "DB Deployment " + name))
    {
        record_out.status    = "error";
        record_out.error_msg = "Failed to create DB deployment " + name;
        std::cerr << "[K8sController] " << record_out.error_msg << "\n";
        return "";
    }
    std::cout << "[K8sController] Created DB deployment " << name
              << " [cpu=" << tier.db_max_cpu << " mem=" << tier.db_max_memory << "]\n";

    // 5. Create Service
    std::string svc_json = buildServiceJson(ns, name, port, name);
    std::string svc_url  = api_server_ + "/api/v1/namespaces/" + ns + "/services";
    HttpResponse svc_resp = curlPost(svc_url, svc_json);
    if (!checkApiResponse(svc_resp, "DB Service " + name))
    {
        record_out.status    = "error";
        record_out.error_msg = "Failed to create DB service " + name;
        std::cerr << "[K8sController] " << record_out.error_msg << "\n";
        return "";
    }

    // Get ClusterIP
    HttpResponse svc_get  = curlGet(api_server_ + "/api/v1/namespaces/" + ns + "/services/" + name);
    std::string cluster_ip = parseClusterIp(svc_get.body);

    // 6. Wait for pod Running
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

    // 7. Build connection string
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
//  buildBeDeploymentJson — BE Deployment with resource limits
//  Keeps hostPath for source code mounting.
// ─────────────────────────────────────────────────────────────
std::string K8sController::buildBeDeploymentJson(const SiteEntry& site,
                                                  const std::string& ns,
                                                  const std::string& name,
                                                  const std::map<std::string, std::string>& env_map,
                                                  const std::string& max_cpu,
                                                  const std::string& max_memory)
{
    std::string image;
    if      (site.be_type == "node")   image = "node:20-alpine";
    else if (site.be_type == "python") image = "python:3.11-alpine";
    else if (site.be_type == "go")     image = "golang:1.22-alpine";
    else if (site.be_type == "java")   image = "eclipse-temurin:17-alpine";
    else if (site.be_type == "php")    image = "php:8.2-cli-alpine";
    else                               image = "ubuntu:22.04";

    Json::Value root;
    root["apiVersion"] = "apps/v1";
    root["kind"] = "Deployment";
    root["metadata"]["name"] = name;
    root["metadata"]["namespace"] = ns;
    
    Json::Value& spec = root["spec"];
    spec["replicas"] = 1;
    spec["selector"]["matchLabels"]["app"] = name;
    
    Json::Value& template_ = spec["template"];
    template_["metadata"]["labels"]["app"] = name;
    
    Json::Value& pod_spec = template_["spec"];
    
    Json::Value container;
    container["name"] = "be";
    container["image"] = image;
    
    std::string shell_cmd = "cd /app && " + site.run_cmd;
    container["command"].append("/bin/sh");
    container["command"].append("-c");
    container["command"].append(shell_cmd);
    container["workingDir"] = "/app";
    
    Json::Value p;
    p["containerPort"] = site.be_port;
    container["ports"].append(p);
    
    Json::Value env(Json::arrayValue);
    for (const auto& [k, v] : env_map) {
        Json::Value e;
        e["name"] = k;
        e["value"] = v;
        env.append(e);
    }
    container["env"] = env;
    
    Json::Value resources;
    resources["limits"]["cpu"] = max_cpu;
    resources["limits"]["memory"] = max_memory;
    resources["requests"]["cpu"] = max_cpu;
    resources["requests"]["memory"] = max_memory;
    container["resources"] = resources;
    
    Json::Value volumeMount;
    volumeMount["name"] = "be-src";
    volumeMount["mountPath"] = "/app";
    container["volumeMounts"].append(volumeMount);
    
    pod_spec["containers"].append(container);
    
    Json::Value volume;
    volume["name"] = "be-src";
    volume["hostPath"]["path"] = site.be_folder;
    volume["hostPath"]["type"] = "Directory";
    pod_spec["volumes"].append(volume);
    
    Json::FastWriter writer;
    std::string out = writer.write(root);
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    return out;
}

// ─────────────────────────────────────────────────────────────
//  spinUpBeContainer — production pipeline:
//  1. Lookup resource tier for the domain
//  2. Create namespace
//  3. Deploy BE Deployment with resource limits (validate)
//  4. Deploy Service (validate)
//  5. Deploy Ingress for public gateway (validate)
//  6. Wait for pod Running
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

    // 1. Lookup resource tier
    ResourceTier tier = lookupResourceTier(site.domain);

    // 2. Create namespace
    if (!ensureNamespace(ns))
    {
        record_out.status    = "error";
        record_out.error_log = "Failed to create namespace " + ns;
        std::cerr << "[K8sController] " << record_out.error_log << "\n";
        return "";
    }

    // 3. Combine conn_env with working directory env var and deploy
    auto env = conn_env;
    env["APP_DIR"] = "/app";

    std::string dep_json = buildBeDeploymentJson(site, ns, name, env,
                                                  tier.be_max_cpu, tier.be_max_memory);
    std::string dep_url  = api_server_ + "/apis/apps/v1/namespaces/" + ns + "/deployments";
    HttpResponse dep_resp = curlPost(dep_url, dep_json);
    if (!checkApiResponse(dep_resp, "BE Deployment " + name))
    {
        record_out.status    = "error";
        record_out.error_log = "Failed to create BE deployment " + name;
        std::cerr << "[K8sController] " << record_out.error_log << "\n";
        return "";
    }
    std::cout << "[K8sController] Created BE deployment " << name
              << " [cpu=" << tier.be_max_cpu << " mem=" << tier.be_max_memory << "]\n";

    // 4. Create Service
    std::string svc_json = buildServiceJson(ns, name, site.be_port, name);
    std::string svc_url  = api_server_ + "/api/v1/namespaces/" + ns + "/services";
    HttpResponse svc_resp = curlPost(svc_url, svc_json);
    if (!checkApiResponse(svc_resp, "BE Service " + name))
    {
        record_out.status    = "error";
        record_out.error_log = "Failed to create BE service " + name;
        std::cerr << "[K8sController] " << record_out.error_log << "\n";
        return "";
    }

    // Get ClusterIP
    HttpResponse svc_get    = curlGet(api_server_ + "/api/v1/namespaces/" + ns + "/services/" + name);
    std::string cluster_ip = parseClusterIp(svc_get.body);

    // 5. Create Ingress for public gateway
    std::string ingress_name = "ingress-" + safe_domain;
    std::string ing_json = buildIngressJson(ns, ingress_name, site.domain, name,
                                             static_cast<int>(site.be_port));
    std::string ing_url  = api_server_ + "/apis/networking.k8s.io/v1/namespaces/" + ns + "/ingresses";
    HttpResponse ing_resp = curlPost(ing_url, ing_json);
    if (!checkApiResponse(ing_resp, "Ingress " + ingress_name))
    {
        // Ingress failure is non-fatal — the service still works within the cluster
        std::cerr << "[K8sController] WARNING: Ingress creation failed for " << site.domain
                  << " (service still accessible internally)\n";
    }
    else
    {
        std::cout << "[K8sController] Ingress " << ingress_name
                  << " created for " << site.domain << "\n";
    }

    // 6. Wait for pod Running
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
    HttpResponse resp = curlDelete(api_server_ + "/api/v1/namespaces/" + ns);
    bool ok = (resp.code >= 200 && resp.code < 300);
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
    HttpResponse pods_resp = curlGet(
        api_server_ + "/api/v1/namespaces/" + ns +
        "/pods?labelSelector=app%3D" + deployment_name);

    auto np = pods_resp.body.find("\"name\":\"");
    if (np == std::string::npos) return "No pod found";
    np += 8;
    auto ne = pods_resp.body.find("\"", np);
    std::string pod_name = pods_resp.body.substr(np, ne - np);

    std::string url = api_server_ + "/api/v1/namespaces/" + ns +
                      "/pods/" + pod_name + "/log?tailLines=" +
                      std::to_string(tail_lines);
    return curlGet(url).body;
}

// ─────────────────────────────────────────────────────────────
//  runKanikoBuildJob
// ─────────────────────────────────────────────────────────────
bool K8sController::runKanikoBuildJob(const std::string& ns,
                                      const std::string& name,
                                      const std::string& git_url,
                                      const std::string& destination_image,
                                      int timeout_secs)
{
    ensureNamespace(ns);

    Json::Value job(Json::objectValue);
    job["apiVersion"] = "batch/v1";
    job["kind"] = "Job";

    Json::Value metadata(Json::objectValue);
    metadata["name"] = name;
    metadata["namespace"] = ns;
    job["metadata"] = metadata;

    Json::Value spec(Json::objectValue);
    spec["backoffLimit"] = 1;

    Json::Value templateObj(Json::objectValue);
    Json::Value podSpec(Json::objectValue);
    podSpec["restartPolicy"] = "Never";

    Json::Value container(Json::objectValue);
    container["name"] = "kaniko-builder";
    container["image"] = "gcr.io/kaniko-project/executor:latest";

    Json::Value args(Json::arrayValue);
    args.append("--context=" + git_url);
    args.append("--destination=" + destination_image);
    args.append("--no-push");
    container["args"] = args;

    Json::Value containers(Json::arrayValue);
    containers.append(container);
    podSpec["containers"] = containers;

    templateObj["spec"] = podSpec;
    spec["template"] = templateObj;
    job["spec"] = spec;

    Json::FastWriter writer;
    std::string job_json = writer.write(job);

    std::string url = api_server_ + "/apis/batch/v1/namespaces/" + ns + "/jobs";
    HttpResponse resp = curlPost(url, job_json);
    return checkApiResponse(resp, "runKanikoBuildJob " + name);
}
