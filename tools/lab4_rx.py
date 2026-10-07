#!/usr/bin/env python3
"""Host receiver for the Lab 4 data logger.

Sends `start`, then parses the framed stream and checks it:
  - every frame's CRC-16/CCITT-FALSE
  - sample-frame sequence numbers have no gaps (no dropped data)
and prints the logger's status text as it arrives.

    pip install pyserial
    python3 tools/lab4_rx.py /dev/ttyACM0 --seconds 3600 --csv samples.csv

Exit status is 0 only if no frame was lost or corrupted.
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

SYNC = 0xA5
T_SAMPLES = 0x01
T_TEXT = 0x02


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def read_exact(port, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = port.read(n - len(buf))
        if not chunk:
            raise TimeoutError
        buf += chunk
    return buf


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=3_000_000)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--csv", help="write filtered samples to this file")
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=1.0)
    port.reset_input_buffer()
    port.write(b"start\n")

    csv = open(args.csv, "w", encoding="ascii") if args.csv else None
    expected_seq = None
    frames = gaps = lost = crc_errors = resyncs = 0
    end = time.monotonic() + args.seconds

    try:
        while time.monotonic() < end:
            b = port.read(1)
            if not b:
                continue
            if b[0] != SYNC:
                resyncs += 1
                continue
            hdr = b + read_exact(port, 5)
            _, ftype, seq, length = struct.unpack("<BBHH", hdr)
            if length > 512:
                resyncs += 1
                continue
            body = read_exact(port, length + 2)
            payload, (crc,) = body[:length], struct.unpack("<H", body[length:])
            if crc16_ccitt(hdr + payload) != crc:
                crc_errors += 1
                continue

            if ftype == T_TEXT:
                print(f"[logger] {payload.decode('ascii', 'replace')}")
            elif ftype == T_SAMPLES:
                frames += 1
                if expected_seq is not None and seq != expected_seq:
                    missing = (seq - expected_seq) & 0xFFFF
                    gaps += 1
                    lost += missing
                    print(f"GAP: expected seq {expected_seq}, got {seq} ({missing} frames lost)")
                expected_seq = (seq + 1) & 0xFFFF
                if csv:
                    samples = struct.unpack(f"<{length // 2}h", payload)
                    csv.write("\n".join(str(s) for s in samples) + "\n")
    except TimeoutError:
        print("timeout waiting for data")
    finally:
        port.write(b"stop\n")
        if csv:
            csv.close()

    print(f"\nsample frames {frames}, gaps {gaps}, frames lost {lost}, "
          f"CRC errors {crc_errors}, resync bytes {resyncs}")
    return 0 if frames > 0 and gaps == 0 and crc_errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
