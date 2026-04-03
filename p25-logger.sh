#!/bin/bash
# Quick start script for P25 Control Channel Logger
# Run on your Raspberry Pi after building.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_DIR="${SCRIPT_DIR}/p25-logger"
BUILD_DIR="${SCRIPT_DIR}/build"
BINARY="${BUILD_DIR}/p25-cc-logger"

usage() {
    echo "P25 Control Channel Logger"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  build       Build the p25-cc-logger binary"
    echo "  run         Run the logger (NDJSON to stdout, logs to stderr)"
    echo "  docker      Run via Docker"
    echo "  test        Test SDR connection and decode"
    echo "  rr-config   Generate config from RadioReference API"
    echo ""
    echo "Examples:"
    echo "  $0 build"
    echo "  $0 run                           # stdout"
    echo "  $0 run 2>/dev/null | jq .        # pretty print"
    echo "  $0 run >> cc_log.ndjson          # log to file"
    echo "  $0 run 2>/dev/null | jq 'select(.type == \"GRP_V_CH_GRANT\")'"
    echo ""
}

cmd_build() {
    echo "=== Building p25-cc-logger ==="
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    cmake ..
    make -j$(nproc) p25-cc-logger
    echo ""
    echo "=== Build complete: ${BINARY} ==="
}

cmd_run() {
    if [ ! -f "${BINARY}" ]; then
        echo "ERROR: Binary not found. Run '$0 build' first."
        exit 1
    fi

    CONFIG="${CONFIG_DIR}/config.json"
    if [ ! -f "${CONFIG}" ]; then
        echo "ERROR: Config not found at ${CONFIG}"
        exit 1
    fi

    exec "${BINARY}" --config "${CONFIG}" "$@"
}

cmd_docker() {
    echo "=== Building Docker image ==="
    docker build -f Dockerfile.cc-logger -t p25-cc-logger .

    echo "=== Running ==="
    docker run --rm -it \
        --device=/dev/bus/usb \
        -v "${CONFIG_DIR}:/app" \
        p25-cc-logger "$@"
}

cmd_test() {
    echo "=== Testing P25 CC Logger (30 second capture) ==="
    timeout 30 "${BINARY}" --config "${CONFIG_DIR}/config.json" 2>&1 | head -50
    echo ""
    echo "=== Test complete ==="
}

cmd_rr_config() {
    if [ -z "$1" ]; then
        echo "Usage: $0 rr-config --system-id 10471 --api-key KEY --username USER --password PASS"
        exit 1
    fi

    python3 "${SCRIPT_DIR}/p25-logger/rr_config_gen.py" \
        --output-dir "${CONFIG_DIR}" \
        --short-name "alachua" \
        "$@"
}

case "${1:-}" in
    build)    shift; cmd_build "$@" ;;
    run)      shift; cmd_run "$@" ;;
    docker)   shift; cmd_docker "$@" ;;
    test)     shift; cmd_test "$@" ;;
    rr-config) shift; cmd_rr_config "$@" ;;
    *)        usage ;;
esac
