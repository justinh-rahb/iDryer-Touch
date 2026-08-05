#pragma once

#include <stdint.h>
#include <menu_meta.h>
#include <menu_cache.h>

// Material presets, discovered from the controller's own menu rather than
// hardcoded. Under PRESETS each material is a submenu of exactly three items:
//
//   [56] PLA          submenu, 3 children
//    ├ [57] TEMPERATURE   value, degC
//    ├ [58] TIME          value, minutes
//    └ [59] START         action
//
// The layout is regular across all 17 (PLA … PC-CF plus MY1-MY3, the
// user-definable slots), so walking first_child/child_count picks up whatever
// the controller firmware ships — a new material appears without a code change.
//
// The START action is deliberately ignored. It is META_SCOPE_GLOBAL, so which
// unit it runs on is the controller's business (presumably its active unit) and
// not something this firmware can state. Reading temp/time and sending an
// ordinary drying command with an explicit unitId is deterministic and correct
// for multi-unit machines, at the cost of duplicating a little logic.

namespace idryer_touch {

// PRESETS submenu. An id rather than a role because the metadata carries no
// role for it; validated structurally below, so a menu reshuffle degrades to
// "no presets" instead of showing nonsense.
constexpr uint16_t kMenuPresetsRoot = 55;
constexpr uint8_t  kMaxPresets      = 24;

struct MenuPreset {
    uint16_t    id     = 0;      // the material submenu
    uint16_t    tempId = 0;
    uint16_t    timeId = 0;
    const char *name   = nullptr;
};

// Fills out[] and returns how many were found.
inline uint8_t collectPresets(MenuPreset *out, uint8_t maxOut,
                              uint8_t lang = 0) {
    if (!out || maxOut == 0) return 0;
    if (kMenuPresetsRoot >= MENU_META_COUNT) return 0;
    if (lang >= MENU_LANG_COUNT) lang = 0;

    const MenuMeta &root = g_menu_meta[kMenuPresetsRoot];
    if (root.type != META_SUBMENU || root.first_child < 0) return 0;

    uint8_t n = 0;
    for (uint16_t i = 0; i < root.child_count && n < maxOut; i++) {
        const uint16_t id = (uint16_t)root.first_child + i * 4u;  // stride: sub + 3 children
        if (id >= MENU_META_COUNT) break;

        const MenuMeta &m = g_menu_meta[id];
        // Structural check — anything that is not "submenu with 3 children" is
        // not a preset, whatever the ids happen to be.
        if (m.type != META_SUBMENU || m.parent != (int16_t)kMenuPresetsRoot) continue;
        if (m.child_count != 3 || m.first_child < 0) continue;

        const uint16_t t = (uint16_t)m.first_child;
        if (t + 2 >= MENU_META_COUNT) continue;
        if (g_menu_meta[t].type != META_VALUE || g_menu_meta[t + 1].type != META_VALUE) continue;

        out[n].id     = id;
        out[n].tempId = t;
        out[n].timeId = t + 1;
        out[n].name   = m.title[lang] ? m.title[lang] : "";
        n++;
    }
    return n;
}

inline int presetTemp(const MenuPreset &p) {
    return (int)(g_menu_cache.getFloat(p.tempId, 0) + 0.5f);
}
inline int presetMinutes(const MenuPreset &p) {
    return (int)(g_menu_cache.getFloat(p.timeId, 0) + 0.5f);
}

} // namespace idryer_touch
