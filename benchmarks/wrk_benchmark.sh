#!/usr/bin/env bash
# 重現 README 裡的壓測數據。
#
# 用法：benchmarks/wrk_benchmark.sh [build_dir=build] [io_threads=0] [seconds=10]
#
# 需要：已建置的專案、wrk（HTTP 壓測，https://github.com/wg/wrk）、taskset（util-linux）
# 預設把 server 綁在 CPU 0、壓測 client 綁在 CPU 1，避免兩者搶同一顆核心造成數據失真。
# 機器核心較多時，可調整 SERVER_CPUS / CLIENT_CPUS 並加大 io_threads 觀察多執行緒擴展性。
set -euo pipefail

BUILD_DIR=${1:-build}
IO_THREADS=${2:-0}
SECONDS_PER_RUN=${3:-10}
SERVER_CPUS=${SERVER_CPUS:-0}
CLIENT_CPUS=${CLIENT_CPUS:-1}
ECHO_PORT=${ECHO_PORT:-9000}
HTTP_PORT=${HTTP_PORT:-9001}

ECHO_SERVER="$BUILD_DIR/examples/echo_server"
HTTP_SERVER="$BUILD_DIR/examples/http_server"
ECHO_BENCH="$BUILD_DIR/benchmarks/echo_bench"

for bin in "$ECHO_SERVER" "$HTTP_SERVER" "$ECHO_BENCH"; do
    [[ -x "$bin" ]] || { echo "missing $bin — build first: cmake -S . -B $BUILD_DIR && cmake --build $BUILD_DIR -j" >&2; exit 1; }
done

SERVER_PID=""
cleanup() { [[ -n "$SERVER_PID" ]] && kill "$SERVER_PID" 2>/dev/null && wait "$SERVER_PID" 2>/dev/null || true; }
trap cleanup EXIT

start_server() {  # binary port
    taskset -c "$SERVER_CPUS" "$1" "$2" "$IO_THREADS" >/dev/null &
    SERVER_PID=$!
    sleep 0.5
}

stop_server() {
    cleanup
    SERVER_PID=""
    sleep 0.5
}

echo "== machine: $(nproc) CPUs, server on CPU $SERVER_CPUS ($IO_THREADS I/O threads), client on CPU $CLIENT_CPUS"

for conns_size in "100 64" "1000 64" "100 16384"; do
    read -r conns size <<<"$conns_size"
    echo "== echo ping-pong: $conns connections, $size-byte messages, ${SECONDS_PER_RUN}s"
    start_server "$ECHO_SERVER" "$ECHO_PORT"
    taskset -c "$CLIENT_CPUS" "$ECHO_BENCH" 127.0.0.1 "$ECHO_PORT" "$conns" "$size" "$SECONDS_PER_RUN" | tail -1
    stop_server
done

if command -v wrk >/dev/null 2>&1; then
    echo "== HTTP keep-alive (wrk): 100 connections, ${SECONDS_PER_RUN}s"
    start_server "$HTTP_SERVER" "$HTTP_PORT"
    taskset -c "$CLIENT_CPUS" wrk -t1 -c100 -d"${SECONDS_PER_RUN}s" --latency "http://127.0.0.1:$HTTP_PORT/"
    stop_server
else
    echo "== skipping HTTP benchmark: wrk not found in PATH"
fi
