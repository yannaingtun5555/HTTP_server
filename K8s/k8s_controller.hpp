#ifndef K8S_CONTROLLER_HPP
#define K8S_CONTROLLER_HPP

#include <string>
#include <vector>
#include <map>
#include "../Configuration/site_config.hpp"
#include "../DB/server_db.hpp"

// ─────────────────────────────────────────────────────────────
//  K8sController
//  Talks to the Kubernetes API server via libcurl + kubeadm
//  REST API (application/json). Manages namespaces, Deployments
//  and Services for BE and DB containers.
// ─────────────────────────────────────────────────────────────
class K8sController
{
public:
    K8sController() = default;

    // Initialize with API server URL and path to kubeconfig
    void init(const std::string& api_server, const std::string& kubeconfig_path);

    // ── Namespace ────────────────────────────────────────────
    bool ensureNamespace(const std::string& ns);

    // ── DB container lifecycle ────────────────────────────────
    // Spin up a DB container for a given domain + DbEntry.
    // Returns the connection string on success ("" on failure).
    // Blocks until the pod is Ready (or timeout_secs exceeded).
    std::string spinUpDbContainer(const std::string& domain,
                                  const DbEntry& db,
                                  const std::string& db_user,
                                  const std::string& db_password,
                                  DbContainerRecord& record_out,
                                  int timeout_secs = 120);

    // ── BE container lifecycle ────────────────────────────────
    // Spin up the BE container.  conn_env maps env-var-name → connection-string.
    // Returns pod name on success ("" on failure); error_log set if failure.
    std::string spinUpBeContainer(const SiteEntry& site,
                                  const std::map<std::string, std::string>& conn_env,
                                  BeContainerRecord& record_out,
                                  int timeout_secs = 180);

    // ── Teardown ─────────────────────────────────────────────
    bool deleteNamespace(const std::string& ns);

    // ── Logs ─────────────────────────────────────────────────
    // Fetch last N bytes of logs from the BE pod
    std::string getPodLogs(const std::string& ns,
                           const std::string& pod_name,
                           int tail_lines = 50);

private:
    std::string api_server_;
    std::string kubeconfig_;
    std::string token_;           // Bearer token from kubeconfig
    std::string ca_cert_path_;    // Path to CA cert for TLS verification

    // ── libcurl helpers ──────────────────────────────────────
    std::string curlGet (const std::string& url);
    std::string curlPost(const std::string& url, const std::string& body);
    std::string curlDelete(const std::string& url);

    // ── Kubernetes manifest builders ─────────────────────────
    std::string buildDbDeploymentJson(const std::string& ns,
                                      const std::string& name,
                                      const DbEntry& db,
                                      const std::string& db_user,
                                      const std::string& db_password);

    std::string buildServiceJson(const std::string& ns,
                                 const std::string& name,
                                 int port,
                                 const std::string& selector_app);

    std::string buildBeDeploymentJson(const SiteEntry& site,
                                      const std::string& ns,
                                      const std::string& name,
                                      const std::map<std::string, std::string>& env);

    // ── Polling helpers ──────────────────────────────────────
    // Wait until the first pod in a Deployment is Running.
    // Returns the pod name or "" on timeout.
    std::string waitForPodRunning(const std::string& ns,
                                  const std::string& deployment_name,
                                  int timeout_secs);

    // Parse ClusterIP from a Service JSON response
    std::string parseClusterIp(const std::string& service_json);

    // Read token and CA cert from kubeconfig file
    void parseKubeconfig();

    // DB type → Docker image mapping
    static std::string dbImage(const std::string& db_type);
    // DB type → default port
    static int dbPort(const std::string& db_type);
    // DB type → env-var name for connection string
    static std::string dbEnvVar(const std::string& alias);
};

extern K8sController k8s_controller;

#endif // K8S_CONTROLLER_HPP
