#!/usr/bin/env python3
"""Flash/RAM size report for firmware ELFs, with deltas against a baseline.

    # record sizes
    size_report.py --elf build/**/*.elf --json sizes.json
    # compare with a baseline (e.g. the target branch's sizes.json) and print Markdown
    size_report.py --elf build/**/*.elf --baseline base/sizes.json --markdown >> "$GITHUB_STEP_SUMMARY"

Flash = text + data (data is stored in flash and copied to RAM).
RAM   = data + bss (NOLOAD sections such as the stack and heap count as bss).
Exit status 1 if --max-flash-growth is given and any image grew by more bytes.
"""
import argparse
import glob
import json
import os
import subprocess
import sys

SIZE = os.environ.get("ARM_SIZE", "arm-none-eabi-size")


def measure(elf: str) -> dict:
    out = subprocess.run([SIZE, "--format=berkeley", elf], check=True, capture_output=True,
                         text=True).stdout.splitlines()[1].split()
    text, data, bss = int(out[0]), int(out[1]), int(out[2])
    return {"flash": text + data, "ram": data + bss}


def fmt_delta(new: int, old) -> str:
    if old is None:
        return "new"
    d = new - old
    return "±0" if d == 0 else f"{d:+d}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", nargs="+", required=True, help="ELF files or glob patterns")
    ap.add_argument("--json", help="write the measured sizes here")
    ap.add_argument("--baseline", help="sizes.json from the comparison build")
    ap.add_argument("--markdown", action="store_true", help="print a Markdown table")
    ap.add_argument("--max-flash-growth", type=int, help="fail if any image's flash grows more")
    args = ap.parse_args()

    files = sorted({f for pattern in args.elf for f in glob.glob(pattern, recursive=True)})
    if not files:
        print("no ELF files matched", file=sys.stderr)
        return 1
    sizes = {os.path.basename(f): measure(f) for f in files}
    base = json.load(open(args.baseline)) if args.baseline and os.path.exists(args.baseline) else {}

    if args.json:
        with open(args.json, "w") as f:
            json.dump(sizes, f, indent=2, sort_keys=True)

    failed = False
    if args.markdown:
        print("| Image | Flash (B) | Δ | RAM (B) | Δ |")
        print("| --- | ---: | ---: | ---: | ---: |")
    for name, s in sizes.items():
        old = base.get(name, {})
        if args.markdown:
            print(f"| {name} | {s['flash']} | {fmt_delta(s['flash'], old.get('flash'))} "
                  f"| {s['ram']} | {fmt_delta(s['ram'], old.get('ram'))} |")
        if args.max_flash_growth is not None and old and s["flash"] - old["flash"] > args.max_flash_growth:
            failed = True
            print(f"{name}: flash grew by {s['flash'] - old['flash']} bytes", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
