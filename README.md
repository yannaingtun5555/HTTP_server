# ⚡ Multi-Tenant C++ Real-Time PaaS Platform & Application Gateway

[![Language](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Networking](https://img.shields.io/badge/Boost-Asio%20%7C%20Beast-orange.svg)](https://www.boost.org/)
[![Orchestration](https://img.shields.io/badge/Kubernetes-REST%20API-326CE5.svg)](https://kubernetes.io/)
[![Database](https://img.shields.io/badge/PostgreSQL-15%2B-336791.svg)](https://www.postgresql.org/)
[![Admin](https://img.shields.io/badge/Control%20Panel-Django-092E20.svg)](https://www.djangoproject.com/)
[![Tests](https://img.shields.io/badge/E2E%20Tests-Pytest-yellow.svg)](https://docs.pytest.org/)

A high-performance, multi-tenant C++17 Real-Time **Platform-as-a-Service (PaaS)** and application gateway (inspired by Render & Vercel). Built on **Boost.Asio**, **Boost.Beast**, **Kubernetes**, **Kaniko**, **PostgreSQL**, and **Django**, it provides dynamic container orchestration, live log streaming, automated SSL certificates, and git push webhooks.

---

## 📌 Table of Contents

- [Features](#-key-features)
- [System Architecture](#-system-architecture)
- [Directory Layout](#-project-directory-layout)
- [Prerequisites & Dependencies](#-prerequisites--dependencies)
- [Quick Start](#-quick-start--building)
- [Management Script](#-management--cli-control)
- [Configuration Reference](#-configuration-reference)
- [Testing & E2E Verification](#-testing--end-to-end-e2e-verification)
- [License](#-license)

---

## ✨ Key Features

- ⚡ **Ultra-Low Latency & High Throughput**: Serves static assets and reverse-proxies API calls at **~28,000 req/sec** with sub-millisecond core latency.
- 📡 **Real-Time Container Log Terminal**: Live stream pod build & runtime logs dynamically into the Django Admin dashboard.
- 🔄 **GitHub & GitLab Auto-Deploy Webhooks**: Trigger zero-downtime platform redeployments instantly on `git push`.
- 📦 **In-Cluster Kaniko Builder**: Dynamically builds container images inside Kubernetes without host Docker daemon access.
- 🔒 **Automated SSL/TLS Certificates**: Integrated `cert-manager.io` Let's Encrypt provisioning for tenant custom domains.
- 🛡️ **Zero-Downtime Hot-Swapping & Health Probes**: Active TCP/HTTP health probing before proxying live user traffic.
- 🛑 **Token-Bucket Rate Limiter**: Built-in thread-safe rate limiter protecting tenant applications against DDoS attacks.
- 🏢 **Multi-Tenant Database Orchestration**: Automatically provisions PostgreSQL, Redis, MySQL, or MongoDB containers per tenant with auto-wired connection strings.

---

## 📐 System Architecture

```text
                               ┌───────────────────────────────┐
                               │        Incoming Request       │
                               └───────────────┬───────────────┘
                                               │
                                               ▼
                               ┌───────────────────────────────┐
                               │     C++ Core HTTP Server      │
                               │    (Boost.Asio / Beast)       │
                               └───────┬───────────────┬───────┘
                                       │               │
             ┌─────────────────────────┘               └─────────────────────────┐
             │                                                                   │
             ▼                                                                   ▼
  ┌──────────────────────┐                                            ┌──────────────────────┐
  │  Static File Route   │                                            │ Reverse Proxy / API  │
  ├──────────────────────┤                                            ├──────────────────────┤
  │  • LRU File Cache    │                                            │ • Rate Limiter       │
  │  • FrontEnd Builder  │                                            │ • Internal API       │
  └──────────────────────┘                                            └──────────┬───────────┘
                                                                                 │
                                         ┌───────────────────────────────────────┴───────────────────────────────────────┐
                                         │                                                                               │
                                         ▼                                                                               ▼
                          ┌───────────────────────────────┐                                               ┌───────────────────────────────┐
                          │   PostgreSQL State DB         │                                               │   K8s Controller Engine       │
                          │   (server_db / pqxx)          │                                               │   (Kaniko / cert-manager)     │
                          └───────────────────────────────┘                                               └──────────────┬────────────────┘
                                                                                                                         │
                                                                                        ┌────────────────────────────────┴────────────────────────────────┐
                                                                                        │                                                                 │
                                                                                        ▼                                                                 ▼
                                                                         ┌───────────────────────────────┐                 ┌───────────────────────────────┐
                                                                         │   Tenant App Containers       │                 │   Tenant Database Containers  │
                                                                         │   (Deployments / Services)    │                 │   (PVCs / Stateful Data)      │
                                                                         └───────────────────────────────┘                 └──────────────┬────────────────┘
```

---

## 📁 Project Directory Layout

```text
HTTP_server/
├── setup.sh                 # Distribution detection, dependency installer & builder
├── http_server.sh           # Daemon management script (start/stop/status/log/restart)
├── server.conf              # Core server, DB, SSL, & K8s master configuration
├── sites.conf               # Multi-tenant domain & resource definitions
├── CMakeLists.txt           # CMake build specifications
├── main.cpp                 # Application entry point
├── admin_panel/             # Django Admin Dashboard
│   ├── portal/              # Webhook handlers, views, models, forms, and URLs
│   └── templates/           # Real-Time terminal & admin UI templates
├── Core/                    # Core connection handlers & internal REST API
├── K8s/                     # Kubernetes REST controller, Kaniko builder, & cert-manager
├── DB/                      # PostgreSQL state management & schema interface
├── Configuration/           # Config file parsers (server & site)
├── FrontEnd/                # Automated frontend build pipeline
├── Proxy/                   # Asynchronous HTTP proxy & Token-Bucket Rate Limiter
├── Routing/                 # Virtual-host static request router
├── File_Management/         # Static file I/O & LRU cache implementation
├── Monitoring/              # Boost.Log structured logging system
├── Thread/                  # Thread pool dispatcher
├── tests/                   # Automated E2E test suite & sample applications
│   ├── e2e/                 # Pytest E2E test scripts (API, Admin, Proxy SSL, Rate Limiter)
│   └── sample_app/          # Reference full-stack test application (FE + BE + DB)
└── logs/                    # Operational logs directory
```

---

## 🛠️ Prerequisites & Dependencies

- **Compiler**: GCC 7+ or Clang 6+ (supporting C++17)
- **Build System**: CMake 3.10+
- **Libraries**:
  - `Boost` 1.66+ (log, system, thread, filesystem, asio, beast)
  - `libpqxx` (PostgreSQL C++ client)
  - `libcurl` (Kubernetes REST API client)
  - `JsonCpp` (JSON parser)
  - `Python 3` + `Django` + `pytest` + `requests`

> **Note**: Running `./setup.sh` automatically detects your Linux distribution and installs all required dependencies.

---

## ⚙️ Quick Start & Building

### 1. Automatic One-Command Setup

```bash
git clone <repository-url>
cd HTTP_server
chmod +x setup.sh
./setup.sh
```

### 2. Manual Build

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

The compiled binary will be placed at `build/HTTP_Server`.

---

## 🕹️ Management & CLI Control

```bash
./http_server.sh start       # Start server + Docker PostgreSQL + Django Admin Panel
./http_server.sh stop        # Gracefully stop all server background processes
./http_server.sh restart     # Restart all services
./http_server.sh status      # Display server process & port status
./http_server.sh log         # Stream live server logs
```

---

## ⚙️ Configuration Reference (`server.conf`)

```ini
# Network Interface & SSL
server_host=0.0.0.0
server_port=8000
ssl_enabled=false
ssl_cert_path=./server.crt
ssl_key_path=./server.key

# PostgreSQL Master Database
server_db_host=127.0.0.1
server_db_port=5432
server_db_name=server_db
server_db_user=admin
server_db_password=changeme

# Kubernetes API Access
k8s_kubeconfig=/home/yan9htun/.kube/config
k8s_api_server=https://127.0.0.1:6443

# Internal API & Admin Panel
internal_api_path_prefix=/_api/internal
internal_api_secret=changeme-secret
admin_backend_host=127.0.0.1
admin_backend_port=8080
admin_backend_path_prefix=/_admin
```

---

## 🧪 Testing & End-to-End (E2E) Verification

Run the full automated E2E test suite:

```bash
python3 -m pytest tests/e2e/test_api.py tests/e2e/test_admin.py tests/e2e/test_proxy_ssl.py tests/e2e/test_rate_limiter.py -v
```

---

## 📄 License

Distributed under the MIT License. See `LICENSE` for more details.
