#!/bin/sh
# Start the web UI (port 80) and make sure mha_ir runs with the ESPHome plugin. Idempotent.
# Run it after every P3 reboot unless the patched rootfs is flashed (install.sh adds it to post_init.sh).
# `start.sh restart` also restarts both even when they already run (used after an update).
D=/data/esphome-ir
PLUG=$D/libirplug.so

running() { ps | grep "$1" | grep -v grep >/dev/null; }

[ "$1" = restart ] && killall tcpsvd-lite 2>/dev/null
running tcpsvd-lite || { $D/web/httpd.sh 80 >/dev/null 2>&1 & }

[ "$1" = restart ] && killall mha_ir 2>/dev/null && sleep 2
n=0
while [ $n -lt 4 ]; do
  running "mha_ir -r $PLUG" && break
  # app_monitor.sh restarts mha_ir by itself (stock args on unpatched firmware); replace that one.
  killall mha_ir 2>/dev/null && sleep 2
  running "mha_ir -r $PLUG" || { mha_ir -r $PLUG >/dev/null 2>&1 & }
  sleep 3
  n=$((n + 1))
done
running "mha_ir -r $PLUG" && echo "mha_ir runs with $PLUG" || { echo "mha_ir does not run with the plugin" >&2; exit 1; }
