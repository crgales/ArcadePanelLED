#!/usr/bin/env python3
"""Send images and text to the MatrixPortal S3 LED panel over USB serial.

Images are scaled to the largest size that fits the panel and centered unless
--x/--y or --no-scale are given.

Default-image uploads require matching firmware: IMAGED <bytes> replies with
RDY <block size>, then ACK <total bytes> after each block is saved to LittleFS.
The host waits for each ACK before sending the next block.

Examples:
    python MatrixPanelHost.py -p /dev/ttyACM0 image logo.png
    python MatrixPanelHost.py -p /dev/ttyACM0 image logo.png --x 10 --y 4
    python MatrixPanelHost.py -p /dev/ttyACM0 imaged logo.png   # store as boot default
    python MatrixPanelHost.py -p /dev/ttyACM0 text "HIGH SCORE" --x 2 --y 20 --color FF00FF --size 2
    python MatrixPanelHost.py -p /dev/ttyACM0 clear
"""

import argparse
import io
import sys
import time

import serial
from PIL import Image

PANEL_WIDTH = 384
PANEL_HEIGHT = 64
DEFAULT_BAUD = 921600
CHUNK = 4096


def parse_color(value):
    """Accept 'RRGGBB', '#RRGGBB', '0xRRGGBB' or a decimal value."""
    text = value.strip().lstrip("#")
    if text.lower().startswith("0x"):
        return int(text, 16)
    try:
        return int(text, 16)
    except ValueError:
        return int(text, 10)


def to_rgb565_bytes(img):
    data = bytearray()
    for r, g, b in img.get_flattened_data():
        packed = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        data += packed.to_bytes(2, "big")
    return bytes(data)


def read_line(ser, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", "replace").strip()
        if line:
            return line
    raise TimeoutError("No response from panel")


def wait_for(ser, prefix, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = read_line(ser, timeout=max(0.1, deadline - time.time()))
        if line.startswith("ERR"):
            raise RuntimeError(f"Panel error: {line}")
        if line.startswith(prefix):
            return line
    raise TimeoutError(f"Timed out waiting for '{prefix}'")


def send_command(ser, command):
    ser.write((command + "\n").encode("utf-8"))
    ser.flush()


def prepare_image(path, width, height, scale):
    img = Image.open(path).convert("RGB")
    target_w = width or PANEL_WIDTH
    target_h = height or PANEL_HEIGHT
    if scale:
        ratio = min(target_w / img.width, target_h / img.height)
        img = img.resize(
            (max(1, round(img.width * ratio)), max(1, round(img.height * ratio))),
            Image.LANCZOS,
        )
    elif width or height:
        img = img.resize((width or img.width, height or img.height), Image.LANCZOS)
    return img


def cmd_image(ser, args):
    img = prepare_image(args.path, args.width, args.height, not args.no_scale)
    payload = to_rgb565_bytes(img)

    x = args.x if args.x is not None else (PANEL_WIDTH - img.width) // 2
    y = args.y if args.y is not None else (PANEL_HEIGHT - img.height) // 2

    send_command(ser, f"IMG {x} {y} {img.width} {img.height} 16")
    wait_for(ser, "RDY")

    for offset in range(0, len(payload), CHUNK):
        ser.write(payload[offset:offset + CHUNK])
    ser.flush()

    print(wait_for(ser, "OK", timeout=30.0))


def cmd_image_default(ser, args):
    img = prepare_image(args.path, args.width, args.height, not args.no_scale)
    if img.width > PANEL_WIDTH or img.height > PANEL_HEIGHT:
        img = prepare_image(args.path, PANEL_WIDTH, PANEL_HEIGHT, True)
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=True)
    payload = buf.getvalue()

    send_command(ser, f"IMAGED {len(payload)}")
    ready = wait_for(ser, "RDY")
    fields = ready.split()
    if len(fields) != 2 or fields[0] != "RDY" or not fields[1].isdigit():
        raise RuntimeError(
            "Panel does not advertise an IMAGED block size; update the panel firmware"
        )
    block_size = int(fields[1])
    if not 0 < block_size <= CHUNK:
        raise RuntimeError(f"Invalid IMAGED block size: {block_size}")

    for offset in range(0, len(payload), block_size):
        block = payload[offset:offset + block_size]
        if ser.write(block) != len(block):
            raise RuntimeError("Incomplete IMAGED serial write")
        ser.flush()
        acknowledgement = wait_for(ser, "ACK")
        expected = f"ACK {offset + len(block)}"
        if acknowledgement != expected:
            raise RuntimeError(
                f"Unexpected IMAGED acknowledgement: {acknowledgement!r}; "
                f"expected {expected!r}"
            )

    print(wait_for(ser, "OK", timeout=30.0))


def cmd_text(ser, args):
    color = parse_color(args.color)
    wrap = 1 if args.wrap else 0
    send_command(ser, f"TEXT {args.x} {args.y} 0x{color:06X} {args.size} {wrap} {args.message}")
    print(wait_for(ser, "OK"))


def cmd_clear(ser, _args):
    send_command(ser, "CLEAR")
    print(wait_for(ser, "OK"))


def cmd_fill(ser, args):
    send_command(ser, f"FILL 0x{parse_color(args.color):06X}")
    print(wait_for(ser, "OK"))


def cmd_ping(ser, _args):
    send_command(ser, "PING")
    print(wait_for(ser, "OK"))


def build_parser():
    parser = argparse.ArgumentParser(description="Drive the MatrixPortal S3 LED panel over serial.")
    parser.add_argument("-p", "--port", required=True, help="Serial port, e.g. /dev/ttyACM0 or COM5")
    parser.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--no-clear", action="store_true", help="Do not clear the panel first")

    sub = parser.add_subparsers(dest="command", required=True)

    p_img = sub.add_parser("image", help="Send an image file")
    p_img.add_argument("path")
    p_img.add_argument("--x", type=int, help="X offset on the panel (default: centered)")
    p_img.add_argument("--y", type=int, help="Y offset on the panel (default: centered)")
    p_img.add_argument("--width", type=int, help="Bounding width in pixels")
    p_img.add_argument("--height", type=int, help="Bounding height in pixels")
    p_img.add_argument("--no-scale", action="store_true", help="Send at native size instead of scaling to fit")
    p_img.set_defaults(func=cmd_image)

    p_imgd = sub.add_parser("imaged", help="Upload an image as the stored default (shown at boot)")
    p_imgd.add_argument("path")
    p_imgd.add_argument("--width", type=int, help="Bounding width in pixels")
    p_imgd.add_argument("--height", type=int, help="Bounding height in pixels")
    p_imgd.add_argument("--no-scale", action="store_true", help="Send at native size (still shrunk if larger than the panel)")
    p_imgd.set_defaults(func=cmd_image_default)

    p_txt = sub.add_parser("text", help="Send a text message")
    p_txt.add_argument("message")
    p_txt.add_argument("--x", type=int, default=0)
    p_txt.add_argument("--y", type=int, default=0)
    p_txt.add_argument("--color", default="FFFFFF", help="Hex RGB, e.g. FF8800")
    p_txt.add_argument("--size", type=int, default=1, help="Font scale factor")
    p_txt.add_argument("--wrap", action="store_true")
    p_txt.set_defaults(func=cmd_text)

    p_fill = sub.add_parser("fill", help="Fill the whole panel with a color")
    p_fill.add_argument("color", nargs="?", default="000000")
    p_fill.set_defaults(func=cmd_fill)

    sub.add_parser("clear", help="Blank the panel").set_defaults(func=cmd_clear)
    sub.add_parser("ping", help="Check the link").set_defaults(func=cmd_ping)

    return parser


def main():
    args = build_parser().parse_args()

    with serial.Serial(args.port, args.baud, timeout=1, write_timeout=30) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()

        if not args.no_clear and args.command in ("image", "text"):
            send_command(ser, "CLEAR")
            wait_for(ser, "OK")

        try:
            args.func(ser, args)
        except (RuntimeError, TimeoutError) as exc:
            print(exc, file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
