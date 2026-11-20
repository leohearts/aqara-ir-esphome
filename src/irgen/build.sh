#!/bin/sh
# Rebuild irgen from irgen.yaml for the Aqara P3 (MIPS32r2, static musl) and optionally upload it.
# Usage: ./build.sh            build only -> .esphome/build/irgen/mips/irgen
#        ./build.sh upload     build, then copy to the device's /data/esphome-ir/irgen over telnet+nc
# Env:   ESPHOME (default: esphome in PATH), DEVICE (default: AC-Controller-3630.lan)
set -e
cd "$(dirname "$0")"
ESPHOME=${ESPHOME:-esphome}
DEVICE=${DEVICE:-AC-Controller-3630.lan}

$ESPHOME compile --only-generate irgen.yaml
B=.esphome/build/irgen
mkdir -p $B/mips
ZIG="python3 -m ziglang c++ -target mipsel-linux-musleabi -mcpu=mips32r2 -msoft-float"
OBJS=""
for src in $(find $B/src -name '*.cpp'); do
  obj=$B/mips/$(echo $src | tr / _).o
  $ZIG -Os -std=gnu++20 -DUSE_HOST -fno-exceptions -ffunction-sections -fdata-sections -w -I$B/src -c $src -o $obj
  OBJS="$OBJS $obj"
done
$ZIG -static -Os -Wl,--gc-sections -s -o $B/mips/irgen $OBJS
md5sum $B/mips/irgen

[ "$1" = upload ] || exit 0
# Device side: listen with nc, then send the file; telnet login is admin with an empty password.
{ sleep 2; printf 'admin\r\n'; sleep 1; printf '\r\nnc -l -p 9092 > /data/esphome-ir/irgen.new &\r\n'; sleep 15; } \
  | ncat $DEVICE 23 >/dev/null &
sleep 6
ncat --send-only $DEVICE 9092 < $B/mips/irgen
sleep 3
{ sleep 2; printf 'admin\r\n'; sleep 1; printf '\r\nchmod +x /data/esphome-ir/irgen.new && mv /data/esphome-ir/irgen.new /data/esphome-ir/irgen && md5sum /data/esphome-ir/irgen\r\n'; sleep 3; } \
  | ncat $DEVICE 23 | grep -a '  /data/esphome-ir/irgen'
