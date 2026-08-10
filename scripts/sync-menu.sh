#!/usr/bin/env bash
#
# Deliberately re-sync the menu mirror in lib/idryer-menu/src/ from the pinned
# iDryerControllerV2 submodule.
#
# WHY THIS IS OPT-IN
# ------------------
# The menu metadata is firmware-coupled. VERSION_MAJOR in the mirrored version.h
# is the Link <-> RP2040 compatibility marker (see TODO.md), and menu ids shift
# between controller releases. Mirroring a controller revision your dryer is not
# running produces a Link build that disagrees with the hardware about what menu
# item 189 means.
#
# Upstream drives this from a pre-build symlink (config-exmple/menu ->
# .../iDryerControllerV2/src/menu), so every build silently adopts whatever the
# controller checkout happens to be. This fork removes that symlink: builds use
# the committed mirror, and moving to a new controller release is an explicit,
# reviewable commit.
#
# The public iDryerControllerV2 repo is a single squashed commit at v2.0.0 — it
# carries no v1.x history. The committed mirror (controller v1.0.2) is the only
# copy of the v1 menu that exists. Do not run this unless you intend to move to
# controller v2.0.0 and your dryer's RP2040 is running it.
#
# Usage:
#   scripts/sync-menu.sh          # show what would change
#   scripts/sync-menu.sh --apply  # actually copy

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

SRC="vendor/iDryerControllerV2/src/menu"
DST="lib/idryer-menu/src"
FILES=(menu_meta.h menu_ids.h menu_cache.h menu_cache.cpp)
VERSION_SRC="vendor/iDryerControllerV2/src/version.h"

[[ -d "$SRC" ]] || {
  echo "error: $SRC missing — run scripts/bootstrap.sh" >&2
  exit 1
}

APPLY=0
[[ "${1:-}" == "--apply" ]] && APPLY=1

echo "Controller submodule @ $(git -C vendor/iDryerControllerV2 rev-parse --short HEAD)"
echo "  current mirror: VERSION_MAJOR $(sed -n 's/^#define VERSION_MAJOR \([0-9]*\).*/\1/p' "$DST/version.h")"
echo "  incoming:       VERSION_MAJOR $(sed -n 's/^#define VERSION_MAJOR \([0-9]*\).*/\1/p' "$VERSION_SRC")"
echo

changed=0
for f in "${FILES[@]}"; do
  if ! diff -q "$SRC/$f" "$DST/$f" >/dev/null 2>&1; then
    echo "  differs: $f"
    changed=1
  fi
done
diff -q "$VERSION_SRC" "$DST/version.h" >/dev/null 2>&1 || { echo "  differs: version.h"; changed=1; }

if [[ $changed -eq 0 ]]; then
  echo "Mirror is already in sync."
  exit 0
fi

if [[ $APPLY -eq 0 ]]; then
  echo
  echo "Dry run. Re-run with --apply to copy, then verify your RP2040 firmware version matches."
  exit 0
fi

for f in "${FILES[@]}"; do
  cp "$SRC/$f" "$DST/$f"
done
cp "$VERSION_SRC" "$DST/version.h"

echo
echo "Mirror updated. Rebuild and confirm against real hardware before committing."
