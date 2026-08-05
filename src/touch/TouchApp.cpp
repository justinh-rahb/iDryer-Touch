// iDryer Touch — local-only firmware. No portal, no MQTT, no TLS, no claiming.
//
//   RP2040 controller  <--UART 115200-->  ESP32 (this file)  <--WiFi-->  LAN
//
// The web layer (AP fallback, captive portal, /setup form, OTA upload, status
// WebSocket) is ported from iHeater-Remote's src/standalone/StandaloneApp.cpp,
// adapted for the dryer. Differences worth knowing:
//
//   * iHeater has no sensors, so its dashboard could only report the command it
//     sent. The dryer reports real per-unit telemetry from the RP2040.
//   * iHeater's control surface is 8 fixed modes. The dryer's is the controller's
//     own menu tree, exposed generically via /api/menu + /api/set.
//   * No Bambu/Moonraker integrations here — the dryer's binding story is
//     different and not in scope yet.
//
// What this keeps from iDryer-Link: the UART bridge to the RP2040 and the menu
// mirror (g_menu_meta + g_menu_cache). What it drops: iDryer::Link and everything
// it drags in — cloud state machine, HTTP portal API, MQTT + mbedtls, claiming,
// portal OTA, and core's LocalAccess (which authenticates against a portal-issued
// device token that a de-clouded device never receives).
//
// Dropping menu_buildFullJson() matters more than it looks: it holds a 38,856
// byte function-local StaticJsonDocument in .bss to assemble the portal's menu
// payload. That single object overflows dram0_0_seg on a classic ESP32. The UIs
// render from g_menu_cache directly, so nothing calls it and --gc-sections drops
// it. /api/menu is paged for the same reason.

#if defined(IDRYER_TOUCH_LOCAL)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <IPAddress.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <string.h>

#include <idryer_uart.h>
#include <hal/hal_arduino.h>

#include <menu_commands.h>
#include <menu_cache.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#include "version.h"
#include "TouchApp.h"
#include "TouchDashboard.h"
#include "TouchLogo.h"
#include "TouchState.h"
#include "TouchDisplay.h"
#include "TouchUi.h"
#include "MenuPresets.h"
#include "MenuFilter.h"

using namespace idryer;

#ifndef IDRYER_UART_RX_PIN
#define IDRYER_UART_RX_PIN 27
#endif
#ifndef IDRYER_UART_TX_PIN
#define IDRYER_UART_TX_PIN 22
#endif
#ifndef IDRYER_AP_PASSWORD
#define IDRYER_AP_PASSWORD ""
#endif

namespace idryer_touch {
namespace {

constexpr int      kUartRxPin           = IDRYER_UART_RX_PIN;
constexpr int      kUartTxPin           = IDRYER_UART_TX_PIN;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kRestartDelayMs      = 800;
constexpr uint16_t kDnsPort             = 53;
constexpr size_t   kMaxWifiSsidLen      = 32;
constexpr size_t   kMaxWifiPasswordLen  = 64;
constexpr uint8_t  kMaxUnits            = MENU_MAX_UNITS;

const char *kPrefsNamespace = "idryer-touch";
const char *kWifiSsidKey    = "ssid";
const char *kWifiPasswordKey = "pass";

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

DNSServer        s_dnsServer;
WebServer        s_server(80);
WebSocketsServer s_statusSocket(81);

hal::ArduinoSerial s_uartSerial(Serial1, 1);
UartBridge         s_uart;
ConfigReceiver     s_configRx;

// Mirror of the controller state learned over UART. iDryer::Link used to own
// this; de-clouded we keep only what the local UIs render.
struct UnitState {
    float    airTempC    = 0.0f;
    float    airHumidity = 0.0f;
    float    heaterPower = 0.0f;
    bool     fanOn       = false;
    uint8_t  mode        = 0;
    float    targetTempC = 0.0f;
    uint32_t durationS   = 0;
    uint32_t elapsedS    = 0;
};

UnitState s_units[kMaxUnits];

bool     s_mcuConnected = false;
bool     s_apMode       = false;
uint8_t  s_unitsCount   = 1;
uint16_t s_configTid    = 0;
char     s_mcuSerial[24] = {0};

String   s_apSsid;
String   s_stationSsid;
String   s_wifiCredentialSource = "none";
uint32_t s_restartAtMs = 0;

bool   s_otaActive       = false;
bool   s_otaOk           = false;
size_t s_otaBytesWritten = 0;
String s_otaError;

// ── UART: controller → ESP32 ─────────────────────────────────────────────────

void requestConfig() {
    UartCmdPayload cmd{};
    cmd.command = UartCmdCode::GetConfig;
    cmd.unitId  = 0;  // RP2040 rejects unitId >= NUM_UNITS, so 0xFF is not valid here
    s_uart.sendCommand(cmd, false);
}

void onHello(const UartHelloPayload &p, const UartFrameHeader &) {
    HAL_LOG_INFO("UART", "Hello: type=%u fw=%u units=%u serial=%s",
                 p.deviceType, p.firmwareVersion, p.unitsCount, p.mcuSerial);

    UartHelloAckPayload ack{};
    ack.ipAddress = (uint32_t)WiFi.localIP();
    strncpy(ack.ssid, WiFi.SSID().c_str(), sizeof(ack.ssid) - 1);
    s_uart.sendHelloAck(ack);

    s_mcuConnected = true;
    strncpy(s_mcuSerial, p.mcuSerial, sizeof(s_mcuSerial) - 1);
    // Clamp rather than ignore. The UART contract carries units[4] but the menu
    // mirror is MENU_MAX_UNITS (3 on controller v2), so a controller reporting
    // more used to fall through this guard and leave a stale count from an
    // earlier session — the UI then showed the wrong number of units with no
    // hint anything had been dropped.
    if (p.unitsCount >= 1) {
        if (p.unitsCount > kMaxUnits) {
            HAL_LOG_WARN("UART", "controller reports %u units, menu mirror supports %u - clamping",
                         p.unitsCount, kMaxUnits);
            s_unitsCount = kMaxUnits;
        } else {
            s_unitsCount = p.unitsCount;
        }
    }

    requestConfig();
}

void onTelemetry(const UartTelemetryPayload &p, const UartFrameHeader &hdr) {
    for (uint8_t i = 0; i < p.count && i < kMaxUnits; i++) {
        const auto &e = p.units[i];
        if (e.unitId >= kMaxUnits) continue;
        s_units[e.unitId].airTempC    = e.temperatureC10 / 10.0f;
        s_units[e.unitId].airHumidity = e.humidityPct10  / 10.0f;
        s_units[e.unitId].heaterPower = e.heaterPowerPct / 100.0f;
        s_units[e.unitId].fanOn       = (e.fanOn != 0);
    }
    s_uart.sendTelemetryAck(hdr.sequence);
}

void onStatus(const UartStatusPayload &p, const UartFrameHeader &) {
    for (uint8_t i = 0; i < p.count && i < kMaxUnits; i++) {
        const auto &e = p.units[i];
        if (e.unitId >= kMaxUnits) continue;
        s_units[e.unitId].mode        = (uint8_t)e.mode;
        s_units[e.unitId].targetTempC = e.targetTempC10 / 10.0f;
        s_units[e.unitId].durationS   = (uint32_t)e.durationMinutes * 60u;
        s_units[e.unitId].elapsedS    = e.elapsedSeconds;
    }
}

// The RP2040 streams the menu as JSON fragments; ConfigReceiver reassembles.
// Unlike the cloud build we only parse into g_menu_cache — no republish.
void onConfigChunk(const UartConfigChunkPayload &p, uint8_t dataLen,
                   const UartFrameHeader &hdr) {
    auto result = s_configRx.processFragment(p, dataLen, hdr.flags);
    s_uart.sendConfigAck(hdr.sequence);
    if (result != ConfigFragResult::Complete) return;

    const char *json  = s_configRx.getJson();
    const bool  delta = s_configRx.isDelta();
    const bool  ok    = delta ? menu_parseDelta(json) : menu_parseFullConfig(json);

    HAL_LOG_INFO("MENU", "RX %s %u bytes -> %s",
                 delta ? "delta" : "full", s_configRx.getLength(), ok ? "ok" : "FAILED");
    s_configRx.reset();
}

void onLog(const uint8_t *payload, uint8_t length) {
    if (length < sizeof(UartLogPayload)) return;
    const auto *log = reinterpret_cast<const UartLogPayload *>(payload);
    HAL_LOG_INFO("UART", "Log[%s] %s/%s: %s (U%u)",
                 log->severity, log->source, log->event, log->message, log->unitId + 1);
}

// ── UART: ESP32 → controller ─────────────────────────────────────────────────

void sendStart(UartDryerMode mode, uint8_t unitId, int tempC, uint32_t arg1) {
    UartCmdPayload cmd{};
    cmd.command     = UartCmdCode::Start;
    cmd.targetState = (uint8_t)mode;
    cmd.unitId      = unitId;
    cmd.arg0        = (uint32_t)(tempC * 10);
    cmd.arg1        = arg1;
    s_uart.sendCommand(cmd);
}

void sendStop(uint8_t unitId) {
    UartCmdPayload cmd{};
    cmd.command = UartCmdCode::Stop;
    cmd.unitId  = unitId;
    s_uart.sendCommand(cmd);
}

// Pushes a menu edit back to the controller. Same ConfigPush path the cloud
// build's "set"/"invoke" commands used — the jog wheel and this share a route.
void sendConfigPush(const StaticJsonDocument<128> &doc) {
    char json[128];
    size_t len = serializeJson(doc, json, sizeof(json));

    UartConfigChunkPayload p{};
    p.transferId = ++s_configTid;
    p.totalSize  = (uint16_t)len;
    p.chunkIndex = 0;
    memcpy(p.data, json, len);
    s_uart.sendConfigPushChunk(p, UART_CONFIG_CHUNK_HEADER_SIZE + (uint8_t)len,
                               UART_FLAG_ACK_REQ | UART_FLAG_LAST_FRAGMENT);
}

void sendMenuSet(uint16_t id, uint8_t unit, float value) {
    StaticJsonDocument<128> doc;
    doc["cmd"]  = "set";
    doc["id"]   = id;
    doc["unit"] = unit;
    doc["val"]  = value;
    sendConfigPush(doc);
}

void sendMenuInvoke(uint16_t id) {
    StaticJsonDocument<128> doc;
    doc["cmd"] = "invoke";
    doc["id"]  = id;
    sendConfigPush(doc);
}

// ── WiFi ─────────────────────────────────────────────────────────────────────

bool loadStoredWifiCredentials(String &ssid, String &password) {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, true)) return false;
    ssid     = prefs.getString(kWifiSsidKey, "");
    password = prefs.getString(kWifiPasswordKey, "");
    prefs.end();
    ssid.trim();
    return ssid.length() > 0;
}

bool loadWifiCredentials(String &ssid, String &password, String &source) {
    if (loadStoredWifiCredentials(ssid, password)) {
        source = "stored";
        return true;
    }
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    ssid     = WIFI_SSID;
    password = WIFI_PASSWORD;
    ssid.trim();
    if (ssid.length() > 0) {
        source = "build";
        return true;
    }
#endif
    source = "none";
    return false;
}

bool saveStoredWifiCredentials(const String &ssid, const String &password) {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) return false;
    const bool ok = prefs.putString(kWifiSsidKey, ssid) > 0 &&
                    prefs.putString(kWifiPasswordKey, password) >= 0;
    prefs.end();
    return ok;
}

bool clearStoredWifiCredentials() {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) return false;
    prefs.remove(kWifiSsidKey);
    prefs.remove(kWifiPasswordKey);
    prefs.end();
    return true;
}

void scheduleRestart() { s_restartAtMs = millis() + kRestartDelayMs; }

void updateRestart() {
    if (s_restartAtMs == 0) return;
    if ((int32_t)(millis() - s_restartAtMs) >= 0) {
        Serial.println("[TOUCH] Restarting");
        Serial.flush();
        ESP.restart();
    }
}

void startAccessPoint() {
    uint64_t mac = ESP.getEfuseMac();
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "iDryer Touch %04X",
             static_cast<unsigned>((mac >> 32) & 0xFFFF));
    s_apSsid = ssid;

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(kApIp, kApGateway, kApSubnet);
    const char *password = IDRYER_AP_PASSWORD;
    if (strlen(password) >= 8) {
        WiFi.softAP(ssid, password);
    } else {
        WiFi.softAP(ssid);
    }
    s_apMode = true;
    s_dnsServer.start(kDnsPort, "*", kApIp);

    Serial.printf("[TOUCH] AP SSID=%s IP=%s (captive portal DNS up)\n",
                  ssid, WiFi.softAPIP().toString().c_str());
}

void configureWiFi() {
    WiFi.persistent(false);
    WiFi.setSleep(false);

    String ssid, password, source;
    if (loadWifiCredentials(ssid, password, source)) {
        s_stationSsid          = ssid;
        s_wifiCredentialSource = source;

        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());
        Serial.printf("[TOUCH] Connecting to SSID=%s source=%s\n",
                      ssid.c_str(), source.c_str());

        const uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED &&
               (uint32_t)(millis() - start) < kWifiConnectTimeoutMs) {
            delay(100);
        }

        if (WiFi.status() == WL_CONNECTED) {
            s_apMode = false;
            Serial.printf("[TOUCH] WiFi connected IP=%s\n",
                          WiFi.localIP().toString().c_str());
            return;
        }

        Serial.println("[TOUCH] WiFi connect timeout, starting AP");
        WiFi.disconnect(true);
    } else {
        s_stationSsid          = "";
        s_wifiCredentialSource = "none";
    }

    startAccessPoint();
}

String localIpString() {
    return s_apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

const char *wifiModeString() { return s_apMode ? "ap" : "sta"; }

// ── HTTP helpers ─────────────────────────────────────────────────────────────

bool isIpLiteral(const String &host) {
    for (size_t i = 0; i < host.length(); ++i) {
        const char c = host[i];
        if ((c < '0' || c > '9') && c != '.' && c != ':') return false;
    }
    return host.length() > 0;
}

bool captiveRedirectIfNeeded() {
    if (!s_apMode) return false;
    const String host = s_server.hostHeader();
    if (host.length() == 0 || isIpLiteral(host)) return false;
    s_server.sendHeader("Location", String("http://") + kApIp.toString() + "/", true);
    s_server.send(302, "text/plain", "");
    return true;
}

String htmlEscape(const char *raw) {
    String out;
    for (const char *p = raw ? raw : ""; *p; ++p) {
        switch (*p) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '\"': out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += *p;       break;
        }
    }
    return out;
}

// ── Status ───────────────────────────────────────────────────────────────────

String statusJson() {
    StaticJsonDocument<1024> doc;
    doc["firmwareVersion"]      = VERSION_STR;
    doc["mcuConnected"]         = s_mcuConnected;
    doc["mcuSerial"]            = s_mcuSerial;
    doc["wifiMode"]             = wifiModeString();
    doc["ip"]                   = localIpString();
    doc["ssid"]                 = s_apMode ? s_apSsid : WiFi.SSID();
    doc["configuredWifiSsid"]   = s_stationSsid;
    doc["wifiCredentialSource"] = s_wifiCredentialSource;
    doc["restartPending"]       = s_restartAtMs != 0;
    doc["otaActive"]            = s_otaActive;
    doc["otaOk"]                = s_otaOk;
    doc["otaBytesWritten"]      = s_otaBytesWritten;
    doc["otaError"]             = s_otaError;
    doc["screenTimeoutS"]       = display::screenTimeout();
    doc["screenAsleep"]         = display::asleep();
    doc["menuRevision"]         = g_menu_cache.revision;
    doc["unitsCount"]           = s_unitsCount;

    JsonArray units = doc.createNestedArray("units");
    for (uint8_t i = 0; i < s_unitsCount && i < kMaxUnits; i++) {
        JsonObject u = units.createNestedObject();
        u["airTempC"]    = s_units[i].airTempC;
        u["airHumidity"] = s_units[i].airHumidity;
        u["heaterPower"] = s_units[i].heaterPower;
        u["fanOn"]       = s_units[i].fanOn;
        u["mode"]        = s_units[i].mode;
        u["targetTempC"] = s_units[i].targetTempC;
        u["durationS"]   = s_units[i].durationS;
        u["elapsedS"]    = s_units[i].elapsedS;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

void sendActionStatus() { s_server.send(200, "application/json", statusJson()); }

void pushStatusIfChanged(bool force = false) {
    static String   lastStatus;
    static uint32_t lastCheckMs = 0;
    const uint32_t  now = millis();
    if (!force && now - lastCheckMs < 500) return;
    lastCheckMs = now;

    String status = statusJson();
    if (force || status != lastStatus) {
        s_statusSocket.broadcastTXT(status);
        lastStatus = status;
    }
}

void onStatusSocketEvent(uint8_t client, WStype_t type, uint8_t *, size_t) {
    if (type == WStype_CONNECTED) {
        String status = statusJson();
        s_statusSocket.sendTXT(client, status);
    }
}

// ── Pages ────────────────────────────────────────────────────────────────────

void handleRoot() {
    if (captiveRedirectIfNeeded()) return;
    s_server.send_P(200, "text/html", kTouchDashboardHtml);
}

void handleLogo() {
    // Immutable for the life of a firmware image, so let the browser keep it —
    // the captive portal in AP mode reloads the page a lot.
    s_server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
    s_server.send_P(200, "image/png",
                    reinterpret_cast<const char *>(kTouchLogoPng), kTouchLogoPngLen);
}

// Idle-blanking choices. Values are seconds; 0 means never blank.
String screenTimeoutOptions() {
    static const uint16_t kValues[]  = {0, 15, 30, 60, 120, 300, 600, 1800};
    static const char*    kLabels[]  = {"Never", "15 seconds", "30 seconds",
                                        "1 minute", "2 minutes", "5 minutes",
                                        "10 minutes", "30 minutes"};
    const uint16_t current = display::screenTimeout();
    String out;
    for (size_t i = 0; i < sizeof(kValues) / sizeof(kValues[0]); i++) {
        out += "<option value=\"" + String(kValues[i]) + "\"";
        if (kValues[i] == current) out += " selected";
        out += ">" + String(kLabels[i]) + "</option>";
    }
    return out;
}

void handleDisplayPost() {
    if (!s_server.hasArg("timeout")) {
        s_server.send(400, "text/plain", "Missing timeout.");
        return;
    }
    const long seconds = s_server.arg("timeout").toInt();
    if (seconds < 0 || seconds > 3600) {
        s_server.send(400, "text/plain", "Timeout must be 0-3600 seconds.");
        return;
    }
    display::setScreenTimeout((uint16_t)seconds);
    Serial.printf("[TOUCH] screen timeout = %lds\n", seconds);
    s_server.sendHeader("Location", "/setup", true);
    s_server.send(303, "text/plain", "Saved");
}

void handleSetupPage() {
    if (captiveRedirectIfNeeded()) return;
    String page = R"HTML(<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><link rel=icon type=image/png href="/logo.png"><title>iDryer Touch setup</title><style>:root{color-scheme:dark;--bg:#090d14;--p:#101722;--l:#29384b;--t:#eaf1fa;--m:#91a2b8;--a:#5ba9ff;font-family:system-ui,sans-serif}body{margin:0;background:var(--bg);color:var(--t)}main{max-width:620px;margin:auto;padding:24px 15px}.card{background:var(--p);border:1px solid var(--l);border-radius:12px;padding:16px;margin:14px 0}.logo{display:flex;align-items:center;gap:10px;font-size:24px;font-weight:800;letter-spacing:-1px}.logo img{width:40px;height:40px;border-radius:9px}h1{font-size:23px}h2{font-size:13px;text-transform:uppercase;letter-spacing:.08em;color:var(--m)}label{display:grid;gap:6px;margin:12px 0;color:var(--m);font-size:13px}input,select,button{box-sizing:border-box;width:100%;min-height:43px;border-radius:8px;border:1px solid #38506b;background:#0c131d;color:var(--t);font:inherit;padding:0 10px}button{background:#1d5d99;border-color:#69b4ff;font-weight:700;margin-top:8px;cursor:pointer}button.sec{background:#202a38;border-color:#38506b}.note{font-size:13px;line-height:1.45;color:var(--m)}a{color:var(--a)}</style><main><p><a href="/">&larr; Dashboard</a></p><div class=logo><img src="/logo.png" alt="iDryer" width=40 height=40>iDryer Touch</div><h1>Wi-Fi setup</h1><p class=note>Everything stays on this device. Leave the password blank to keep the stored one when the network name is unchanged.</p><form method=post action="/api/setup"><section class=card><h2>Wi-Fi</h2><label>Network name<input name=ssid value="__SSID__" maxlength=32></label><label>Password<input name=password type=password placeholder="(unchanged)" maxlength=64></label><button>Save and restart</button></section></form><section class=card><h2>Display</h2><p class=note>Blanks the touch panel after this much idle time. The tap that wakes it is ignored, so it cannot press whatever happens to be under your finger.</p><form method=post action="/api/display"><label>Screen timeout<select name=timeout>__TIMEOUT_OPTS__</select></label><button>Save display settings</button></form></section><section class=card><h2>Stored credentials</h2><p class=note>Source: __SOURCE__</p><form method=post action="/api/wifi/clear"><button class=sec>Forget Wi-Fi</button></form></section><p class=note><a href="/fw">Firmware update</a> &middot; iDryer Touch v__VERSION__</p></main>)HTML";
    page.replace("__TIMEOUT_OPTS__", screenTimeoutOptions());
    page.replace("__SSID__",    htmlEscape(s_stationSsid.c_str()));
    page.replace("__SOURCE__",  htmlEscape(s_wifiCredentialSource.c_str()));
    page.replace("__VERSION__", VERSION_STR);
    s_server.send(200, "text/html", page);
}

void handleFirmwarePage() {
    if (captiveRedirectIfNeeded()) return;
    String page = R"HTML(<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><link rel=icon type=image/png href="/logo.png"><title>iDryer Touch firmware</title><style>:root{color-scheme:dark;--bg:#090d14;--p:#101722;--l:#29384b;--t:#eaf1fa;--m:#91a2b8;--a:#5ba9ff;--bad:#ff8893;font-family:system-ui,sans-serif}body{margin:0;background:var(--bg);color:var(--t)}main{max-width:560px;margin:auto;padding:36px 15px}.logo{display:flex;align-items:center;gap:10px;font-size:24px;font-weight:800;letter-spacing:-1px}.logo img{width:40px;height:40px;border-radius:9px}.card{background:var(--p);border:1px solid var(--l);border-radius:12px;padding:18px;margin:16px 0}h1{font-size:22px;margin:14px 0 7px}.note{color:var(--m);font-size:13px;line-height:1.5}input,button{box-sizing:border-box;width:100%;min-height:45px;border-radius:8px;border:1px solid #38506b;background:#0c131d;color:var(--t);font:inherit;padding:8px;margin:8px 0}button{background:#1d5d99;border-color:#69b4ff;font-weight:700;cursor:pointer}.warning{color:var(--bad)}a{color:var(--a)}</style><main><p><a href="/">&larr; Dashboard</a></p><div class=logo><img src="/logo.png" alt="iDryer" width=40 height=40>iDryer Touch</div><h1>Firmware update</h1><p class=note>Installed: v__VERSION__</p><section class=card><p class="note warning"><strong>All units are stopped before the update is written.</strong> Do not disconnect power while the upload is in progress.</p><input id=file accept=".bin,application/octet-stream" type=file><button id=install>Install update</button><p class=note id=msg></p></section><p class=note>Upload the <code>-firmware.bin</code> asset, not the factory image.</p></main><script>const f=document.getElementById('file'),m=document.getElementById('msg');document.getElementById('install').onclick=()=>{let file=f.files[0];if(!file){m.textContent='Choose a firmware .bin first.';return}let x=new XMLHttpRequest(),d=new FormData();d.append('firmware',file,file.name);x.open('POST','/api/ota');x.upload.onprogress=e=>m.textContent=e.lengthComputable?'Uploading '+Math.round(e.loaded/e.total*100)+'%':'Uploading…';x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(_){}if(x.status>=200&&x.status<300){m.textContent='Update written. Rebooting…';setTimeout(waitForDevice,1500)}else m.textContent=r.error||'Update failed.'};x.onerror=()=>m.textContent='Update failed.';x.send(d)};function waitForDevice(){let end=Date.now()+120000,t=setInterval(async()=>{try{let r=await fetch('/',{cache:'no-store'});if(r.ok){clearInterval(t);location.href='/'}}catch(_){}if(Date.now()>end){clearInterval(t);m.textContent='The device is restarting. Reopen the dashboard when it is back online.'}},1000)}</script>)HTML";
    page.replace("__VERSION__", VERSION_STR);
    s_server.send(200, "text/html", page);
}

// ── API ──────────────────────────────────────────────────────────────────────

void handleStatus() { sendActionStatus(); }

// Serves the menu tree as the UIs need it: metadata from flash, values from the
// cache. Paged because the full tree is ~26 KB serialized and must never be
// assembled in one buffer (see the file header).
void handleMenu() {
    const uint16_t from  = s_server.hasArg("from")  ? s_server.arg("from").toInt()  : 0;
    const uint16_t count = s_server.hasArg("count") ? s_server.arg("count").toInt() : 32;
    const uint8_t  lang  = g_menu_cache.getLang() < MENU_LANG_COUNT ? g_menu_cache.getLang() : 0;

    String out;
    out.reserve(4096);
    out += "{\"from\":" + String(from) + ",\"total\":" + String(MENU_META_COUNT) + ",\"items\":[";

    bool first = true;
    for (uint16_t id = from; id < MENU_META_COUNT && id < from + count; id++) {
        // Same filter the panel uses, so the two UIs hide the same things —
        // PORT CONFIG and the portal branch. See MenuFilter.h.
        if (!isMenuItemVisible(id)) continue;
        const MenuMeta &m = g_menu_meta[id];
        if (!first) out += ',';
        first = false;
        out += "{\"id\":" + String(m.id);
        out += ",\"t\":"  + String((int)m.type);
        out += ",\"n\":\"" + htmlEscape(m.title[lang] ? m.title[lang] : "") + "\"";
        out += ",\"p\":"  + String(m.parent);
        if (m.unit[lang] && m.unit[lang][0]) out += ",\"u\":\"" + htmlEscape(m.unit[lang]) + "\"";
        out += ",\"g\":" + String(m.scope == META_SCOPE_PER_UNIT ? 1 : 0);
        if (m.type == META_VALUE || m.type == META_TOGGLE) {
            out += ",\"min\":"  + String(m.min_val, 2);
            out += ",\"max\":"  + String(m.max_val, 2);
            out += ",\"step\":" + String(m.step, 2);
            out += ",\"val\":"  + String(g_menu_cache.getFloat(id), 2);
        }
        out += '}';
    }
    out += "]}";
    s_server.send(200, "application/json", out);
}

// Material presets, enumerated from the controller's menu (see MenuPresets.h).
// Exposed as its own endpoint so the dashboard does not have to rebuild the
// submenu tree from /api/menu just to draw a grid.
void handlePresets() {
    MenuPreset presets[kMaxPresets];
    const uint8_t n = collectPresets(presets, kMaxPresets, g_menu_cache.getLang());

    String out;
    out.reserve(1024);
    out += "{\"count\":" + String(n) + ",\"presets\":[";
    for (uint8_t i = 0; i < n; i++) {
        if (i) out += ',';
        out += "{\"id\":"      + String(presets[i].id);
        out += ",\"name\":\"" + htmlEscape(presets[i].name) + "\"";
        out += ",\"tempId\":"  + String(presets[i].tempId);
        out += ",\"timeId\":"  + String(presets[i].timeId);
        out += ",\"temp\":"    + String(presetTemp(presets[i]));
        out += ",\"minutes\":" + String(presetMinutes(presets[i]));
        out += '}';
    }
    out += "]}";
    s_server.send(200, "application/json", out);
}

void handleSetPost() {
    if (!s_server.hasArg("id") || !s_server.hasArg("val")) {
        s_server.send(400, "application/json", "{\"error\":\"id and val required\"}");
        return;
    }
    const long id = s_server.arg("id").toInt();
    if (id < 0 || id >= MENU_META_COUNT) {
        s_server.send(400, "application/json", "{\"error\":\"id out of range\"}");
        return;
    }
    const uint8_t unit = s_server.hasArg("unit") ? s_server.arg("unit").toInt() : 0;
    if (unit >= kMaxUnits) {
        s_server.send(400, "application/json", "{\"error\":\"unit out of range\"}");
        return;
    }

    // Clamp to the controller's own declared bounds so a hand-crafted request
    // cannot push the RP2040 outside what its menu permits.
    const MenuMeta &m = g_menu_meta[id];
    float value = s_server.arg("val").toFloat();
    if (m.type == META_VALUE || m.type == META_TOGGLE) {
        if (value < m.min_val) value = m.min_val;
        if (value > m.max_val) value = m.max_val;
    }

    sendMenuSet((uint16_t)id, unit, value);
    s_server.send(200, "application/json", "{\"ok\":true}");
}

void handleInvokePost() {
    const long id = s_server.hasArg("id") ? s_server.arg("id").toInt() : -1;
    if (id < 0 || id >= MENU_META_COUNT) {
        s_server.send(400, "application/json", "{\"error\":\"id out of range\"}");
        return;
    }
    sendMenuInvoke((uint16_t)id);
    s_server.send(200, "application/json", "{\"ok\":true}");
}

void handleCommandPost() {
    const String what = s_server.arg("do");
    const long   unit = s_server.hasArg("unit") ? s_server.arg("unit").toInt() : 0;

    if (what != "get_config" && (unit < 0 || unit >= kMaxUnits)) {
        s_server.send(400, "application/json", "{\"error\":\"unit out of range\"}");
        return;
    }

    if (what == "stop") {
        sendStop((uint8_t)unit);
    } else if (what == "drying") {
        sendStart(UartDryerMode::Drying, (uint8_t)unit,
                  s_server.arg("temperature").toInt(),
                  (uint32_t)s_server.arg("duration").toInt());
    } else if (what == "storage") {
        sendStart(UartDryerMode::Storage, (uint8_t)unit,
                  s_server.arg("temperature").toInt(),
                  (uint32_t)s_server.arg("humidity").toInt());
    } else if (what == "get_config") {
        requestConfig();
    } else {
        s_server.send(400, "application/json", "{\"error\":\"unknown command\"}");
        return;
    }
    sendActionStatus();
}

void handleSetupPost() {
    String ssid = s_server.arg("ssid");
    const String password = s_server.arg("password");
    ssid.trim();

    if (!ssid.length()) {
        s_server.send(400, "text/plain", "Enter a network name.");
        return;
    }
    if (ssid.length() > kMaxWifiSsidLen || password.length() > kMaxWifiPasswordLen ||
        (password.length() && password.length() < 8)) {
        s_server.send(400, "text/plain", "Invalid Wi-Fi credentials.");
        return;
    }
    // A blank password preserves the stored one only when the SSID is unchanged;
    // a new network still needs its password.
    if (!password.length() && ssid != s_stationSsid) {
        s_server.send(400, "text/plain", "Enter the password for a new Wi-Fi network.");
        return;
    }

    String storedSsid, storedPassword;
    if (!password.length()) loadStoredWifiCredentials(storedSsid, storedPassword);
    if (!saveStoredWifiCredentials(ssid, password.length() ? password : storedPassword)) {
        s_server.send(500, "text/plain", "Could not save Wi-Fi credentials.");
        return;
    }

    s_stationSsid          = ssid;
    s_wifiCredentialSource = "stored";
    scheduleRestart();
    s_server.sendHeader("Location", "/setup", true);
    s_server.send(303, "text/plain", "Saved");
}

void handleWifiPost() {
    if (!s_server.hasArg("ssid")) {
        s_server.send(400, "application/json", "{\"error\":\"missing ssid\"}");
        return;
    }
    String ssid     = s_server.arg("ssid");
    String password = s_server.hasArg("password") ? s_server.arg("password") : "";
    ssid.trim();

    if (ssid.length() == 0 || ssid.length() > kMaxWifiSsidLen) {
        s_server.send(400, "application/json", "{\"error\":\"ssid length must be 1..32\"}");
        return;
    }
    if (password.length() > kMaxWifiPasswordLen ||
        (password.length() > 0 && password.length() < 8)) {
        s_server.send(400, "application/json", "{\"error\":\"password length must be 0 or 8..64\"}");
        return;
    }
    if (!saveStoredWifiCredentials(ssid, password)) {
        s_server.send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }

    s_stationSsid          = ssid;
    s_wifiCredentialSource = "stored";
    scheduleRestart();
    Serial.printf("[TOUCH] saved WiFi SSID=%s, restarting\n", ssid.c_str());
    sendActionStatus();
}

void handleWifiClearPost() {
    if (!clearStoredWifiCredentials()) {
        s_server.send(500, "application/json", "{\"error\":\"clear failed\"}");
        return;
    }
    s_stationSsid          = "";
    s_wifiCredentialSource = "none";
    scheduleRestart();
    Serial.println("[TOUCH] cleared stored WiFi credentials, restarting");
    s_server.sendHeader("Location", "/setup", true);
    s_server.send(303, "text/plain", "Cleared");
}

void handleOtaPost() {
    if (Update.hasError() || !s_otaOk) {
        StaticJsonDocument<192> doc;
        doc["error"]        = s_otaError.length() ? s_otaError : Update.errorString();
        doc["bytesWritten"] = s_otaBytesWritten;
        String out;
        serializeJson(doc, out);
        s_server.send(500, "application/json", out);
        return;
    }

    scheduleRestart();
    StaticJsonDocument<192> doc;
    doc["ok"]             = true;
    doc["bytesWritten"]   = s_otaBytesWritten;
    doc["restartPending"] = true;
    String out;
    serializeJson(doc, out);
    s_server.send(200, "application/json", out);
}

void handleOtaUpload() {
    HTTPUpload &upload = s_server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        s_otaActive       = true;
        s_otaOk           = false;
        s_otaBytesWritten = 0;
        s_otaError        = "";

        // Stop every unit before rewriting flash — the RP2040 keeps heating on
        // its own if the ESP32 reboots mid-cycle without telling it to stop.
        for (uint8_t i = 0; i < s_unitsCount && i < kMaxUnits; i++) sendStop(i);

        Serial.printf("[TOUCH] OTA upload start filename=%s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            s_otaError = Update.errorString();
            Serial.printf("[TOUCH] OTA begin failed: %s\n", s_otaError.c_str());
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!Update.hasError()) {
            const size_t written = Update.write(upload.buf, upload.currentSize);
            s_otaBytesWritten += written;
            if (written != upload.currentSize) {
                s_otaError = Update.errorString();
                Serial.printf("[TOUCH] OTA write failed: %s\n", s_otaError.c_str());
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!Update.hasError() && Update.end(true)) {
            s_otaOk    = true;
            s_otaError = "";
            Serial.printf("[TOUCH] OTA complete bytes=%u\n", (unsigned)s_otaBytesWritten);
        } else {
            s_otaOk    = false;
            s_otaError = Update.errorString();
            Serial.printf("[TOUCH] OTA end failed: %s\n", s_otaError.c_str());
        }
        s_otaActive = false;
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        s_otaActive = false;
        s_otaOk     = false;
        s_otaError  = "upload aborted";
        Serial.println("[TOUCH] OTA upload aborted");
    }
}

void configureRoutes() {
    s_server.on("/",               HTTP_GET,  handleRoot);
    s_server.on("/logo.png",       HTTP_GET,  handleLogo);
    s_server.on("/setup",          HTTP_GET,  handleSetupPage);
    s_server.on("/fw",             HTTP_GET,  handleFirmwarePage);
    s_server.on("/api/status",     HTTP_GET,  handleStatus);
    s_server.on("/api/menu",       HTTP_GET,  handleMenu);
    s_server.on("/api/presets",    HTTP_GET,  handlePresets);
    s_server.on("/api/set",        HTTP_POST, handleSetPost);
    s_server.on("/api/invoke",     HTTP_POST, handleInvokePost);
    s_server.on("/api/command",    HTTP_POST, handleCommandPost);
    s_server.on("/api/setup",      HTTP_POST, handleSetupPost);
    s_server.on("/api/display",    HTTP_POST, handleDisplayPost);
    s_server.on("/api/wifi",       HTTP_POST, handleWifiPost);
    s_server.on("/api/wifi/clear", HTTP_POST, handleWifiClearPost);
    s_server.on("/api/ota",        HTTP_POST, handleOtaPost, handleOtaUpload);

    // Captive-portal probes, so joining the AP opens the dashboard directly.
    s_server.on("/generate_204",             HTTP_GET, handleRoot);
    s_server.on("/gen_204",                  HTTP_GET, handleRoot);
    s_server.on("/hotspot-detect.html",      HTTP_GET, handleRoot);
    s_server.on("/library/test/success.html", HTTP_GET, handleRoot);
    s_server.on("/ncsi.txt",                 HTTP_GET, handleRoot);
    s_server.on("/connecttest.txt",          HTTP_GET, handleRoot);
    s_server.on("/redirect",                 HTTP_GET, handleRoot);
    s_server.onNotFound([]() {
        if (s_apMode) {
            handleRoot();
        } else {
            s_server.send(404, "application/json", "{\"error\":\"not found\"}");
        }
    });
}

} // namespace

// ── Seam for the touch UI (see TouchState.h) ─────────────────────────────────

DeviceView deviceView() {
    DeviceView v;
    v.mcuConnected = s_mcuConnected;
    v.apMode       = s_apMode;
    v.unitsCount   = s_unitsCount;
    v.menuRevision = g_menu_cache.revision;

    const String ip   = localIpString();
    const String ssid = s_apMode ? s_apSsid : WiFi.SSID();
    strncpy(v.ip,        ip.c_str(),   sizeof(v.ip) - 1);
    strncpy(v.ssid,      ssid.c_str(), sizeof(v.ssid) - 1);
    strncpy(v.mcuSerial, s_mcuSerial,  sizeof(v.mcuSerial) - 1);
    strncpy(v.firmware,  VERSION_STR,  sizeof(v.firmware) - 1);

    for (uint8_t i = 0; i < kMaxUnits; i++) {
        v.units[i].airTempC    = s_units[i].airTempC;
        v.units[i].airHumidity = s_units[i].airHumidity;
        v.units[i].heaterPower = s_units[i].heaterPower;
        v.units[i].targetTempC = s_units[i].targetTempC;
        v.units[i].fanOn       = s_units[i].fanOn;
        v.units[i].mode        = s_units[i].mode;
        v.units[i].durationS   = s_units[i].durationS;
        v.units[i].elapsedS    = s_units[i].elapsedS;
    }
    return v;
}

void cmdStartDrying(uint8_t unit, int tempC, uint32_t minutes) {
    if (unit < kMaxUnits) sendStart(UartDryerMode::Drying, unit, tempC, minutes);
}
void cmdStartStorage(uint8_t unit, int tempC, uint32_t humidityPct) {
    if (unit < kMaxUnits) sendStart(UartDryerMode::Storage, unit, tempC, humidityPct);
}
void cmdStop(uint8_t unit) {
    if (unit < kMaxUnits) sendStop(unit);
}
void cmdRequestConfig() { requestConfig(); }

void cmdSetMenuValue(uint16_t id, uint8_t unit, float value) {
    if (id >= MENU_META_COUNT || unit >= kMaxUnits) return;
    // Clamp to the controller's own declared bounds, as /api/set does.
    const MenuMeta &m = g_menu_meta[id];
    if (m.type == META_VALUE || m.type == META_TOGGLE) {
        if (value < m.min_val) value = m.min_val;
        if (value > m.max_val) value = m.max_val;
    }
    sendMenuSet(id, unit, value);
}

void cmdInvokeMenu(uint16_t id) {
    if (id >= MENU_META_COUNT) return;
    sendMenuInvoke(id);
}

// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("[TOUCH] iDryer Touch firmware starting");

    idryer::hal::initArduinoHal(&Serial, false);

    configureWiFi();

    configureRoutes();
    s_server.begin();
    s_statusSocket.begin();
    s_statusSocket.onEvent(onStatusSocketEvent);

    // Some ESP32 pins default to JTAG/strapping roles; reset before UART use.
    gpio_reset_pin((gpio_num_t)kUartRxPin);
    gpio_reset_pin((gpio_num_t)kUartTxPin);
    Serial1.begin(115200, SERIAL_8N1, kUartRxPin, kUartTxPin);
    s_uart.begin(&s_uartSerial, 115200);

    s_uart.setHelloHandler(onHello);
    s_uart.setTelemetryHandler(onTelemetry);
    s_uart.setStatusHandler(onStatus);
    s_uart.setConfigChunkHandler(onConfigChunk);
    s_uart.setLogHandler(onLog);

    // Panel last: a failure here must not cost the web UI or the UART bridge,
    // which are what make the device usable in the first place.
    if (display::begin()) {
        ui::begin();
        Serial.println("[TOUCH] display ready");
    } else {
        Serial.println("[TOUCH] display init FAILED — continuing headless");
    }

    Serial.printf("[TOUCH] Web UI: http://%s/  (UART rx=%d tx=%d)\n",
                  localIpString().c_str(), kUartRxPin, kUartTxPin);
}

void loop() {
    if (s_apMode) s_dnsServer.processNextRequest();
    s_server.handleClient();
    s_statusSocket.loop();
    s_uart.loop();
    updateRestart();

    const uint32_t now = millis();

    // The RP2040 only sets uartLinkReady (and starts forwarding its error log)
    // once it sees a heartbeat. cloudState is reported as local-only.
    static uint32_t s_lastHeartbeat = 0;
    if (now - s_lastHeartbeat >= 5000) {
        s_lastHeartbeat = now;
        UartHeartbeatPayload hb{};
        hb.uptimeSeconds   = now / 1000;
        hb.wifiRssiDbm     = (int16_t)WiFi.RSSI();
        hb.errorsSinceBoot = 0;
        hb.cloudState      = static_cast<UartLinkCloudState>(1);
        s_uart.sendHeartbeat(hb);
    }

    // The RP2040 sends Hello only at its own boot. If the ESP32 restarted later
    // we have to ask, or we never learn the unit count or get the menu.
    static uint32_t s_lastHelloReq = 0;
    static uint8_t  s_helloReqs    = 0;
    if (!s_mcuConnected && s_helloReqs < 12 && now - s_lastHelloReq >= 5000) {
        s_lastHelloReq = now;
        s_helloReqs++;
        UartHelloPayload req{};
        req.role            = UartRole::HelloRequest;
        req.firmwareVersion = VERSION_NUMBER;
        s_uart.sendHello(req, false);
    }

    pushStatusIfChanged();

    ui::tick();
    display::loop();

    delay(2);
}

} // namespace idryer_touch

// Arduino entry points. main_v2.cpp is compiled out by the same guard.
void setup() { idryer_touch::setup(); }
void loop()  { idryer_touch::loop(); }

#endif // IDRYER_TOUCH_LOCAL
