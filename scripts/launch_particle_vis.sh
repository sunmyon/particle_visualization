#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

MODE="${1:-auto}"
if [[ "$MODE" == "auto" || "$MODE" == "gui" || "$MODE" == "headless" ]]; then
  shift || true
else
  MODE="auto"
fi

if [[ ! -f "$REPO_ROOT/example/output_0000.dat" ]]; then
  echo "Sample data not found. Preparing default sample..."
  bash "$REPO_ROOT/example/download_data.sh"
fi

GUI_BIN="$REPO_ROOT/particle_vis"
HEADLESS_BIN="$REPO_ROOT/build-headless-local/particle_vis"

display_is_usable() {
  if [[ -z "${DISPLAY:-}" ]]; then
    return 1
  fi

  # Prefer active probes when available.
  if command -v xdpyinfo >/dev/null 2>&1; then
    xdpyinfo -display "$DISPLAY" >/dev/null 2>&1
    return $?
  fi

  if command -v xset >/dev/null 2>&1; then
    xset -display "$DISPLAY" q >/dev/null 2>&1
    return $?
  fi

  # Fail safe in auto mode if we cannot probe display reachability.
  echo "DISPLAY is set to '${DISPLAY}', but no X11 probe tool (xdpyinfo/xset) is available." >&2
  return 1
}

pick_bin() {
  if [[ "$MODE" == "gui" ]]; then
    echo "$GUI_BIN"
    return
  fi
  if [[ "$MODE" == "headless" ]]; then
    echo "$HEADLESS_BIN"
    return
  fi

  if display_is_usable; then
    echo "$GUI_BIN"
  else
    if [[ -n "${DISPLAY:-}" ]]; then
      echo "DISPLAY is set to '${DISPLAY}' but not reachable; falling back to headless mode." >&2
    fi
    echo "$HEADLESS_BIN"
  fi
}

BIN="$(pick_bin)"
if [[ ! -x "$BIN" ]]; then
  echo "Binary not found: $BIN" >&2
  echo "Build first, e.g.:" >&2
  echo "  cmake --preset linux-gui-x11 && cmake --build --preset linux-gui-x11" >&2
  echo "or" >&2
  echo "  cmake --preset linux-headless-gcc && cmake --build --preset linux-headless-gcc" >&2
  exit 1
fi

# Cluster runtime fix: keep Mesa on system libstdc++ (avoids GLIBCXX mismatch).
export LD_LIBRARY_PATH="/usr/lib64:/lib64"

if [[ "$BIN" == "$HEADLESS_BIN" ]]; then
  export PARTICLE_VIS_EGL_PLATFORM="${PARTICLE_VIS_EGL_PLATFORM:-surfaceless}"
fi

echo "Launching: $BIN (mode=$MODE)"
exec "$BIN" "$@"
