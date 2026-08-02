# iDryer Touch

A cloud-free fork of [iDryer Link](https://github.com/pavluchenkor/iDryer-Link) that
puts a local web UI — and, in progress, a touchscreen — on an iDryer.

No portal account, no MQTT broker, no TLS, no device claiming. The firmware talks
to the dryer's RP2040 controller over UART and serves everything itself on your
LAN. Same idea as [iHeater Remote](https://github.com/justinh-rahb/iHeater-Remote),
applied to the dryer.

> **Status: pre-hardware.** The firmware builds and the web UI is verified in a
> browser, but nothing has been flashed to a board yet. The UART wiring below is
> derived from datasheets and needs bench confirmation. Do not treat this as
> working firmware.

## What it does

- **Local web dashboard** — per-unit air temperature, humidity, heater duty, fan
  state, mode and run progress, pushed over a WebSocket.
- **Drying and storage control** — start, stop, set targets, per unit.
- **Controller menu browser** — the dryer's own menu tree, rendered from what the
  controller declares over UART. Editing a value writes it back the same way the
  jog wheel does.
- **Wi-Fi setup** — joins your network, or falls back to an `iDryer Touch XXXX`
  access point with a captive portal.
- **Local OTA** — upload firmware from the browser. A/B partitions, no cloud.
- **Touch GUI on an ESP32 CYD** — planned, not built. See
  [the plan](docs/developer/TOUCH_PLAN.md).

The stock 1.3" OLED and jog wheel keep working. The web UI and the eventual touch
GUI are *additional* clients of the controller's menu, not replacements — no
iDryerControllerV2 firmware changes are required.

## Hardware

Target board is the classic **ESP32-2432S028R "Cheap Yellow Display"** — 2.8"
320×240 ILI9341, XPT2046 resistive touch, classic ESP32, 4 MB flash, no PSRAM.

Only three GPIOs on that board are unallocated (22, 27, and input-only 35), so the
UART bridge to the controller uses the `CN1` header:

| CN1 pin | Connects to |
| --- | --- |
| GND | RJ45 GND |
| GPIO22 | controller UART RX |
| GPIO27 | controller UART TX |
| 3V3 | — |

Power the board from the RJ45's 5V into `P1` VIN. `P1`'s TX/RX are wired to the
CH340 — use that header for power only.

The RJ45 on the dryer is **power and UART, not Ethernet**. Do not plug it into a
switch or router.

Pins are build flags (`IDRYER_UART_TX_PIN` / `IDRYER_UART_RX_PIN`), so other
boards only need a new environment.

## Build

```bash
scripts/bootstrap.sh
```

```bash
scripts/build.sh cyd-2432s028r
```

`bootstrap.sh` initialises submodules and verifies the dependency layout — upstream
ships two symlinks pointing into the original author's home directory, so a plain
`git clone` of the parent project cannot build. See
[TOUCH_PLAN.md §2](docs/developer/TOUCH_PLAN.md) for what was replaced and why
`idryer-core` is pinned to an exact commit rather than a branch.

Other useful invocations:

```bash
scripts/build.sh --list
```

```bash
scripts/sync-menu.sh
```

`sync-menu.sh` re-mirrors the controller's menu metadata. It is deliberately
opt-in and dry-run by default — the mirror is firmware-coupled, and adopting a
menu your dryer is not running makes the two ends disagree about what a given
menu item means.

## HTTP API

Everything the UI does is available directly.

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/` | Dashboard |
| GET | `/setup` | Wi-Fi form |
| GET | `/fw` | Firmware upload page |
| GET | `/api/status` | Full device + per-unit state as JSON |
| GET | `/api/menu?from=&count=` | Menu tree, paged |
| POST | `/api/command?do=drying\|storage\|stop\|get_config` | Run control |
| POST | `/api/set?id=&unit=&val=` | Write a menu value |
| POST | `/api/invoke?id=` | Trigger a menu action |
| POST | `/api/wifi`, `/api/wifi/clear`, `/api/setup` | Credentials |
| POST | `/api/ota` | Firmware upload |

A WebSocket on **port 81** pushes the same payload as `/api/status` whenever it
changes.

`/api/menu` is paged because the controller's full tree is ~26 KB serialised and
must never be assembled in one buffer — see the note on `menu_buildFullJson` in
[TouchApp.cpp](src/touch/TouchApp.cpp).

## Layout

| Path | What |
| --- | --- |
| `src/touch/` | The local-only firmware (this fork's work) |
| `src/main_v2.cpp` | Upstream's cloud bridge, compiled out by `IDRYER_TOUCH_LOCAL` |
| `lib/idryer-core` | Submodule, pinned — upstream's platform library |
| `lib/idryer-menu/` | Committed mirror of the controller's menu metadata |
| `vendor/iDryerControllerV2` | Submodule — controller firmware, reference + menu source |
| `docs/developer/TOUCH_PLAN.md` | Architecture, findings, and what's left |

The cloud path is guarded rather than deleted, so merges from `upstream` stay
clean. Upstream's ESP32-C3 environments still build unchanged.

## Credits

- [pavluchenkor/iDryer-Link](https://github.com/pavluchenkor/iDryer-Link),
  [iDryerControllerV2](https://github.com/pavluchenkor/iDryerControllerV2) and
  [idryer-core](https://github.com/pavluchenkor/idryer-core) — the upstream
  project this forks.
- The web layer is adapted from
  [iHeater Remote](https://github.com/justinh-rahb/iHeater-Remote).

Upstream's own documentation is kept under [docs/](docs/) and still describes the
cloud product.
