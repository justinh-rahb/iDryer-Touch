// Board bring-up for the ESP32-2432S028R ("Cheap Yellow Display").
//
// Pin, rotation and colour constants are taken from a working bring-up on this
// exact board — see docs/ARCHITECTURE.md. The two that cost the most
// time to rediscover:
//
//   * PENIRQ is deliberately NOT wired (pin_int = -1). It varies across CYD
//     revisions; pressure is polled over SPI instead.
//   * Rotation is a swap only. Adding a mirror after the swap flips the
//     landscape image top to bottom.
//
// LovyanGFX is configured entirely in code, which avoids TFT_eSPI's
// User_Setup.h global-macro approach fighting PlatformIO's dependency finder.

#if defined(IDRYER_TOUCH_LOCAL)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <Preferences.h>

#include "TouchDisplay.h"

namespace idryer_touch {
namespace display {
namespace {

constexpr char kPrefsNamespace[] = "idryer-touch";
constexpr char kCalibKey[]       = "tcal";
constexpr char kCalibRotKey[]    = "tcalrot";
constexpr char kBacklightKey[]   = "bl";

// Landscape. 1 and 3 are the two landscape orientations; 3 puts the USB and
// header connectors at the bottom, which is right way up on this board — 1 gave
// an upside-down image on hardware.
constexpr uint8_t kRotation = 3;

// LVGL renders in horizontal slices rather than a full framebuffer: 320x240x2
// would be 150 KB and this chip has no PSRAM. 40 lines is 25.6 KB and leaves
// the SPI DMA comfortably fed.
constexpr int  kDrawLines  = 40;
constexpr size_t kBufPixels = (size_t)kWidth * kDrawLines;

class CydPanel : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341  panel_;
    lgfx::Bus_SPI        bus_;
    lgfx::Light_PWM      light_;
    lgfx::Touch_XPT2046  touch_;

public:
    CydPanel() {
        {   // Display bus — HSPI. These are the ESP32's default HSPI pins.
            auto cfg = bus_.config();
            cfg.spi_host    = HSPI_HOST;
            cfg.spi_mode    = 0;
            // 24 MHz is the value proven on this board. 40 MHz works on most
            // units and is noticeably smoother; drop back if you see tearing or
            // speckle on the first flash.
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 14;
            cfg.pin_mosi    = 13;
            cfg.pin_miso    = 12;
            cfg.pin_dc      = 2;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }
        {   // Panel. No reset line is broken out; the chip resets over the bus.
            auto cfg = panel_.config();
            cfg.pin_cs           = 15;
            cfg.pin_rst          = -1;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.readable         = false;
            cfg.invert           = false;
            // ILI9341 on this board is wired BGR, which is LovyanGFX's default
            // for the panel (rgb_order false). Setting it true tints everything.
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            panel_.config(cfg);
        }
        {   // Backlight on GPIO21, dimmable.
            auto cfg = light_.config();
            cfg.pin_bl      = 21;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            light_.config(cfg);
            panel_.setLight(&light_);
        }
        {   // Touch — separate bus (VSPI), so it never contends with the panel.
            auto cfg = touch_.config();
            cfg.x_min      = 300;
            cfg.x_max      = 3900;
            cfg.y_min      = 300;
            cfg.y_max      = 3900;
            cfg.pin_int    = -1;      // see file header: PENIRQ is unreliable here
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.spi_host   = VSPI_HOST;
            cfg.freq       = 1000000;
            cfg.pin_sclk   = 25;
            cfg.pin_mosi   = 32;
            cfg.pin_miso   = 39;
            cfg.pin_cs     = 33;
            touch_.config(cfg);
            panel_.setTouch(&touch_);
        }
        setPanel(&panel_);
    }
};

CydPanel   s_lcd;
bool       s_ready = false;
uint8_t    s_backlight = 80;

lv_disp_draw_buf_t s_drawBuf;
lv_color_t        *s_buf = nullptr;
lv_disp_drv_t      s_dispDrv;
lv_indev_drv_t     s_indevDrv;

void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    s_lcd.startWrite();
    s_lcd.setAddrWindow(area->x1, area->y1, w, h);
    // Handing LovyanGFX an explicit rgb565_t lets it do the byte swap the SPI
    // panel needs, which is why lv_conf.h leaves LV_COLOR_16_SWAP at 0.
    s_lcd.writePixels(reinterpret_cast<lgfx::rgb565_t *>(pixels),
                      (uint32_t)w * h);
    s_lcd.endWrite();

    lv_disp_flush_ready(drv);
}

void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
    int32_t x = 0, y = 0;
    if (s_lcd.getTouch(&x, &y)) {
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Stored calibration is 8 uint16 values (LovyanGFX's corner parameters).
//
// The stored rotation is checked alongside it. Calibration is measured in the
// orientation that was active when it was taken, so changing kRotation silently
// invalidates it — without this check, a rotation fix would leave a board
// calibrated upside down and the touch targets mirrored.
bool loadCalibration() {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, true)) return false;
    uint16_t data[8];
    const size_t  got = prefs.getBytes(kCalibKey, data, sizeof(data));
    const uint8_t rot = prefs.getUChar(kCalibRotKey, 0xFF);
    const uint8_t bl  = prefs.getUChar(kBacklightKey, 80);
    prefs.end();
    if (bl >= 10 && bl <= 100) s_backlight = bl;
    if (got != sizeof(data) || rot != kRotation) return false;
    s_lcd.setTouchCalibrate(data);
    return true;
}

void runCalibration() {
    s_lcd.fillScreen(lv_color_hex(0x090d14).full ? 0x0000 : 0x0000);
    s_lcd.setTextColor(0xFFFFFFU, 0x090D14U);
    s_lcd.setTextDatum(textdatum_t::middle_center);
    s_lcd.drawString("Touch the corners", kWidth / 2, kHeight / 2 - 12);
    s_lcd.drawString("to calibrate",      kWidth / 2, kHeight / 2 + 10);
    delay(900);

    uint16_t data[8];
    s_lcd.calibrateTouch(data, 0x5BA9FFU, 0x090D14U, 18);

    Preferences prefs;
    if (prefs.begin(kPrefsNamespace, false)) {
        prefs.putBytes(kCalibKey, data, sizeof(data));
        prefs.putUChar(kCalibRotKey, kRotation);
        prefs.end();
    }
}

} // namespace

bool begin() {
    if (!s_lcd.init()) return false;

    s_lcd.setRotation(kRotation);      // landscape, 320x240
    s_lcd.fillScreen(0x0000);
    s_lcd.setBrightness(0);            // stay dark until something is drawn

    if (!loadCalibration()) {
        s_lcd.setBrightness((uint8_t)(s_backlight * 255 / 100));
        runCalibration();
    }

    s_buf = (lv_color_t *)heap_caps_malloc(kBufPixels * sizeof(lv_color_t),
                                           MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_buf) return false;

    lv_init();
    lv_disp_draw_buf_init(&s_drawBuf, s_buf, nullptr, kBufPixels);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res  = kWidth;
    s_dispDrv.ver_res  = kHeight;
    s_dispDrv.flush_cb = flush;
    s_dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&s_dispDrv);

    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type    = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = readTouch;
    lv_indev_drv_register(&s_indevDrv);

    s_ready = true;
    return true;
}

void loop() {
    if (!s_ready) return;
    lv_timer_handler();
}

bool ready() { return s_ready; }

void setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    if (percent < 5)   percent = 5;
    s_backlight = percent;
    s_lcd.setBrightness((uint8_t)((uint16_t)percent * 255 / 100));

    Preferences prefs;
    if (prefs.begin(kPrefsNamespace, false)) {
        prefs.putUChar(kBacklightKey, percent);
        prefs.end();
    }
}

uint8_t backlight() { return s_backlight; }

void recalibrateTouch() {
    if (!s_ready) return;
    runCalibration();
    lv_obj_invalidate(lv_scr_act());
}

} // namespace display
} // namespace idryer_touch

#endif // IDRYER_TOUCH_LOCAL
