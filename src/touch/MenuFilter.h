#pragma once

#include <stdint.h>
#include <menu_meta.h>
#include "MenuPresets.h"

// Which controller menu items the touch browser shows.
//
// Two reasons to hide a branch, kept separate because they age differently:
//
//   COVERED  this panel already has a purpose-built page for it. Filtering the
//            whole subtree keeps the browser to what is not otherwise reachable.
//            The roots are derived from the ids the UI already uses, so adding a
//            page removes its subtree automatically — no list to maintain.
//
//   BLOCKED  unrecoverable from the UI if got wrong. Only PORT CONFIG qualifies.
//            Everything else destructive — scale calibration, PID autotune — is
//            reachable but confirm-gated, because those are redoable.
//
// Everything else is shown. Filtering by subtree rather than by id matters:
// menu ids move between controller releases (13 was PRESET_PLA_TEMP on v1 and
// is FIRST STAGE on v2), so an id list would rot silently. Three roots survive
// a reshuffle far better than seventy leaves.

namespace idryer_touch {

// Leaves the UI already renders; their parents are the covered roots.
constexpr uint16_t kUiDryTempId   = 3;   // DRYING  -> the DRY page
constexpr uint16_t kUiStoreTempId = 7;   // STORAGE -> the STORE page

// PORT CONFIG. A bad combination is not merely wrong, it is unrecoverable from
// the UI — the controller's own docs say to erase and reflash. Not something to
// leave one stray press away on a touch panel.
constexpr uint16_t kBlockedPortConfig = 194;

// PORTAL: cloud claiming, plus IGNOR EXT CMD. This fork has no portal, so CLAIM
// is dead weight. IGNOR EXT CMD blocks the very commands these UIs send, and is
// a confusing thing to leave one tap away.
//
// Caveat worth knowing: if that flag is already set, hiding it removes the only
// way to clear it from here. Protocol v2 reports it in the Status payload, so
// the right answer is to surface the state (see TODO) rather than offer the
// toggle — until then, the jog wheel or the web MENU on an older build are the
// escape hatches.
constexpr uint16_t kBlockedPortal = 187;


inline int16_t menuParentOf(uint16_t id) {
    return id < MENU_META_COUNT ? g_menu_meta[id].parent : -1;
}

// True if `id` is `root` or sits anywhere beneath it.
inline bool isUnder(uint16_t id, uint16_t root) {
    if (id == root) return true;
    int16_t p = menuParentOf(id);
    for (uint8_t guard = 0; guard < 8 && p >= 0; guard++) {   // depth is 4; 8 is slack
        if ((uint16_t)p == root) return true;
        p = menuParentOf((uint16_t)p);
    }
    return false;
}

inline bool isBlockedMenuItem(uint16_t id) {
    return isUnder(id, kBlockedPortConfig) || isUnder(id, kBlockedPortal);
}

inline bool isCoveredMenuItem(uint16_t id) {
    const int16_t dry   = menuParentOf(kUiDryTempId);
    const int16_t store = menuParentOf(kUiStoreTempId);
    if (dry   >= 0 && isUnder(id, (uint16_t)dry))   return true;
    if (store >= 0 && isUnder(id, (uint16_t)store)) return true;
    // PRESETS deliberately NOT filtered. The MAT page is a shortcut for running
    // one; the tree is where you edit temperatures and times, and the MY1-MY3
    // slots are only worth having if they are editable from the panel too.
    return false;
}

inline bool isMenuItemVisible(uint16_t id) {
    if (id >= MENU_META_COUNT) return false;
    return !isBlockedMenuItem(id) && !isCoveredMenuItem(id);
}

// Visible children of `parent`, in id order. Returns how many were written.
inline uint8_t collectVisibleChildren(uint16_t parent, uint16_t *out, uint8_t maxOut) {
    if (!out || maxOut == 0 || parent >= MENU_META_COUNT) return 0;
    const MenuMeta &p = g_menu_meta[parent];
    if (p.type != META_SUBMENU || p.first_child < 0) return 0;

    uint8_t n = 0;
    // child_count is the number of children, but they are not guaranteed
    // contiguous, so scan by parent link rather than trusting first_child+i.
    for (uint16_t id = (uint16_t)p.first_child; id < MENU_META_COUNT && n < maxOut; id++) {
        if (g_menu_meta[id].parent != (int16_t)parent) continue;
        if (!isMenuItemVisible(id)) continue;
        out[n++] = id;
    }
    return n;
}

} // namespace idryer_touch
