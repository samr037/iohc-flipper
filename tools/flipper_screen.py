#!/usr/bin/env python3
"""Capture or stream the Flipper Zero screen via the protobuf RPC.

Subcommands:
    snapshot [out.png]   capture one frame, save PNG (default: flipper.png)
    stream [out_dir]     live capture to a sequence of PNGs (Ctrl-C to stop)

Framebuffer is 128x64, 1 bit per pixel, column-major page-stacked
(8 vertical pixels per byte, LSB = top). See FZ display driver (ST756x family).
"""
from __future__ import annotations
import sys
import time
from pathlib import Path

from PIL import Image
from flipperzero_protobuf import FlipperProto

PORT = "/dev/cu.usbmodemflip_Qu3r1"
W, H = 128, 64


def fb_to_image(fb: bytes) -> Image.Image:
    if len(fb) < W * H // 8:
        raise ValueError(f"framebuffer too small: {len(fb)} bytes")
    img = Image.new("1", (W, H), 1)  # white background
    px = img.load()
    for page in range(H // 8):
        for col in range(W):
            b = fb[page * W + col]
            for bit in range(8):
                if b & (1 << bit):
                    px[col, page * 8 + bit] = 0  # black pixel
    return img


def snapshot(out: Path) -> None:
    proto = FlipperProto(serial_port=PORT)
    fb = proto.rpc_gui_snapshot_screen()
    fb_to_image(fb).resize((W * 4, H * 4)).save(out)
    print(f"saved {out} ({len(fb)} fb bytes)")


def stream(out_dir: Path, scale: int = 4) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    proto = FlipperProto(serial_port=PORT)
    proto.rpc_gui_start_screen_stream()
    print("streaming. Ctrl-C to stop.")
    i = 0
    try:
        while True:
            fb = proto.rpc_gui_snapshot_screen()
            img = fb_to_image(fb).resize((W * scale, H * scale))
            path = out_dir / f"frame_{i:05d}.png"
            img.save(path)
            print(f"frame {i:5d} -> {path.name}", end="\r")
            i += 1
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    finally:
        proto.rpc_gui_stop_screen_stream()
        print(f"\nstopped. {i} frames in {out_dir}")


def main() -> int:
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    cmd = args[0]
    if cmd == "snapshot":
        out = Path(args[1]) if len(args) > 1 else Path("flipper.png")
        snapshot(out)
    elif cmd == "stream":
        out_dir = Path(args[1]) if len(args) > 1 else Path("captures")
        stream(out_dir)
    else:
        print(f"unknown subcommand: {cmd}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
