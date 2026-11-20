#!/bin/sh
# Build tcpsvd-lite for the P3 (static MIPS32r2). Needs: pip install ziglang
set -e
cd "$(dirname "$0")"
python3 -m ziglang cc -target mipsel-linux-musleabi -mcpu=mips32r2 -msoft-float -static -Os -s \
  tcpsvd-lite.c -o tcpsvd-lite
ls -l tcpsvd-lite
