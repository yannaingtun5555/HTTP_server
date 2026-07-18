#!/bin/bash

# Always run from the script's directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

if [ -x "./build/HTTP_Server" ]; then
    EXECUTABLE="./build/HTTP_Server"
elif [ -x "./HTTP_Server" ]; then
    EXECUTABLE="./HTTP_Server"
else
    echo "Error: HTTP_Server binary not found. Build first: mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

LOG_FILE="./logs/temp.log"
PID_FILE="./HTTP_Server.pid"
MODE_FILE="./HTTP_Server.mode"
CONFIG_FILE="./server.conf"
LOGS_DIR="./logs/server_log"
WEB_ROOT="/var/www/html"

# ── Admin panel settings ──────────────────────────────────────
ADMIN_DIR="$SCRIPT_DIR/admin_panel"
ADMIN_PID_FILE="/tmp/admin_panel.pid"
ADMIN_LOG_FILE="./logs/admin_panel.log"
PG_CONTAINER_NAME="pg_server_db"
PG_IMAGE="postgres:15-alpine"
PYTHON="/home/yan9htun/.pyenv/versions/3.12.13/bin/python3"
GUNICORN="/home/yan9htun/.pyenv/versions/3.12.13/bin/gunicorn"

mkdir -p ./logs

setup_permissions() {
    echo "Checking permissions for $WEB_ROOT..."

    if [ ! -d "$WEB_ROOT" ]; then
        echo "Directory $WEB_ROOT does not exist. Creating it..."
        sudo mkdir -p "$WEB_ROOT"
        sudo chown -R "$(whoami):$(whoami)" "$WEB_ROOT"
        sudo chmod -R 755 "$WEB_ROOT"
        sudo find "$WEB_ROOT" -type f -exec chmod 644 {} \;
        echo "Directory created and permissions set."
        return
    fi

    current_owner=$(stat -c "%U:%G" "$WEB_ROOT")
    required_owner="$(whoami):$(whoami)"
    if [ "$current_owner" != "$required_owner" ]; then
        echo "Changing ownership of $WEB_ROOT to $required_owner..."
        sudo chown -R "$required_owner" "$WEB_ROOT"
    fi

    current_permissions=$(stat -c "%a" "$WEB_ROOT")
    if [ "$current_permissions" != "755" ]; then
        echo "Setting directory permissions to 755..."
        sudo chmod -R 755 "$WEB_ROOT"
    fi

    find "$WEB_ROOT" -type f -exec sh -c 'test "$(stat -c "%a" "$1")" = "644" || echo "Changing file permissions of $1 to 644..." && chmod 644 "$1"' sh {} \;

    echo "Permissions checked and updated if necessary."
}

is_running() {
    if [ ! -f "$PID_FILE" ]; then
        return 1
    fi

    local pid
    pid=$(cat "$PID_FILE")
    if [ -z "$pid" ] || ! ps -p "$pid" > /dev/null 2>&1; then
        rm -f "$PID_FILE" "$MODE_FILE"
        return 1
    fi

    return 0
}

get_mode() {
    if [ -f "$MODE_FILE" ]; then
        cat "$MODE_FILE"
    else
        echo "static"
    fi
}

update_config() {
    local key="$1"
    local new_value="$2"

    if [ -f "$CONFIG_FILE" ]; then
        sed -i "s/^$key=.*/$key=$new_value/" "$CONFIG_FILE"
        echo "$key updated to $new_value in $CONFIG_FILE"
    else
        echo "Configuration file not found!"
        exit 1
    fi
}

# ── Admin panel helpers ───────────────────────────────────────

admin_pg_running() {
    docker inspect -f '{{.State.Running}}' "$PG_CONTAINER_NAME" 2>/dev/null | grep -q 'true'
}

admin_panel_running() {
    # Primary: check PID file
    if [ -f "$ADMIN_PID_FILE" ]; then
        local pid
        pid=$(cat "$ADMIN_PID_FILE" 2>/dev/null)
        [ -n "$pid" ] && ps -p "$pid" > /dev/null 2>&1 && return 0
    fi
    # Fallback: look for a gunicorn process bound to 8080
    pgrep -f 'gunicorn.*admin_panel.wsgi' > /dev/null 2>&1
}

start_admin_panel() {
    echo ""
    echo "[Admin] Starting PostgreSQL..."

    # If the container exists but is stopped, just start it; otherwise create it
    if docker inspect "$PG_CONTAINER_NAME" > /dev/null 2>&1; then
        docker start "$PG_CONTAINER_NAME" > /dev/null 2>&1
    else
        docker run -d --name "$PG_CONTAINER_NAME" \
            -e POSTGRES_DB=server_db \
            -e POSTGRES_USER=admin \
            -e POSTGRES_PASSWORD=changeme \
            -p 127.0.0.1:5432:5432 \
            "$PG_IMAGE" > /dev/null 2>&1
    fi

    # Wait for PostgreSQL to be ready (up to 20 s)
    local retries=20
    printf "[Admin] Waiting for PostgreSQL"
    while [ $retries -gt 0 ]; do
        if docker exec "$PG_CONTAINER_NAME" pg_isready -U admin -d server_db > /dev/null 2>&1; then
            echo " ready."
            break
        fi
        printf "."
        sleep 1
        retries=$((retries - 1))
    done

    if [ $retries -eq 0 ]; then
        echo " TIMEOUT. Check Docker."
        return 1
    fi

    # Apply schema (idempotent — uses IF NOT EXISTS)
    docker exec -i "$PG_CONTAINER_NAME" \
        psql -U admin -d server_db < "$SCRIPT_DIR/DB/schema.sql" > /dev/null 2>&1

    # Run Django migrations (subshell keeps cwd intact)
    ( cd "$ADMIN_DIR" && "$PYTHON" manage.py migrate --run-syncdb > /dev/null 2>&1 )

    echo "[Admin] Starting admin panel (gunicorn)..."

    # Kill any stale gunicorn process on 8080 before starting fresh
    if admin_panel_running; then
        pkill -f 'gunicorn.*admin_panel.wsgi' 2>/dev/null
        sleep 1
        rm -f "$ADMIN_PID_FILE"
    fi

    # Start gunicorn in a subshell so cwd is not changed for the parent
    ( cd "$ADMIN_DIR" && \
        "$GUNICORN" \
            --bind 127.0.0.1:8080 \
            --workers 2 \
            --pid "$ADMIN_PID_FILE" \
            --log-file "$SCRIPT_DIR/$ADMIN_LOG_FILE" \
            --daemon \
            admin_panel.wsgi:application )

    # Wait up to 5 s for gunicorn to write its PID file
    local g_wait=5
    while [ $g_wait -gt 0 ]; do
        if admin_panel_running; then break; fi
        sleep 1
        g_wait=$((g_wait - 1))
    done

    if admin_panel_running; then
        local admin_pid
        admin_pid=$(cat "$ADMIN_PID_FILE" 2>/dev/null || pgrep -f 'gunicorn.*admin_panel.wsgi' | head -1)
        echo "[Admin] Admin panel running (PID: $admin_pid)."
        echo "[Admin] Access at: http://localhost:8000/_admin/"
    else
        echo "[Admin] WARNING: Admin panel failed to start. Check $ADMIN_LOG_FILE"
    fi
}

stop_admin_panel() {
    echo ""
    if admin_panel_running; then
        echo "[Admin] Stopping admin panel (gunicorn)..."
        # Kill master + all workers by matching the process name
        pkill -f 'gunicorn.*admin_panel.wsgi' 2>/dev/null
        sleep 2
        # Force-kill any survivors
        pkill -9 -f 'gunicorn.*admin_panel.wsgi' 2>/dev/null
        rm -f "$ADMIN_PID_FILE"
        echo "[Admin] Admin panel stopped."
    else
        echo "[Admin] Admin panel is not running."
        rm -f "$ADMIN_PID_FILE"
    fi

    if admin_pg_running; then
        echo "[Admin] Stopping PostgreSQL container..."
        docker stop "$PG_CONTAINER_NAME" > /dev/null 2>&1
        echo "[Admin] PostgreSQL stopped."
    fi
}

status_admin_panel() {
    echo ""
    echo "  --- Admin Panel ---"
    if admin_panel_running; then
        local admin_pid
        admin_pid=$(cat "$ADMIN_PID_FILE" 2>/dev/null || pgrep -f 'gunicorn.*admin_panel.wsgi' | head -1)
        echo "  Django/gunicorn : RUNNING  (PID: $admin_pid)"
        echo "  Access URL      : http://localhost:8000/_admin/"
    else
        echo "  Django/gunicorn : STOPPED"
    fi
    if admin_pg_running; then
        echo "  PostgreSQL      : RUNNING  (Docker: $PG_CONTAINER_NAME)"
    else
        echo "  PostgreSQL      : STOPPED"
    fi
}

# ── Main server start helper ───────────────────────────────────

start_server() {
    local mode="$1"   # start | reverse
    local label="$2"

    if is_running; then
        echo "Server is already running (PID: $(cat "$PID_FILE"), mode: $(get_mode))."
        echo "Stop it first with: $0 stop"
        return 1
    fi

    echo "Starting server in $label mode..."

    if [ "$mode" = "start" ]; then
        setup_permissions
    fi

    # Start admin panel first so it's ready when C++ server comes up
    start_admin_panel

    nohup "$EXECUTABLE" "$mode" >> "$LOG_FILE" 2>&1 &
    local pid=$!
    echo "$pid" > "$PID_FILE"
    echo "$mode" > "$MODE_FILE"

    sleep 2

    if ! ps -p "$pid" > /dev/null 2>&1; then
        echo "Server failed to start. Check $LOG_FILE"
        rm -f "$PID_FILE" "$MODE_FILE"
        tail -20 "$LOG_FILE" 2>/dev/null
        return 1
    fi

    echo ""
    echo "Server started in $label mode (PID: $pid)."
    echo "Logs: $LOG_FILE"
    echo "Stop with: $0 stop"
}

start() {
    start_server "start" "STATIC"
}

reverse() {
    start_server "reverse" "REVERSE PROXY"
}

stop() {
    if ! is_running; then
        echo "Server is not running."
        rm -f "$PID_FILE" "$MODE_FILE"
    else
        local pid mode
        pid=$(cat "$PID_FILE")
        mode=$(get_mode)

        echo "Stopping server (PID: $pid, mode: $mode)..."

        kill "$pid" 2>/dev/null

        for _ in 1 2 3 4 5; do
            if ! ps -p "$pid" > /dev/null 2>&1; then
                break
            fi
            sleep 1
        done

        if ps -p "$pid" > /dev/null 2>&1; then
            echo "Process did not exit gracefully, sending SIGKILL..."
            kill -9 "$pid" 2>/dev/null
        fi

        rm -f "$PID_FILE" "$MODE_FILE"
        echo "Server stopped. Log file kept at $LOG_FILE."
    fi

    # Always stop admin panel alongside the main server
    stop_admin_panel
}

status() {
    if is_running; then
        echo "Server is running."
        echo "  PID  : $(cat "$PID_FILE")"
        echo "  Mode : $(get_mode)"
        echo "  Logs : $LOG_FILE"
    else
        echo "Server is not running."
    fi
    status_admin_panel
}

log() {
    if [ -f "$LOG_FILE" ]; then
        tail -f "$LOG_FILE"
    else
        echo "Log file not found."
    fi
}

restart() {
    local mode
    if [ -f "$MODE_FILE" ]; then
        mode=$(cat "$MODE_FILE")
    else
        mode="start"
    fi

    stop

    if [ "$mode" = "reverse" ]; then
        reverse
    else
        start
    fi
}

clear_logs() {
    if [ -d "$LOGS_DIR" ]; then
        echo "Clearing log files in $LOGS_DIR..."
        sudo rm -rf "$LOGS_DIR"/*
        echo "Log files cleared."
    else
        echo "Log directory $LOGS_DIR does not exist."
        exit 1
    fi
}

help() {
    echo "Usage: $0 {start|reverse|stop|status|log|restart|config|clear_logs|sites|deploy|delete-site|site-logs|help}"
    echo
    echo "Server commands:"
    echo "  start        Start in platform mode (virtual-host static + K8s backends)"
    echo "               Also auto-starts: PostgreSQL (Docker) + Admin Panel (gunicorn)"
    echo "  reverse      Start in legacy REVERSE PROXY mode (background)"
    echo "               Also auto-starts: PostgreSQL (Docker) + Admin Panel (gunicorn)"
    echo "  stop         Stop the server + admin panel + PostgreSQL"
    echo "  status       Show PID, running mode, and admin panel status"
    echo "  log          Tail the server log"
    echo "  restart      Restart in the same mode as last run"
    echo "  config <key> <value>  Update server.conf"
    echo "  clear_logs   Clear files in $LOGS_DIR"
    echo
    echo "Platform commands:"
    echo "  sites        List all managed sites from server DB"
    echo "  deploy <domain>  Trigger re-deploy of a site (calls internal API)"
    echo "  delete-site <domain>  Delete a site and its K8s containers"
    echo "  site-logs <domain>  Stream K8s BE pod logs for a site"
    echo "  help         Show this help"
    echo
    echo "Admin panel:"
    echo "  URL          http://localhost:8000/_admin/"
    echo "  Log          $ADMIN_LOG_FILE"
}

# ── Platform commands ─────────────────────────────────────────
sites() {
    INTERNAL_SECRET=$(grep '^internal_api_secret=' server.conf 2>/dev/null | cut -d= -f2 || echo 'changeme-secret')
    API_URL=$(grep '^internal_api_path_prefix=' server.conf 2>/dev/null | cut -d= -f2 || echo '/_api/internal')
    PORT=$(grep '^server_port=' server.conf 2>/dev/null | cut -d= -f2 || echo '80')
    curl -s -H "X-Internal-Secret: $INTERNAL_SECRET" \
         "http://127.0.0.1:$PORT${API_URL}/status/" 2>/dev/null \
         || echo "Server not running or internal API unavailable"
}

deploy_site() {
    local domain="$1"
    if [ -z "$domain" ]; then echo "Usage: $0 deploy <domain>"; exit 1; fi
    INTERNAL_SECRET=$(grep '^internal_api_secret=' server.conf 2>/dev/null | cut -d= -f2 || echo 'changeme-secret')
    PORT=$(grep '^server_port=' server.conf 2>/dev/null | cut -d= -f2 || echo '80')
    echo "Triggering deploy for $domain via internal API..."
    curl -s -X POST \
         -H "X-Internal-Secret: $INTERNAL_SECRET" \
         -H "Content-Type: application/json" \
         -d "{\"domain\":\"$domain\"}" \
         "http://127.0.0.1:$PORT/_api/internal/reload"
    echo
}

delete_site_cmd() {
    local domain="$1"
    if [ -z "$domain" ]; then echo "Usage: $0 delete-site <domain>"; exit 1; fi
    INTERNAL_SECRET=$(grep '^internal_api_secret=' server.conf 2>/dev/null | cut -d= -f2 || echo 'changeme-secret')
    PORT=$(grep '^server_port=' server.conf 2>/dev/null | cut -d= -f2 || echo '80')
    echo "Deleting site $domain..."
    curl -s -X DELETE \
         -H "X-Internal-Secret: $INTERNAL_SECRET" \
         "http://127.0.0.1:$PORT/_api/internal/delete/$domain"
    echo
}

site_logs() {
    local domain="$1"
    if [ -z "$domain" ]; then echo "Usage: $0 site-logs <domain>"; exit 1; fi
    safe=$(echo "$domain" | tr '.' '-')
    echo "Fetching K8s logs for be-$safe in namespace site-$safe..."
    kubectl logs -n "site-$safe" -l "app=be-$safe" --tail=100 -f 2>/dev/null \
        || echo "kubectl not configured or pod not found"
}

case "$1" in
    start)
        start
        ;;
    reverse)
        reverse
        ;;
    stop)
        stop
        ;;
    status)
        status
        ;;
    log)
        log
        ;;
    restart)
        restart
        ;;
    config)
        if [ "$#" -ne 3 ]; then
            echo "Usage: $0 config <key> <new_value>"
            exit 1
        fi
        update_config "$2" "$3"
        ;;
    clear_logs)
        clear_logs
        ;;
    sites)
        sites
        ;;
    deploy)
        deploy_site "$2"
        ;;
    delete-site)
        delete_site_cmd "$2"
        ;;
    site-logs)
        site_logs "$2"
        ;;
    help|--help|-h)
        help
        ;;
    *)
        echo "Usage: $0 {start|reverse|stop|status|log|restart|config|clear_logs|sites|deploy|delete-site|site-logs|help}"
        exit 1
        ;;
esac
