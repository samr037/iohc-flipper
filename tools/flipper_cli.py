#!/usr/bin/env python3
"""Small helper to run a Flipper CLI command and print the response.

Usage:
    .venv/bin/python tools/flipper_cli.py 'device_info'
    .venv/bin/python tools/flipper_cli.py 'subghz tx 433920000 1 0 10'
"""
import sys
import time
import serial

PORT = "/dev/cu.usbmodemflip_Qu3r1"


def run(cmd: str, wait_s: float = 1.0) -> str:
    with serial.Serial(PORT, timeout=1) as s:
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"\r\n")
        time.sleep(0.2)
        s.read(s.in_waiting or 0)  # drain prompt
        s.write(cmd.encode() + b"\r\n")
        time.sleep(wait_s)
        return s.read(s.in_waiting or 16384).decode(errors="replace")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(1)
    print(run(sys.argv[1], wait_s=float(sys.argv[2]) if len(sys.argv) > 2 else 1.0))
