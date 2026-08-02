# iDryer Touch — architecture and plan

Fork of [pavluchenkor/iDryer-Link](https://github.com/pavluchenkor/iDryer-Link).
Goal: remove cloud dependence, add a local web UI, and add a touch GUI on an
ESP32 CYD display — following the pattern already proven in
[iHeater-Remote](https://github.com/justinh-rahb/iHeater-Remote).

## 1. What the ecosystem actually looks like

```
                 UART 115200 (RJ45, NOT ethernet)
  RP2040 controller  <───────────────────────>  ESP32 Link
  iDryerControllerV2                            iDryer-Link (this repo)
  ├─ owns heating/PID/sensors                   ├─ WiFi / Improv
  ├─ owns EEPROM config                         ├─ MQTT → portal.idryer.org
  ├─ owns the 1.3" OLED (U8g2)                  ├─ claiming / device token
  └─ owns the jog wheel (ISRencoder)            └─ Home Assistant discovery
```

Three findings that shape everything below.

**The ESP32 is a thin bridge.** `src/main_v2.cpp` is ~600 lines. Everything
substantial — WiFi, MQTT, TLS, claiming, LocalAccess, HA discovery — lives in
`idryer-core`, consumed as a library. Product code just wires UART handlers to
core's publish/command API.

**The menu is a protocol, and the ESP32 is already a client of it.** The RP2040
owns the menu tree, but exposes it over UART: `get_config` returns the whole
tree as chunked JSON, and `set` / `invoke` write back. `lib/idryer-menu/`
carries a generated mirror of the tree:

- `menu_meta.h` — 202 items: id, RU/EN title, unit label, type
  (submenu/action/value/toggle), value type, `parent` / `first_child` /
  `child_count`, `min`/`max`/`step`, scope (global vs per-unit), semantic role.
- `menu_cache.h` — live values, `[menu_id][unit]`, with float/int/bool accessors.

That is a complete, self-describing UI model. Core's own design doc
(`lib/idryer-core/contracts/___capabilities_and_menu_as_protocol.md`) calls this
"menu as a declarative protocol" — the portal renders device cards from it
without hardcoding device types.

**Therefore the touch GUI does not replace the controller's display.** It is a
*second, parallel* menu client that renders from the same `menu_meta` +
`menu_cache` data and writes back with the same `set`/`invoke` commands. The
stock OLED and jog wheel keep working; the touch screen is additive. **No
iDryerControllerV2 firmware changes are required.** The same model feeds the web
UI, so the touch GUI and the web UI are two renderers over one state tree.

> Caveat: the dryer has *not* migrated to `menu_protocol_v1` yet. Core's doc puts
> Dryer/iDryerRP2040 in "Этап 2 … не трогаем" — it still uses the older chunked
> config flow that `main_v2.cpp` implements. Build against the flow in
> `onConfigChunk` / `publishConfig`, not against the newer contract.

## 2. Repo setup (done)

Upstream ships two symlinks into the original author's home directory, so a
fresh clone cannot build:

| Path | Upstream target | Now |
| --- | --- | --- |
| `lib/idryer-core` | `/Users/ruslanpavlucenko/…/docs/idryer-core` | submodule → `pavluchenkor/idryer-core` |
| `config-exmple/menu` | `/Users/ruslanpavlucenko/…/iDryerControllerV2/src/menu` | removed; `vendor/iDryerControllerV2` submodule added instead |

`.gitmodules` was empty because the old `idryer-protocol` submodule was deleted
in favour of the `idryer-core` symlink — there was nothing to recurse into.

**The menu mirror is committed, not regenerated per build.** Upstream's
`copy_menu.py` refreshes `lib/idryer-menu/src/` from the `config-exmple/menu`
symlink on every build. Restoring that symlink against the public controller repo
silently changed `VERSION_MAJOR` 1 → 2 — and per [TODO.md](../TODO.md) that is
the Link↔RP2040 UART compatibility marker — plus it renamed menu ids
(`MENU_CMD_IGNORE_EXTERNAL` → `MENU_IGNORE_EXTERNAL_CMD`). The public
`iDryerControllerV2` repo is a **single squashed commit at v2.0.0** with no v1.x
history, so the committed mirror (controller v1.0.2) is the only copy of the v1
menu in existence.

The symlink is therefore deliberately absent: `copy_menu.py` takes its documented
`[MENU] Using cached files` fallback, builds are deterministic, and adopting a new
controller release is an explicit commit via `scripts/sync-menu.sh`. Before ever
running that with `--apply`, confirm what firmware your dryer's RP2040 actually
runs — a mismatched mirror means Link and the hardware disagree about what menu
item 189 means.

**`idryer-core` is pinned to `d3df6af` (UART protocol v2).** The pin must match
the UART contract of the controller firmware the dryer runs, because
`UartBridge::validateLength` compares payload length by *exact equality*: a
version skew does not degrade, it silently drops whole frame types.

The first pin here was `6cbc8ab` (protocol v1), chosen because `main_v2.cpp` —
upstream's cloud bridge — referenced `Config::hasHeaterPower` and
`Telemetry::weightG`, which core later renamed and removed. That was the wrong
consumer to pin for. Against a v2.0.0 controller it would have failed like this:

| Frame | v1 | v2 | Result on a v2 controller |
| --- | ---: | ---: | --- |
| Hello | 86 | 94 | rejected — MCU never detected |
| Status | 133 | 134 | rejected — mode/target/progress never update |
| Telemetry | 29 | 29 | accepted |

So temperature and humidity would have updated live while the UI insisted the
controller was missing — the worst kind of half-working. `main_v2.cpp` was
instead updated for the renamed fields (`hasHeater`, `hasFan`, `hasWeight`, and
weights published through `publishWeights` rather than staged in telemetry).

Setup and build:

```bash
scripts/bootstrap.sh
```

```bash
scripts/build.sh esp32c3-super-mini-prod
```

CI (`.github/workflows/ci.yml`) builds the four upstream targets on push/PR;
`release.yml` publishes factory + OTA images on `v*` tags. Both check out
submodules recursively and run `scripts/bootstrap.sh --check` before compiling.

## 3. De-clouding: copy iHeater-Remote's shape

iHeater-Remote does **not** rip the cloud out. It adds a parallel entry point and
selects it with a build flag — which keeps upstream merges clean. Copy this:

- `src/main.cpp` line 1 is `#if defined(IHEATER_REMOTE_LOCAL)` — the whole
  original main is skipped and `standalone::setup()/loop()` runs instead.
- A dedicated PIO env sets `build_src_filter` to include `src/standalone/` and
  exclude what it replaces, defines `-DIHEATER_REMOTE_LOCAL=1`, and stubs the
  cloud macros (`-DIDRYER_API_BASE=\"\"`, `-DMQTT_BROKER=\"\"`) because core is
  compiled as a whole archive even though the cloud runtime is never constructed.
- The local app is one 1205-line `StandaloneApp.cpp` plus a PROGMEM HTML blob:
  `WebServer` on :80, `WebSocketsServer` on :81 pushing status JSON, POST
  endpoints (`/api/mode`, `/api/timer`, `/api/wifi`, `/api/ota`), `Preferences`
  for config, AP fallback with captive-portal redirect, and local OTA upload.

For iDryer-Touch that becomes `src/touch/`, `-DIDRYER_TOUCH_LOCAL=1`, and a guard
at the top of `main_v2.cpp`.

**Gotcha — do not reuse core's `LocalAccess` as the local API.** It looks like
the obvious win (WS server on :81, mDNS `_idryer._tcp`, same command sink as
MQTT), but `iDryer.cpp:472` starts it with `DeviceIdentity.token`, issued by
portal claiming. On an unclaimed device the token is empty and every client is
rejected. Either bypass its auth or, as iHeater-Remote does, run your own
WebSocketsServer. Its *envelope* format
(`{"type":"command","command":…,"data":…}`) is still worth keeping so existing
app clients work.

Cloud touchpoints to leave unconstructed: `cloud/cloud_state_machine`,
`cloud/http_api`, `mqtt/mqtt_client` (+ `root_ca.h` TLS), `claiming`,
`ota_receiver` (portal-driven OTA — replace with web upload).

## 4. The CYD target — ESP32-2432S028R

Classic 2.8" resistive CYD: ILI9341 320×240 over HSPI, XPT2046 resistive touch on
a separate SPI bus, classic dual-core ESP32, 4 MB flash, **no PSRAM**.

### GPIO — the binding physical constraint

**Confirmed on working hardware**, not just datasheets:
[justinh-rahb/klipper-micro](https://github.com/justinh-rahb/klipper-micro) is a
native ESP-IDF app running on this exact board, and it drives an unmodified
Klipper MCU over **UART2 on TX=GPIO22, RX=GPIO27 at 250000 baud**
(`src/klipper_client.c:20-23`) — the same assignment used here, at 2× the baud
rate we need. The CN1 wiring below is proven, not inferred.

The board allocates nearly every other pin. Cross-checked against two
independent pinouts (Random Nerd Tutorials, Mischianti) and klipper-micro's
`src/board.c`:

| Function | GPIO |
| --- | --- |
| TFT (ILI9341) | MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, backlight 21 |
| Touch (XPT2046) | IRQ 36, MOSI 32, MISO 39, CLK 25, CS 33 |
| microSD | MISO 19, MOSI 23, SCK 18, CS 5 |
| RGB LED | 4 (R), 16 (G), 17 (B) — active low |
| LDR / speaker / boot | 34 / 26 / 0 |

**Exactly three GPIOs are free: 22, 27, and 35 — and 35 is input-only.** Header
breakout:

- `CN1` — GND, **GPIO22**, **GPIO27**, 3V3
- `P3` — GND, **GPIO35** (input only), GPIO22, GPIO21 (backlight — do not drive)
- `P1` — VIN, TX (GPIO1), RX (GPIO3), GND — tied to the CH340, use for power only
- `P4` — GPIO26, GND (speaker)

So the UART bridge to the RP2040 lands on **CN1: TX = GPIO22, RX = GPIO27** —
one connector, which also carries GND and 3V3. That leaves GPIO35 spare (usable
as a sense input only). Power the board from the RJ45's 5V into `P1` VIN.

Pins are set by `-DIDRYER_UART_TX_PIN` / `-DIDRYER_UART_RX_PIN`; `main_v2.cpp`
still defaults to the C3's GPIO6/7.

### Memory — measured, not estimated

The first thing that actually broke was **not flash — it was static DRAM**.
Building upstream's cloud firmware for a classic ESP32 fails to link:

```
region `dram0_0_seg' overflowed by 424 bytes
```

Cause, found with `nm --size-sort` over the objects: `menu_buildFullJson()` in
`lib/idryer-menu/src/menu_commands.cpp:249` holds a **38,856-byte function-local
`static StaticJsonDocument`** in `.bss` to assemble the portal's menu payload.
The C3's memory layout absorbs it; the classic ESP32's `dram0_0_seg` does not.
Upstream's own comment in `main_v2.cpp` flags the related output buffer as
"~38 КБ … нельзя" and heap-allocates it — but the JSON document itself stayed
static.

De-clouding removes it for free: nothing in the local build calls
`menu_buildFullJson` (the UIs render from `g_menu_cache` directly), so
`--gc-sections` drops it. Verified absent from the linked ELF, along with
mbedtls.

Measured, `pio run -e cyd-2432s028r`:

| | Cloud build (C3) | **Touch build (CYD)** |
| --- | ---: | ---: |
| RAM (static) | 117,692 | **66,440** |
| Flash | 1,107,902 / 1,310,720 (84.5%) | **850,405 / 1,966,080 (43.3%)** |

**Yes — A/B OTA survives.** `min_spiffs.csv` gives two 1,966,080-byte app slots
plus ~124 KB filesystem. At 850 KB there is ~1.1 MB of headroom per slot for
LVGL, TFT_eSPI and the web UI (call it 300–400 KB together), so the touch build
should land near 60% with OTA intact. `huge_app.csv` is not needed and would kill
OTA anyway.

Remaining large statics, if headroom ever gets tight: `g_menu_meta` (11,312 B),
`s_configDoc` (8,232 B), `s_configRx` (8,202 B), `g_menu_cache` (2,432 B).

### Display config worth reusing

Board constants from a working ILI9341 + XPT2046 + LVGL bring-up on this
hardware, so display bring-up doesn't rediscover them:

- LCD on SPI2, touch on SPI3. Both SPI hosts get consumed; UART is unaffected.
- **Don't rely on PENIRQ** (GPIO36) — it varies across CYD revisions. Poll
  pressure over SPI instead.
- Rotation: `swap_xy` only. Adding `mirror_x` after the swap flips the
  landscape image top-to-bottom.
- Panel is BGR 16bpp, and LVGL must render RGB565 **byte-swapped** — SPI takes
  MSB first while LVGL's RGB565 is little-endian on ESP32.
- Touch: raw range roughly 250..3850; after the driver's own scaling, map
  x 15..226 and y 20..301 onto 240/320.
- Partial render, 20 lines → 320×20×2 = 12,800 bytes of DMA-capable internal
  RAM. Pixel clock 24 MHz.

Software stack is LVGL 8 + LovyanGFX (configured in code, so TFT_eSPI's
`User_Setup.h` global macros never fight PlatformIO's dependency finder).

**Touch calibration runs on first boot** — a resistive panel is unusable without
it. The four corner targets appear if NVS holds no calibration, and the result is
stored; Info → BACKLIGHT is a manual redo hook.

Static RAM is 70,644 bytes, but the runtime figure is higher: the LVGL draw
buffer is 320×40×2 = 25,600 bytes of DMA-capable internal RAM allocated at
`display::begin()`, plus LVGL's own heap for objects and styles. A full
framebuffer would be 150 KB and there is no PSRAM, hence partial rendering.

The touch UI deliberately does **not** render the menu tree — see the sizing
notes in `src/touch/TouchUi.cpp`.

A WiFi/touch coexistence problem had been reported on plain-ESP32 CYD hardware
under a prebuilt MicroPython LVGL image. **It does not reproduce here.** With
this firmware the SoftAP and captive portal run while touch calibration and UI
navigation both work, so the radio and the panel coexist fine under Arduino and
there is no reason to move to ESP32-S3.

Verified on hardware: rotation **3** (1 is upside down on this board), SPI at
**40 MHz** with no artifacts, and touch calibration persisting in NVS across a
reflash.

## 5. Sequence

- [x] Confirm the CYD variant and map free GPIO for the UART bridge.
- [x] **`src/touch/` + `cyd-2432s028r` env** — guard `main_v2.cpp`, stub cloud
      macros, keep the UART bridge and menu cache. Links clean on classic ESP32,
      serves `/api/status`, `/api/menu`, `/api/set`, `/api/command`.
- [x] Partition switch to `min_spiffs.csv`; CI matrix widened to the CYD target.
- [x] **Web UI** — ported from iHeater-Remote's `StandaloneApp.cpp` +
      `RemoteDashboard.h`: PROGMEM dashboard, status WebSocket on :81, AP
      fallback with captive portal, `/setup` Wi-Fi form, `/fw` OTA upload.
      Restructured for the dryer (per-unit telemetry cards, drying/storage
      controls, generic menu-tree browser). Cost: ~44 KB flash, ~0.8 KB RAM.
- [x] **Display + touch GUI written** — LovyanGFX board class, LVGL 8 bindings,
      and five screens (home / dry / store / info / no-link). Compiles at 63.9%
      flash. Untested on hardware.
- [ ] **Bench bring-up** — flash it, confirm Hello/telemetry/menu over UART
      against the real RP2040 on GPIO22/27, then that the panel initialises,
      touch calibrates, and touch still responds with WiFi up.
- [ ] **Touch/web parity** — the touch GUI should drive the same endpoints so
      there is one control path, not two.

## 6. Open questions

- Keep Home Assistant discovery? It is MQTT-based but points at a *local* broker,
  so it is compatible with "no cloud" — iHeater-Remote dropped it, but the dryer
  has far more telemetry worth exposing.
- Keep mDNS + the existing WS envelope so the stock iDryer mobile app can still
  talk to the device on LAN, or go fully independent?
- The stock 1.3" OLED and jog wheel keep working regardless. Worth adding a menu
  toggle to disable the controller's own UI once the touch GUI is trusted?

## 7. Reference checkouts

Not submodules — cloned alongside this repo for reading:

| Repo | Path | Why |
| --- | --- | --- |
| `pavluchenkor/iDryer-Unit` | `../iDryer-Unit` | Hardware, CAD, assembly (209 MB) |
| `pavluchenkor/iDryerController` | `../iDryerController` | v1 controller firmware |
| `justinh-rahb/iHeater-Remote` | `../iHeater-Link` | The de-cloud pattern being copied |
| `justinh-rahb/klipper-micro` | `../klipper-micro` | Unrelated project, but a working CYD bring-up — source of the pin, rotation and touch-calibration constants above |

`iDryerControllerV2` and `idryer-core` *are* submodules — see §2.
