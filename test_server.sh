#!/usr/bin/env bash
# ================================================================
#  test_server.sh  —  HTTP Platform Server Test Suite
#  Usage:  bash test_server.sh [port]      (default 8000)
#  All tests complete in ~15 seconds
# ================================================================

PORT=${1:-8000}
BASE="http://127.0.0.1:${PORT}"
SERVER_BIN="./build/HTTP_Server"
SERVER_CONF="./server.conf"
PASS=0
FAIL=0
WARN=0
SRV_PID=0
LOG=/tmp/srv_test.log

# ── helpers ────────────────────────────────────────────────────
pass() { echo "PASS | $1"; PASS=$((PASS+1)); }
fail() { echo "FAIL | $1"; FAIL=$((FAIL+1)); }
warn() { echo "WARN | $1"; WARN=$((WARN+1)); }
info() { echo "INFO | $1"; }
sep()  { printf '\n%-60s\n' "--- $1 " | tr ' ' '-'; }

G()    { curl -s -o /dev/null -w "%{http_code}" --max-time 3 "$@" 2>/dev/null; }
GBODY(){ curl -s --max-time 3 "$@" 2>/dev/null; }
GHEAD(){ curl -s -I  --max-time 3 "$@" 2>/dev/null; }

# ── server lifecycle ──────────────────────────────────────────
stop_server() {
    if [ "$SRV_PID" -gt 0 ]; then
        kill "$SRV_PID" 2>/dev/null
        wait "$SRV_PID" 2>/dev/null
        SRV_PID=0
    fi
}
trap stop_server EXIT

start_server() {
    # kill any leftover server
    pkill -x HTTP_Server 2>/dev/null || true
    sleep 0.3

    "$SERVER_BIN" "$SERVER_CONF" >"$LOG" 2>&1 &
    SRV_PID=$!

    # wait up to 5 s for port to open
    local i=0
    while [ $i -lt 20 ]; do
        if ss -tlnp 2>/dev/null | grep -q ":${PORT}"; then
            info "Server started  PID=$SRV_PID  port=$PORT"
            return 0
        fi
        sleep 0.25
        i=$((i+1))
    done
    echo "ERROR: server did not start within 5 s"
    cat "$LOG"
    exit 1
}

# ── fixtures ──────────────────────────────────────────────────
fixtures() {
    mkdir -p sites/localhost/public/about
    printf '<html><head><title>localhost</title></head><body><h1>localhost vhost</h1></body></html>\n' \
        > sites/localhost/public/index.html
    printf '<html><body><h1>About localhost</h1></body></html>\n' \
        > sites/localhost/public/about/index.html

    mkdir -p sites/smoke.test/public
    printf '<html><head><title>smoke.test</title></head><body><h1>smoke.test vhost</h1></body></html>\n' \
        > sites/smoke.test/public/index.html

    mkdir -p sites/example.com/public
    printf '<html><body><h1>example.com</h1></body></html>\n' \
        > sites/example.com/public/index.html

    info "Fixtures ready"
}

# parallel GET — fires $1 requests to Host:$2 path:$3, echoes count of 200s
par_gets() {
    local n=$1 host=$2 path=${3:-/}
    local tmp; tmp=$(mktemp)
    local i pids
    pids=""
    for i in $(seq 1 "$n"); do
        # append newline after status code so grep -c works
        { G -H "Host: $host" "$BASE$path"; echo; } >>"$tmp" &
        pids="$pids $!"
    done
    for p in $pids; do wait "$p" 2>/dev/null || true; done
    local cnt; cnt=$(grep -c "^200$" "$tmp" 2>/dev/null || echo 0)
    rm -f "$tmp"
    printf '%d' "$cnt"
}

# ── test groups ───────────────────────────────────────────────

t_static() {
    sep "GROUP 1  Static Serving"
    local S B

    S=$(G -H "Host: localhost" "$BASE/")
    [ "$S" = "200" ] && pass "1.1  GET /  Host:localhost  ->  $S" \
                     || fail "1.1  GET /  expected 200 got $S"

    B=$(GBODY -H "Host: localhost" "$BASE/")
    echo "$B" | grep -qi "localhost" \
        && pass "1.2  Body has 'localhost' ($(printf '%s' "$B" | wc -c) bytes)" \
        || fail "1.2  Body missing 'localhost'"

    S=$(G -H "Host: localhost" "$BASE/about/")
    [ "$S" = "200" ] && pass "1.3  GET /about/  ->  $S" \
                     || fail "1.3  GET /about/  expected 200 got $S"

    S=$(G -H "Host: localhost" "$BASE/no-such-file-xyz.html")
    [ "$S" = "404" ] && pass "1.4  404 for missing file  ->  $S" \
                     || fail "1.4  404  expected 404 got $S"
}

t_vhosts() {
    sep "GROUP 2  Virtual Hosts"
    local S B1 B2

    S=$(G -H "Host: smoke.test" "$BASE/")
    [ "$S" = "200" ] && pass "2.1  smoke.test  ->  $S" \
                     || fail "2.1  smoke.test  expected 200 got $S"

    S=$(G -H "Host: example.com" "$BASE/")
    [ "$S" = "200" ] && pass "2.2  example.com  ->  $S" \
                     || fail "2.2  example.com  expected 200 got $S"

    B1=$(GBODY -H "Host: localhost"  "$BASE/")
    B2=$(GBODY -H "Host: smoke.test" "$BASE/")
    [ "$B1" != "$B2" ] \
        && pass "2.3  Content isolated (localhost != smoke.test)" \
        || warn "2.3  Both vhosts returned same body"

    S=$(G -H "Host: smoke.test" "$BASE/about/")
    [ "$S" = "404" ] \
        && pass "2.4  smoke.test /about/  ->  404  (not shared)" \
        || warn "2.4  smoke.test /about/  ->  $S"

    S=$(G -H "Host: no.such.host.xyz" "$BASE/")
    case "$S" in 200|404) pass "2.5  Unknown vhost  ->  $S  (no crash)" ;;
                       *) fail "2.5  Unknown vhost  ->  $S" ;; esac
}

t_api() {
    sep "GROUP 3  Internal API"
    local S J SEC="changeme-secret"

    S=$(G -H "Host: localhost" "$BASE/_api/internal/status")
    [ "$S" = "401" ] && pass "3.1  No auth  ->  401" \
                     || fail "3.1  No auth  expected 401 got $S"

    S=$(G -H "Host: localhost" -H "X-Internal-Secret: wrong" "$BASE/_api/internal/status")
    [ "$S" = "401" ] && pass "3.2  Wrong secret  ->  401" \
                     || fail "3.2  Wrong secret  expected 401 got $S"

    S=$(G -H "Host: localhost" -H "X-Internal-Secret: $SEC" "$BASE/_api/internal/status")
    [ "$S" = "200" ] && pass "3.3  Correct secret  ->  200" \
                     || fail "3.3  Correct secret  expected 200 got $S"

    J=$(GBODY -H "Host: localhost" -H "X-Internal-Secret: $SEC" "$BASE/_api/internal/status")
    echo "$J" | grep -q '"server"'  && pass "3.4  JSON has 'server'" || fail  "3.4  JSON: $J"
    echo "$J" | grep -q '"success"' && pass "3.5  JSON has 'success'"|| fail  "3.5  JSON: $J"
    echo "$J" | grep -q '"db"'      && pass "3.6  JSON has 'db'"     || fail  "3.6  JSON: $J"

    S=$(G -X POST -H "Host: localhost" -H "X-Internal-Secret: $SEC" \
        -H "Content-Type: application/json" -d '{}' "$BASE/_api/internal/deploy")
    case "$S" in 400|422|500|503) pass "3.7  POST /deploy bad body  ->  $S" ;;
                               *) warn "3.7  POST /deploy  ->  $S" ;; esac

    S=$(G -H "Host: localhost" -H "X-Internal-Secret: $SEC" "$BASE/_api/internal/bad-xyz")
    [ "$S" = "404" ] && pass "3.8  Unknown route  ->  404" \
                     || fail "3.8  Unknown route  expected 404 got $S"
}

t_proxy() {
    sep "GROUP 4  Proxy"
    local S

    info "4.1  Admin proxy (max 8 s, Django not running)..."
    S=$(G --max-time 8 -H "Host: localhost" "$BASE/_admin/")
    case "$S" in 502|503|504) pass "4.1  Admin proxy  ->  $S  (expected)" ;;
                           *) warn "4.1  Admin proxy  ->  $S" ;; esac

    S=$(G -H "Host: localhost" "$BASE/api/health")
    [ "$S" = "503" ] && pass "4.2  /api/ no-BE  ->  503" \
                     || warn "4.2  /api/ no-BE  ->  $S"
}

t_protocol() {
    sep "GROUP 5  HTTP Protocol"
    local S H

    S=$(G --http1.1 -H "Host: localhost" "$BASE/")
    [ "$S" = "200" ] && pass "5.1  HTTP/1.1  ->  200" || fail "5.1  HTTP/1.1  ->  $S"

    S=$(G --http1.1 -H "Connection: keep-alive" -H "Host: localhost" "$BASE/")
    [ "$S" = "200" ] && pass "5.2  Keep-alive  ->  200" || fail "5.2  Keep-alive  ->  $S"

    H=$(GHEAD -H "Host: localhost" "$BASE/")
    echo "$H" | grep -qi "content-type"   && pass "5.3  Content-Type header present"   || fail "5.3  Content-Type missing"
    echo "$H" | grep -qi "server:"        && pass "5.4  Server header present"         || warn "5.4  Server header missing"
    echo "$H" | grep -qi "content-length" && pass "5.5  Content-Length present"        || warn "5.5  Content-Length missing"

    S=$(G -X HEAD -H "Host: localhost" "$BASE/")
    case "$S" in 200|405) pass "5.6  HEAD  ->  $S" ;; *) fail "5.6  HEAD  ->  $S" ;; esac
}

t_concurrency() {
    sep "GROUP 6  Concurrency"
    local S

    # 6.1 -- 30 concurrent requests with ab (ApacheBench)
    info "6.1  30 concurrent GETs via ab..."
    local AB_OUT; AB_OUT=$(ab -n 30 -c 10 -H 'Host: localhost' \
        "http://127.0.0.1:${PORT}/" 2>&1)
    local NON200; NON200=$(echo "$AB_OUT" | grep 'Non-2xx' | awk '{print $NF}' || echo 0)
    NON200=${NON200:-0}
    local OK; OK=$((30 - NON200))
    if echo "$AB_OUT" | grep -q 'Requests per second'; then
        RPS=$(echo "$AB_OUT" | grep 'Requests per second' | awk '{print $4}')
        [ "$OK" -ge 28 ] && pass "6.1  30 concurrent GETs: $OK/30 x 200  ($RPS req/s)" \
                          || fail "6.1  Concurrent: only $OK/30 OK  (non-2xx: $NON200)"
    else
        fail "6.1  ab failed: $(echo $AB_OUT | head -c 120)"
    fi

    S=$(G -H "Host: localhost" "$BASE/")
    [ "$S" = "200" ] && pass "6.2  Server alive after ab load -> $S" \
                     || fail "6.2  Server crashed -> $S"

    # 6.3 -- Mixed vhost load
    info "6.3  Mixed-vhost 30 GETs via ab..."
    local OK3=0
    for HOST in localhost smoke.test example.com; do
        local R; R=$(ab -n 10 -c 5 -H "Host: $HOST" \
            "http://127.0.0.1:${PORT}/" 2>&1)
        local NF; NF=$(echo "$R" | grep 'Non-2xx' | awk '{print $NF}' || echo 0)
        NF=${NF:-0}
        OK3=$((OK3 + 10 - NF))
    done
    [ "$OK3" -ge 27 ] && pass "6.3  Mixed-vhost 30 GETs: $OK3/30 x 200" \
                      || fail "6.3  Mixed-vhost: only $OK3/30 OK"

    S=$(G -H "Host: localhost" "$BASE/")
    [ "$S" = "200" ] && pass "6.4  Server alive after mixed load -> $S" \
                     || fail "6.4  Server dead -> $S"
}

t_errors() {
    sep "GROUP 7  Error Handling"
    local S L

    L=$(GBODY -H "Host: localhost" "$BASE/no-such-file-xyz.html" | wc -c)
    [ "$L" -gt 1 ] && pass "7.1  404 body non-empty  ($L bytes)" \
                   || warn "7.1  404 body empty"

    S=$(G -H "Host: " "$BASE/")
    case "$S" in 200|400|404) pass "7.2  Empty Host header  ->  $S  (no crash)" ;;
                           *) warn "7.2  Empty Host  ->  $S" ;; esac
}

# ── main ──────────────────────────────────────────────────────
echo ""
echo "================================================================"
echo "  HTTP Platform Server -- Test Suite     port=${PORT}"
echo "  $(date)"
echo "================================================================"

fixtures
start_server

t_static
t_vhosts
t_api
t_proxy
t_protocol
t_concurrency
t_errors

echo ""
echo "================================================================"
printf "  PASSED: %d   FAILED: %d   WARNINGS: %d\n" "$PASS" "$FAIL" "$WARN"
echo "================================================================"

if [ "$FAIL" -gt 0 ]; then
    echo ""
    echo "--- Server log (last 20 lines) ---"
    tail -20 "$LOG" 2>/dev/null || true
    exit 1
fi
exit 0
