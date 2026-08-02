# TODO

Open items for iDryer Touch. Roughly ordered — nothing below "Hardware bring-up"
is worth much until that section is done.

## Hardware bring-up — blocking

Nothing in this repo has been flashed to a board. Everything below is written,
compiles, and is unverified.

- [ ] **UART link.** Confirm Hello / telemetry / status / menu against the real
      RP2040 on CN1 (TX=GPIO22, RX=GPIO27). The pins are proven on this board by
      another project, but not with *this* firmware or the iDryer's controller.
- [ ] **Panel init.** SPI is set to 40 MHz in `TouchDisplay.cpp`; 24 MHz is the
      value proven on this hardware. Drop back if there is speckle or tearing.
- [ ] **Touch orientation.** `offset_rotation` is 0 against display rotation 1.
      If the axes come out swapped or inverted, that constant is the knob.
- [ ] **Touch calibration flow.** Runs automatically when NVS holds none. Verify
      the corner targets are reachable and the stored result survives a reboot.
- [ ] **WiFi + touch together.** Bringing WiFi up has been seen to leave the
      XPT2046 unresponsive on plain-ESP32 CYD hardware — under a prebuilt
      MicroPython LVGL image, never retested on Arduino. This firmware needs both
      at once, so confirm early.
- [ ] Check free heap after `display::begin()` — the LVGL draw buffer is 25.6 KB
      of DMA-capable internal RAM on top of 70.6 KB static, and WiFi wants its
      own.

## Controller features not surfaced

The cloud build exposed these and the local build does not. All are UART plumbing
that already exists in `idryer-core`.

- [ ] `find` command (locate a unit).
- [ ] `clear_errors` command.
- [ ] `profile` mode — multi-stage temperature ramps. `UartDryerMode::Profile` is
      decoded for display but cannot be started from either UI.
- [ ] **Weights.** `setWeightsHandler` is never registered, so scale readings are
      dropped. The dryer reports them and the hardware has load cells.
- [ ] **RFID.** `setRfidHandler` likewise — spool tag detection is not surfaced.
- [ ] **Controller error log.** `onLog` prints to Serial only. These are the
      events a user most wants to see (sensor faults, over-temperature); they
      should reach the web UI, and probably the touch panel's fault view.

## Security

- [ ] **The web UI has no authentication at all.** Anyone on the LAN can start
      heating, change menu values, or flash firmware via `/api/ota`. Defensible
      on a trusted home network, not obviously defensible as a default. Decide
      between a shared password, a first-run pairing token, or documenting the
      exposure loudly.
- [ ] OTA accepts any `.bin` with no signature or version check. A wrong image
      means serial recovery.

## Discovery and integration

- [ ] **mDNS was dropped along with `LocalAccess`.** Core advertised
      `_idryer._tcp` using the serial as hostname; nothing does now, so the
      device is IP-only and the stock mobile app cannot discover it.
- [ ] Decide whether to keep the app-compatible WebSocket envelope
      (`{"type":"command",...}`) so existing clients keep working.
- [ ] Home Assistant discovery — MQTT-based, but against a *local* broker, so it
      is compatible with "no cloud". The dryer has far more worth exposing than
      iHeater did.

## UI

- [ ] Touch header uses a text wordmark; the web UI uses the real logo. Matching
      it needs a PNG decoder or a converted RGB565 array.
- [ ] Backlight button cycles 100/50/20% with no indication of the current level.
- [ ] Web UI has no unit-count control and no profile editor.
- [ ] Touch recalibration is unreachable — `display::recalibrateTouch()` exists
      but nothing calls it.

## Build and CI

- [x] ~~CI has never run.~~ All five matrix jobs pass on PR #1; artifacts upload.
- [ ] `release.yml` is entirely untested — merged factory image, per-chip
      bootloader offsets and release notes are all unexercised. It only fires on
      a `v*` tag, so it stays unproven until the first release.
- [ ] CI actions emit a Node 20 deprecation warning (`checkout@v4`, `cache@v4`,
      `setup-python@v5`, `upload-artifact@v4`). Harmless now; bump when v5/v6
      land.
- [ ] Artifacts are ~8-10 MB per target because the whole build directory's
      `.elf` goes up. Fine, but trim if it ever matters.
- [ ] `min_spiffs.csv` leaves ~124 KB of filesystem that nothing uses. Either use
      it (touch calibration backup, controller error ring buffer) or reclaim it.

## Repo hygiene

- [ ] `README.ru.md` and most of `docs/` still describe upstream's cloud product
      — portal accounts, the mobile app, the web flasher. Prune or port.
- [ ] `extra_scripts/post_build.py` copies firmware to a flasher-portal path that
      is irrelevant here. The CYD env skips it, but it still runs for the
      inherited ESP32-C3 environments.
- [ ] Decide whether to keep upstream's C3/S3 environments at all, or drop them
      once this fork stops tracking `upstream`.

## Inherited from upstream

- [ ] Разобраться с версией совместимости Link <-> RP2040.
      Сейчас `VERSION_MAJOR` используется как признак совместимости UART/прошивок
      и одновременно связан с обновлением меню. Нужно решить, где должна жить
      версия контракта: в firmware major, в UART/protocol contract yaml или
      отдельно в idryer-core.

      *Still open, and it bites this fork directly: `scripts/sync-menu.sh` exists
      precisely because the menu mirror and `VERSION_MAJOR` move together.*
