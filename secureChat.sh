#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# List of real services — the single source of truth for "all"
SERVICES=(auth audit file messaging gateway)

# ==================== COLORS ====================
BLUE='\033[0;34m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
RESET='\033[0m'

title()   { echo -e "\n${BLUE}========================================${RESET}"; echo -e "${BLUE}  $1${RESET}"; echo -e "${BLUE}========================================${RESET}"; }
success() { echo -e "${GREEN}✅ $1${RESET}"; }
failure() { echo -e "${RED}❌ $1${RESET}"; }
info()    { echo -e "${YELLOW}➜  $1${RESET}"; }

# ==================== PATH HELPERS ====================
# No more "all" branch — every target passed here is a real service now.
source_dir() {
    case $1 in
        auth)  echo "$SCRIPT_DIR/auth" ;;
        audit) echo "$SCRIPT_DIR/audit" ;;
        file) echo "$SCRIPT_DIR/file" ;;
        messaging) echo "$SCRIPT_DIR/messaging" ;;
        gateway) echo "$SCRIPT_DIR/gateway" ;;
    esac
}

build_dir() {
    case $1 in
        auth)  echo "$SCRIPT_DIR/auth/build" ;;
        audit) echo "$SCRIPT_DIR/audit/build" ;;
        file) echo "$SCRIPT_DIR/file/build" ;;
        messaging) echo "$SCRIPT_DIR/messaging/build" ;;
        gateway) echo "$SCRIPT_DIR/gateway/build" ;;
    esac
}

test_target() {
    case $1 in
        auth)  echo "auth_tests" ;;
        audit) echo "audit_tests" ;;
        file)  echo "file_tests" ;;
        messaging)  echo "messaging_tests" ;;
        gateway) echo "gateway_tests" ;;
    esac
}

# usage() unchanged from your last version

# ==================== COMMANDS ====================
# Each cmd_* now handles "all" itself, by looping over SERVICES,
# instead of relying on source_dir/build_dir to resolve "all".

cmd_build() {
    local target="${1:-all}"

    if [ "$target" == "all" ]; then
        for s in "${SERVICES[@]}"; do
            cmd_build "$s"
        done
        return
    fi

    local src build
    src=$(source_dir "$target")
    build=$(build_dir "$target")

    if [ ! -f "$build/build.ninja" ]; then
        info "Configuring $target..."
        cmake -S "$src" -B "$build" -G Ninja
    fi

    title "Building: $target"
    cmake --build "$build" -j"$(nproc)"

    success "Build complete!"
}

cmd_test() {
    local target="${1:-all}"

    if [ "$target" == "all" ]; then
        for s in "${SERVICES[@]}"; do
            cmd_test "$s"
        done
        return
    fi

    cmd_build "$target"

    title "Testing: $target"
    local test_t
    test_t=$(test_target "$target")
    ctest --test-dir "$(build_dir "$target")" -R "$test_t" --output-on-failure -V

    success "All tests passed!"
}

cmd_run() {
    # unchanged — "all" was never valid here anyway, still requires one real target
    local target="${1:-}"

    case "$target" in
        auth|audit|file|messaging|gateway)
            cmd_test "$target"
            local bin
            bin="$(build_dir "$target")/bin/$target"
            title "Running: $target"
            "$bin"
            ;;

        *)
            failure "'run' requires a specific target: auth, audit, file, messaging, or gateway"
            exit 1
            ;;
    esac
}

cmd_clean() {
    local target="${1:-all}"

    if [ "$target" == "all" ]; then
        for s in "${SERVICES[@]}"; do
            cmd_clean "$s"
        done
        return
    fi

    local build
    build=$(build_dir "$target")

    title "Cleaning: $target"
    rm -rf "$build"
    success "Cleaned $build"
}

cmd_rebuild() {
    cmd_clean "${1:-all}"
    cmd_build "${1:-all}"
}

# ==================== ENTRY POINT ====================

case "${1:-}" in
    build)
        cmd_build "${2:-all}"
        ;;
    test)
        cmd_test "${2:-all}"
        ;;
    run)
        cmd_run "${2:-}"
        ;;
    clean)
        cmd_clean "${2:-all}"
        ;;
    rebuild)
        cmd_rebuild "${2:-all}"
        ;;
    *)
        failure "Unknown command: ${1:-}"
        echo "Usage: $0 {build|test|run|clean|rebuild} [service]"
        exit 1
        ;;
esac