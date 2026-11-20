#!/usr/bin/env python3
"""Wrap a squashfs into the Realtek 'r6cr' image that fw_update on the Aqara P3 accepts.
Usage: mkimg.py new.sqfs rootfs_out.bin
Layout copied from the vendor/community rootfs images: 16-byte big-endian header
(magic, 0x2d0000, 0xe00000, payload length), payload = squashfs zero-padded to 4 KiB + 0x802 zero
bytes + 2-byte value making the big-endian 16-bit word sum of the payload zero."""
import struct
import sys

sq = open(sys.argv[1], "rb").read()
body = sq + b"\0" * ((-len(sq)) % 4096) + b"\0" * 0x802
s = sum(struct.unpack(">%dH" % (len(body) // 2), body)) & 0xFFFF
body += struct.pack(">H", (-s) & 0xFFFF)
open(sys.argv[2], "wb").write(struct.pack(">4sIII", b"r6cr", 0x2D0000, 0xE00000, len(body)) + body)
print("payload", len(body), "bytes; must stay below the 16 MiB rootfs partition")
