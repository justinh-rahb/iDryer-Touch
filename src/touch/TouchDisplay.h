#pragma once

#include <stdint.h>

// ILI9341 + XPT2046 bring-up for the ESP32-2432S028R, plus the LVGL bindings.
namespace idryer_touch {
namespace display {

// Panel geometry after rotation. Everything in the UI assumes landscape.
constexpr int16_t kWidth  = 320;
constexpr int16_t kHeight = 240;

// Brings up SPI, the panel, touch and LVGL. Returns false if any stage failed;
// the firmware stays useful over the web UI in that case.
bool begin();

// Drive LVGL. Cheap to call every loop; it rate-limits internally.
void loop();

bool ready();

// 0-100. Persisted, so the panel comes back at the same level after a reboot.
void setBacklight(uint8_t percent);
uint8_t backlight();

// Idle blanking. 0 disables it. Persisted.
void     setScreenTimeout(uint16_t seconds);
uint16_t screenTimeout();

// True while the backlight is off from an idle timeout.
bool asleep();

// Turn the backlight back on and restart the idle countdown. The touch layer
// calls this itself; expose it so non-touch events can wake the panel too.
void wake();

// Re-runs the corner-target calibration and stores the result. Touch on a
// resistive panel is unusable without this, so it runs automatically on first
// boot; this is the manual redo.
void recalibrateTouch();

} // namespace display
} // namespace idryer_touch
