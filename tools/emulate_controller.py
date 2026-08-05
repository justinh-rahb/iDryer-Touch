#!/usr/bin/env python3
"""
Продвинутый эмулятор RP2040 для UART-протокола iDryer

НАЗНАЧЕНИЕ:
Эмулирует MCU (RP2040) для тестирования LINK (ESP32) без физического
контроллера. Автоматический режим с периодической отправкой данных.

ЗАПУСК:
python3 emulate_controller.py --port /dev/ttyUSB0 --units 2 --fw-major 2

ПАРАМЕТРЫ:
--port     UART порт (по умолчанию /dev/cu.usbserial-130)
--baud     Скорость (по умолчанию 115200)
--units    Количество юнитов 1-4 (по умолчанию 2)
--fw-major MAJOR версия firmware (по умолчанию 2)
--session  Длительность сессии секунд (по умолчанию 120)
--rfid     Отправить RFID событие через 15с

АВТОМАТИЧЕСКИЙ РЕЖИМ:
- Hello через 2с после запуска
- Telemetry каждые 5с, Status каждые 10с, Heartbeat каждые 5с
- Реагирует на HelloRequest, Command, ConfigPush от ESP32
- Поддержка версионирования протокола

ВОЗМОЖНОСТИ:
- ✅ Актуальные структуры данных (HelloPayload 86 байт, StatusEntry 32 байта)
- ✅ Двусторонняя коммуникация с ESP32
- ✅ Эмуляция units topology и RFID событий
- ✅ Поддержка claiming flow

Протокол: docs/02-uart/01-uart.md
Зависимости: pip install pyserial
Обновлено: 2026-03-25
Статус: ✅ Актуален (проверен на соответствие протоколу)
"""

import argparse
import json
import re
import pathlib
import struct
import sys
import time

import serial

# ---------------------------------------------------------------------------
# Константы протокола
# ---------------------------------------------------------------------------
SOF = 0xAA
PROTOCOL_VERSION = 2   # iDryerControllerV2 v2.0.0 / idryer-core d3df6af

# FLAGS
FLAG_ACK_REQUIRED = 0x01
FLAG_IS_ACK       = 0x02
FLAG_ERROR        = 0x04
FLAG_FRAGMENTED   = 0x08
FLAG_LAST_FRAGMENT = 0x10

UART_MAX_PAYLOAD         = 200   # contracts/_generated/uart_protocol.h
CONFIG_CHUNK_HEADER_SIZE = 6     # transferId(2) totalSize(2) chunkIndex(1) pad(1)

# MessageKind
KIND_HELLO          = 0x01
KIND_HELLO_ACK      = 0x02
KIND_TELEMETRY      = 0x10
KIND_TELEMETRY_ACK  = 0x11
KIND_WEIGHTS        = 0x12
KIND_STATUS         = 0x13
KIND_RFID           = 0x14
KIND_COMMAND        = 0x20
KIND_COMMAND_ACK    = 0x21
KIND_CONFIG_PUSH    = 0x30
KIND_CONFIG_ACK     = 0x31
KIND_HEARTBEAT      = 0x40
KIND_ERROR          = 0x50
KIND_LOG            = 0x60
KIND_CLAIM_START    = 0x70

# Role
ROLE_MCU           = 0x01
ROLE_ESP           = 0x02
ROLE_HELLO_REQUEST = 0xFF

# ErrorCode
ERR_NONE           = 0x00

# RfidEvent
RFID_TAG_DETECTED  = 1
RFID_TAG_REMOVED   = 2

# DryerMode
MODE_IDLE    = 0
MODE_DRYING  = 1
MODE_STORAGE = 2
MODE_PROFILE = 3
MODE_FAULT   = 4
MODE_HEATING = 5   # v2
MODE_LIGHT   = 6   # v2

# UartCmdCode (contracts/_generated/uart_protocol.h)
CMD_START        = 0x01
CMD_STOP         = 0x02
CMD_FIND         = 0x03
CMD_GET_CONFIG   = 0x05
CMD_RESET_FAULT  = 0x10
CMD_CLEAR_ERRORS = 0x12

# UnitCapabilities
CAP_HEATER           = 0x0001
CAP_FAN              = 0x0002
CAP_SERVO            = 0x0004
CAP_RH_AIR_SENSOR    = 0x0008
CAP_TEMP_AIR_SENSOR  = 0x0010
CAP_TEMP_HTR_SENSOR  = 0x0020
CAP_ALL              = 0x003F


# ---------------------------------------------------------------------------
# CRC16-CCITT
# ---------------------------------------------------------------------------
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) & 0xFFFF) ^ 0x1021
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# Кадр
# ---------------------------------------------------------------------------
def build_frame(kind: int, payload: bytes, seq: int, flags: int = 0) -> bytes:
    header = bytes([SOF, PROTOCOL_VERSION, flags, kind, seq & 0xFF, len(payload)])
    body = header + payload
    crc = crc16(body)
    return body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def send_frame(ser: serial.Serial, kind: int, payload: bytes, seq: int,
               flags: int = 0) -> int:
    frame = build_frame(kind, payload, seq, flags)
    ser.write(frame)
    return (seq + 1) & 0xFF


# ---------------------------------------------------------------------------
# Парсер входящих кадров
# ---------------------------------------------------------------------------
class FrameParser:
    """Побайтовый парсер UART кадров."""

    HEADER_SIZE = 6  # SOF VER FLAGS KIND SEQ LEN

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data: bytes):
        """Добавить байты, вернуть список (kind, flags, seq, payload) для каждого полного кадра."""
        self._buf.extend(data)
        frames = []
        while True:
            # Ищем SOF
            idx = self._buf.find(SOF)
            if idx < 0:
                self._buf.clear()
                break
            if idx > 0:
                self._buf = self._buf[idx:]

            if len(self._buf) < self.HEADER_SIZE:
                break

            payload_len = self._buf[5]
            total = self.HEADER_SIZE + payload_len + 2  # +2 CRC

            if len(self._buf) < total:
                break

            frame = bytes(self._buf[:total])
            self._buf = self._buf[total:]

            body = frame[:-2]
            crc_lo, crc_hi = frame[-2], frame[-1]
            expected_crc = (crc_hi << 8) | crc_lo
            actual_crc = crc16(body)

            if actual_crc != expected_crc:
                print(f"[RX] CRC mismatch (got {actual_crc:04X}, expected {expected_crc:04X})")
                continue

            ver   = frame[1]
            flags = frame[2]
            kind  = frame[3]
            seq   = frame[4]
            payload = frame[self.HEADER_SIZE:self.HEADER_SIZE + payload_len]

            if ver != PROTOCOL_VERSION:
                print(f"[RX] Версия протокола {ver} != {PROTOCOL_VERSION}")
                continue

            frames.append((kind, flags, seq, payload))

        return frames


# ---------------------------------------------------------------------------
# Payload builders
# ---------------------------------------------------------------------------

# ── Simulated units ───────────────────────────────────────────────────────────
# Previously this emulator transmitted a fixed script: mode was hardcoded to
# DRYING and commands were ACKed then thrown away, so pressing Stop in the UI
# looked broken even though the device had sent the frame correctly. These units
# hold real state and react.

AMBIENT_C = 24.0

class SimUnit:
    def __init__(self, unit_id):
        self.id = unit_id
        self.mode = MODE_IDLE
        self.target_temp = 0.0
        self.target_hum = 0
        self.duration_min = 0
        self.started = 0.0
        self.session = 0
        self.temp = AMBIENT_C + unit_id * 1.5
        self.hum = 45.0 - unit_id * 3.0

    @property
    def running(self):
        return self.mode in (MODE_DRYING, MODE_STORAGE, MODE_PROFILE)

    def start(self, mode, target_temp, arg1):
        self.mode = mode
        self.target_temp = target_temp
        self.session += 1
        self.started = time.time()
        if mode == MODE_DRYING:
            self.duration_min, self.target_hum = int(arg1), 0
        else:                       # storage holds humidity, runs open-ended
            self.duration_min, self.target_hum = 0, int(arg1)

    def stop(self):
        self.mode = MODE_IDLE
        self.target_temp = 0.0
        self.target_hum = 0
        self.duration_min = 0

    def elapsed(self):
        return int(time.time() - self.started) if self.running else 0

    def tick(self, dt):
        """Crude first-order approach to target, so the UI shows movement."""
        goal = self.target_temp if self.running else AMBIENT_C
        self.temp += (goal - self.temp) * min(1.0, dt / 45.0)
        # Drying pulls humidity down; idle lets it creep back up.
        hgoal = (self.target_hum or 12.0) if self.running else 45.0
        self.hum += (hgoal - self.hum) * min(1.0, dt / 60.0)
        # A drying run ends on its own when the timer expires.
        if self.mode == MODE_DRYING and self.duration_min:
            if self.elapsed() >= self.duration_min * 60:
                self.stop()

    def heater_pct(self):
        if not self.running:
            return 0
        gap = self.target_temp - self.temp
        return max(0, min(100, int(gap * 25)))

    def fan(self):
        return self.running



# ── Menu config (answer to GetConfig) ─────────────────────────────────────────
# The device asks for this after every Hello and cannot render its menu browser
# without it. Shape is dictated by menu_parseFullConfig() in lib/idryer-menu:
#
#   * a "vals" object is mandatory — the parser returns false without it
#   * the literal key "full" must be present, otherwise ConfigReceiver::isDelta()
#     treats the payload as a delta (it greps for "full" in the raw JSON)
#   * "rev" (or "v") is the revision the UI displays
#   * per-unit ids carry an array, global ids a scalar; scope comes from
#     g_menu_meta on the device, so sending the wrong shape silently misfiles
#
# Ids below are the documented ones from docs/menu-json-format.json.

MENU_META_H = pathlib.Path(__file__).resolve().parent.parent / "lib/idryer-menu/src/menu_meta.h"

# Entry shape in the generated header:
#   { 3, { "..", ".." }, { "..", ".." },
#     META_VALUE, 2, -1, 0,
#     META_VT_F32, 30.0f, 110.0f, 1.0f, META_SCOPE_PER_UNIT, nullptr },
_META_RE = re.compile(
    r"\{\s*(\d+),\s*\{.*?\},\s*\{.*?\},\s*"
    r"(META_\w+),\s*-?\d+,\s*-?\d+,\s*\d+,\s*"
    r"META_VT_(\w+),\s*([-\d.eE]+)f,\s*([-\d.eE]+)f,\s*([-\d.eE]+)f,\s*"
    r"(META_SCOPE_\w+)",
    re.S)


def load_menu_meta():
    """Parse the generated menu metadata so emitted values are always in range.

    Hardcoding ids is a trap: docs/menu-json-format.json still lists v1's
    key_ids, where 13 was PRESET_PLA_TEMP. On controller v2, id 13 is FIRST
    STAGE with range 1..10, so the documented value of 55 is nonsense. Reading
    the same header the firmware compiles against keeps the two in step.
    """
    try:
        text = MENU_META_H.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    out = []
    for m in _META_RE.finditer(text):
        mid, mtype, _vt, lo, hi, _step, scope = m.groups()
        if mtype not in ("META_VALUE", "META_TOGGLE"):
            continue          # submenu/action items hold no value
        out.append({
            "id": int(mid),
            "toggle": mtype == "META_TOGGLE",
            "min": float(lo),
            "max": float(hi),
            "per_unit": scope == "META_SCOPE_PER_UNIT",
        })
    return out


def build_menu_json(units: int, rev: int = 8, overrides=None) -> bytes:
    """Full-config JSON for menu_parseFullConfig().

    Requirements come from the parser, not from taste:
      * "vals" is mandatory — it returns false without it
      * the literal "full" must appear, or ConfigReceiver::isDelta() greps for
        it, misses, and treats the whole thing as a delta
      * per-unit ids take an array, globals a scalar; the device decides which
        from g_menu_meta, so the wrong shape silently misfiles the value
    """
    meta = load_menu_meta()
    overrides = overrides or {}
    parts = []

    # Language and units-count are positional, not fixed ids: menu_commands.cpp
    # takes them as MENU_META_COUNT-1 and -2. (docs/menu-json-format.json still
    # names 144/143 from v1, where they are now a submenu and an action.)
    #
    # They need real values rather than a mid-range guess. Language especially —
    # its range is 0..1, whose midpoint rounds to 0 (Russian), and because vals
    # is parsed after the document root it silently overrides "lang":"en" there.
    count = len(meta) and max(i["id"] for i in meta) + 1
    total_ids = 202 if not count else max(count, 202)
    lang_id, units_id = total_ids - 1, total_ids - 2
    pinned = {lang_id: 1, units_id: units}   # 1 = en

    if meta:
        for item in meta:
            lo, hi = item["min"], item["max"]
            # Pins are the *starting* value, not a lock — an inbound `set` must
            # still win, or changing the language from the UI would appear to be
            # accepted and then be silently reverted by the next config echo.
            if item["id"] in pinned:
                v = pinned[item["id"]]
            elif item["toggle"]:
                v = 0
            elif hi > lo:
                v = round(lo + (hi - lo) * 0.5)      # mid-range, always legal
            else:
                v = round(lo)
            if item["per_unit"]:
                vals_u = [overrides.get((item["id"], u), v) for u in range(units)]
                parts.append(f'"{item["id"]}":{json.dumps(vals_u)}')
            else:
                parts.append(f'"{item["id"]}":{overrides.get((item["id"], 0), v)}')
    else:
        # Header not found (running the tool standalone) — minimal fallback.
        for i in (3, 4, 7, 8):
            parts.append(f'"{i}":{json.dumps([60] * units)}')

    body = ",".join(parts)
    doc = (f'{{"rev":{rev},"full":true,"units":{units},'
           f'"active":0,"lang":"en","vals":{{{body}}}}}')
    return doc.encode()


def send_config(ser, state, units: int):
    """Chunk the menu JSON over ConfigPush frames."""
    payload = build_menu_json(units, rev=state.get('config_rev', 8),
                              overrides=state.get('overrides', {}))
    total = len(payload)
    chunk_max = UART_MAX_PAYLOAD - CONFIG_CHUNK_HEADER_SIZE
    transfer_id = state['config_tid'] = (state.get('config_tid', 0) + 1) & 0x7FFF

    idx = 0
    for off in range(0, total, chunk_max):
        data = payload[off:off + chunk_max]
        last = (off + len(data)) >= total
        # transferId(u16) totalSize(u16) chunkIndex(u8) pad(u8)
        head = struct.pack('<HHBB', transfer_id, total, idx, 0)
        flags = FLAG_ACK_REQUIRED | (FLAG_LAST_FRAGMENT if last else 0)
        state['seq'] = send_frame(ser, KIND_CONFIG_PUSH, head + data, state['seq'], flags)
        idx += 1
    print(f"[TX] Config: {total}B in {idx} chunk(s), tid={transfer_id}, "
          f"rev={state.get('config_rev', 8)}")


def make_unit_config(unit_id: int, caps: int, scales: list, rfid: list) -> bytes:
    """UnitConfig: 12 байт."""
    scales_b = bytes((scales + [0xFF] * 4)[:4])
    rfid_b   = bytes((rfid   + [0xFF] * 4)[:4])
    return struct.pack('<BBH', unit_id, 0, caps) + scales_b + rfid_b


def make_hello(fw_major: int = 2, fw_minor: int = 0, fw_patch: int = 0,
               units_count: int = 2, mcu_serial: str = "36B955AB4350") -> bytes:
    """
    HelloPayload: 94 байта (protocol v2).
    role(1) + pad(3) + fwVer(4) + workTime(4) + hwVer[16] + unitsCount(1) + units[4](48) + mcuSerial[17]

    v2 расширил hardwareVersion с 8 до 16 байт. UartBridge::validateLength
    сравнивает длину точно, поэтому 86-байтный Hello просто отбрасывается.
    """
    role      = bytes([ROLE_MCU, 0, 0, 0])
    fw_ver    = struct.pack('<I', (fw_major << 16) | (fw_minor << 8) | fw_patch)
    work_time = struct.pack('<I', 3600)
    hw_ver    = b'rp2040-v1'.ljust(16, b'\x00')
    u_count   = bytes([units_count])

    unit0 = make_unit_config(0, CAP_ALL, [0, 1], [0])   # U1: W0,W1 / R0
    unit1 = make_unit_config(1, CAP_ALL, [2],    [1])   # U2: W2 / R1
    unit2 = make_unit_config(2, 0,       [],     [])    # пусто
    unit3 = make_unit_config(3, 0,       [],     [])    # пусто

    serial_b = mcu_serial.encode()[:16].ljust(17, b'\x00')

    payload = role + fw_ver + work_time + hw_ver + u_count + unit0 + unit1 + unit2 + unit3 + serial_b
    assert len(payload) == 94, f"HelloPayload size {len(payload)} != 94"
    return payload


def make_telemetry(units: list) -> bytes:
    """
    TelemetryPayload.
    units = [{'id': 0, 'temp_c': 55.3, 'hum_pct': 45.0, 'heater_pct': 80, 'fan': True}, ...]
    """
    data = bytes([len(units)])
    for u in units:
        temp_c10 = int(u['temp_c'] * 10)
        hum_pct10 = int(u['hum_pct'] * 10)
        data += struct.pack('<BhHBB',
                            u['id'],
                            temp_c10,
                            hum_pct10,
                            u['heater_pct'],
                            1 if u['fan'] else 0)
    # The wire struct is a fixed count(1) + units[4]*7 = 29 bytes, and
    # UartBridge::validateLength compares exactly. Without this padding a
    # single-unit frame is 8 bytes and gets dropped silently.
    while len(data) < 1 + 4 * 7:
        data += struct.pack('<BhHBB', 0, 0, 0, 0, 0)
    assert len(data) == 29, f"TelemetryPayload size {len(data)} != 29"
    return data


def make_status(units: list, uptime: int = 0) -> bytes:
    """
    StatusPayload.
    units = [{'id': 0, 'mode': MODE_DRYING, 'session': 1, 'target_temp': 55.0,
               'target_hum': 20, 'duration_min': 240, 'elapsed': 120, 'remaining': 3480}, ...]
    StatusEntry: 32 байт.
    """
    data = bytes([len(units)])
    for u in units:
        # <BBIhHHIIIIBBBB — 14 fields, 32 bytes. The old format string had only
        # 13: durationMinutes was missing and targetTempC10 was packed unsigned
        # despite being int16_t, so make_status() raised on every call.
        entry = struct.pack('<BBIhHHIIIIBBBB',
                            u['id'],
                            u.get('mode', MODE_IDLE),
                            u.get('session', 0),
                            int(u.get('target_temp', 0) * 10),   # int16, signed
                            u.get('target_hum', 0),
                            u.get('duration_min', 0),
                            u.get('elapsed', 0),
                            u.get('stage_elapsed', 0),
                            u.get('stage_remaining', 0),
                            u.get('remaining', 0),
                            u.get('current_stage', 0),
                            u.get('total_stages', 0),
                            u.get('stage_phase', 0),
                            0)  # _pad
        assert len(entry) == 32, f"StatusEntry size {len(entry)} != 32"
        data += entry
    # Дополнить до 4 юнитов пустыми (упрощение для фиксированного размера)
    empty = struct.pack('<BBIhHHIIIIBBBB', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    while len(data) < 1 + 4 * 32:
        data += empty
    data += struct.pack('<I', uptime)
    # v2: device-wide флаг блокировки внешних команд. 0 = команды принимаются.
    data += bytes([0])
    assert len(data) == 134, f"StatusPayload size {len(data)} != 134"
    return data


def make_weights(sensors: list) -> bytes:
    """
    WeightsPayload.
    sensors = [{'sensor_id': 0, 'unit_id': 0, 'weight_g': 123.4}, ...]
    """
    data = bytes([len(sensors)])
    for s in sensors:
        w_c10 = int(s['weight_g'] * 10)
        data += struct.pack('<BBH', s['sensor_id'], s['unit_id'], w_c10)
    return data


def make_rfid(event: int, reader_id: int, unit_id: int, tag: str = "") -> bytes:
    """
    RfidPayload: 37 байт.
    event: RFID_TAG_DETECTED или RFID_TAG_REMOVED
    """
    tag_b = tag.encode()[:31].ljust(32, b'\x00')
    return struct.pack('<BB', event, reader_id) + tag_b + struct.pack('<BBB', unit_id, 0, 0)


def make_heartbeat(uptime: int, mcu_temp_c: float = 25.0, errors: int = 0) -> bytes:
    """HeartbeatPayload: 9 байт."""
    temp_raw = int(mcu_temp_c * 10)
    return struct.pack('<IhHB', uptime, temp_raw, errors, 0)


def make_ack(ack_seq: int, status: int = ERR_NONE) -> bytes:
    """AckPayload: 2 байта."""
    return struct.pack('<BB', ack_seq, status)


def make_config_ack(ack_seq: int) -> bytes:
    return make_ack(ack_seq)


# ---------------------------------------------------------------------------
# Обработка входящих кадров
# ---------------------------------------------------------------------------
def handle_frame(ser, kind, flags, seq, payload, state):
    """Реагировать на входящий кадр. state — dict с изменяемым состоянием."""

    if kind == KIND_HELLO:
        role = payload[0] if payload else 0
        if role == ROLE_HELLO_REQUEST:
            print(f"[RX] HelloRequest от LINK — отправляем Hello")
            hello = make_hello()
            state['seq'] = send_frame(ser, KIND_HELLO, hello, state['seq'])
            state['hello_sent'] = True
        elif role == ROLE_ESP:
            print(f"[RX] Hello от LINK (role=ESP)")

    elif kind == KIND_HELLO_ACK:
        if len(payload) >= 4:
            ip = struct.unpack('<I', payload[:4])[0]
            ip_str = f"{ip & 0xFF}.{(ip >> 8) & 0xFF}.{(ip >> 16) & 0xFF}.{(ip >> 24) & 0xFF}"
            ssid = payload[4:].rstrip(b'\x00').decode('utf-8', errors='replace')
            print(f"[RX] HelloAck: IP={ip_str} SSID={ssid!r}")

    elif kind == KIND_COMMAND:
        if len(payload) >= 13:
            cmd_code, target_state, unit_id = struct.unpack('<BBB', payload[:3])
            arg0, arg1 = struct.unpack('<II', payload[5:13])
            print(f"[RX] Command: code=0x{cmd_code:02X} unit={unit_id} arg0={arg0} arg1={arg1}")

            units = state['units']
            if cmd_code == CMD_START and unit_id < len(units):
                # arg0 is target temperature in tenths; arg1 is minutes (drying)
                # or target humidity (storage).
                units[unit_id].start(target_state, arg0 / 10.0, arg1)
                u = units[unit_id]
                print(f"      -> unit {unit_id} {'DRYING' if u.mode == MODE_DRYING else 'STORAGE'} "
                      f"target={u.target_temp:.0f}C arg1={arg1}")
            elif cmd_code == CMD_STOP:
                # unitId 0xFF (or out of range) means every unit.
                targets = units if unit_id >= len(units) else [units[unit_id]]
                for u in targets:
                    u.stop()
                print(f"      -> stopped {len(targets)} unit(s)")
            elif cmd_code in (CMD_RESET_FAULT, CMD_CLEAR_ERRORS):
                for u in units:
                    if u.mode == MODE_FAULT:
                        u.stop()
                print("      -> faults cleared")
            elif cmd_code == CMD_FIND:
                print(f"      -> find unit {unit_id} (beep)")
            elif cmd_code == CMD_GET_CONFIG:
                send_config(ser, state, len(units))
        else:
            print(f"[RX] Command (короткий payload {len(payload)}B)")
        if flags & FLAG_ACK_REQUIRED:
            ack = make_ack(seq)
            state['seq'] = send_frame(ser, KIND_COMMAND_ACK, ack, state['seq'], FLAG_IS_ACK)
            print(f"[TX] CommandAck seq={seq}")

    elif kind == KIND_CONFIG_PUSH:
        if len(payload) >= 6:
            transfer_id, total_size, chunk_idx = struct.unpack('<HHH', payload[:6])
            json_data = payload[6:].rstrip(b'\x00').decode('utf-8', errors='replace')
            print(f"[RX] ConfigPush: tid={transfer_id} total={total_size} chunk={chunk_idx}")
            print(f"     JSON: {json_data[:80]}{'...' if len(json_data) > 80 else ''}")
        else:
            json_data = payload.rstrip(b'\x00').decode('utf-8', errors='replace')
            print(f"[RX] ConfigPush (нефрагментированный): {json_data[:80]}")
        if flags & FLAG_ACK_REQUIRED:
            ack = make_config_ack(seq)
            state['seq'] = send_frame(ser, KIND_CONFIG_ACK, ack, state['seq'], FLAG_IS_ACK)

        # Apply the write and echo the new config back, which is what actually
        # updates the device's menu cache. Without this a 'set' is acknowledged
        # and silently discarded, so the UI appears to accept edits that never
        # stick — the same trap the hardcoded status had.
        try:
            req = json.loads(json_data)
        except Exception:
            req = None
        if isinstance(req, dict) and req.get('cmd') == 'set' and 'id' in req:
            mid = int(req['id']); unit = int(req.get('unit', 0))
            val = req.get('val', 0)
            state.setdefault('overrides', {})[(mid, unit)] = val
            state['config_rev'] = state.get('config_rev', 8) + 1
            print(f"      -> set id={mid} unit={unit} val={val}; rev now {state['config_rev']}")
            send_config(ser, state, len(state['units']))
        elif isinstance(req, dict) and req.get('cmd') == 'invoke':
            print(f"      -> invoke id={req.get('id')}")

    elif kind == KIND_HEARTBEAT:
        if len(payload) >= 9:
            uptime, rssi, errors, cloud_state = struct.unpack('<IhHB', payload[:9])
            print(f"[RX] Heartbeat: uptime={uptime}s RSSI={rssi}dBm errors={errors} cloud={cloud_state}")

    elif kind == KIND_ERROR:
        if len(payload) >= 4:
            err_code, last_seq, detail = struct.unpack('<BBH', payload[:4])
            print(f"[RX] Error: code={err_code} seq={last_seq} detail={detail}")

    elif kind == KIND_LOG:
        print(f"[RX] Log: {payload.decode('utf-8', errors='replace')[:100]}")

    elif kind & FLAG_IS_ACK:
        pass  # ACK на наши кадры — игнорируем

    else:
        print(f"[RX] Неизвестный kind=0x{kind:02X} payload={payload.hex()[:32]}")


# ---------------------------------------------------------------------------
# Главный цикл
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Эмулятор RP2040 для UART-протокола iDryer")
    parser.add_argument("--port", default="/dev/cu.usbserial-130",
                        help="UART порт")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--session", type=int, default=120,
                        help="Длительность сессии, секунд")
    parser.add_argument("--fw-major", type=int, default=2,
                        help="MAJOR версия прошивки (для проверки совместимости)")
    parser.add_argument("--units", type=int, default=2,
                        help="Количество юнитов (1-4)")
    parser.add_argument("--rfid", action="store_true",
                        help="Отправить RFID tag_detected событие")
    args = parser.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.05
    ser.dtr = False
    ser.rts = False
    ser.open()
    print(f"[HOST] UART открыт {args.port} @ {args.baud}")

    parser_obj = FrameParser()
    state = {
        'seq': 0,
        'hello_sent': False,
        'units': [SimUnit(i) for i in range(args.units)],
    }

    t_start      = time.time()
    t_hello      = 0.0       # время последнего Hello
    t_telemetry  = 0.0
    t_status     = 0.0
    t_weights    = 0.0
    t_heartbeat  = 0.0
    t_rfid       = 0.0
    rfid_sent    = False

    def elapsed():
        return time.time() - t_start

    try:
        while elapsed() < args.session:
            # --- Чтение и обработка входящих кадров ---
            raw = ser.read(256)
            if raw:
                for kind, flags, seq, payload in parser_obj.feed(raw):
                    handle_frame(ser, kind, flags, seq, payload, state)

            now = elapsed()

            # --- Advance the simulated units ---
            t_now = time.time()
            dt = t_now - state.get('t_last_tick', t_now)
            state['t_last_tick'] = t_now
            if dt > 0:
                for u in state['units']:
                    u.tick(dt)

            # --- Hello (каждые 30 с или первый раз через 2 с) ---
            if now - t_hello > (2.0 if not state['hello_sent'] else 30.0):
                hello = make_hello(
                    fw_major=args.fw_major,
                    units_count=args.units,
                )
                state['seq'] = send_frame(ser, KIND_HELLO, hello, state['seq'])
                state['hello_sent'] = True
                t_hello = now
                print(f"[TX] Hello (fw {args.fw_major}.0.0, {args.units} units)")

            if not state['hello_sent']:
                continue

            # --- Telemetry (каждые 5 с) ---
            if now - t_telemetry > 5.0:
                units_tel = [
                    {'id': u.id, 'temp_c': u.temp, 'hum_pct': u.hum,
                     'heater_pct': u.heater_pct(), 'fan': u.fan()}
                    for u in state['units']
                ]
                tel = make_telemetry(units_tel)
                state['seq'] = send_frame(ser, KIND_TELEMETRY, tel, state['seq'],
                                          FLAG_ACK_REQUIRED)
                t_telemetry = now
                print(f"[TX] Telemetry ({args.units} units)")

            # --- Status (каждые 10 с) ---
            if now - t_status > 10.0:
                units_st = [
                    {'id': u.id, 'mode': u.mode, 'session': u.session,
                     'target_temp': u.target_temp, 'target_hum': u.target_hum,
                     'duration_min': u.duration_min, 'elapsed': u.elapsed(),
                     'remaining': max(0, u.duration_min * 60 - u.elapsed())}
                    for u in state['units']
                ]
                st = make_status(units_st, uptime=int(now))
                state['seq'] = send_frame(ser, KIND_STATUS, st, state['seq'],
                                          FLAG_ACK_REQUIRED)
                t_status = now
                summary = " ".join(
                    f"U{u.id+1}:{['idle','dry','store','profile','fault','heat','light'][u.mode]}"
                    f"@{u.temp:.1f}C" for u in state['units'])
                print(f"[TX] Status {summary}")

            # --- Weights (каждые 10 с) ---
            if now - t_weights > 10.0:
                sensors = [
                    {'sensor_id': i, 'unit_id': i // 2, 'weight_g': 200.0 + i * 50.5}
                    for i in range(min(args.units * 2, 4))
                ]
                wt = make_weights(sensors)
                state['seq'] = send_frame(ser, KIND_WEIGHTS, wt, state['seq'])
                t_weights = now
                print(f"[TX] Weights ({len(sensors)} sensors)")

            # --- Heartbeat (каждые 5 с) ---
            if now - t_heartbeat > 5.0:
                hb = make_heartbeat(uptime=int(now), mcu_temp_c=38.5)
                state['seq'] = send_frame(ser, KIND_HEARTBEAT, hb, state['seq'])
                t_heartbeat = now

            # --- RFID (один раз через 15 с, если --rfid) ---
            if args.rfid and not rfid_sent and now > 15.0:
                rfid = make_rfid(RFID_TAG_DETECTED, reader_id=0, unit_id=0,
                                 tag="AABBCCDDEEFF00")
                state['seq'] = send_frame(ser, KIND_RFID, rfid, state['seq'],
                                          FLAG_ACK_REQUIRED)
                rfid_sent = True
                t_rfid = now
                print(f"[TX] Rfid tag_detected AABBCCDDEEFF00 (R0/U1)")

    except KeyboardInterrupt:
        print("\n[HOST] Прервано")
    finally:
        ser.close()
        print(f"[HOST] UART закрыт. Отправлено кадров: seq={state['seq']}")


if __name__ == "__main__":
    main()
