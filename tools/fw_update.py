#!/usr/bin/env python3
"""Send a signed image to the Lab 10 bootloader over the ST-LINK virtual COM port.

    pip install pyserial
    python3 tools/fw_update.py /dev/ttyACM0 build/.../lab10_solution_app_B.signed.bin --request

--request first sends 'u' to the running application, which reboots into the
bootloader's update mode. Without it, hold B1 while pressing reset.

Exit status: 0 = image accepted ('V'), 1 = rejected or transfer failed.
Also importable: update(port, image_bytes, request=True, abort_after=None)
(used by tools/powercut_test.py).
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

CHUNK = 256


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def wait_for(port, wanted: bytes, timeout: float, echo=False) -> int:
    """Reads until one of `wanted` arrives; returns it, or -1 on timeout."""
    end = time.monotonic() + timeout
    line = b""
    while time.monotonic() < end:
        c = port.read(1)
        if not c:
            continue
        if c[0] in wanted:
            return c[0]
        if echo:
            line += c
            if c == b"\n":
                print("  <", line.decode("ascii", "replace").rstrip())
                line = b""
    return -1


def frame(seq: int, data: bytes) -> bytes:
    body = struct.pack("<HH", seq & 0xFFFF, len(data)) + data
    return b"\x55" + body + struct.pack("<H", crc16(body))


def update(port, image: bytes, request=True, abort_after=None, verbose=True) -> bool:
    """Runs one update. abort_after=N returns False after sending N chunks
    (used to simulate an interrupted transfer)."""
    port.reset_input_buffer()
    if request:
        port.write(b"u")
        time.sleep(0.3)
    port.write(b"UPD" + struct.pack("<I", len(image)))
    if verbose:
        print(f"erasing target slot ({len(image)} bytes to send)...")
    if wait_for(port, b"RX", 30.0, echo=verbose) != ord("R"):
        print("bootloader did not get ready")
        return False

    chunks = [image[i:i + CHUNK] for i in range(0, len(image), CHUNK)]
    for seq, data in enumerate(chunks):
        if abort_after is not None and seq >= abort_after:
            return False
        for _attempt in range(5):
            port.write(frame(seq, data))
            r = wait_for(port, b"ANX", 2.0)
            if r == ord("A"):
                break
            if r == ord("X"):
                print("bootloader aborted (flash error)")
                return False
        else:
            print(f"chunk {seq}: no ACK after 5 attempts")
            return False
        if verbose and seq % 64 == 0:
            print(f"  {seq * CHUNK:7d} / {len(image)}")

    port.write(frame(len(chunks), b""))
    r = wait_for(port, b"VX", 10.0, echo=verbose)
    if verbose:
        print("ACCEPTED" if r == ord("V") else "REJECTED")
    return r == ord("V")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("image")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--request", action="store_true", help="ask the running app to enter update mode")
    args = ap.parse_args()
    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        ok = update(port, open(args.image, "rb").read(), request=args.request)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
