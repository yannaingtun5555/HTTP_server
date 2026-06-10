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
CONFIG_FILE="./config.txt"
LOGS_DIR="./logs/server_log"
WEB_ROOT="/var/www/html"

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
        return 0
    fi

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
    echo "Usage: $0 {start|reverse|stop|status|log|restart|config|clear_logs|help}"
    echo
    echo "Commands:"
    echo "  start        Start in STATIC file serving mode (background)"
    echo "  reverse      Start in REVERSE PROXY mode (background)"
    echo "  stop         Stop the server (works for both modes)"
    echo "  status       Show PID and running mode"
    echo "  log          Tail the server log"
    echo "  restart      Restart in the same mode as last run"
    echo "  config <key> <value>  Update config.txt"
    echo "  clear_logs   Clear files in $LOGS_DIR"
    echo "  help         Show this help"
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
    help|--help|-h)
        help
        ;;
    *)
        echo "Usage: $0 {start|reverse|stop|status|log|restart|config|clear_logs|help}"
        exit 1
        ;;
esac
