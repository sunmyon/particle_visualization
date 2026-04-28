#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

status=0

check_no_include() {
  local scope="$1"
  local pattern="$2"
  local message="$3"
  local allow="${4:-}"
  local matches

  matches="$(
    rg -n --glob '*.{h,hpp,hh,c,cc,cpp,cxx}' \
      "^#include[[:space:]]+[<\"]${pattern}" "$scope" || true
  )"

  if [[ -n "$allow" && -n "$matches" ]]; then
    matches="$(printf '%s\n' "$matches" | grep -Ev "$allow" || true)"
  fi

  if [[ -n "$matches" ]]; then
    printf 'include boundary violation: %s\n' "$message" >&2
    printf '%s\n' "$matches" >&2
    status=1
  fi
}

check_no_include "src/UI" \
  "render/opengl/" \
  "UI must not include OpenGL backend headers directly."

check_no_include "src/UI" \
  "platform/" \
  "UI must not include platform/window backend headers directly."

check_no_include "src/analysis" \
  "UI/" \
  "analysis must not include UI headers."

check_no_include "src/analysis" \
  "render/opengl/" \
  "analysis must not include OpenGL backend headers."

check_no_include "src/analysis" \
  "platform/" \
  "analysis must not include platform/window headers."

check_no_include "src/analysis" \
  "app/" \
  "analysis must not include app-layer headers."

check_no_include "src/FileIO" \
  "UI/" \
  "FileIO must not include UI headers."

check_no_include "src/FileIO" \
  "render/opengl/" \
  "FileIO must not include OpenGL backend headers."

check_no_include "src/render" \
  "UI/" \
  "render must not include UI headers."

if [[ "$status" -eq 0 ]]; then
  printf 'include boundary check passed\n'
fi

exit "$status"
