#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

if [[ -f .env ]]; then
    set -o allexport
    # shellcheck disable=SC1091
    . ./.env
    set +o allexport
fi

JAR="target/faascope-stdds-1.0-SNAPSHOT.jar"
MENU_BIN="build/ui/menu"
SCOPE_BIN="build/asdex/asdex_scope"
WS_PORT="${WS_PORT:-8080}"

QT_PREFIX=""
if command -v brew >/dev/null 2>&1; then
    QT_PREFIX="$(brew --prefix qt 2>/dev/null || brew --prefix qt@6 2>/dev/null || true)"
fi

CMAKE_ARGS=(-S . -B build)
if [[ -n "$QT_PREFIX" ]]; then
    CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$QT_PREFIX")
fi

echo "[build] Building Qt frontend..." >&2
cmake "${CMAKE_ARGS[@]}" >&2
cmake --build build >&2

echo "[build] Building SMES reader..." >&2
mvn -q package

STALE_PIDS="$(lsof -ti "tcp:${WS_PORT}" -sTCP:LISTEN 2>/dev/null || true)"
if [[ -n "$STALE_PIDS" ]]; then
    echo "[run] Killing stale listener(s) on :$WS_PORT - $STALE_PIDS" >&2
    # shellcheck disable=SC2086
    kill $STALE_PIDS 2>/dev/null || true
    sleep 0.3
    # shellcheck disable=SC2086
    kill -9 $STALE_PIDS 2>/dev/null || true
fi

echo "[run] Opening menu..." >&2
if ! AIRPORT="$("$MENU_BIN" --select-only)"; then
    echo "[run] Menu cancelled - nothing started." >&2
    exit 1
fi

AIRPORT="$(printf '%s' "$AIRPORT" | tr -d '\r' | tail -n 1)"
if [[ -z "$AIRPORT" ]]; then
    echo "[run] No airport selected - aborting." >&2
    exit 1
fi

echo "[run] Starting SWIM/Solace consumer for $AIRPORT on websocket port $WS_PORT" >&2
INITIAL_AIRPORT="$AIRPORT" WS_PORT="$WS_PORT" java -jar "$JAR" &
CONSUMER_PID=$!

cleanup() {
    [[ -n "${CONSUMER_PID:-}" ]] || return 0
    kill "$CONSUMER_PID" 2>/dev/null || true
    for _ in 1 2 3 4 5; do
        kill -0 "$CONSUMER_PID" 2>/dev/null || return 0
        sleep 0.1
    done
    kill -9 "$CONSUMER_PID" 2>/dev/null || true
    wait "$CONSUMER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM HUP

for _ in 1 2 3 4 5 6 7 8 9 10; do
    if lsof -ti "tcp:${WS_PORT}" -sTCP:LISTEN >/dev/null 2>&1; then
        break
    fi
    sleep 0.2
done

echo "[run] Opening ASDE-X scope for $AIRPORT" >&2
"$SCOPE_BIN" "$AIRPORT"
