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

// One controller error, as delivered by a UART Log frame. Fields are shorter
// than UartLogPayload's because the panel is 320px wide and the controller's
// own strings are terse ("Open circuit", "Sensor reading invalid") — storing
// the full 163-byte payload six times over would cost DRAM for text no one
// can read on this screen.
struct ErrorEntry {
    char     severity[8]  = {0};   // CRIT / ERROR / WARN
    char     source[16]   = {0};   // THERMISTOR, SHT, HEATER, AIR, ...
    char     event[24]    = {0};   // SENSOR_OPEN, SENSOR_INVALID, ...
    char     message[64]  = {0};   // human text
    uint8_t  unitId       = 0;
    uint32_t atMs         = 0;     // millis() when received
};

constexpr uint8_t kMaxErrors = 6;

// Copied by value rather than handing out pointers into app state — the UI runs
// from the same task, but a snapshot keeps a redraw self-consistent even if a
// UART frame lands midway through it.
struct DeviceView {
    bool     mcuConnected = false;
    bool     apMode       = false;
    uint8_t  unitsCount   = 1;
    uint16_t menuRevision = 0;
    // Count only, so the 500ms redraw can show a badge without copying strings.
    uint8_t  errorCount   = 0;
    char     ip[16]       = {0};
    char     ssid[33]     = {0};
    char     mcuSerial[24]= {0};
    char     firmware[16] = {0};
    UnitView units[MENU_MAX_UNITS];
};

DeviceView deviceView();

// Newest first. Returns false when idx is past the end.
bool errorAt(uint8_t idx, ErrorEntry &out);

void cmdStartDrying(uint8_t unit, int tempC, uint32_t minutes);
void cmdStartStorage(uint8_t unit, int tempC, uint32_t humidityPct);
void cmdStop(uint8_t unit);
void cmdRequestConfig();

// Sends UartCmdCode::ClearErrors (0x12), which on the controller clears the
// EEPROM error log, drops the error overlay, and returns any unit stuck in
// DryerMode::Error to Idle. Deliberately not ResetFault (0x10) — that handler
// is a stub upstream, its resetFault() call still commented out, so it acks
// and does nothing.
void cmdClearErrors();

// Generic menu writes, the same path the jog wheel uses. Needed because the
// EXT/EXT/LNK layout has no jog wheel — this panel is the only local control.
void cmdSetMenuValue(uint16_t id, uint8_t unit, float value);
void cmdInvokeMenu(uint16_t id);

} // namespace idryer_touch
