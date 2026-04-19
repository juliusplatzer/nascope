#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# Load SWIM credentials / queue config from .env if present.
if [[ -f .env ]]; then
    set -o allexport
    # shellcheck disable=SC1091
    . ./.env
    set +o allexport
fi

JAR="target/faascope-stdds-1.0-SNAPSHOT.jar"
MENU_BIN="ui/build/menu"
SCOPE_BIN="asdex/build/scope"

# --- Locate Qt (Homebrew on macOS) -------------------------------------------
QT_PREFIX=""
if command -v brew >/dev/null 2>&1; then
    QT_PREFIX="$(brew --prefix qt 2>/dev/null || brew --prefix qt@6 2>/dev/null || true)"
fi

cmake_args_for() {
    local src="$1" bld="$2"
    CMAKE_ARGS=(-S "$src" -B "$bld")
    [[ -n "$QT_PREFIX" ]] && CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$QT_PREFIX")
}

# --- Build the Qt menu if missing --------------------------------------------
if [[ ! -x "$MENU_BIN" ]]; then
    echo "[run] Building menu…" >&2
    cmake_args_for ui ui/build
    cmake "${CMAKE_ARGS[@]}" >&2
    cmake --build ui/build >&2
fi

# --- Build the ASDE-X scope if missing ---------------------------------------
if [[ ! -x "$SCOPE_BIN" ]]; then
    echo "[run] Building scope…" >&2
    cmake_args_for asdex asdex/build
    cmake "${CMAKE_ARGS[@]}" >&2
    cmake --build asdex/build >&2
fi

# --- Build the reader jar if missing -----------------------------------------
if [[ ! -f "$JAR" ]]; then
    echo "[run] Building reader…" >&2
    mvn -q package
fi

# --- Free port 8080 if a previous run left a stale consumer ------------------
WS_PORT="${WS_PORT:-8080}"
STALE_PIDS="$(lsof -ti "tcp:${WS_PORT}" -sTCP:LISTEN 2>/dev/null || true)"
if [[ -n "$STALE_PIDS" ]]; then
    echo "[run] Killing stale listener(s) on :$WS_PORT — $STALE_PIDS" >&2
    # shellcheck disable=SC2086
    kill $STALE_PIDS 2>/dev/null || true
    sleep 0.3
    # shellcheck disable=SC2086
    kill -9 $STALE_PIDS 2>/dev/null || true
fi

# --- Show menu, capture selection --------------------------------------------
echo "[run] Opening menu…" >&2
if ! AIRPORT="$("$MENU_BIN")"; then
    echo "[run] Menu cancelled — nothing started." >&2
    exit 1
fi

if [[ -z "$AIRPORT" ]]; then
    echo "[run] No airport selected — aborting." >&2
    exit 1
fi

# --- Start the SWIM consumer in the background ------------------------------
echo "[run] Starting SWIM consumer for $AIRPORT" >&2
INITIAL_AIRPORT="$AIRPORT" java -jar "$JAR" &
CONSUMER_PID=$!

cleanup() {
    [[ -n "${CONSUMER_PID:-}" ]] || return 0
    kill "$CONSUMER_PID" 2>/dev/null || true
    # Wait briefly, then force-kill so port 8080 is actually released.
    for _ in 1 2 3 4 5; do
        kill -0 "$CONSUMER_PID" 2>/dev/null || return 0
        sleep 0.1
    done
    kill -9 "$CONSUMER_PID" 2>/dev/null || true
    wait "$CONSUMER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM HUP

# --- Open the scope (blocks until the scope window is closed) ---------------
echo "[run] Opening scope for $AIRPORT" >&2
"$SCOPE_BIN" "$AIRPORT"
