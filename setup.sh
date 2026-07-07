#!/bin/bash
# ============================================================
# HTTP_Server — Universal Linux Setup & Build Script
# Detects your distro, installs dependencies, and builds.
# Works on: Fedora, RHEL, CentOS, Ubuntu, Debian, Arch, openSUSE
# Usage:  chmod +x setup.sh && ./setup.sh
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

log_info()  { echo -e "${GREEN}[✓]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[!]${NC} $1"; }
log_error() { echo -e "${RED}[✗]${NC} $1"; }
log_step()  { echo -e "${CYAN}[→]${NC} $1"; }

# ── Detect Linux Distribution ────────────────────────────────
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO_ID="$ID"
        DISTRO_NAME="$PRETTY_NAME"
    elif [ -f /etc/redhat-release ]; then
        DISTRO_ID="rhel"
        DISTRO_NAME="$(cat /etc/redhat-release)"
    elif [ -f /etc/debian_version ]; then
        DISTRO_ID="debian"
        DISTRO_NAME="Debian $(cat /etc/debian_version)"
    else
        DISTRO_ID="unknown"
        DISTRO_NAME="Unknown Linux"
    fi

    # Normalize to family
    case "$DISTRO_ID" in
        fedora)                    DISTRO_FAMILY="fedora" ;;
        rhel|centos|rocky|alma)    DISTRO_FAMILY="rhel"   ;;
        ubuntu|linuxmint|pop)      DISTRO_FAMILY="debian" ;;
        debian)                    DISTRO_FAMILY="debian" ;;
        arch|manjaro|endeavouros)  DISTRO_FAMILY="arch"   ;;
        opensuse*|sles)            DISTRO_FAMILY="suse"   ;;
        *)                         DISTRO_FAMILY="unknown";;
    esac

    log_info "Detected: $DISTRO_NAME (family: $DISTRO_FAMILY)"
}

# ── Install Dependencies Per Distro ──────────────────────────
install_deps() {
    log_step "Installing build dependencies..."

    case "$DISTRO_FAMILY" in
        fedora)
            sudo dnf install -y \
                cmake make gcc gcc-c++ \
                boost-devel \
                jsoncpp-devel \
                pkg-config \
                wrk 2>/dev/null || true
            ;;
        rhel)
            # RHEL/CentOS/Rocky — enable EPEL for extra packages
            sudo dnf install -y epel-release 2>/dev/null || true
            sudo dnf install -y \
                cmake make gcc gcc-c++ \
                boost-devel \
                jsoncpp-devel \
                pkg-config
            ;;
        debian)
            sudo apt-get update
            sudo apt-get install -y \
                cmake make g++ \
                libboost-all-dev \
                libjsoncpp-dev \
                pkg-config \
                wrk 2>/dev/null || true
            ;;
        arch)
            sudo pacman -Sy --noconfirm \
                cmake make gcc \
                boost \
                jsoncpp \
                pkg-config
            ;;
        suse)
            sudo zypper install -y \
                cmake make gcc-c++ \
                boost-devel \
                jsoncpp-devel \
                pkg-config
            ;;
        *)
            log_error "Unsupported distro: $DISTRO_NAME"
            echo ""
            echo "Please manually install these packages:"
            echo "  - cmake, make, gcc/g++ (C++17 support)"
            echo "  - Boost development libraries (log, system, thread, filesystem)"
            echo "  - JsonCpp development library"
            echo "  - pkg-config"
            echo ""
            echo "Then re-run this script."
            exit 1
            ;;
    esac

    log_info "Dependencies installed."
}

# ── Verify Dependencies ──────────────────────────────────────
verify_deps() {
    log_step "Verifying dependencies..."
    local missing=0

    # Check C++ compiler
    if ! command -v g++ &>/dev/null && ! command -v c++ &>/dev/null; then
        log_error "C++ compiler not found"
        missing=1
    else
        local cxx_version
        cxx_version=$(g++ --version 2>/dev/null | head -1 || c++ --version 2>/dev/null | head -1)
        log_info "C++ compiler: $cxx_version"
    fi

    # Check cmake
    if ! command -v cmake &>/dev/null; then
        log_error "cmake not found"
        missing=1
    else
        log_info "CMake: $(cmake --version | head -1)"
    fi

    # Check make
    if ! command -v make &>/dev/null; then
        log_error "make not found"
        missing=1
    else
        log_info "Make: $(make --version | head -1)"
    fi

    # Check pkg-config
    if ! command -v pkg-config &>/dev/null; then
        log_error "pkg-config not found"
        missing=1
    else
        log_info "pkg-config: $(pkg-config --version)"
    fi

    # Check Boost headers exist
    local boost_found=0
    for dir in /usr/include/boost /usr/local/include/boost; do
        if [ -d "$dir" ]; then
            boost_found=1
            log_info "Boost headers: $dir"
            break
        fi
    done
    if [ "$boost_found" -eq 0 ]; then
        log_error "Boost headers not found in /usr/include or /usr/local/include"
        missing=1
    fi

    # Check jsoncpp
    if pkg-config --exists jsoncpp 2>/dev/null; then
        log_info "JsonCpp: $(pkg-config --modversion jsoncpp)"
    else
        log_error "JsonCpp not found via pkg-config"
        missing=1
    fi

    if [ "$missing" -eq 1 ]; then
        log_error "Some dependencies are missing. Run: $0 --install"
        return 1
    fi

    log_info "All dependencies verified."
    return 0
}

# ── Build the Project ─────────────────────────────────────────
build_project() {
    log_step "Building HTTP_Server..."

    mkdir -p build
    cd build

    cmake -DCMAKE_BUILD_TYPE=Release .. 2>&1 | tail -5
    echo ""

    local cores
    cores=$(nproc 2>/dev/null || echo 2)
    make -j"$cores" 2>&1

    if [ -x "./HTTP_Server" ]; then
        cd "$SCRIPT_DIR"
        log_info "Build successful!"
        log_info "Binary: $(pwd)/build/HTTP_Server ($(du -h build/HTTP_Server | cut -f1))"
    else
        cd "$SCRIPT_DIR"
        log_error "Build failed — check output above."
        exit 1
    fi
}

# ── Setup Runtime Directories ─────────────────────────────────
setup_runtime() {
    log_step "Setting up runtime directories..."

    # Create log directories
    mkdir -p logs/server_log logs/user_data

    # Create error pages if they don't exist
    mkdir -p var/error
    for code in 400 404 500; do
        if [ ! -f "var/error/${code}.html" ]; then
            cat > "var/error/${code}.html" <<EOF
<!DOCTYPE html>
<html>
<head><title>Error ${code}</title></head>
<body>
<h1>Error ${code}</h1>
<p>$([ "$code" = "400" ] && echo "Bad Request" || [ "$code" = "404" ] && echo "Not Found" || echo "Internal Server Error")</p>
<hr><p>HTTP_Server</p>
</body>
</html>
EOF
        fi
    done

    # Create a default index.html if none exists
    if [ ! -f "var/www/index.html" ]; then
        mkdir -p var/www
        cat > "var/www/index.html" <<'EOF'
<!DOCTYPE html>
<html>
<head><title>HTTP_Server</title></head>
<body>
<h1>Welcome to HTTP_Server</h1>
<p>Server is running successfully.</p>
</body>
</html>
EOF
    fi

    # Make http_server.sh executable
    chmod +x http_server.sh 2>/dev/null || true

    log_info "Runtime directories ready."
}

# ── Print Usage Summary ───────────────────────────────────────
print_summary() {
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${GREEN}  HTTP_Server is ready!${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "  Usage:"
    echo "    ./http_server.sh start      Start in static mode"
    echo "    ./http_server.sh reverse    Start in reverse-proxy mode"
    echo "    ./http_server.sh stop       Stop the server"
    echo "    ./http_server.sh status     Check server status"
    echo "    ./http_server.sh restart    Restart the server"
    echo "    ./http_server.sh log        Tail the server log"
    echo "    ./http_server.sh help       Show all commands"
    echo ""
    echo "  Test:"
    echo "    curl http://127.0.0.1:8000/www/main.html"
    echo ""
    echo "  Benchmark:"
    echo "    ./benchmarks/benchmark.sh"
    echo ""
}

# ── Main ──────────────────────────────────────────────────────
main() {
    echo ""
    echo "╔══════════════════════════════════════════════════════╗"
    echo "║     HTTP_Server — Setup & Build                     ║"
    echo "╚══════════════════════════════════════════════════════╝"
    echo ""

    detect_distro

    case "${1:-}" in
        --install|-i)
            install_deps
            verify_deps
            ;;
        --verify|-v)
            verify_deps
            ;;
        --build|-b)
            verify_deps && build_project
            ;;
        --clean|-c)
            log_step "Cleaning build..."
            rm -rf build
            log_info "Build directory removed."
            ;;
        --help|-h)
            echo "Usage: $0 [option]"
            echo ""
            echo "Options:"
            echo "  (no args)     Full setup: install deps → verify → build"
            echo "  --install     Install dependencies only"
            echo "  --verify      Verify dependencies are present"
            echo "  --build       Build only (skip install)"
            echo "  --clean       Remove build directory"
            echo "  --help        Show this help"
            ;;
        *)
            # Full setup: install → verify → build → setup runtime
            install_deps
            verify_deps
            build_project
            setup_runtime
            print_summary
            ;;
    esac
}

main "$@"
