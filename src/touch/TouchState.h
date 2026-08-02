#pragma once

#include <stdint.h>
#include <menu_cache.h>   // MENU_MAX_UNITS

// The seam between the app (UART, WiFi, HTTP) and the touch UI. TouchApp.cpp
// owns the state and implements these; TouchUi.cpp only reads snapshots and
// issues commands, so it never has to know how the controller is reached.
namespace idryer_touch {

struct UnitView {
    float    airTempC    = 0.0f;
    float    airHumidity = 0.0f;
    float    heaterPower = 0.0f;   // 0..1
    float    targetTempC = 0.0f;
    bool     fanOn       = false;
    uint8_t  mode        = 0;      // 0 idle, 1 drying, 2 storage, 3 profile, 4 fault
    uint32_t durationS   = 0;
    uint32_t elapsedS    = 0;
};

// Copied by value rather than handing out pointers into app state — the UI runs
// from the same task, but a snapshot keeps a redraw self-consistent even if a
// UART frame lands midway through it.
struct DeviceView {
    bool     mcuConnected = false;
    bool     apMode       = false;
    uint8_t  unitsCount   = 1;
    uint16_t menuRevision = 0;
    char     ip[16]       = {0};
    char     ssid[33]     = {0};
    char     mcuSerial[24]= {0};
    char     firmware[16] = {0};
    UnitView units[MENU_MAX_UNITS];
};

DeviceView deviceView();

void cmdStartDrying(uint8_t unit, int tempC, uint32_t minutes);
void cmdStartStorage(uint8_t unit, int tempC, uint32_t humidityPct);
void cmdStop(uint8_t unit);
void cmdRequestConfig();

} // namespace idryer_touch
