#!/bin/sh
# Build libirplug.so (the mha_ir wrapper module) for the Aqara P3.
# Needs: python3 -m ziglang (pip install ziglang). The proprietary /lib/libha_ir.so is NOT needed:
# libirplug.so only records DT_NEEDED libha_ir.so and imports protocol_ac_sync, so we link against
# an empty stand-in with that soname and symbol.
set -e
cd "$(dirname "$0")"
CC="python3 -m ziglang cc -target mipsel-linux-musleabi -mcpu=mips32r2 -msoft-float -fPIC -nostdlib -shared -Os"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
# Empty stand-ins: only provide soname and symbol names; the real ones bind on the device.
echo 'void protocol_ac_sync(){}' > "$T/ha_ir.c"
$CC -w -Wl,-soname,libha_ir.so "$T/ha_ir.c" -o "$T/libha_ir.so"
echo 'void popen(){} void pclose(){} void fgetc(){} void snprintf(){}' > "$T/libc.c"
$CC -w -Wl,-soname,libc.so.0 "$T/libc.c" -o "$T/libc.so.0"
$CC -ffreestanding -fno-stack-protector -Wl,-soname,libirplug.so irplug.c \
  -Wl,--no-as-needed "$T/libha_ir.so" "$T/libc.so.0" -s -o libirplug.so
md5sum libirplug.so
