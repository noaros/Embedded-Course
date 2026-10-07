#!/usr/bin/env python3
"""Lab 10 power-cut test: interrupt updates at random points and check the
device always comes back up running a valid, signed image.

Each round:
  1. ask the running app for an update and start sending an image
  2. after a random number of chunks (or randomly during the final verify),
     cut the power with --cut-cmd and restore it with --restore-cmd
  3. wait for the "APP v..." banner and record which slot/version came up

    python3 tools/powercut_test.py /dev/ttyACM0 \
        --images build/.../lab10_solution_app_B.signed.bin build/.../lab10_solution_app_A.signed.bin \
        --rounds 50 \
        --cut-cmd "uhubctl -l 1-1 -p 2 -a off" --restore-cmd "uhubctl -l 1-1 -p 2 -a on"

Real power cuts need a switchable supply: a USB hub with per-port power
switching (uhubctl), a relay, or a lab supply with a remote interface. If you
have none, --cut-cmd "STM32_Programmer_CLI -c port=swd mode=HOTPLUG -hardRst"
resets the MCU through NRST instead. That interrupts flash operations too, but
is weaker than a real brown-out: say which one you used in your report.

Pass = every round ends with an APP banner within --boot-timeout seconds.
"""
import argparse
import random
import re
import shlex
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import fw_update  # noqa: E402

BANNER = re.compile(rb"APP v([0-9.]+) .*slot ([AB])")


def run(cmd: str) -> None:
    subprocess.run(shlex.split(cmd), check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_banner(port, timeout: float):
    end = time.monotonic() + timeout
    buf = b""
    while time.monotonic() < end:
        buf += port.read(256)
        m = BANNER.search(buf)
        if m:
            return m.group(1).decode(), m.group(2).decode()
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--images", nargs="+", required=True,
                    help="signed images to alternate between (one per slot)")
    ap.add_argument("--rounds", type=int, default=50)
    ap.add_argument("--cut-cmd", required=True)
    ap.add_argument("--restore-cmd", default="")
    ap.add_argument("--off-time", type=float, default=0.5)
    ap.add_argument("--boot-timeout", type=float, default=15.0)
    ap.add_argument("--seed", type=int, default=None)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    images = [open(p, "rb").read() for p in args.images]
    results = []

    for rnd in range(1, args.rounds + 1):
        image = images[rnd % len(images)]
        nchunks = (len(image) + fw_update.CHUNK - 1) // fw_update.CHUNK
        cut_at = rng.randint(0, nchunks)  # nchunks = cut while the bootloader verifies
        port = serial.Serial(args.port, 115200, timeout=0.05)
        try:
            if cut_at < nchunks:
                fw_update.update(port, image, request=True, abort_after=cut_at, verbose=False)
            else:
                port.write(b"u")
                time.sleep(0.3)
                port.write(b"UPD" + len(image).to_bytes(4, "little"))
                time.sleep(rng.uniform(0.0, 3.0))
        finally:
            port.close()

        run(args.cut_cmd)
        time.sleep(args.off_time)
        if args.restore_cmd:
            run(args.restore_cmd)

        port = None
        for _ in range(50):  # the serial device reappears after USB power returns
            try:
                port = serial.Serial(args.port, 115200, timeout=0.1)
                break
            except serial.SerialException:
                time.sleep(0.2)
        if port is None:
            print(f"round {rnd}: serial port did not come back")
            results.append(False)
            continue
        with port:
            got = wait_banner(port, args.boot_timeout)
        ok = got is not None
        results.append(ok)
        print(f"round {rnd:3d}: cut after chunk {cut_at:4d}/{nchunks} -> "
              f"{'APP v%s slot %s' % got if ok else 'NO VALID IMAGE BOOTED'}")

    passed = sum(results)
    print(f"\n{passed}/{len(results)} rounds booted a valid image")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
