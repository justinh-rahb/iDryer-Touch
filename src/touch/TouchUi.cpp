// Touch UI for the 320x240 CYD panel.
//
// Deliberately not a port of the web dashboard. On a resistive panel poked with
// a finger, the constraints are: nothing smaller than a fingertip, no text
// entry, no sliders, and one unit on screen at a time (three cards would be
// ~106 px wide each). The controller's 202-item menu tree is not here either —
// that is what the jog wheel and the web UI are for.
//
// Layout is absolute rather than flex-like. The panel is a fixed 320x240, and
// explicit geometry makes the touch-target budget auditable at a glance:
//
//   header  28 px   unit chip cycles units; right side opens Info
//   body   156 px   page content
//   footer  56 px   44 px buttons inside 6 px padding

#if defined(IDRYER_TOUCH_LOCAL)

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include <menu_meta.h>
#include <menu_cache.h>

#include "TouchUi.h"
#include "TouchState.h"
#include "TouchDisplay.h"
#include "MenuPresets.h"
#include "MenuFilter.h"

namespace idryer_touch {
namespace ui {
namespace {

// Palette shared with the web UI.
constexpr uint32_t C_BG        = 0x090d14;
constexpr uint32_t C_PANEL     = 0x101722;
constexpr uint32_t C_PANEL2    = 0x0d141e;
constexpr uint32_t C_LINE      = 0x29384b;
constexpr uint32_t C_EDGE      = 0x233247;
constexpr uint32_t C_TEXT      = 0xeaf1fa;
constexpr uint32_t C_MUTED     = 0x91a2b8;
constexpr uint32_t C_ACCENT    = 0x5ba9ff;
constexpr uint32_t C_ACTIVE    = 0x183d62;
constexpr uint32_t C_ACTIVEDGE = 0x376493;
constexpr uint32_t C_BTN       = 0x172537;
constexpr uint32_t C_BTNEDGE   = 0x38506b;
constexpr uint32_t C_PRIMARY   = 0x1d5d99;
constexpr uint32_t C_PRIMEDGE  = 0x69b4ff;
constexpr uint32_t C_STOP      = 0x4b202a;
constexpr uint32_t C_STOPEDGE  = 0x844150;
constexpr uint32_t C_WARM      = 0xffae57;
constexpr uint32_t C_OK        = 0x4bd192;
constexpr uint32_t C_IDLE      = 0x627287;

constexpr int16_t W = display::kWidth;    // 320
constexpr int16_t HEADER_H = 28;
constexpr int16_t PAGE_H   = display::kHeight - HEADER_H;  // 212
constexpr int16_t BODY_H   = 156;
constexpr int16_t FOOT_H   = 56;
constexpr int16_t BTN_H    = 44;

// Menu ids the controller uses for the drying/storage parameters. Same
// convention the cloud build relied on; bounds are pulled from the live menu
// metadata when the controller has sent it, with these as the fallback.
constexpr uint16_t MID_DRY_TEMP  = 3;
constexpr uint16_t MID_DRY_TIME  = 4;
constexpr uint16_t MID_STORE_TEMP = 7;
constexpr uint16_t MID_STORE_HUM  = 8;

enum Page : uint8_t { PAGE_HOME, PAGE_PRESETS, PAGE_DRY, PAGE_STORE,
                      PAGE_MENU, PAGE_EDIT, PAGE_INFO, PAGE_NOLINK, PAGE_COUNT };

lv_obj_t *s_pages[PAGE_COUNT] = {nullptr};
Page      s_page = PAGE_HOME;
uint8_t   s_unit = 0;

// Header
lv_obj_t *s_unitChip, *s_unitChipLbl, *s_hdrRight, *s_dot;

// Home
lv_obj_t *s_modeDot, *s_modeLbl, *s_targetLbl;
lv_obj_t *s_tempVal, *s_humVal, *s_bar, *s_lineL, *s_lineR;
lv_obj_t *s_btnDry, *s_btnStop;

// Setup pages
lv_obj_t *s_dryTempLbl, *s_dryTimeLbl, *s_storeTempLbl, *s_storeHumLbl;
int s_dryTemp = 60, s_dryTime = 240, s_storeTemp = 45, s_storeHum = 15;

// Info
lv_obj_t *s_kvSsid, *s_kvIp, *s_kvMcu, *s_kvFw, *s_infoNote;

uint32_t s_lastTick = 0;

// Presets: a 3x3 grid, paged rather than scrolled. 17 materials fit two pages,
// and a 98x46 button is a comfortable target where a scroll gesture on resistive
// is not.
constexpr uint8_t kPresetCols = 3, kPresetRows = 3;
constexpr uint8_t kPresetsPerPage = kPresetCols * kPresetRows;
MenuPreset s_presets[kMaxPresets];
uint8_t    s_presetCount = 0;
uint8_t    s_presetPage  = 0;
int8_t     s_presetSel   = -1;          // index into s_presets, -1 = none
lv_obj_t  *s_presetBtn[kPresetsPerPage] = {nullptr};
lv_obj_t  *s_presetLbl[kPresetsPerPage] = {nullptr};
lv_obj_t  *s_presetSubLbl[kPresetsPerPage] = {nullptr};
lv_obj_t  *s_presetFootMore, *s_presetFootMoreLbl;
lv_obj_t  *s_presetFootGo,   *s_presetFootGoLbl;
lv_obj_t  *s_presetFootAlt,  *s_presetFootAltLbl;

// Controller menu browser. 2 cols x 3 rows so 20-character labels fit; paged,
// never scrolled. With EXT/EXT/LNK there is no jog wheel, so this is the only
// local route to the controller's settings.
constexpr uint8_t kMenuCols = 2, kMenuRows = 3;
constexpr uint8_t kMenuPerPage = kMenuCols * kMenuRows;
constexpr uint8_t kMenuMaxChildren = 24;
constexpr uint8_t kMenuMaxDepth = 6;

uint16_t s_menuNode = 0;                      // current submenu
uint16_t s_menuStack[kMenuMaxDepth];          // ancestry for BACK
uint8_t  s_menuDepth = 0;
uint16_t s_menuChildren[kMenuMaxChildren];
uint8_t  s_menuChildCount = 0;
uint8_t  s_menuPage = 0;
int8_t   s_menuSel  = -1;                     // index into s_menuChildren, for actions
lv_obj_t *s_menuTitle;
lv_obj_t *s_menuBtn[kMenuPerPage], *s_menuLbl[kMenuPerPage], *s_menuSub[kMenuPerPage];
lv_obj_t *s_menuFootBack, *s_menuFootBackLbl, *s_menuFootMore, *s_menuFootMoreLbl;

// Generic value editor, reused for any menu item.
uint16_t s_editId = 0;
float    s_editVal = 0;
lv_obj_t *s_editTitle, *s_editValLbl, *s_editRangeLbl;

// ── Small styling helpers ────────────────────────────────────────────────────

void paint(lv_obj_t *o, uint32_t bg, uint32_t border, lv_coord_t radius) {
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(o, border ? 1 : 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
}

lv_obj_t *panel(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h,
                uint32_t bg, uint32_t border, lv_coord_t radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    paint(o, bg, border, radius);
    return o;
}

lv_obj_t *label(lv_obj_t *parent, int16_t x, int16_t y, const char *text,
                const lv_font_t *font, uint32_t colour) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    return l;
}

lv_obj_t *button(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h,
                 const char *text, uint32_t bg, uint32_t edge,
                 lv_event_cb_t cb, void *user, const lv_font_t *font = &lv_font_montserrat_16) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    paint(b, bg, edge, 7);
    // Visible press feedback matters more here than on a mouse UI — resistive
    // panels give no tactile confirmation.
    lv_obj_set_style_bg_color(b, lv_color_hex(C_ACTIVE), LV_STATE_PRESSED);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
    lv_obj_center(l);

    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

void showPage(Page p) {
    s_page = p;
    for (uint8_t i = 0; i < PAGE_COUNT; i++) {
        if (!s_pages[i]) continue;
        if (i == p) lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// Bounds from the controller's own menu metadata when available. Keeps the
// steppers honest against whatever firmware the RP2040 is actually running.
void bounds(uint16_t id, int fallbackMin, int fallbackMax, int &lo, int &hi) {
    if (id < MENU_META_COUNT && g_menu_cache.revision > 0) {
        const MenuMeta &m = g_menu_meta[id];
        if (m.max_val > m.min_val) {
            lo = (int)m.min_val;
            hi = (int)m.max_val;
            return;
        }
    }
    lo = fallbackMin;
    hi = fallbackMax;
}

void step(int &value, int delta, uint16_t id, int fbMin, int fbMax) {
    int lo, hi;
    bounds(id, fbMin, fbMax, lo, hi);
    value += delta;
    if (value < lo) value = lo;
    if (value > hi) value = hi;
}

void fmtDuration(char *buf, size_t n, uint32_t seconds) {
    const uint32_t h = seconds / 3600, m = (seconds % 3600) / 60;
    if (h) snprintf(buf, n, "%luh %02lum", (unsigned long)h, (unsigned long)m);
    else   snprintf(buf, n, "%lum", (unsigned long)m);
}

// ── Events ───────────────────────────────────────────────────────────────────

void onCycleUnit(lv_event_t *) {
    const DeviceView d = deviceView();
    if (d.unitsCount > 1) s_unit = (uint8_t)((s_unit + 1) % d.unitsCount);
    showPage(PAGE_HOME);
}
void onOpenInfo(lv_event_t *)  { showPage(PAGE_INFO); }
void onHome(lv_event_t *)      { showPage(PAGE_HOME); }
void refreshPresetGrid();
void onOpenMenu(lv_event_t *);   // defined with the browser, below

void onOpenPresets(lv_event_t *) {
    s_presetSel = -1;
    s_presetPage = 0;
    showPage(PAGE_PRESETS);
    refreshPresetGrid();
}
void onOpenDry(lv_event_t *)   { showPage(PAGE_DRY); }

void onPresetTap(lv_event_t *e) {
    const uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    const uint8_t idx  = s_presetPage * kPresetsPerPage + slot;
    if (idx >= s_presetCount) return;
    // Select rather than launch. A stray press on a resistive panel should not
    // start a heater; the footer becomes the confirmation.
    s_presetSel = (s_presetSel == (int8_t)idx) ? -1 : (int8_t)idx;
    refreshPresetGrid();
}

void onPresetBack(lv_event_t *) {
    // Rightmost slot is always "get out": drop the armed selection first, and
    // only leave the page once nothing is selected.
    if (s_presetSel >= 0) { s_presetSel = -1; refreshPresetGrid(); return; }
    showPage(PAGE_HOME);
}

void onPresetMore(lv_event_t *) {
    const uint8_t pages = (s_presetCount + kPresetsPerPage - 1) / kPresetsPerPage;
    if (pages > 1) s_presetPage = (uint8_t)((s_presetPage + 1) % pages);
    s_presetSel = -1;
    refreshPresetGrid();
}

void onPresetStart(lv_event_t *) {
    if (s_presetSel < 0 || s_presetSel >= (int8_t)s_presetCount) return;
    const MenuPreset &p = s_presets[s_presetSel];
    // Ordinary drying command with an explicit unitId, not the preset's own
    // START action — that action is global scope, so which unit it runs on is
    // the controller's choice. See MenuPresets.h.
    cmdStartDrying(s_unit, presetTemp(p), (uint32_t)presetMinutes(p));
    s_presetSel = -1;
    showPage(PAGE_HOME);
}
void onOpenStore(lv_event_t *) { showPage(PAGE_STORE); }
void onStop(lv_event_t *)      { cmdStop(s_unit); }
void onRetry(lv_event_t *)     { cmdRequestConfig(); }

void onDryTempStep(lv_event_t *e) {
    step(s_dryTemp, (int)(intptr_t)lv_event_get_user_data(e), MID_DRY_TEMP, 30, 110);
}
void onDryTimeStep(lv_event_t *e) {
    step(s_dryTime, (int)(intptr_t)lv_event_get_user_data(e), MID_DRY_TIME, 1, 1440);
}
void onStoreTempStep(lv_event_t *e) {
    step(s_storeTemp, (int)(intptr_t)lv_event_get_user_data(e), MID_STORE_TEMP, 30, 110);
}
void onStoreHumStep(lv_event_t *e) {
    step(s_storeHum, (int)(intptr_t)lv_event_get_user_data(e), MID_STORE_HUM, 1, 90);
}

void onStartDry(lv_event_t *) {
    cmdStartDrying(s_unit, s_dryTemp, (uint32_t)s_dryTime);
    showPage(PAGE_HOME);
}
void onStartStore(lv_event_t *) {
    cmdStartStorage(s_unit, s_storeTemp, (uint32_t)s_storeHum);
    showPage(PAGE_HOME);
}
void onBacklight(lv_event_t *) {
    // Cycle rather than offering a slider — see the file header.
    const uint8_t cur = display::backlight();
    display::setBacklight(cur > 75 ? 50 : (cur > 40 ? 20 : 100));
}
void onRestart(lv_event_t *) { ESP.restart(); }

// ── Construction ─────────────────────────────────────────────────────────────

void buildHeader(lv_obj_t *root) {
    lv_obj_t *h = panel(root, 0, 0, W, HEADER_H, 0x0b111a, 0, 0);
    lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(h, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(h, 1, 0);

    // Wordmark rather than the PNG the web UI uses: LVGL would need a PNG
    // decoder for a 20 px mark, which is not worth the flash.
    label(h, 6, 8, "i", &lv_font_montserrat_14, C_ACCENT);
    label(h, 12, 8, "Dryer", &lv_font_montserrat_14, C_TEXT);

    s_unitChip = lv_btn_create(h);
    lv_obj_remove_style_all(s_unitChip);
    lv_obj_set_pos(s_unitChip, 56, 3);
    lv_obj_set_size(s_unitChip, 96, 22);
    paint(s_unitChip, C_ACTIVE, C_ACTIVEDGE, 6);
    lv_obj_add_event_cb(s_unitChip, onCycleUnit, LV_EVENT_CLICKED, nullptr);
    s_unitChipLbl = lv_label_create(s_unitChip);
    lv_label_set_text(s_unitChipLbl, "UNIT 1");
    lv_obj_set_style_text_font(s_unitChipLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_unitChipLbl, lv_color_hex(C_TEXT), 0);
    lv_obj_center(s_unitChipLbl);

    // The whole right side is one target that opens Info.
    lv_obj_t *right = lv_btn_create(h);
    lv_obj_remove_style_all(right);
    lv_obj_set_pos(right, 160, 0);
    lv_obj_set_size(right, W - 160, HEADER_H);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(right, onOpenInfo, LV_EVENT_CLICKED, nullptr);

    // 106 px for the address label: "192.168.1.84 AP" is ~85 px at this size,
    // so the dot sits further left than the visual centre would suggest.
    s_dot = panel(right, 44, 11, 6, 6, C_IDLE, 0, 3);
    s_hdrRight = label(right, 54, 8, "", &lv_font_montserrat_12, C_MUTED);
}

lv_obj_t *newPage(lv_obj_t *root) {
    lv_obj_t *p = panel(root, 0, HEADER_H, W, PAGE_H, C_BG, 0, 0);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

lv_obj_t *footer(lv_obj_t *page) {
    lv_obj_t *f = panel(page, 0, BODY_H, W, FOOT_H, 0x0b111a, 0, 0);
    lv_obj_set_style_border_side(f, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(f, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(f, 1, 0);
    return f;
}

void buildHome(lv_obj_t *root) {
    lv_obj_t *p = newPage(root);
    s_pages[PAGE_HOME] = p;

    s_modeDot = panel(p, 8, 12, 8, 8, C_IDLE, 0, 4);
    s_modeLbl = label(p, 22, 5, "--", &lv_font_montserrat_16, C_TEXT);
    s_targetLbl = label(p, 150, 8, "", &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_width(s_targetLbl, 162);
    lv_obj_set_style_text_align(s_targetLbl, LV_TEXT_ALIGN_RIGHT, 0);

    // Two readouts, not six: these are the numbers you walk up to read.
    // Height is 68, not 60: montserrat_36 has a 40 px line height, and a 60 px
    // panel with a 1 px border leaves a 58 px content box — the digits clip.
    // Units live in the caption because LVGL's Montserrat faces are ASCII only,
    // so there is no degree sign to put next to the value.
    lv_obj_t *a = panel(p, 6, 28, 149, 68, C_PANEL2, C_EDGE, 6);
    label(a, 8, 6, "AIR TEMP (C)", &lv_font_montserrat_12, C_MUTED);
    s_tempVal = label(a, 8, 18, "--", &lv_font_montserrat_36, C_TEXT);

    lv_obj_t *b = panel(p, 165, 28, 149, 68, C_PANEL2, C_EDGE, 6);
    label(b, 8, 6, "HUMIDITY (%)", &lv_font_montserrat_12, C_MUTED);
    s_humVal = label(b, 8, 18, "--", &lv_font_montserrat_36, C_TEXT);

    s_bar = lv_bar_create(p);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_pos(s_bar, 6, 112);
    lv_obj_set_size(s_bar, 308, 6);
    paint(s_bar, C_PANEL2, C_EDGE, 3);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 3, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    s_lineL = label(p, 6, 126, "", &lv_font_montserrat_12, C_MUTED);
    s_lineR = label(p, 170, 126, "", &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_width(s_lineR, 144);
    lv_obj_set_style_text_align(s_lineR, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *f = footer(p);
    // Four across: 72 px each. Narrower than the 98 px elsewhere but still a
    // comfortable target, and MENU has to be here — in the EXT/EXT/LNK layout
    // this panel is the only local route to the controller's settings.
    s_btnDry  = button(f, 7,   6, 72, BTN_H, "DRY",   C_BTN,  C_BTNEDGE,  onOpenPresets, nullptr,
                       &lv_font_montserrat_14);
                button(f, 84,  6, 72, BTN_H, "STORE", C_BTN,  C_BTNEDGE,  onOpenStore, nullptr,
                       &lv_font_montserrat_14);
                button(f, 161, 6, 72, BTN_H, "MENU",  C_BTN,  C_BTNEDGE,  onOpenMenu,  nullptr,
                       &lv_font_montserrat_14);
    s_btnStop = button(f, 238, 6, 72, BTN_H, "STOP",  C_STOP, C_STOPEDGE, onStop,      nullptr,
                       &lv_font_montserrat_14);
}

// One stepper row: [ - ] [ value ] [ + ]. 58 px targets, 46 px tall.
lv_obj_t *stepper(lv_obj_t *page, int16_t y, const char *caption,
                  lv_event_cb_t cb, int stepDown, int stepUp) {
    label(page, 6, y, caption, &lv_font_montserrat_12, C_MUTED);
    const int16_t sy = y + 14;
    button(page, 6, sy, 58, 46, "-", C_BTN, C_BTNEDGE, cb, (void *)(intptr_t)stepDown,
           &lv_font_montserrat_28);
    lv_obj_t *box = panel(page, 70, sy, 180, 46, C_PANEL2, C_EDGE, 7);
    button(page, 256, sy, 58, 46, "+", C_BTN, C_BTNEDGE, cb, (void *)(intptr_t)stepUp,
           &lv_font_montserrat_28);

    lv_obj_t *v = lv_label_create(box);
    lv_label_set_text(v, "--");
    lv_obj_set_style_text_font(v, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(C_TEXT), 0);
    lv_obj_center(v);
    return v;
}

void buildPresets(lv_obj_t *root) {
    lv_obj_t *p = newPage(root);
    s_pages[PAGE_PRESETS] = p;

    // 3 cols x 3 rows in the 156 px body: 3*46 + 2*6 = 150, and 98 px wide.
    constexpr int16_t bw = 98, bh = 46, gap = 6, x0 = 7, y0 = 4;
    for (uint8_t r = 0; r < kPresetRows; r++) {
        for (uint8_t c = 0; c < kPresetCols; c++) {
            const uint8_t slot = r * kPresetCols + c;
            lv_obj_t *b = button(p, x0 + c * (bw + gap), y0 + r * (bh + gap),
                                 bw, bh, "", C_BTN, C_BTNEDGE, onPresetTap,
                                 (void *)(uintptr_t)slot, &lv_font_montserrat_14);
            // button() centres one label; add a second line under it.
            s_presetBtn[slot] = b;
            s_presetLbl[slot] = lv_obj_get_child(b, 0);
            lv_obj_align(s_presetLbl[slot], LV_ALIGN_TOP_MID, 0, 6);
            s_presetSubLbl[slot] = label(b, 0, 27, "", &lv_font_montserrat_12, C_MUTED);
            lv_obj_set_width(s_presetSubLbl[slot], bw);
            lv_obj_set_style_text_align(s_presetSubLbl[slot], LV_TEXT_ALIGN_CENTER, 0);
        }
    }

    lv_obj_t *f = footer(p);
    s_presetFootMore = button(f, 7,   6, 98, BTN_H, "MORE", C_BTN, C_BTNEDGE,
                              onPresetMore, nullptr, &lv_font_montserrat_14);
    s_presetFootMoreLbl = lv_obj_get_child(s_presetFootMore, 0);
    s_presetFootGo   = button(f, 111, 6, 98, BTN_H, "CUSTOM", C_BTN, C_BTNEDGE,
                              onOpenDry, nullptr, &lv_font_montserrat_14);
    s_presetFootGoLbl = lv_obj_get_child(s_presetFootGo, 0);
    s_presetFootAlt  = button(f, 215, 6, 98, BTN_H, "BACK", C_BTN, C_BTNEDGE,
                              onPresetBack, nullptr, &lv_font_montserrat_14);
    s_presetFootAltLbl = lv_obj_get_child(s_presetFootAlt, 0);
}

void refreshPresetGrid() {
    char buf[40];
    const uint8_t pages = s_presetCount ? (s_presetCount + kPresetsPerPage - 1) / kPresetsPerPage : 1;

    for (uint8_t slot = 0; slot < kPresetsPerPage; slot++) {
        const uint8_t idx = s_presetPage * kPresetsPerPage + slot;
        lv_obj_t *b = s_presetBtn[slot];
        if (!b) continue;
        if (idx >= s_presetCount) {
            lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(b, LV_OBJ_FLAG_HIDDEN);
        const MenuPreset &p = s_presets[idx];
        lv_label_set_text(s_presetLbl[slot], p.name ? p.name : "?");

        // Names come from flash, values from the cache — so between boot and the
        // controller's first config the grid knows every material but none of
        // their settings. Say that, rather than printing a confident "0C 0m".
        const bool known = g_menu_cache.revision > 0;
        const int  mins  = presetMinutes(p);
        if (!known)          snprintf(buf, sizeof(buf), "...");
        else if (mins >= 60) snprintf(buf, sizeof(buf), "%dC %dh%02d", presetTemp(p), mins / 60, mins % 60);
        else                 snprintf(buf, sizeof(buf), "%dC %dm", presetTemp(p), mins);
        lv_label_set_text(s_presetSubLbl[slot], buf);

        const bool sel = known && (s_presetSel == (int8_t)idx);
        lv_obj_set_style_bg_color(b, lv_color_hex(sel ? C_PRIMARY : C_BTN), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(sel ? C_PRIMEDGE : C_BTNEDGE), 0);
        // Starting a run from unknown settings would send 0 C for 0 minutes.
        lv_obj_set_style_text_color(s_presetLbl[slot],
                                    lv_color_hex(known ? C_TEXT : C_IDLE), 0);
        if (known) lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        else       lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    }

    // Footer follows the selection: browse -> commit.
    if (s_presetSel >= 0 && s_presetSel < (int8_t)s_presetCount) {
        const MenuPreset &p = s_presets[s_presetSel];
        snprintf(buf, sizeof(buf), "DRY %s", p.name ? p.name : "");
        lv_label_set_text(s_presetFootMoreLbl, buf);
        lv_obj_set_style_bg_color(s_presetFootMore, lv_color_hex(C_PRIMARY), 0);
        lv_obj_set_style_border_color(s_presetFootMore, lv_color_hex(C_PRIMEDGE), 0);
        lv_obj_remove_event_cb(s_presetFootMore, onPresetMore);
        lv_obj_add_event_cb(s_presetFootMore, onPresetStart, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(s_presetFootAltLbl, "CANCEL");
    } else {
        snprintf(buf, sizeof(buf), pages > 1 ? "MORE %u/%u" : "MORE", s_presetPage + 1, pages);
        lv_label_set_text(s_presetFootMoreLbl, buf);
        lv_obj_set_style_bg_color(s_presetFootMore, lv_color_hex(C_BTN), 0);
        lv_obj_set_style_border_color(s_presetFootMore, lv_color_hex(C_BTNEDGE), 0);
        lv_obj_remove_event_cb(s_presetFootMore, onPresetStart);
        lv_obj_add_event_cb(s_presetFootMore, onPresetMore, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(s_presetFootAltLbl, "BACK");
    }
}

void refreshMenuGrid();
void openEditor(uint16_t id);

void loadMenuNode(uint16_t node) {
    s_menuNode = node;
    s_menuPage = 0;
    s_menuSel  = -1;
    s_menuChildCount = collectVisibleChildren(node, s_menuChildren, kMenuMaxChildren);
}

void onOpenMenu(lv_event_t *) {   // fwd-declared above for buildHome
    s_menuDepth = 0;
    loadMenuNode(0);
    showPage(PAGE_MENU);
    refreshMenuGrid();
}

void onMenuBack(lv_event_t *) {
    if (s_menuSel >= 0) { s_menuSel = -1; refreshMenuGrid(); return; }
    if (s_menuDepth == 0) { showPage(PAGE_HOME); return; }
    loadMenuNode(s_menuStack[--s_menuDepth]);
    refreshMenuGrid();
}

void onMenuMore(lv_event_t *) {
    const uint8_t pages = (s_menuChildCount + kMenuPerPage - 1) / kMenuPerPage;
    if (pages > 1) s_menuPage = (uint8_t)((s_menuPage + 1) % pages);
    s_menuSel = -1;
    refreshMenuGrid();
}

void onMenuRun(lv_event_t *) {
    if (s_menuSel < 0 || s_menuSel >= (int8_t)s_menuChildCount) return;
    cmdInvokeMenu(s_menuChildren[s_menuSel]);
    s_menuSel = -1;
    refreshMenuGrid();
}

void onMenuTap(lv_event_t *e) {
    const uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    const uint8_t idx  = s_menuPage * kMenuPerPage + slot;
    if (idx >= s_menuChildCount) return;
    const uint16_t id = s_menuChildren[idx];
    const MenuMeta &m = g_menu_meta[id];

    if (m.type == META_SUBMENU) {
        if (s_menuDepth < kMenuMaxDepth) s_menuStack[s_menuDepth++] = s_menuNode;
        loadMenuNode(id);
        refreshMenuGrid();
    } else if (m.type == META_ACTION) {
        // Never fire on the first tap. Scale calibration and PID autotune live
        // down here, and a stray press on resistive should not start either.
        s_menuSel = (s_menuSel == (int8_t)idx) ? -1 : (int8_t)idx;
        refreshMenuGrid();
    } else if (m.type == META_TOGGLE) {
        const uint8_t u = (m.scope == META_SCOPE_PER_UNIT) ? s_unit : 0;
        cmdSetMenuValue(id, u, g_menu_cache.getFloat(id, u) != 0.0f ? 0.0f : 1.0f);
    } else {
        openEditor(id);
    }
}

void buildMenu(lv_obj_t *root) {
    lv_obj_t *p = newPage(root);
    s_pages[PAGE_MENU] = p;

    s_menuTitle = label(p, 7, 1, "", &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_width(s_menuTitle, W - 14);

    constexpr int16_t bw = 150, bh = 44, gx = 6, gy = 4, x0 = 7, y0 = 16;
    for (uint8_t r = 0; r < kMenuRows; r++)
        for (uint8_t c = 0; c < kMenuCols; c++) {
            const uint8_t slot = r * kMenuCols + c;
            lv_obj_t *b = button(p, x0 + c * (bw + gx), y0 + r * (bh + gy), bw, bh, "",
                                 C_BTN, C_BTNEDGE, onMenuTap, (void *)(uintptr_t)slot,
                                 &lv_font_montserrat_12);
            s_menuBtn[slot] = b;
            s_menuLbl[slot] = lv_obj_get_child(b, 0);
            lv_obj_align(s_menuLbl[slot], LV_ALIGN_TOP_MID, 0, 6);
            s_menuSub[slot] = label(b, 0, 25, "", &lv_font_montserrat_12, C_MUTED);
            lv_obj_set_width(s_menuSub[slot], bw);
            lv_obj_set_style_text_align(s_menuSub[slot], LV_TEXT_ALIGN_CENTER, 0);
        }

    lv_obj_t *f = footer(p);
    // Footer convention across every page: the rightmost slot is the way out
    // (BACK / HOME / CANCEL), paging and primary actions sit to its left.
    s_menuFootMore = button(f, 7, 6, 150, BTN_H, "MORE", C_BTN, C_BTNEDGE, onMenuMore,
                            nullptr, &lv_font_montserrat_14);
    s_menuFootMoreLbl = lv_obj_get_child(s_menuFootMore, 0);
    s_menuFootBack = button(f, 163, 6, 150, BTN_H, "BACK", C_BTN, C_BTNEDGE, onMenuBack,
                            nullptr, &lv_font_montserrat_14);
    s_menuFootBackLbl = lv_obj_get_child(s_menuFootBack, 0);
}

void refreshMenuGrid() {
    char buf[48];
    const uint8_t lang  = g_menu_cache.getLang() < MENU_LANG_COUNT ? g_menu_cache.getLang() : 0;
    const uint8_t pages = s_menuChildCount ? (s_menuChildCount + kMenuPerPage - 1) / kMenuPerPage : 1;

    const char *nodeName = (s_menuNode < MENU_META_COUNT && g_menu_meta[s_menuNode].title[lang])
                         ? g_menu_meta[s_menuNode].title[lang] : "MENU";
    if (pages > 1) snprintf(buf, sizeof(buf), "%s   %u/%u", nodeName, s_menuPage + 1, pages);
    else           snprintf(buf, sizeof(buf), "%s", nodeName);
    lv_label_set_text(s_menuTitle, buf);

    for (uint8_t slot = 0; slot < kMenuPerPage; slot++) {
        const uint8_t idx = s_menuPage * kMenuPerPage + slot;
        lv_obj_t *b = s_menuBtn[slot];
        if (idx >= s_menuChildCount) { lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(b, LV_OBJ_FLAG_HIDDEN);

        const uint16_t id = s_menuChildren[idx];
        const MenuMeta &m = g_menu_meta[id];
        lv_label_set_text(s_menuLbl[slot], m.title[lang] ? m.title[lang] : "?");

        const uint8_t u = (m.scope == META_SCOPE_PER_UNIT) ? s_unit : 0;
        switch (m.type) {
        case META_SUBMENU: snprintf(buf, sizeof(buf), "%u items", m.child_count); break;
        case META_ACTION:  snprintf(buf, sizeof(buf), "run"); break;
        case META_TOGGLE:  snprintf(buf, sizeof(buf), g_menu_cache.getFloat(id, u) != 0.0f ? "ON" : "OFF"); break;
        default: {
            const char *unit = m.unit[lang] ? m.unit[lang] : "";
            snprintf(buf, sizeof(buf), "%d %s", (int)(g_menu_cache.getFloat(id, u) + 0.5f), unit);
            break;
        }}
        lv_label_set_text(s_menuSub[slot], buf);

        const bool sel = (s_menuSel == (int8_t)idx);
        lv_obj_set_style_bg_color(b, lv_color_hex(sel ? C_STOP : C_BTN), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(sel ? C_STOPEDGE : C_BTNEDGE), 0);
    }

    // Footer turns into the confirmation when an action is armed.
    if (s_menuSel >= 0 && s_menuSel < (int8_t)s_menuChildCount) {
        const MenuMeta &m = g_menu_meta[s_menuChildren[s_menuSel]];
        snprintf(buf, sizeof(buf), "RUN %s", m.title[lang] ? m.title[lang] : "");
        lv_label_set_text(s_menuFootMoreLbl, buf);
        lv_obj_set_style_bg_color(s_menuFootMore, lv_color_hex(C_STOP), 0);
        lv_obj_set_style_border_color(s_menuFootMore, lv_color_hex(C_STOPEDGE), 0);
        lv_obj_remove_event_cb(s_menuFootMore, onMenuMore);
        lv_obj_add_event_cb(s_menuFootMore, onMenuRun, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(s_menuFootBackLbl, "CANCEL");   // same slot, same role
    } else {
        snprintf(buf, sizeof(buf), pages > 1 ? "MORE %u/%u" : "MORE", s_menuPage + 1, pages);
        lv_label_set_text(s_menuFootMoreLbl, buf);
        lv_obj_set_style_bg_color(s_menuFootMore, lv_color_hex(C_BTN), 0);
        lv_obj_set_style_border_color(s_menuFootMore, lv_color_hex(C_BTNEDGE), 0);
        lv_obj_remove_event_cb(s_menuFootMore, onMenuRun);
        lv_obj_add_event_cb(s_menuFootMore, onMenuMore, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(s_menuFootBackLbl, s_menuDepth ? "BACK" : "HOME");
    }
}

void refreshEditor();

void onEditStep(lv_event_t *e) {
    const int mult = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_editId >= MENU_META_COUNT) return;
    const MenuMeta &m = g_menu_meta[s_editId];
    const float step = (m.step > 0.0f) ? m.step : 1.0f;
    s_editVal += step * mult;
    if (s_editVal < m.min_val) s_editVal = m.min_val;
    if (s_editVal > m.max_val) s_editVal = m.max_val;
    refreshEditor();
}

void onEditSave(lv_event_t *) {
    if (s_editId < MENU_META_COUNT) {
        const MenuMeta &m = g_menu_meta[s_editId];
        const uint8_t u = (m.scope == META_SCOPE_PER_UNIT) ? s_unit : 0;
        cmdSetMenuValue(s_editId, u, s_editVal);
    }
    showPage(PAGE_MENU);
    refreshMenuGrid();
}

void onEditCancel(lv_event_t *) { showPage(PAGE_MENU); refreshMenuGrid(); }

void openEditor(uint16_t id) {
    if (id >= MENU_META_COUNT) return;
    const MenuMeta &m = g_menu_meta[id];
    const uint8_t u = (m.scope == META_SCOPE_PER_UNIT) ? s_unit : 0;
    s_editId  = id;
    s_editVal = g_menu_cache.getFloat(id, u);
    showPage(PAGE_EDIT);
    refreshEditor();
}

// One editor for every value item: bounds, step and units all come from the
// metadata, so there is nothing per-item to maintain.
void buildEdit(lv_obj_t *root) {
    lv_obj_t *p = newPage(root);
    s_pages[PAGE_EDIT] = p;

    s_editTitle = label(p, 7, 6, "", &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_width(s_editTitle, W - 14);
    s_editRangeLbl = label(p, 7, 26, "", &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_width(s_editRangeLbl, W - 14);

    button(p, 6, 48, 58, 60, "-", C_BTN, C_BTNEDGE, onEditStep, (void *)(intptr_t)-1,
           &lv_font_montserrat_28);
    lv_obj_t *box = panel(p, 70, 48, 180, 60, C_PANEL2, C_EDGE, 7);
    button(p, 256, 48, 58, 60, "+", C_BTN, C_BTNEDGE, onEditStep, (void *)(intptr_t)1,
           &lv_font_montserrat_28);
    s_editValLbl = lv_label_create(box);
    lv_label_set_text(s_editValLbl, "--");
    lv_obj_set_style_text_font(s_editValLbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_editValLbl, lv_color_hex(C_TEXT), 0);
    lv_obj_center(s_editValLbl);

    lv_obj_t *f = footer(p);
    button(f, 7,   6, 202, BTN_H, "SAVE",   C_PRIMARY, C_PRIMEDGE, onEditSave,   nullptr);
    button(f, 215, 6, 98,  BTN_H, "CANCEL", C_BTN,     C_BTNEDGE,  onEditCancel, nullptr);
}

void refreshEditor() {
    if (s_editId >= MENU_META_COUNT) return;
    const MenuMeta &m = g_menu_meta[s_editId];
    const uint8_t lang = g_menu_cache.getLang() < MENU_LANG_COUNT ? g_menu_cache.getLang() : 0;
    char buf[56];

    snprintf(buf, sizeof(buf), "%s", m.title[lang] ? m.title[lang] : "");
    lv_label_set_text(s_editTitle, buf);

    const char *unit = m.unit[lang] ? m.unit[lang] : "";
    if (m.scope == META_SCOPE_PER_UNIT)
        snprintf(buf, sizeof(buf), "unit %u   %d..%d %s", s_unit + 1,
                 (int)m.min_val, (int)m.max_val, unit);
    else
        snprintf(buf, sizeof(buf), "%d..%d %s  (global)", (int)m.min_val, (int)m.max_val, unit);
    lv_label_set_text(s_editRangeLbl, buf);

    // Integer display unless the step is fractional — PID gains need decimals.
    if (m.step > 0.0f && m.step < 1.0f) snprintf(buf, sizeof(buf), "%.2f %s", s_editVal, unit);
    else                                snprintf(buf, sizeof(buf), "%d %s", (int)(s_editVal + 0.5f), unit);
    lv_label_set_text(s_editValLbl, buf);
}

void buildDry(lv_obj_t *root) {
    lv_obj_t *p = newPage(root);
    s_pages[PAGE_DRY] = p;
    s_dryTempLbl = stepper(p, 6,  "TEMPERATURE", onDryTempStep, -5, 5);
    s_dryTimeLbl = stepper(p, 76, "DURATION",    onDryTimeStep, -30, 30);

    lv_obj_t *f = footer(p);
    button(f, 7,   6, 202, BTN_H, "START DRYING", C_PRIMARY, C_PRIMEDGE, onStartDry, nullptr);
    button(f, 215, 6, 98,  BTN_H, "CANCEL",       C_BTN,     C_BTNEDGE,  onHome,     nullptr);
}

void buildStore(lv_obj_t *root) {
    lv_obj_t *p = newPage(root);
    s_pages[PAGE_STORE] = p;
    s_storeTempLbl = stepper(p, 6,  "TEMPERATURE",     onStoreTempStep, -5, 5);
    s_storeHumLbl  = stepper(p, 76, "HUMIDITY TARGET", onStoreHumStep,  -5, 5);

    lv_obj_t *f = footer(p);
    button(f, 7,   6, 202, BTN_H, "START STORAGE", C_PRIMARY, C_PRIMEDGE, onStartStore, nullptr);
    button(f, 215, 6, 98,  BTN_H, "CANCEL",        C_BTN,     C_BTNEDGE,  onHome,       nullptr);
}

lv_obj_t *kvRow(lv_obj_t *p, int16_t y, const char *key) {
    label(p, 6, y, key, &lv_font_montserrat_12, C_MUTED);
    lv_obj_t *v = label(p, 120, y, "--", &lv_font_montserrat_12, C_TEXT);
    lv_obj_set_width(v, 194);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    return v;
}

void buildInfo(lv_obj_t *root) {
    lv_obj_t *p = newPage(root);
    s_pages[PAGE_INFO] = p;

    s_kvSsid = kvRow(p, 8,  "Wi-Fi");
    s_kvIp   = kvRow(p, 30, "Address");
    s_kvMcu  = kvRow(p, 52, "Controller");
    s_kvFw   = kvRow(p, 74, "Firmware");

    s_infoNote = label(p, 6, 100, "", &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_width(s_infoNote, 308);
    lv_label_set_long_mode(s_infoNote, LV_LABEL_LONG_WRAP);

    lv_obj_t *f = footer(p);
    button(f, 7,   6, 98,  BTN_H, "BACKLIGHT", C_BTN, C_BTNEDGE, onBacklight, nullptr,
           &lv_font_montserrat_12);
    button(f, 111, 6, 98,  BTN_H, "RESTART",   C_BTN, C_BTNEDGE, onRestart,   nullptr,
           &lv_font_montserrat_12);
    button(f, 215, 6, 98,  BTN_H, "BACK",      C_BTN, C_BTNEDGE, onHome,      nullptr);
}

void buildNoLink(lv_obj_t *root) {
    lv_obj_t *p = newPage(root);
    s_pages[PAGE_NOLINK] = p;

    lv_obj_t *t = label(p, 0, 44, "Controller not detected", &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(t, W);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *n = label(p, 24, 72,
        "Check the RJ45 cable to the iDryer.\nRetrying the UART handshake every 5 seconds.",
        &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_width(n, W - 48);
    lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *f = footer(p);
    button(f, 7,   6, 150, BTN_H, "RETRY LINK", C_BTN, C_BTNEDGE, onRetry,     nullptr);
    button(f, 163, 6, 150, BTN_H, "INFO",       C_BTN, C_BTNEDGE, onOpenInfo,  nullptr);
}

// ── Refresh ──────────────────────────────────────────────────────────────────

void refresh() {
    const DeviceView d = deviceView();
    char buf[64];

    // Re-scan presets whenever the controller sends a new config. Names come
    // from the metadata (fixed), values from the cache (live), so a preset
    // edited on the jog wheel shows its new temperature here.
    static uint16_t s_presetRev = 0xFFFF;
    if (d.menuRevision != s_presetRev) {
        s_presetRev   = d.menuRevision;
        s_presetCount = collectPresets(s_presets, kMaxPresets, g_menu_cache.getLang());
        if (s_presetPage * kPresetsPerPage >= s_presetCount) s_presetPage = 0;
        s_presetSel = -1;
        if (s_page == PAGE_PRESETS) refreshPresetGrid();
    }

    if (s_unit >= d.unitsCount) s_unit = 0;
    const UnitView &u = d.units[s_unit];

    // Header
    if (d.unitsCount > 1) snprintf(buf, sizeof(buf), "UNIT %u  %u/%u",
                                   s_unit + 1, s_unit + 1, d.unitsCount);
    else                  snprintf(buf, sizeof(buf), "UNIT %u", s_unit + 1);
    lv_label_set_text(s_unitChipLbl, d.mcuConnected ? buf : "NO LINK");
    lv_obj_set_style_bg_color(s_unitChip,
        lv_color_hex(d.mcuConnected ? C_ACTIVE : 0x1c1420), 0);

    lv_obj_set_style_bg_color(s_dot, lv_color_hex(d.mcuConnected ? C_OK : C_IDLE), 0);
    snprintf(buf, sizeof(buf), "%s%s", d.ip, d.apMode ? " AP" : "");
    lv_label_set_text(s_hdrRight, buf);

    // A missing controller takes over the screen, but only from Home — it must
    // not yank the user out of a setup page they are part-way through.
    if (!d.mcuConnected && s_page == PAGE_HOME)      showPage(PAGE_NOLINK);
    else if (d.mcuConnected && s_page == PAGE_NOLINK) showPage(PAGE_HOME);

    if (s_page == PAGE_HOME) {
        static const char *kModes[] = {"Idle", "Drying", "Storage", "Profile", "Fault"};
        const uint8_t m = u.mode < 5 ? u.mode : 0;
        const bool active = (m == 1 || m == 2 || m == 3);

        lv_label_set_text(s_modeLbl, kModes[m]);
        lv_obj_set_style_bg_color(s_modeDot,
            lv_color_hex(m == 4 ? C_STOPEDGE : (active ? C_WARM : C_IDLE)), 0);

        // Both setpoints in one line — "what I asked for" is a single idea, and
        // it keeps the duration visible without spending another metric box.
        if (active && u.durationS) {
            const uint32_t h = u.durationS / 3600, m2 = (u.durationS % 3600) / 60;
            if (h) snprintf(buf, sizeof(buf), "target  %d C - %luh %02lum",
                            (int)(u.targetTempC + 0.5f), (unsigned long)h, (unsigned long)m2);
            else   snprintf(buf, sizeof(buf), "target  %d C - %lum",
                            (int)(u.targetTempC + 0.5f), (unsigned long)m2);
        } else if (active) {
            snprintf(buf, sizeof(buf), "target  %d C", (int)(u.targetTempC + 0.5f));
        } else {
            snprintf(buf, sizeof(buf), "target  --");
        }
        lv_label_set_text(s_targetLbl, buf);

        snprintf(buf, sizeof(buf), "%.1f", u.airTempC);
        lv_label_set_text(s_tempVal, buf);
        snprintf(buf, sizeof(buf), "%.1f", u.airHumidity);
        lv_label_set_text(s_humVal, buf);

        if (u.durationS) {
            const uint32_t pct = (uint32_t)((uint64_t)u.elapsedS * 1000u / u.durationS);
            lv_bar_set_value(s_bar, (int32_t)(pct > 1000 ? 1000 : pct), LV_ANIM_OFF);
            lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
            char el[24];
            fmtDuration(el, sizeof(el), u.elapsedS);
            snprintf(buf, sizeof(buf), "%s elapsed", el);
        } else {
            lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
            snprintf(buf, sizeof(buf), "%s", active ? "no time limit" : "");
        }
        lv_label_set_text(s_lineL, buf);

        snprintf(buf, sizeof(buf), "fan %s  heater %d%%",
                 u.fanOn ? "on" : "off", (int)(u.heaterPower * 100.0f + 0.5f));
        lv_label_set_text(s_lineR, buf);

        // Point the footer at whatever is actually useful: STOP is dead weight
        // on an idle unit, and DRY is the obvious next action.
        lv_obj_set_style_bg_color(s_btnStop,
            lv_color_hex(active ? C_STOP : 0x131c28), 0);
        lv_obj_set_style_border_color(s_btnStop,
            lv_color_hex(active ? C_STOPEDGE : C_BTNEDGE), 0);
        lv_obj_set_style_bg_color(s_btnDry,
            lv_color_hex(active ? C_BTN : C_PRIMARY), 0);
        lv_obj_set_style_border_color(s_btnDry,
            lv_color_hex(active ? C_BTNEDGE : C_PRIMEDGE), 0);
    }

    if (s_page == PAGE_PRESETS) refreshPresetGrid();

    if (s_page == PAGE_DRY) {
        snprintf(buf, sizeof(buf), "%d C", s_dryTemp);
        lv_label_set_text(s_dryTempLbl, buf);
        if (s_dryTime >= 60) snprintf(buf, sizeof(buf), "%dh %02dm", s_dryTime / 60, s_dryTime % 60);
        else                 snprintf(buf, sizeof(buf), "%dm", s_dryTime);
        lv_label_set_text(s_dryTimeLbl, buf);
    }

    if (s_page == PAGE_STORE) {
        snprintf(buf, sizeof(buf), "%d C", s_storeTemp);
        lv_label_set_text(s_storeTempLbl, buf);
        snprintf(buf, sizeof(buf), "%d %%", s_storeHum);
        lv_label_set_text(s_storeHumLbl, buf);
    }

    if (s_page == PAGE_INFO) {
        lv_label_set_text(s_kvSsid, d.ssid[0] ? d.ssid : "not connected");
        lv_label_set_text(s_kvIp,   d.ip);
        lv_label_set_text(s_kvMcu,  d.mcuSerial[0] ? d.mcuSerial : "not detected");
        snprintf(buf, sizeof(buf), "%s  menu rev %u", d.firmware, d.menuRevision);
        lv_label_set_text(s_kvFw, buf);
        snprintf(buf, sizeof(buf),
                 "Wi-Fi, updates and the full menu are set from http://%s/", d.ip);
        lv_label_set_text(s_infoNote, buf);
    }
}

} // namespace

void begin() {
    lv_obj_t *root = lv_scr_act();
    lv_obj_remove_style_all(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    buildHeader(root);
    buildHome(root);
    buildPresets(root);
    buildMenu(root);
    buildEdit(root);
    buildDry(root);
    buildStore(root);
    buildInfo(root);
    buildNoLink(root);

    // Seed the steppers from whatever the controller currently has, when known.
    if (g_menu_cache.revision > 0) {
        const int t = (int)g_menu_cache.getFloat(MID_DRY_TEMP);
        const int d = (int)g_menu_cache.getFloat(MID_DRY_TIME);
        if (t > 0) s_dryTemp = t;
        if (d > 0) s_dryTime = d;
    }

    showPage(PAGE_HOME);
    refresh();
    display::setBacklight(display::backlight());
}

void tick() {
    const uint32_t now = millis();
    if (now - s_lastTick < 500) return;
    s_lastTick = now;
    refresh();
}

} // namespace ui
} // namespace idryer_touch

#endif // IDRYER_TOUCH_LOCAL
