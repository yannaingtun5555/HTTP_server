#!/usr/bin/env bash
# ============================================================
# HTTP_Server Performance Benchmark
# Usage:
#   ./benchmark.sh                    # Quick test (10s)
#   ./benchmark.sh full               # Full test suite
#   ./benchmark.sh custom 30s 4 200   # Custom: duration threads connections
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

HOST="127.0.0.1"
PORT="8000"
BASE_URL="http://${HOST}:${PORT}"
RESULT_FILE="$SCRIPT_DIR/benchmark_results.txt"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# ── Find wrk ─────────────────────────────────────────────────
find_wrk() {
    if [ -x "$SCRIPT_DIR/wrk" ]; then
        WRK="$SCRIPT_DIR/wrk"
    elif command -v wrk &>/dev/null; then
        WRK="wrk"
    else
        echo -e "${RED}[✗] wrk not found!${NC}"
        echo "  Install with: sudo dnf install wrk   (Fedora)"
        echo "            or: sudo apt install wrk   (Ubuntu)"
        exit 1
    fi
    echo -e "${GREEN}[✓]${NC} Using: $WRK"
}

# ── Check server is running ───────────────────────────────────
check_server() {
    echo -e "${CYAN}[→]${NC} Checking server at ${BASE_URL}..."
    if curl -s --connect-timeout 3 "$BASE_URL" >/dev/null 2>&1; then
        echo -e "${GREEN}[✓]${NC} Server is responding"
    else
        echo -e "${RED}[✗]${NC} Server not responding at ${BASE_URL}"
        echo ""
        echo "  Start the server first:"
        echo "    ./http_server.sh start"
        echo ""
        exit 1
    fi
}

# ── Run a single benchmark ────────────────────────────────────
run_bench() {
    local label="$1"
    local duration="$2"
    local threads="$3"
    local connections="$4"
    local url="$5"

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${CYAN}  $label${NC}"
    echo "  URL: $url"
    echo "  Duration: $duration | Threads: $threads | Connections: $connections"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""

    $WRK -t"$threads" -c"$connections" -d"$duration" --latency "$url" 2>&1 | tee -a "$RESULT_FILE"
    echo "" | tee -a "$RESULT_FILE"
}

# ── Quick Test ────────────────────────────────────────────────
quick_test() {
    local threads=$(nproc)
    [ "$threads" -gt 4 ] && threads=4

    echo "╔══════════════════════════════════════════════╗"
    echo "║    Quick Benchmark (10 seconds)             ║"
    echo "╚══════════════════════════════════════════════╝"

    echo "=== Quick Benchmark — $(date) ===" > "$RESULT_FILE"

    run_bench "Static File (GET /www/main.html)" "10s" "$threads" 100 "${BASE_URL}/www/main.html"

    echo -e "${GREEN}[✓]${NC} Results saved to: $RESULT_FILE"
}

# ── Full Test Suite ───────────────────────────────────────────
full_test() {
    local threads=$(nproc)
    [ "$threads" -gt 4 ] && threads=4

    echo "╔══════════════════════════════════════════════╗"
    echo "║    Full Benchmark Suite                     ║"
    echo "╚══════════════════════════════════════════════╝"

    echo "=== Full Benchmark Suite — $(date) ===" > "$RESULT_FILE"
    echo "System: $(uname -srm)" >> "$RESULT_FILE"
    echo "CPU: $(nproc) cores" >> "$RESULT_FILE"
    echo "" >> "$RESULT_FILE"

    # Test 1: Low concurrency (realistic)
    run_bench "Test 1: Low Concurrency" "10s" 2 10 "${BASE_URL}/www/main.html"

    # Test 2: Medium concurrency
    run_bench "Test 2: Medium Concurrency" "15s" "$threads" 100 "${BASE_URL}/www/main.html"

    # Test 3: High concurrency (stress test)
    run_bench "Test 3: High Concurrency (Stress)" "15s" "$threads" 500 "${BASE_URL}/www/main.html"

    # Test 4: Max connections (breaking point)
    run_bench "Test 4: Max Connections" "10s" "$threads" 1000 "${BASE_URL}/www/main.html"

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${GREEN}  All tests complete!${NC}"
    echo "  Results saved to: $RESULT_FILE"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

# ── Custom Test ───────────────────────────────────────────────
custom_test() {
    local duration="${1:-15s}"
    local threads="${2:-$(nproc)}"
    local connections="${3:-100}"

    echo "=== Custom Benchmark — $(date) ===" > "$RESULT_FILE"
    run_bench "Custom Test" "$duration" "$threads" "$connections" "${BASE_URL}/www/main.html"

    echo -e "${GREEN}[✓]${NC} Results saved to: $RESULT_FILE"
}

# ── Help ──────────────────────────────────────────────────────
show_help() {
    echo "Usage: $0 [mode] [options]"
    echo ""
    echo "Modes:"
    echo "  (no args)                Quick 10-second test"
    echo "  full                     Full suite (4 tests, ~1 minute)"
    echo "  custom <dur> <thr> <con> Custom test"
    echo "  help                     Show this help"
    echo ""
    echo "Examples:"
    echo "  $0                       # Quick test"
    echo "  $0 full                  # Full suite"
    echo "  $0 custom 30s 4 200      # 30s, 4 threads, 200 connections"
    echo ""
    echo "Prerequisites:"
    echo "  1. Server must be running:  ./http_server.sh start"
    echo "  2. wrk must be available (included in benchmarks/ or system)"
}

# ── Main ──────────────────────────────────────────────────────
main() {
    find_wrk
    check_server

    case "${1:-quick}" in
        quick|"")    quick_test ;;
        full)        full_test ;;
        custom)      custom_test "$2" "$3" "$4" ;;
        help|--help) show_help ;;
        *)           show_help ;;
    esac
}

main "$@"
