#!/usr/bin/env python3
# Copyright 2026 Keychron
# SPDX-License-Identifier: GPL-2.0-or-later
"""
Read and decode the Prospector status carrier from a Keychron K10 Max.

The firmware's rawhid transport emits a 32-byte report on Keychron's raw-HID
interface (usage page 0xFF60, usage 0x61), which the LKBT51 module forwards over
Bluetooth as well as USB:

    [0]      0xAE   Prospector group
    [1]      0x01   sub-command: status frame
    [2]      26     payload length
    [3..28]  the 26-byte Prospector manufacturer-data payload, verbatim
    [29]     host lock state (bit0 Caps, bit1 Num, bit2 Scroll) - NOT protocol
    [30..31] zero

This tool is a *verification* aid. It proves that the on-device state collection
and the encoder produce the right 26 bytes. It is not a Prospector scanner: the
scanner only reads BLE advertisements, which this keyboard cannot emit.

Usage:
    pip install hid
    python3 prospector_receiver.py [--raw] [--seconds N]
"""

import argparse
import struct
import sys
import time

VID = 0x3434          # Keychron
PID = 0x0AA0          # K10 Max ANSI
GROUP = 0xAE
SUB_STATUS = 0x01
PAYLOAD_LEN = 26

FLAG_CAPS_WORD = 1 << 0
FLAG_CHARGING = 1 << 1
FLAG_USB_CONNECTED = 1 << 2
FLAG_USB_HID_READY = 1 << 3
FLAG_BLE_CONNECTED = 1 << 4
FLAG_BLE_BONDED = 1 << 5

ROLE = {0: "standalone", 1: "central", 2: "peripheral"}


def scanner_accepts(payload):
    """The scanner's gate, transcribed from status_scanner.c."""
    return (
        len(payload) >= PAYLOAD_LEN
        and payload[0] == 0xFF
        and payload[1] == 0xFF
        and payload[2] == 0xAB
        and payload[3] == 0xCD
    )


def decode(payload, aux):
    (mid0, mid1, su0, su1, version, battery, layer, profile_slot,
     conn, flags, role, index) = struct.unpack_from("<BBBBBBBBBBBB", payload, 0)
    periph = list(payload[12:15])
    layer_name = payload[15:19].decode("ascii", "replace").rstrip("\x00")
    kb_id = payload[19:23]
    mods, wpm, channel = payload[23], payload[24], payload[25]

    locks = "".join(
        c if aux & (1 << i) else "-" for i, c in enumerate(("C", "N", "S"))
    )

    return (
        f"ver={version >> 4}.{version & 0x0F}.{(profile_slot >> 3) & 0x07} "
        f"bat={battery}% layer={layer} prof={profile_slot & 0x07} conn={conn} "
        f"flags=0x{flags:02X} role={ROLE.get(role, role)} "
        f"periph={periph} mods=0x{mods:02X} wpm={wpm} ch={channel} "
        f"name={layer_name!r} id={kb_id.hex().upper()} locks={locks} "
        f"valid={'yes' if scanner_accepts(payload) else 'NO'}"
    )


def main():
    try:
        import hid
    except ImportError:
        sys.exit("needs the hid package:  pip install hid")

    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", action="store_true", help="print raw reports")
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds")
    args = ap.parse_args()

    # The keyboard exposes several HID interfaces; the raw-HID one is the
    # vendor-defined usage page 0xFF60 / usage 0x61 (QMK_RAW usage).
    path = None
    for info in hid.enumerate(VID, PID):
        if info.get("usage_page") == 0xFF60 and info.get("usage") == 0x61:
            path = info["path"]
            break
    if path is None:
        sys.exit(f"no raw-HID interface (usage page 0xFF60/0x61) on {VID:04X}:{PID:04X}.\n"
                 "Is the keyboard plugged in and running the Prospector firmware?")

    try:
        # pyhidapi >= 1.0 exposes hid.Device; the older hidapi bindings expose
        # hid.device() with open_path(). Support both.
        if hasattr(hid, "Device"):
            dev = hid.Device(path=path)
        else:
            dev = hid.device()
            dev.open_path(path)
    except Exception as exc:  # noqa: BLE001 - surfaced to the user
        sys.exit(f"could not open raw-HID interface: {exc}")

    def read_report():
        try:
            return dev.read(64, timeout=1000)
        except TypeError:
            try:
                return dev.read(64, 1000)
            except TypeError:
                return dev.read(64)

    print(f"listening on {VID:04X}:{PID:04X} (Ctrl-C to stop)", flush=True)
    started = time.time()
    try:
        while True:
            if args.seconds and time.time() - started > args.seconds:
                break
            data = read_report()
            if not data:
                continue
            # hidapi may or may not include a report id
            if len(data) >= 33 and data[0] == 0:
                data = data[1:]
            if len(data) < 30 or data[0] != GROUP or data[1] != SUB_STATUS:
                if args.raw:
                    print("raw:", data[:32].hex(" "))
                continue
            if data[2] != PAYLOAD_LEN:
                print(f"bad length {data[2]}", flush=True)
                continue
            payload = bytes(data[3:3 + PAYLOAD_LEN])
            if args.raw:
                print("raw:", payload.hex(" "))
            print(f"[Prospector] {decode(payload, data[29])}", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        dev.close()


if __name__ == "__main__":
    main()
