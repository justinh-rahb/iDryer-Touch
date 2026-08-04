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

### Wiring

USB-TTL adapter to the CYD's `CN1` header, crossed:

| Adapter | CN1 |
| --- | --- |
| GND | GND |
| RX | GPIO22 (the CYD's TX) |
| TX | GPIO27 (the CYD's RX) |
| VCC | **leave disconnected** |

The CYD is powered over its own USB, so do not also feed it from the adapter.
**Set the adapter to 3.3 V logic** — a 5 V TX into GPIO27 exceeds the ESP32's
3.6 V absolute maximum.

### Protocol version

Updated to UART protocol v2 to match `iDryerControllerV2` v2.0.0 and the pinned
`idryer-core`. Three things were wrong for v2, all of which fail the same way —
`UartBridge::validateLength` compares payload length by exact equality, so a
mismatched frame is dropped in silence rather than erroring:

- `PROTOCOL_VERSION` was 1, and the emulator also rejected inbound v2 frames.
- Hello was 86 bytes; v2 grew `hardwareVersion` from 8 to 16 (94 bytes).
- Status lacked the trailing `ignoreExternalCmd` byte (134 bytes).

Two were broken before that, independent of the version: `make_status()` packed
13 fields into a 14-field struct — missing `durationMinutes`, and treating the
signed `targetTempC10` as unsigned — so it raised on every call; and
`make_telemetry()` did not pad to four units, emitting 8 bytes where the fixed
wire struct is 29.

Frame sizes are asserted in the builders now, so a future contract change fails
loudly instead of going quiet.

---

Upstream also shipped `mock_portal.py`, `fake_bambu/` and `fake_moonraker/`.
All three exercised cloud and printer-integration paths that this fork does not
build, so they were removed.
