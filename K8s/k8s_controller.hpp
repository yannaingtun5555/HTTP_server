#ifndef K8S_CONTROLLER_HPP
#define K8S_CONTROLLER_HPP

#include <string>
#include <vector>
#include <map>
#include "../Configuration/site_config.hpp"
#include "../DB/server_db.hpp"

// ─────────────────────────────────────────────────────────────
//  ResourceTier — per-tenant compute/storage specifications
//  Fetched from the master database or sites.conf.
// ─────────────────────────────────────────────────────────────
struct ResourceTier
{
    std::string db_max_cpu    = "250m";
    std::string db_max_memory = "256Mi";
    int         db_storage_gb = 5;
    std::string be_max_cpu    = "500m";
    std::string be_max_memory = "512Mi";
};

// ─────────────────────────────────────────────────────────────
//  K8sController
//  Talks to the Kubernetes API server via libcurl + kubeadm
//  REST API (application/json). Manages namespaces, Deployments,
//  Services, PersistentVolumeClaims, and Ingresses for BE and
//  DB containers in a Render-like PaaS architecture.
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

    // ── Kaniko Container Builder ─────────────────────────────
    // Run an in-cluster Kaniko build job for a given git repository URL
    bool runKanikoBuildJob(const std::string& ns,
                           const std::string& name,
                           const std::string& git_url,
                           const std::string& destination_image,
                           int timeout_secs = 300);

    // ── Teardown ─────────────────────────────────────────────
    bool deleteNamespace(const std::string& ns);

    // ── Logs ─────────────────────────────────────────────────
    // Fetch last N bytes of logs from the BE pod
    std::string getPodLogs(const std::string& ns,
                           const std::string& pod_name,
                           int tail_lines = 50);

    // ── Resource tier lookup ─────────────────────────────────
    // Query the master database and sites.conf for custom specs.
    ResourceTier lookupResourceTier(const std::string& domain);

private:
    std::string api_server_;
    std::string kubeconfig_;
    std::string token_;           // Bearer token from kubeconfig
    std::string ca_cert_path_;    // Path to CA cert for TLS verification

    struct HttpResponse {
        long code = 0;
        std::string body;
    };

    // ── libcurl helpers ──────────────────────────────────────
    HttpResponse curlGet (const std::string& url);
    HttpResponse curlPost(const std::string& url, const std::string& body);
    HttpResponse curlDelete(const std::string& url);

    // ── Kubernetes manifest builders ─────────────────────────

    // PVC — requests storage via K3s local-path provisioner
    std::string buildPersistentVolumeClaimJson(const std::string& ns,
                                                const std::string& name,
                                                int size_gb);

    // Certificate — cert-manager.io/v1 Let's Encrypt TLS Certificate
    std::string buildCertificateJson(const std::string& ns,
                                      const std::string& name,
                                      const std::string& domain,
                                      const std::string& secret_name);

    // Ingress — Traefik host-based routing (networking.k8s.io/v1) with optional TLS
    std::string buildIngressJson(const std::string& ns,
                                  const std::string& name,
                                  const std::string& domain,
                                  const std::string& backend_service_name,
                                  int backend_port,
                                  const std::string& tls_secret_name = "");

    // DB Deployment with PVC mount + resource limits
    std::string buildDbDeploymentJson(const std::string& ns,
                                      const std::string& name,
                                      const DbEntry& db,
                                      const std::string& db_user,
                                      const std::string& db_password,
                                      const std::string& pvc_name,
                                      const std::string& max_cpu,
                                      const std::string& max_memory);

    // ClusterIP Service
    std::string buildServiceJson(const std::string& ns,
                                 const std::string& name,
                                 int port,
                                 const std::string& selector_app);

    // BE Deployment with resource limits (keeps hostPath for source)
    std::string buildBeDeploymentJson(const SiteEntry& site,
                                      const std::string& ns,
                                      const std::string& name,
                                      const std::map<std::string, std::string>& env,
                                      const std::string& max_cpu,
                                      const std::string& max_memory);

    // ── API response validation ──────────────────────────────
    // Returns true if the K8s API response indicates success (200/201).
    // Logs detailed error messages with context on failure.
    bool checkApiResponse(const HttpResponse& response,
                          const std::string& context);

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
    // DB type → data directory mount path inside container
    static std::string dbMountPath(const std::string& db_type);

    // Allow test harness access to private builders
    friend class K8sControllerTestable;
};

extern K8sController k8s_controller;

#endif // K8S_CONTROLLER_HPP
