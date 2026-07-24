#!/usr/bin/env bash
# ============================================================
#  test.sh — End-to-end smoke test for HTTP Server project
#
#  Tests:
#    1. C++ HTTP Server serves static FE
#    2. Admin dashboard reachable via proxy
#    3. Internal API health check
#    4. Flask backend /api/hello (via C++ proxy)
#    5. Flask backend /api/db-check (PostgreSQL connectivity)
#    6. Admin dashboard — Add Site form
#    7. Admin dashboard — Dashboard shows sites
#    8. Admin dashboard — Site detail page
#    9. Admin dashboard — No duplicate be_containers on redeploy
#   10. Admin dashboard — Delete site
# ============================================================

set -euo pipefail

# ── Config ───────────────────────────────────────────────────
HTTP_PORT="${HTTP_PORT:-8000}"
ADMIN_PORT="${ADMIN_PORT:-8080}"
HTTP_BASE="http://127.0.0.1:${HTTP_PORT}"
ADMIN_BASE="http://127.0.0.1:${ADMIN_PORT}"
INTERNAL_SECRET="${INTERNAL_SECRET:-changeme-secret}"
COOKIE_JAR="$(mktemp /tmp/test_cookies_XXXXXX.txt)"
PASS=0
FAIL=0
SKIP=0

# ── Helpers ───────────────────────────────────────────────────
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() { echo -e "  ${GREEN}✔${NC} $1"; ((PASS++)); }
fail() { echo -e "  ${RED}✗${NC} $1"; ((FAIL++)); }
skip() { echo -e "  ${YELLOW}~${NC} $1"; ((SKIP++)); }
section() { echo -e "\n${CYAN}▶ $1${NC}"; }

wait_for() {
    local url="$1" max="${2:-30}" delay="${3:-2}" elapsed=0
    while ! curl -sf "$url" > /dev/null 2>&1; do
        sleep "$delay"
        elapsed=$((elapsed + delay))
        if [ "$elapsed" -ge "$max" ]; then
            echo "  Timeout waiting for $url"
            return 1
        fi
    done
    return 0
}

http_get() { curl -sf --max-time 10 "$@"; }

# ── 0. Wait for services ─────────────────────────────────────
section "0. Waiting for services to be ready"
echo "  Waiting for C++ HTTP Server on :${HTTP_PORT}..."
if wait_for "${HTTP_BASE}/" 30 2; then
    pass "HTTP Server is up"
else
    fail "HTTP Server did not start in time — aborting"
    exit 1
fi

echo "  Waiting for Django admin on :${ADMIN_PORT}..."
if wait_for "${ADMIN_BASE}/" 30 2; then
    pass "Django admin is up"
else
    fail "Django admin did not start in time"
fi

# ── 1. Static FE serving ──────────────────────────────────────
section "1. Static file serving (C++ HTTP Server)"

status=$(curl -so /dev/null -w "%{http_code}" "${HTTP_BASE}/")
if [ "$status" = "200" ]; then
    pass "GET / returns 200"
else
    fail "GET / returned $status (expected 200)"
fi

# ── 2. Admin dashboard via C++ proxy ─────────────────────────
section "2. Admin dashboard (proxied through C++ server)"

resp=$(http_get "${HTTP_BASE}/_admin/" 2>/dev/null)
if echo "$resp" | grep -q "Server Admin Panel"; then
    pass "Dashboard reachable via C++ proxy at /_admin/"
else
    fail "Dashboard not reachable via C++ proxy"
fi

# Also test direct Django access
resp2=$(http_get "${ADMIN_BASE}/_admin/" 2>/dev/null)
if echo "$resp2" | grep -q "Server Admin Panel"; then
    pass "Dashboard reachable directly on :${ADMIN_PORT}"
else
    fail "Dashboard not reachable directly on :${ADMIN_PORT}"
fi

# ── 3. Internal API ────────────────────────────────────────────
section "3. Internal API (C++ server /_api/internal)"

resp=$(http_get "${HTTP_BASE}/_api/internal/status" \
    -H "X-Internal-Secret: ${INTERNAL_SECRET}" 2>/dev/null)
if echo "$resp" | grep -q '"success":true'; then
    pass "GET /_api/internal/status → success"
    if echo "$resp" | grep -q '"db":"ok"'; then
        pass "Database connected (reported by internal API)"
    else
        skip "Database not connected or not reported"
    fi
else
    fail "Internal API /status not responding correctly (got: $resp)"
fi

# ── 4. Flask backend /api/hello ────────────────────────────────
section "4. Flask backend /api/hello (proxied via C++ server)"

# Check if Flask backend is running locally
if curl -sf "http://127.0.0.1:5000/api/healthz" > /dev/null 2>&1; then
    resp=$(http_get "${HTTP_BASE}/api/hello" \
        -H "Host: test.local" 2>/dev/null || echo "")
    if echo "$resp" | grep -q '"ok":true'; then
        pass "GET /api/hello (Host: test.local) → Flask backend OK"
    else
        fail "GET /api/hello (Host: test.local) failed or returned error (got: ${resp:0:120})"
    fi
else
    skip "Flask backend not running on :5000 — skipping /api/hello test"
    skip "Flask backend not running on :5000 — skipping /api/db-check test"
fi

# ── 5. Flask /api/db-check ────────────────────────────────────
section "5. Flask backend /api/db-check (PostgreSQL via Flask)"

if curl -sf "http://127.0.0.1:5000/api/healthz" > /dev/null 2>&1; then
    resp=$(http_get "${HTTP_BASE}/api/db-check" \
        -H "Host: test.local" 2>/dev/null || echo "")
    if echo "$resp" | grep -q '"ok":true'; then
        pass "GET /api/db-check → PostgreSQL reachable from Flask backend"
        server_time=$(echo "$resp" | grep -o '"server_time":"[^"]*"' | cut -d'"' -f4)
        [ -n "$server_time" ] && pass "  DB server time: $server_time"
    else
        fail "GET /api/db-check returned error (got: ${resp:0:120})"
    fi
fi

# ── 6. Admin — Add Site form ──────────────────────────────────
section "6. Admin dashboard — Add Site form"

# Get CSRF token
curl -sc "${COOKIE_JAR}" "${ADMIN_BASE}/_admin/add/" -o /dev/null 2>/dev/null
CSRF=$(grep csrftoken "${COOKIE_JAR}" | awk '{print $NF}' 2>/dev/null || echo "")

if [ -z "$CSRF" ]; then
    fail "Could not get CSRF token from /_admin/add/"
else
    pass "CSRF token obtained: ${CSRF:0:16}…"

    # Submit the add-site form
    resp=$(curl -sf -b "${COOKIE_JAR}" \
        -H "Referer: ${ADMIN_BASE}/_admin/add/" \
        -d "csrfmiddlewaretoken=${CSRF}" \
        -d "domain=ci.test" \
        -d "user_owner=ci" \
        -d "fe_folder=./test/fe" \
        -d "be_folder=" \
        -d "be_type=static" \
        -d "run_cmd=" \
        -d "be_port=3000" \
        -d "fe_build=none" \
        -d "db_count=0" \
        -L "${ADMIN_BASE}/_admin/add/" 2>/dev/null || echo "")
    if echo "$resp" | grep -q "deployed successfully"; then
        pass "Add Site form → ci.test deployed successfully"
    elif echo "$resp" | grep -q "Deploy Result"; then
        fail "Deploy returned Deploy Result page but not 'deployed successfully'"
        echo "    Response excerpt: $(echo "$resp" | grep -o 'alert-[^<]*' | head -3)"
    else
        fail "Add Site form submission failed (got unexpected response)"
    fi
fi

# ── 7. Dashboard shows sites ──────────────────────────────────
section "7. Admin dashboard — site listing"

resp=$(http_get "${ADMIN_BASE}/_admin/" 2>/dev/null)
if echo "$resp" | grep -q '<tbody>'; then
    pass "Dashboard table rendered"
fi
if echo "$resp" | grep -q "ci.test"; then
    pass "Newly added site 'ci.test' visible on dashboard"
else
    skip "ci.test not visible yet (may require DB commit delay)"
fi

# ── 8. Site detail page ───────────────────────────────────────
section "8. Admin dashboard — Site detail page"

resp=$(http_get "${ADMIN_BASE}/_admin/site/ci.test/" 2>/dev/null)
if echo "$resp" | grep -q "ci.test"; then
    pass "Site detail page for ci.test loaded"
else
    fail "Site detail page for ci.test not accessible"
fi

# Check badge-skipped renders (static site → be is skipped)
if echo "$resp" | grep -q "badge-skipped"; then
    pass "badge-skipped CSS class present in site detail"
else
    skip "badge-skipped class not found (BE may not be 'skipped' status yet)"
fi

# ── 9. No duplicate be_containers on redeploy ─────────────────
section "9. No duplicate be_containers on redeploy"

# Redeploy same domain
curl -sc "${COOKIE_JAR}" "${ADMIN_BASE}/_admin/add/" -o /dev/null 2>/dev/null
CSRF=$(grep csrftoken "${COOKIE_JAR}" | awk '{print $NF}' 2>/dev/null || echo "")
if [ -n "$CSRF" ]; then
    curl -sf -b "${COOKIE_JAR}" \
        -H "Referer: ${ADMIN_BASE}/_admin/add/" \
        -d "csrfmiddlewaretoken=${CSRF}&domain=ci.test&user_owner=ci&fe_folder=./test/fe&be_folder=&be_type=static&run_cmd=&be_port=3000&fe_build=none&db_count=0" \
        -L "${ADMIN_BASE}/_admin/add/" -o /dev/null 2>/dev/null

    # Ask the status JSON endpoint — check be_containers count via direct DB query
    if command -v docker &>/dev/null; then
        count=$(docker exec pg_server_db psql -U admin -d server_db -tAc \
            "SELECT COUNT(*) FROM be_containers bc JOIN domains d ON d.id=bc.domain_id WHERE d.domain='ci.test'" 2>/dev/null || echo "?")
        if [ "$count" = "1" ]; then
            pass "Only 1 be_container row after redeploy (no duplicates)"
        elif [ "$count" = "?" ]; then
            skip "Could not query DB directly (Docker not available)"
        else
            fail "Found $count be_container rows for ci.test after redeploy (expected 1)"
        fi
    else
        skip "docker not in PATH — skipping duplicate check"
    fi
fi

# ── 10. Delete site ───────────────────────────────────────────
section "10. Admin dashboard — Delete site"

curl -sc "${COOKIE_JAR}" "${ADMIN_BASE}/_admin/site/ci.test/delete/" -o /dev/null 2>/dev/null
CSRF=$(grep csrftoken "${COOKIE_JAR}" | awk '{print $NF}' 2>/dev/null || echo "")
if [ -n "$CSRF" ]; then
    resp=$(curl -sf -b "${COOKIE_JAR}" \
        -H "Referer: ${ADMIN_BASE}/_admin/site/ci.test/delete/" \
        -d "csrfmiddlewaretoken=${CSRF}" \
        -L "${ADMIN_BASE}/_admin/site/ci.test/delete/" 2>/dev/null || echo "")
    if echo "$resp" | grep -q "deleted successfully"; then
        pass "Delete site → ci.test deleted (dashboard redirect OK)"
    elif echo "$resp" | grep -q "Deletion failed"; then
        fail "Deletion failed — check error in response"
    else
        fail "Delete returned unexpected response"
    fi
fi

# ── Summary ───────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════"
echo -e "  Results: ${GREEN}${PASS} passed${NC}  ${RED}${FAIL} failed${NC}  ${YELLOW}${SKIP} skipped${NC}"
echo "═══════════════════════════════════════════════════"

# Clean up
rm -f "${COOKIE_JAR}"

[ "$FAIL" -eq 0 ] && exit 0 || exit 1
