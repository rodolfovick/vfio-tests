#!/usr/bin/env python3
#
# Offline VFIO BAR mmap alignment check from /proc/pid/maps lines.
# Usage: paste maps lines on stdin, or pass as a file argument.
#
# Example:
#   echo "ff78c0000000-ffa701f00000 rw-s 40000000000 00:06 1397 /dev/vfio/devices/vfio0" | ./vfio-check-alignment.py
#

import sys

GiB = 1 << 30
MiB = 1 << 20
KiB = 1 << 10

def fmt(s):
    if s >= GiB:
        return f'{s // GiB} GiB' if s % GiB == 0 else f'{s / GiB:.2f} GiB'
    if s >= MiB:
        return f'{s // MiB} MiB' if s % MiB == 0 else f'{s / MiB:.2f} MiB'
    if s >= KiB:
        return f'{s // KiB} KiB'
    return f'{s} B'

def ctz(v):
    n = 0
    while v and not (v & 1):
        n += 1
        v >>= 1
    return n

def pow2ceil(v):
    p = 1
    while p < v:
        p <<= 1
    return p

if len(sys.argv) > 1:
    f = open(sys.argv[1])
else:
    f = sys.stdin

for line in f:
    line = line.strip()
    if not line or ('vfio' not in line and '/dev/vfio' not in line):
        continue

    parts = line.split()
    range_str = parts[0]
    offset = int(parts[2], 16)
    backing = parts[-1]

    start_hex, end_hex = range_str.split('-')
    start = int(start_hex, 16)
    end = int(end_hex, 16)
    size = end - start
    region = offset >> 40
    align = 1 << ctz(start)
    expected = min(pow2ceil(size), GiB)

    if align >= expected:
        status = '✓'
    else:
        status = f'WARN: underaligned (expected {fmt(expected)})'

    print(f'  {start_hex}-{end_hex}  Region {region}  [size={fmt(size):>12s}]  aligned={fmt(align):<10s}  {status}')
    print(f'    backing: {backing}')
