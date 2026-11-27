"""Capture the real LCD frame from the device over serial.

Usage: python scripts/screenshot.py OUT.png [KEYS] [--port COM7]
KEYS are serial test keys sent first, e.g. "b+s" (hold, turn, press).
"""

import argparse
import time

import serial
from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("keys", nargs="?", default="")
    parser.add_argument("--port", default="COM7")
    args = parser.parse_args()
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 10
    port.dtr = port.rts = False  # set before opening so the ESP32 is not reset
    with port:
        for key in args.keys:
            port.write(key.encode())
            time.sleep(0.25)
        port.reset_input_buffer()
        port.write(b"d")
        while True:
            line = port.readline()
            if not line:
                raise SystemExit("No frame received")
            if line.startswith(b"SHOT "):
                break
        width, height = map(int, line.split()[1:3])
        data = port.read(width * height)
    if len(data) != width * height:
        raise SystemExit(f"Short frame: {len(data)} bytes")
    image = Image.new("RGB", (width, height))
    image.putdata([((p >> 5) * 255 // 7, ((p >> 2) & 7) * 255 // 7, (p & 3) * 255 // 3) for p in data])
    image.save(args.out)


if __name__ == "__main__":
    main()
