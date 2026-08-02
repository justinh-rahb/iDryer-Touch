# Tools

## `emulate_controller.py`

Pretends to be the iDryer's RP2040 controller over UART, so the firmware can be
exercised without a dryer attached. Given nothing in this repo has been flashed
yet, this is the cheapest way to test the UART bridge, the menu flow and both
UIs against something that talks back.

Only needs `pyserial`.

```bash
python3 tools/emulate_controller.py --port /dev/cu.usbserial-XXXX --units 2
```

It sends Hello two seconds after start, then telemetry every 5 s, status every
10 s and heartbeats, and it responds to HelloRequest, Command and ConfigPush
coming back from the ESP32. `--rfid` injects a tag event, `--session` sets the
run length, `--fw-major` picks the reported firmware major.

Wire the emulator's serial adapter to the CYD's `CN1` (TX=GPIO22, RX=GPIO27,
crossed), not to the CYD's USB port — that is the CH340 console.

> Written against the same UART protocol this firmware uses, but it came from
> upstream and has not been re-verified against this build. If a field looks
> wrong, trust `lib/idryer-core/src/uart/` over the emulator.

---

Upstream also shipped `mock_portal.py`, `fake_bambu/` and `fake_moonraker/`.
All three exercised cloud and printer-integration paths that this fork does not
build, so they were removed.
