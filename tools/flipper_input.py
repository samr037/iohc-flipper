#!/usr/bin/env python3
"""Inject input events into the Flipper Zero via its CLI.

Usage:
    flipper_input.py <key> [type]

Keys:  up, down, left, right, ok, back
Types: short (default), long, press, release

Examples:
    flipper_input.py ok
    flipper_input.py back long
    flipper_input.py up short
"""
import sys
import time
import serial

PORT = "/dev/cu.usbmodemflip_Qu3r1"
KEYS = {"up", "down", "left", "right", "ok", "back"}
TYPES = {"short", "long", "press", "release"}


def send(key: str, kind: str = "short") -> str:
    if key not in KEYS:
        raise ValueError(f"key must be one of {KEYS}")
    if kind not in TYPES:
        raise ValueError(f"type must be one of {TYPES}")
    with serial.Serial(PORT, timeout=1) as s:
        time.sleep(0.2)
        s.reset_input_buffer()
        s.write(f"input send {key} {kind}\r\n".encode())
        time.sleep(0.3)
        return s.read(s.in_waiting or 1024).decode(errors="replace")


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)
    key = sys.argv[1]
    kind = sys.argv[2] if len(sys.argv) > 2 else "short"
    print(send(key, kind))
