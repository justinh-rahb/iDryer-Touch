# TODO

Open items for iDryer Touch. Roughly ordered — nothing below "Hardware bring-up"
is worth much until that section is done.

## Hardware bring-up — blocking

Flashed and running on an ESP32-D0WD-V3 CYD (MAC b0:cb:d8:da:f5:90). The panel
half is verified; the controller half is not, because no dryer is attached yet.

- [x] ~~Panel init.~~ Boots and draws at **40 MHz** SPI — no need to fall back to
      the 24 MHz that was known-good elsewhere.
- [x] ~~Touch orientation.~~ `setRotation(1)` came up upside down; `3` is correct
      for this board. Calibration now stores the rotation it was taken in and
      forces a redo when it changes.
- [x] ~~Touch calibration flow.~~ Corner targets reachable, result persists in
      NVS across a reflash, and correctly does *not* re-run on later boots.
- [x] ~~**WiFi + touch together.**~~ **Does not reproduce on Arduino.** The AP is
      up (captive portal serving) while touch calibration and UI navigation both
      work. The MicroPython-era finding was specific to that image; plain-ESP32
      CYD hardware is fine here, and no move to ESP32-S3 is needed.
- [x] ~~Idle blanking.~~ Screen blanks on timeout and the waking tap is swallowed
      — verified on hardware that it cannot press the button underneath.
- [ ] **UART link.** Still unverified — no dryer attached. Confirm Hello /
      telemetry / status / menu against the real RP2040 on CN1 (TX=GPIO22,
      RX=GPIO27). Hello is going out at 94 bytes, which is the protocol-v2 size,
      so the core repin is live.
- [ ] Check free heap after `display::begin()` — the LVGL draw buffer is 25.6 KB
      of DMA-capable internal RAM on top of 70.7 KB static, and WiFi wants its
      own.
- [ ] Web UI has not been exercised on-device yet; the AP comes up but nothing
      has connected to it.

## Controller features not surfaced

The cloud build exposed these and the local build does not. All are UART plumbing
that already exists in `idryer-core`.

- [ ] `find` command (locate a unit).
- [ ] `clear_errors` command.
- [ ] `profile` mode — multi-stage temperature ramps. `UartDryerMode::Profile` is
      decoded for display but cannot be started from either UI.
- [ ] **Weights.** `setWeightsHandler` is never registered in the touch build, so
      scale readings are dropped. The dryer reports them and the hardware has
      load cells.
- [ ] **`ignoreExternalCmd` is reported but ignored.** Protocol v2 adds it to the
      Status payload. If the user has turned on the controller's "IGNOR EXT CMD"
      menu toggle, it rejects Start/Stop/Find/ClearErrors and only lets
      GetConfig through — both UIs would appear to work while every button did
      nothing. Read the flag and say so on screen.
- [ ] Protocol v2 adds `UartDryerMode::Heating` (5) and `LightAnimation` (6).
      `TouchUi.cpp` clamps unknown modes to "Idle", so those two would display
      wrongly. Add them to `kModes[]`.
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
- [ ] Screen timeout is web-only. Worth surfacing on the Info screen too, since
      that is where someone standing at the dryer would look for it.
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

- [x] ~~Upstream's cloud-era docs still shipped.~~ Removed: portal onboarding,
      staging access, flasher-portal build scripts, the private/public repo
      workflow, and index pages pointing at the deleted `idryer-protocol`
      submodule. The RJ45 diagrams are kept — the controller side is unchanged.
- [ ] **The two upstream RJ45 diagrams disagree.** The controller schematic puts
      UART on pins 4/6 with ground on 7; `wiring.png` maps colours to pins 3/5
      with ground on 8. Resolve with a meter before first power-up and correct
      whichever diagram is wrong.
- [ ] `extra_scripts/post_build.py` copies firmware to a flasher-portal path that
      is irrelevant here. The CYD env skips it, but it still runs for the
      inherited ESP32-C3 environments.
- [ ] Decide whether to keep upstream's C3/S3 environments at all, or drop them
      once this fork stops tracking `upstream`.
- [ ] **Clones are ~230 MB.** Upstream's initial commit carried `.pio-home/` — a
      whole PlatformIO toolchain, ~450 MB of blobs — and removed it in
      `49b1421`, so it is permanent in history. Purging it needs a rewrite of
      *upstream's* commits, which destroys the shared ancestry that makes
      `git merge upstream/main` cheap. Deliberately not done; `--depth 1` is the
      workaround. Revisit only if this fork stops tracking upstream.

## Inherited from upstream

- [ ] Разобраться с версией совместимости Link <-> RP2040.
      Сейчас `VERSION_MAJOR` используется как признак совместимости UART/прошивок
      и одновременно связан с обновлением меню. Нужно решить, где должна жить
      версия контракта: в firmware major, в UART/protocol contract yaml или
      отдельно в idryer-core.

      *Still open, and it bites this fork directly: `scripts/sync-menu.sh` exists
      precisely because the menu mirror and `VERSION_MAJOR` move together.*
