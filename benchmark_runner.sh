#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════
#  benchmark_runner.sh
#  Production-grade benchmark orchestration for the K8s PaaS stack.
#
#  Usage:
#    ./benchmark_runner.sh [NAMESPACE] [BACKEND_URL]
#
#  Defaults:
#    NAMESPACE   = site-benchmark-local
#    BACKEND_URL = http://127.0.0.1:3000
#
#  Outputs (all written to ./benchmark/):
#    load_test_results.txt   — wrk/ab raw output
#    resource_usage.csv      — per-second kubectl top pods
#    summary_report.json     — latency percentiles + error rates
# ═══════════════════════════════════════════════════════════════
set -euo pipefail

# ── Configuration ─────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BENCHMARK_DIR="${SCRIPT_DIR}/benchmark"
NAMESPACE="${1:-site-benchmark-local}"
BACKEND_URL="${2:-http://127.0.0.1:3000}"
HEALTH_ENDPOINT="${BACKEND_URL}/health"
DATA_ENDPOINT="${BACKEND_URL}/data"

# Load test parameters
CONCURRENT_USERS=50
TOTAL_REQUESTS=10000
TEST_DURATION="30s"       # used by wrk; ab uses total requests
RESOURCE_POLL_INTERVAL=1  # seconds between kubectl top calls

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Helpers ───────────────────────────────────────────────────
log()  { echo -e "${CYAN}[benchmark]${NC} $*"; }
ok()   { echo -e "${GREEN}[✓]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }
err()  { echo -e "${RED}[✗]${NC} $*"; }
die()  { err "$@"; exit 1; }

# ── Detect available load-test tool ───────────────────────────
detect_load_tool() {
    if command -v wrk &>/dev/null; then
        echo "wrk"
    elif command -v hey &>/dev/null; then
        echo "hey"
    elif command -v ab &>/dev/null; then
        echo "ab"
    else
        echo "none"
    fi
}

# ═══════════════════════════════════════════════════════════════
#  PHASE 0: Pre-flight checks
# ═══════════════════════════════════════════════════════════════
preflight() {
    log "Phase 0: Pre-flight checks"

    # Check kubectl
    if ! command -v kubectl &>/dev/null; then
        warn "kubectl not found — resource monitoring will be skipped"
    else
        ok "kubectl available"
    fi

    # Check load test tool
    LOAD_TOOL="$(detect_load_tool)"
    if [[ "$LOAD_TOOL" == "none" ]]; then
        warn "No load-test tool found (wrk, hey, or ab). Will use curl fallback."
        LOAD_TOOL="curl"
    else
        ok "Load-test tool: ${LOAD_TOOL}"
    fi

    # Check namespace (if kubectl available)
    if command -v kubectl &>/dev/null; then
        if kubectl get namespace "$NAMESPACE" &>/dev/null; then
            ok "Namespace '${NAMESPACE}' exists"
        else
            warn "Namespace '${NAMESPACE}' not found — resource monitoring disabled"
        fi
    fi

    # Check backend is reachable
    log "Checking backend at ${HEALTH_ENDPOINT} ..."
    local http_code
    http_code=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 5 "$HEALTH_ENDPOINT" 2>/dev/null || echo "000")
    if [[ "$http_code" == "200" ]]; then
        ok "Backend reachable (HTTP ${http_code})"
    else
        die "Backend unreachable at ${HEALTH_ENDPOINT} (HTTP ${http_code}). Start the backend first."
    fi
}

# ═══════════════════════════════════════════════════════════════
#  PHASE 1: Directory cleanup
# ═══════════════════════════════════════════════════════════════
cleanup_output_dir() {
    log "Phase 1: Cleaning output directory"

    mkdir -p "$BENCHMARK_DIR"

    # Remove previous results safely
    local count=0
    for ext in txt json csv log; do
        # Note the explicit space before 'do' and split logic
        for f in "${BENCHMARK_DIR}"/*."${ext}"; do
            if [[ -e "$f" ]]; then
                rm -f "$f"
                ((count++))
            fi
        done
    done

    if [[ $count -gt 0 ]]; then
        ok "Removed ${count} stale file(s) from ${BENCHMARK_DIR}/"
    else
        ok "Output directory clean"
    fi
}

# ═══════════════════════════════════════════════════════════════
#  PHASE 2: Resource monitoring (background)
# ═══════════════════════════════════════════════════════════════
RESOURCE_PID=""

start_resource_monitor() {
    local csv_file="${BENCHMARK_DIR}/resource_usage.csv"

    if ! command -v kubectl &>/dev/null; then
        warn "kubectl not available — skipping resource monitoring"
        return
    fi

    if ! kubectl get namespace "$NAMESPACE" &>/dev/null 2>&1; then
        warn "Namespace '${NAMESPACE}' not found — skipping resource monitoring"
        return
    fi

    log "Phase 2: Starting resource monitor (namespace=${NAMESPACE})"

    # CSV header
    echo "timestamp,pod_name,cpu_cores,memory_mib" > "$csv_file"

    # Background loop: capture kubectl top every RESOURCE_POLL_INTERVAL seconds
    (
        while true; do
            local ts
            ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

            # kubectl top pods outputs:
            #   NAME          CPU(cores)   MEMORY(bytes)
            #   be-xxx-yyy    12m          45Mi
            kubectl top pods -n "$NAMESPACE" --no-headers 2>/dev/null | while read -r pod cpu mem; do
                # Normalize CPU: strip trailing 'm', convert cores to millicores string
                local cpu_val="$cpu"
                # Normalize Memory: strip trailing 'Mi', 'Gi', 'Ki'
                local mem_val="$mem"

                # Convert memory to MiB
                local mem_mib
                if [[ "$mem" == *Gi ]]; then
                    mem_mib=$(echo "${mem%Gi} * 1024" | bc 2>/dev/null || echo "${mem%Gi}")
                elif [[ "$mem" == *Mi ]]; then
                    mem_mib="${mem%Mi}"
                elif [[ "$mem" == *Ki ]]; then
                    mem_mib=$(echo "scale=2; ${mem%Ki} / 1024" | bc 2>/dev/null || echo "${mem%Ki}")
                else
                    mem_mib="$mem"
                fi

                echo "${ts},${pod},${cpu_val},${mem_mib}" >> "$csv_file"
            done

            sleep "$RESOURCE_POLL_INTERVAL"
        done
    ) &
    RESOURCE_PID=$!
    ok "Resource monitor started (PID ${RESOURCE_PID})"
}

stop_resource_monitor() {
    if [[ -n "$RESOURCE_PID" ]] && kill -0 "$RESOURCE_PID" 2>/dev/null; then
        kill "$RESOURCE_PID" 2>/dev/null || true
        wait "$RESOURCE_PID" 2>/dev/null || true
        ok "Resource monitor stopped"
    fi

    local csv_file="${BENCHMARK_DIR}/resource_usage.csv"
    if [[ -f "$csv_file" ]]; then
        local lines
        lines=$(wc -l < "$csv_file")
        ok "Resource data: ${lines} entries in resource_usage.csv"
    fi
}

# ═══════════════════════════════════════════════════════════════
#  PHASE 3: Load testing
# ═══════════════════════════════════════════════════════════════
run_load_test() {
    local results_file="${BENCHMARK_DIR}/load_test_results.txt"

    log "Phase 3: Load testing (${CONCURRENT_USERS} concurrent, ${TOTAL_REQUESTS} requests)"

    {
        echo "════════════════════════════════════════════════════"
        echo "  Load Test Results"
        echo "  Backend:     ${BACKEND_URL}"
        echo "  Concurrency: ${CONCURRENT_USERS}"
        echo "  Requests:    ${TOTAL_REQUESTS}"
        echo "  Tool:        ${LOAD_TOOL}"
        echo "  Started:     $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "════════════════════════════════════════════════════"
        echo ""
    } > "$results_file"

    case "$LOAD_TOOL" in
        wrk)
            run_load_wrk "$results_file"
            ;;
        hey)
            run_load_hey "$results_file"
            ;;
        ab)
            run_load_ab "$results_file"
            ;;
        curl)
            run_load_curl "$results_file"
            ;;
    esac

    ok "Load test complete — results in load_test_results.txt"
}

# ── wrk ───────────────────────────────────────────────────────
run_load_wrk() {
    local out="$1"

    # Create a Lua script for POST /data requests
    local lua_script="${BENCHMARK_DIR}/.wrk_post.lua"
    cat > "$lua_script" << 'LUAEOF'
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"
counter = 0
function request()
    counter = counter + 1
    local body = string.format('{"key":"wrk_%d","value":"bench"}', counter)
    return wrk.format(nil, nil, nil, body)
end
LUAEOF

    echo "── GET /health ──────────────────────────────────" >> "$out"
    wrk -t4 -c"$CONCURRENT_USERS" -d"$TEST_DURATION" \
        "$HEALTH_ENDPOINT" 2>&1 >> "$out"

    echo "" >> "$out"
    echo "── POST /data ───────────────────────────────────" >> "$out"
    wrk -t4 -c"$CONCURRENT_USERS" -d"$TEST_DURATION" \
        -s "$lua_script" "$DATA_ENDPOINT" 2>&1 >> "$out"

    rm -f "$lua_script"
}

# ── hey ───────────────────────────────────────────────────────
run_load_hey() {
    local out="$1"

    echo "── GET /health ──────────────────────────────────" >> "$out"
    hey -n "$TOTAL_REQUESTS" -c "$CONCURRENT_USERS" \
        "$HEALTH_ENDPOINT" 2>&1 >> "$out"

    echo "" >> "$out"
    echo "── POST /data ───────────────────────────────────" >> "$out"
    hey -n "$TOTAL_REQUESTS" -c "$CONCURRENT_USERS" \
        -m POST -T "application/json" \
        -d '{"key":"hey_bench","value":"test"}' \
        "$DATA_ENDPOINT" 2>&1 >> "$out"
}

# ── ab (ApacheBench) ──────────────────────────────────────────
run_load_ab() {
    local out="$1"

    echo "── GET /health ──────────────────────────────────" >> "$out"
    ab -n "$TOTAL_REQUESTS" -c "$CONCURRENT_USERS" \
        "$HEALTH_ENDPOINT" 2>&1 >> "$out"

    echo "" >> "$out"
    echo "── POST /data ───────────────────────────────────" >> "$out"

    local post_body="${BENCHMARK_DIR}/.ab_post.json"
    echo '{"key":"ab_bench","value":"test"}' > "$post_body"
    ab -n "$TOTAL_REQUESTS" -c "$CONCURRENT_USERS" \
        -p "$post_body" -T "application/json" \
        "$DATA_ENDPOINT" 2>&1 >> "$out"
    rm -f "$post_body"
}

# ── curl fallback (sequential with timing) ────────────────────
run_load_curl() {
    local out="$1"
    local total=200  # reduced for curl fallback
    local success_2xx=0
    local fail_5xx=0
    local fail_other=0
    local latencies=()

    echo "── curl fallback (${total} sequential requests) ─" >> "$out"
    echo "" >> "$out"

    for ((i=1; i<=total; i++)); do
        # Alternate GET /health and POST /data
        if (( i % 2 == 0 )); then
            local result
            result=$(curl -s -o /dev/null -w "%{http_code} %{time_total}" \
                     --connect-timeout 5 --max-time 10 \
                     -X POST -H "Content-Type: application/json" \
                     -d "{\"key\":\"curl_${i}\",\"value\":\"test\"}" \
                     "$DATA_ENDPOINT" 2>/dev/null || echo "000 0")
        else
            local result
            result=$(curl -s -o /dev/null -w "%{http_code} %{time_total}" \
                     --connect-timeout 5 --max-time 10 \
                     "$HEALTH_ENDPOINT" 2>/dev/null || echo "000 0")
        fi

        local code time_s
        code=$(echo "$result" | awk '{print $1}')
        time_s=$(echo "$result" | awk '{print $2}')

        # Convert seconds to milliseconds
        local time_ms
        time_ms=$(echo "$time_s * 1000" | bc 2>/dev/null || echo "0")
        latencies+=("$time_ms")

        if [[ "$code" =~ ^2 ]]; then
            ((success_2xx++))
        elif [[ "$code" =~ ^5 ]]; then
            ((fail_5xx++))
        else
            ((fail_other++))
        fi

        # Progress every 50 requests
        if (( i % 50 == 0 )); then
            echo "  [${i}/${total}] 2xx=${success_2xx} 5xx=${fail_5xx}" >> "$out"
        fi
    done

    echo "" >> "$out"
    echo "Total:    ${total}" >> "$out"
    echo "Success:  ${success_2xx}" >> "$out"
    echo "5xx:      ${fail_5xx}" >> "$out"
    echo "Other:    ${fail_other}" >> "$out"
}

# ═══════════════════════════════════════════════════════════════
#  PHASE 4: Latency analysis + summary report
# ═══════════════════════════════════════════════════════════════
generate_summary() {
    local summary_file="${BENCHMARK_DIR}/summary_report.json"
    local results_file="${BENCHMARK_DIR}/load_test_results.txt"

    log "Phase 4: Generating summary report"

    # Run a dedicated latency probe: 500 timed requests
    local latency_count=500
    local latency_raw="${BENCHMARK_DIR}/.latency_raw.txt"
    > "$latency_raw"

    log "  Collecting ${latency_count} latency samples ..."

    local success_2xx=0
    local fail_5xx=0
    local fail_other=0

    for ((i=1; i<=latency_count; i++)); do
        local result
        if (( i % 3 == 0 )); then
            result=$(curl -s -o /dev/null -w "%{http_code} %{time_total}" \
                     --connect-timeout 5 --max-time 10 \
                     -X POST -H "Content-Type: application/json" \
                     -d "{\"key\":\"lat_${i}\",\"value\":\"probe\"}" \
                     "$DATA_ENDPOINT" 2>/dev/null || echo "000 0")
        else
            result=$(curl -s -o /dev/null -w "%{http_code} %{time_total}" \
                     --connect-timeout 5 --max-time 10 \
                     "$HEALTH_ENDPOINT" 2>/dev/null || echo "000 0")
        fi

        local code time_s
        code=$(echo "$result" | awk '{print $1}')
        time_s=$(echo "$result" | awk '{print $2}')

        # Convert to milliseconds
        local time_ms
        time_ms=$(echo "scale=2; $time_s * 1000" | bc 2>/dev/null || echo "0")
        echo "$time_ms" >> "$latency_raw"

        if [[ "$code" =~ ^2 ]]; then
            ((success_2xx++))
        elif [[ "$code" =~ ^5 ]]; then
            ((fail_5xx++))
        else
            ((fail_other++))
        fi
    done

    # Calculate percentiles
    local sorted_file="${BENCHMARK_DIR}/.latency_sorted.txt"
    sort -n "$latency_raw" > "$sorted_file"

    local total_lines
    total_lines=$(wc -l < "$sorted_file")

    local lat_min lat_max lat_median lat_p99 lat_avg

    if [[ $total_lines -gt 0 ]]; then
        lat_min=$(head -1 "$sorted_file")
        lat_max=$(tail -1 "$sorted_file")

        local median_line=$(( (total_lines + 1) / 2 ))
        lat_median=$(sed -n "${median_line}p" "$sorted_file")

        local p99_line=$(( (total_lines * 99 + 99) / 100 ))
        [[ $p99_line -gt $total_lines ]] && p99_line=$total_lines
        lat_p99=$(sed -n "${p99_line}p" "$sorted_file")

        lat_avg=$(awk '{ sum += $1; n++ } END { if (n>0) printf "%.2f", sum/n; else print "0" }' "$sorted_file")
    else
        lat_min="0"; lat_max="0"; lat_median="0"; lat_p99="0"; lat_avg="0"
    fi

    # Error rate
    local error_rate
    if [[ $latency_count -gt 0 ]]; then
        error_rate=$(echo "scale=4; ($fail_5xx + $fail_other) / $latency_count * 100" | bc 2>/dev/null || echo "0")
    else
        error_rate="0"
    fi

    # Write JSON summary
    cat > "$summary_file" << JSONEOF
{
    "benchmark_metadata": {
        "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
        "backend_url": "${BACKEND_URL}",
        "namespace": "${NAMESPACE}",
        "load_tool": "${LOAD_TOOL}",
        "concurrent_users": ${CONCURRENT_USERS},
        "total_load_requests": ${TOTAL_REQUESTS}
    },
    "latency_ms": {
        "sample_count": ${latency_count},
        "min": ${lat_min},
        "max": ${lat_max},
        "median": ${lat_median},
        "p99": ${lat_p99},
        "avg": ${lat_avg}
    },
    "status_codes": {
        "success_2xx": ${success_2xx},
        "server_error_5xx": ${fail_5xx},
        "other_errors": ${fail_other},
        "error_rate_percent": ${error_rate}
    },
    "output_files": {
        "load_test": "benchmark/load_test_results.txt",
        "resource_csv": "benchmark/resource_usage.csv",
        "summary": "benchmark/summary_report.json"
    }
}
JSONEOF

    # Cleanup temp files
    rm -f "$latency_raw" "$sorted_file"

    ok "Summary report written to summary_report.json"
}

# ═══════════════════════════════════════════════════════════════
#  PHASE 5: Print results
# ═══════════════════════════════════════════════════════════════
print_results() {
    local summary_file="${BENCHMARK_DIR}/summary_report.json"

    echo ""
    echo -e "${BOLD}═══════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  Benchmark Results Summary${NC}"
    echo -e "${BOLD}═══════════════════════════════════════════════════${NC}"

    if [[ -f "$summary_file" ]] && command -v python3 &>/dev/null; then
        python3 -c "
import json, sys
with open('${summary_file}') as f:
    d = json.load(f)
lat = d['latency_ms']
st  = d['status_codes']
print(f\"  Backend:     {d['benchmark_metadata']['backend_url']}\")
print(f\"  Load Tool:   {d['benchmark_metadata']['load_tool']}\")
print(f\"  Concurrency: {d['benchmark_metadata']['concurrent_users']}\")
print()
print(f\"  Latency (ms):\")
print(f\"    Min:    {lat['min']}\")
print(f\"    Median: {lat['median']}\")
print(f\"    Avg:    {lat['avg']}\")
print(f\"    p99:    {lat['p99']}\")
print(f\"    Max:    {lat['max']}\")
print()
print(f\"  Status Codes:\")
print(f\"    2xx OK:     {st['success_2xx']}\")
print(f\"    5xx Error:  {st['server_error_5xx']}\")
print(f\"    Other:      {st['other_errors']}\")
print(f\"    Error Rate: {st['error_rate_percent']}%\")
" 2>/dev/null || cat "$summary_file"
    elif [[ -f "$summary_file" ]]; then
        cat "$summary_file"
    fi

    echo ""
    echo -e "${BOLD}  Output files:${NC}"
    # Remove the 2>/dev/null from the 'for' line entirely
    for f in "${BENCHMARK_DIR}"/*.txt "${BENCHMARK_DIR}"/*.csv "${BENCHMARK_DIR}"/*.json; do
        if [[ -e "$f" ]]; then
            echo "    $(basename "$f") ($(wc -c < "$f" 2>/dev/null | awk '{print $1}') bytes)"
        fi
    done
    echo -e "${BOLD}═══════════════════════════════════════════════════${NC}"
}

# ═══════════════════════════════════════════════════════════════
#  MAIN
# ═══════════════════════════════════════════════════════════════
main() {
    echo ""
    echo -e "${BOLD}═══════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  K8s PaaS Benchmark Runner${NC}"
    echo -e "${BOLD}═══════════════════════════════════════════════════${NC}"
    echo ""

    preflight
    echo ""

    cleanup_output_dir
    echo ""

    start_resource_monitor
    echo ""

    run_load_test
    echo ""

    # Small gap to collect final resource samples
    sleep 2

    stop_resource_monitor
    echo ""

    generate_summary
    echo ""

    print_results
}

# Trap to ensure resource monitor is killed on exit
trap 'stop_resource_monitor 2>/dev/null' EXIT INT TERM

main "$@"
