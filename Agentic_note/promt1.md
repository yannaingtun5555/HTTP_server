You are an expert Principal Systems Architect and Senior Cloud Native Engineer specializing in C++, libcurl, distributed systems, and Kubernetes multi-tenant infrastructure.

Your task is to refactor and expand the current C++ project into a production-grade Platform-as-a-Service (PaaS) core engine, and provide a full, minimal end-to-end testing application environment.

---

### PART 1: C++ CORE REFACTORING & PRODUCTION hardening

1. **Safe Stateful Workloads (StatefulSets for Databases):**
   - Refactor `buildDbDeploymentJson` to output a Kubernetes `apps/v1` `StatefulSet` instead of a standard `Deployment`.
   - Implement the `volumeClaimTemplates` array natively within the StatefulSet specification to handle PVC provisioning dynamically per replica, ensuring storage is deterministic and safe from concurrent write corruption.
   - Set the `serviceName` parameter correctly to handle headless/ClusterIP database routing.

2. **Eliminate String-Concatenation JSON (Safe Serialization):**
   - Completely eliminate all raw string literals and string manipulation (`+` concatenations) used to construct Kubernetes resource payloads (`buildPersistentVolumeClaimJson`, `buildIngressJson`, `buildDbDeploymentJson`, `buildServiceJson`, `buildBeDeploymentJson`).
   - Use the pre-imported `jsoncpp` library (`Json::Value`, `Json::StreamWriterBuilder`) to programmatically build the JSON object graphs. This prevents payload corruption from malformed environment variables and avoids injection risks.

3. **Secure Multi-Tenancy (Eradicate hostPath):**
   - Eliminate `hostPath` volume mounts inside `buildBeDeploymentJson`. 
   - Replace it with a decoupled production paradigm: update the container spec to leverage an isolated image-tag scheme (`site.image_name` / `site.image_tag`) pulled from a secure image registry, OR utilize a distinct, restricted `PersistentVolumeClaim` with `local-path` for application source code. Adjust data structures structurally as needed.

4. **Resiliency & Self-Healing (Probes & Redundancy):**
   - Upgrade the Backend container template to support configurable scaling (`replicas: 2` or higher by default).
   - Inject `livenessProbe` and `readinessProbe` blocks into the backend container spec (e.g., executing an HTTP GET on `/healthz` or a TCP socket check on the application's native port) to allow Kubernetes to automatically recycle deadlocked instances.

5. **Robust JSON Parsing & Status Extraction:**
   - In `parseClusterIp` and `waitForPodRunning`, replace the fragile string `find()` scanning with proper `jsoncpp` tree lookups to securely extract fields (`spec.clusterIP`, `status.phase`, and `metadata.name`). Fix hidden errors where an unparseable response could lead to segmentation faults or undefined memory behaviors.

---

### PART 2: COMPREHENSIVE END-TO-END SANDBOX TEST SUITE

Generate a complete, minimal, and fully functional multi-tier containerized stack to thoroughly test the revised C++ controller engine. Do not provide placeholders or mock snippets—write code that runs.

1. **Minimal React Frontend (Client-Facing Dashboard):**
   - Provide a clean, minimal React setup (using standard functional components/hooks).
   - It must include an interactive dashboard showing cluster health, system connection logs, and a button to trigger data writing to the backend.
   - Package it with a functional `Dockerfile` using a multi-stage build (Node builder layer to an Nginx static server layer).

2. **Node.js Express Backend:**
   - Provide a complete Express.js script handling routes for `/healthz` (serving the Kubernetes readiness/liveness probes), `/api/status`, and a database read/write test endpoint.
   - It must dynamically consume database URLs from standard injection paths (e.g., `PROCESS.ENV.MAIN_DB_URL` and `PROCESS.ENV.CACHE_DB_URL`).
   - Package it with a fully optimized, production-ready `Dockerfile` built on an Alpine base.

3. **Multi-Database Validation Layer:**
   - Set up the environment configs to support orchestration testing for **two separate database engines simultaneously** (e.g., one Primary PostgreSQL container for transaction tracking and one Redis/MongoDB container for temporary data caching).

4. **External Dependency & Module Bug Fixes:**
   - Audit and fix downstream dependencies related to the multi-tier build: ensure CORS is handled properly in the Node.js backend layer, configure proper error catcher middleware to log connection drops gracefully, and ensure standard networking timeouts are set so microservice failures do not bubble up into total application crashes.

---

Maintain clean, clean, production-level modern standards across all code outputs (C++, JavaScript/React, and Dockerfiles).Fix the possible bugs in the projects after allparts complete