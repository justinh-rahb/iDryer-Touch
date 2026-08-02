#!/usr/bin/env bash
#
# One-command dev setup for iDryer-Touch.
#
# Upstream (pavluchenkor/iDryer-Link) ships two symlinks that point into the
# original author's home directory:
#
#   lib/idryer-core     -> /Users/ruslanpavlucenko/.../docs/idryer-core
#   config-exmple/menu  -> /Users/ruslanpavlucenko/.../iDryerControllerV2/src/menu
#
# This fork replaces both with real submodules, so a fresh clone builds without
# any host-specific setup. This script makes that reproducible.
#
# Usage:
#   scripts/bootstrap.sh            # init submodules, verify, check toolchain
#   scripts/bootstrap.sh --check    # verify only, no network/checkout (for CI)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

CHECK_ONLY=0
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=1

info()  { printf '\033[0;36m==>\033[0m %s\n' "$*"; }
ok()    { printf '\033[0;32m  ok\033[0m %s\n' "$*"; }
warn()  { printf '\033[0;33m  !!\033[0m %s\n' "$*" >&2; }
die()   { printf '\033[0;31m  XX\033[0m %s\n' "$*" >&2; exit 1; }

# ── Submodules ───────────────────────────────────────────────────────────────
# NOTE: idryer-core is pinned to an exact commit, not a branch tip. It must stay
# on a revision whose UART contract matches the controller firmware the dryer is
# running — the bridge validates payload length by exact equality, so a version
# skew silently drops whole frame types rather than degrading. Re-pin
# deliberately, and re-check UART_PROTOCOL_VER when you do.
if [[ $CHECK_ONLY -eq 0 ]]; then
  info "Initializing submodules (recursive)"
  git submodule update --init --recursive
fi

info "Verifying dependencies"

[[ -f lib/idryer-core/src/iDryer.h ]] \
  || die "lib/idryer-core is empty — run: git submodule update --init --recursive"
ok "lib/idryer-core @ $(git -C lib/idryer-core rev-parse --short HEAD)"

[[ -f vendor/iDryerControllerV2/src/menu/menu_meta.h ]] \
  || die "vendor/iDryerControllerV2 is empty — run: git submodule update --init --recursive"
ok "vendor/iDryerControllerV2 @ $(git -C vendor/iDryerControllerV2 rev-parse --short HEAD)"

# The menu mirror is committed, not regenerated per build. Upstream's
# config-exmple/menu symlink is deliberately absent so copy_menu.py takes its
# documented "cached files" path — see scripts/sync-menu.sh for why.
[[ -f lib/idryer-menu/src/menu_meta.h ]] \
  || die "lib/idryer-menu/src/menu_meta.h missing — the committed menu mirror is required"
ok "menu mirror VERSION_MAJOR $(sed -n 's/^#define VERSION_MAJOR \([0-9]*\).*/\1/p' lib/idryer-menu/src/version.h) (committed, not auto-synced)"

if [[ -e config-exmple/menu ]]; then
  warn "config-exmple/menu exists — builds will silently overwrite the menu mirror."
  warn "Remove it unless you intend that; use scripts/sync-menu.sh instead."
fi

# Guard against the upstream absolute-path symlinks creeping back in via a merge.
if find lib config-exmple -maxdepth 2 -type l -lname '/Users/*' 2>/dev/null | grep -q .; then
  die "absolute-path symlink from upstream detected — re-run this script after removing it"
fi

# ── Toolchain ────────────────────────────────────────────────────────────────
if command -v pio >/dev/null 2>&1; then
  ok "PlatformIO $(pio --version 2>/dev/null | awk '{print $NF}')"
else
  warn "PlatformIO not found — install with: pip install --upgrade platformio"
  [[ $CHECK_ONLY -eq 1 ]] && exit 1
fi

info "Ready. Build with: scripts/build.sh [env ...]"
