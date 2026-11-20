#!/bin/sh
# Build the release tarball aqara-ir-esphome-p3.tar.gz in the repository root (needs python3 -m ziglang).
set -e
cd "$(dirname "$0")/.."
sh src/irplug/build.sh
sh device/web/build.sh
T=$(mktemp -d); P=$T/aqara-ir-esphome
mkdir -p $P/web
cp device/install.sh device/start.sh src/irgen/irgen.yaml src/irplug/libirplug.so $P/
cp device/web/handler.sh device/web/httpd.sh device/web/index.html device/web/tcpsvd-lite $P/web/
tar czf aqara-ir-esphome-p3.tar.gz -C $T aqara-ir-esphome
rm -rf $T
ls -l aqara-ir-esphome-p3.tar.gz
