# HTTP Server

A modular C++17 HTTP server built with **Boost.Asio** and **Boost.Beast**. It supports two operating modes:

| Mode | Command | Purpose |
|------|---------|---------|
| **Static** | `start` | Serve files from a local web root |
| **Reverse proxy** | `reverse` | Forward requests to configured backend services |

The server uses an asynchronous I/O model with a Boost.Asio thread pool for request handling.

---

## Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Build](#build)
- [Quick Start](#quick-start)
- [Server Management (`http_server.sh`)](#server-management-http_serversh)
- [Configuration (`config.txt`)](#configuration-configtxt)
- [Reverse Proxy](#reverse-proxy)
- [Project Layout](#project-layout)
- [Logs](#logs)

---

## Features

- HTTP/1.1 request/response handling
- **Static file serving** (GET / POST)
- **Async reverse proxy** with connection pooling and keep-alive
- Multi-backend routing by URL path prefix
- Configurable connect and response timeouts
- `Expect: 100-continue` support in proxy mode
- Forwarding headers (`X-Forwarded-For`, `Via`, `X-Proxy-By`)
- Logging (Boost.Log + JSON user activity)
- Background daemon control via shell script

---

## Requirements

- C++17 compiler (GCC / Clang)
- CMake 3.10+
- Boost 1.66+ (project tested with Boost 1.86)
- JsonCpp

Boost and JsonCpp are linked statically in `CMakeLists.txt`. Adjust `BOOST_ROOT` in `CMakeLists.txt` if your Boost install path differs.

---

## Build

```bash
git clone <repository-url>
cd HTTP_server

mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

The binary is produced at `build/HTTP_Server`.

---

## Quick Start

### Static mode

```bash
cd /path/to/HTTP_server
./http_server.sh start
curl http://127.0.0.1:8000/index.html
./http_server.sh stop
```

### Reverse proxy mode

```bash
# Terminal 1 — start a backend (example)
python3 -m http.server 3000 --bind 127.0.0.1

# Terminal 2 — start proxy
./http_server.sh reverse
curl http://127.0.0.1:8000/api/your-path
./http_server.sh stop
```

> **Tip:** Always use `./http_server.sh` to start/stop. Running `./build/HTTP_Server` directly in a terminal blocks that shell and can cause **"Address already in use"** if a background instance is already running.

---

## Server Management (`http_server.sh`)

Run all commands from the project root:

```bash
./http_server.sh <command>
# or
./http_server <command>        # symlink to http_server.sh
```

| Command | Description |
|---------|-------------|
| `start` | Start in **static** mode (background via `nohup`) |
| `reverse` | Start in **reverse proxy** mode (background) |
| `stop` | Stop the server (works for both modes) |
| `status` | Show PID, running mode, and log path |
| `log` | Tail `logs/temp.log` |
| `restart` | Stop and restart in the **same mode** as last run |
| `config <key> <value>` | Update a key in `config.txt` |
| `clear_logs` | Clear files in `logs/server_log/` |
| `help` | Show command help |

**Runtime files created by the script:**

| File | Purpose |
|------|---------|
| `HTTP_Server.pid` | Process ID of the running server |
| `HTTP_Server.mode` | Last started mode (`start` or `reverse`) |
| `logs/temp.log` | stdout/stderr from the server process |

---

## Configuration (`config.txt`)

The server reads **`config.txt`** from the project root (not JSON). Lines starting with `#` are comments.

### Full example

```txt
# --- Server (both modes) ---
server_host=0.0.0.0
server_port=8000
root_dir=/var/www/html
default_file=index.html

# --- Reverse proxy only ---
backend_connect_timeout=30
backend_response_timeout=60

# Backend: API service
backend.api.host=127.0.0.1
backend.api.port=3000
backend.api.path_prefix=/api

# Backend: Web service (catch-all)
backend.web.host=127.0.0.1
backend.web.port=4000
backend.web.path_prefix=/
```

### Server settings

| Key | Default | Used in | Description |
|-----|---------|---------|-------------|
| `server_host` | `0.0.0.0` | Both | Bind address |
| `server_port` | `8000` | Both | Listen port |
| `root_dir` | `/var/www/html` | Static | Document root for file serving |
| `default_file` | `index.html` | Static | Default index file name |

### Reverse proxy settings

| Key | Default | Description |
|-----|---------|-------------|
| `backend_connect_timeout` | `30` | Seconds to wait for DNS resolve + TCP connect + request write |
| `backend_response_timeout` | `60` | Seconds to wait for backend response |

### Backend blocks

Each backend is defined with three keys using the pattern:

```txt
backend.<name>.host=<hostname or IP>
backend.<name>.port=<port>
backend.<name>.path_prefix=<url prefix>
```

| Field | Example | Description |
|-------|---------|-------------|
| `<name>` | `api`, `web` | Logical label (your choice) |
| `host` | `127.0.0.1` | Backend hostname or IP |
| `port` | `3000` | Backend TCP port |
| `path_prefix` | `/api` | URL prefix this backend handles |

**Routing rule:** longest matching `path_prefix` wins.

| Request path | Matched backend |
|--------------|-----------------|
| `/api/users` | `backend.api` (`/api`) |
| `/api/` | `backend.api` |
| `/` | `backend.web` (`/`) |
| `/about` | `backend.web` |

All three fields (`host`, `port`, `path_prefix`) are required per backend; incomplete blocks are skipped with a log error.

### Update config at runtime

```bash
./http_server.sh config server_port 8080
./http_server.sh restart
```

---

## Reverse Proxy

### How it works

1. Client connects to the proxy on `server_port`.
2. Proxy matches the request path against configured `path_prefix` values.
3. Request is forwarded asynchronously to the matched backend.
4. Response is streamed back to the client with proxy headers added.

### Path forwarding

The proxy forwards the **full request path** to the backend unchanged.

Example: `GET /api/test.json` → backend receives `GET /api/test.json`.

Ensure the backend serves files at that path, or place files accordingly:

```bash
mkdir -p /tmp/api-backend/api
echo '{"ok":true}' > /tmp/api-backend/api/test.json
cd /tmp/api-backend && python3 -m http.server 3000 --bind 127.0.0.1
```

### Response headers added by proxy

- `X-Forwarded-For` — client IP
- `Via: 1.1 http_server_proxy`
- `X-Proxy-By: http_server_proxy`
- `X-Proxy: http_server_proxy` (on backend responses)

### HTTP status codes from proxy

| Code | Meaning |
|------|---------|
| `200` | Backend responded successfully |
| `404` | Proxy worked; backend has no matching resource |
| `502` | No backend matched, or backend unreachable |
| `504` | Backend connect or read timed out |

### Manual test checklist

```bash
# 1. Start backends
python3 -m http.server 3000 --bind 127.0.0.1   # API
python3 -m http.server 4000 --bind 127.0.0.1   # Web

# 2. Start proxy
./http_server.sh reverse
./http_server.sh status

# 3. Test
curl -v http://127.0.0.1:8000/                  # → web backend
curl -v http://127.0.0.1:8000/api/test.json     # → API backend

# 4. Stop
./http_server.sh stop
```

### Direct binary usage (advanced)

```bash
./build/HTTP_Server start      # static mode (foreground)
./build/HTTP_Server reverse    # reverse proxy mode (foreground)
```

Prefer `http_server.sh` for background operation and clean stop/start.

---

## Project Layout

```
HTTP_server/
├── build/                  # CMake build output (HTTP_Server binary)
├── config.txt              # Server and proxy configuration
├── http_server.sh          # Start / stop / status control script
├── main.cpp
├── Configuration/          # config.txt parser
├── Core/                   # Connection, session, request handler
├── Proxy/                  # Async reverse proxy (ProxyHandler)
├── Routing/                # Static request routing
├── File_Management/        # Static file read/write
├── Monitoring/             # Logging
├── Thread/                 # Thread pool
├── Error_handling/         # Error types and logging
└── logs/
    ├── temp.log            # Main process log (http_server.sh)
    ├── server_log/         # Rotating Boost.Log files
    └── user_data/          # Per-client JSON activity
```

---

## Logs

```bash
./http_server.sh log                          # live tail of temp.log
cat logs/temp.log                             # full process output
ls logs/server_log/                           # detailed server logs
ls logs/user_data/                            # per-IP client activity JSON
```

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `Address already in use` | `./http_server.sh stop` or `pkill -f HTTP_Server` |
| `502 Bad Gateway` | Backend not running, or wrong host/port in `config.txt` |
| `404` through proxy | Proxy works; backend path does not exist — check path forwarding |
| Binary not found | Run `cmake .. && make` inside `build/` |
| Server not stopping | `./http_server.sh stop` sends SIGTERM then SIGKILL if needed |
