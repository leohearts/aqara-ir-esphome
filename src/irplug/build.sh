#!/bin/sh
# Build libirplug.so (the mha_ir wrapper module) for the Aqara P3.
# Needs: python3 -m ziglang (pip install --user ziglang) and libha_ir.so copied from the device's /lib
# into this directory (it is only linked against for DT_NEEDED, never shipped).
set -e
cd "$(dirname "$0")"
[ -f libha_ir.so ] || { echo "copy /lib/libha_ir.so from the device here first"; exit 1; }
[ "$(md5sum < libha_ir.so | cut -d' ' -f1)" = 2b73769db0b7569344ea5ca58fd925cc ] ||
  echo "WARNING: libha_ir.so differs from the one the OFF_* offsets in irplug.c were taken from"
CC="python3 -m ziglang cc -target mipsel-linux-musleabi -mcpu=mips32r2 -msoft-float -fPIC -nostdlib -shared -Os"
# Empty stand-in for uClibc's libc.so.0: only provides the symbol names; the real ones bind on the device.
echo 'void popen(){} void pclose(){} void fgetc(){} void snprintf(){}' > stub_libc.c
$CC -w -Wl,-soname,libc.so.0 stub_libc.c -o libc.so.0
$CC -ffreestanding -fno-stack-protector -Wl,-soname,libirplug.so irplug.c \
  -Wl,--no-as-needed ./libha_ir.so ./libc.so.0 -s -o libirplug.so
rm -f stub_libc.c libc.so.0
md5sum libirplug.so
