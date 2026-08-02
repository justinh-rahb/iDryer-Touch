#!/usr/bin/env bash
#
# Build one or more PlatformIO environments, with the dependency check up front
# so a broken submodule fails fast instead of 200 lines into a compile.
#
# Usage:
#   scripts/build.sh                       # build platformio.ini default_envs
#   scripts/build.sh esp32c3-super-mini-prod
#   scripts/build.sh --all                 # every env defined in platformio.ini
#   scripts/build.sh --list                # list available envs
#   scripts/build.sh --clean <env>         # clean before building

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

info() { printf '\033[0;36m==>\033[0m %s\n' "$*"; }

list_envs() {
  sed -n 's/^\[env:\(.*\)\]$/\1/p' platformio.ini
}

CLEAN=0
ENVS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --list)  list_envs; exit 0 ;;
    --all)   while IFS= read -r e; do ENVS+=("$e"); done < <(list_envs); shift ;;
    --clean) CLEAN=1; shift ;;
    -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
    *)       ENVS+=("$1"); shift ;;
  esac
done

scripts/bootstrap.sh --check

if [[ ${#ENVS[@]} -eq 0 ]]; then
  info "Building default_envs from platformio.ini"
  [[ $CLEAN -eq 1 ]] && pio run -t clean
  pio run
  exit 0
fi

for env in "${ENVS[@]}"; do
  info "Building $env"
  [[ $CLEAN -eq 1 ]] && pio run -e "$env" -t clean
  pio run -e "$env"
done

info "Built: ${ENVS[*]}"
