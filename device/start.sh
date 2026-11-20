#!/bin/sh
# Start the web UI (port 80) and make sure mha_ir runs with the ESPHome plugin. Idempotent.
# Run it after every P3 reboot unless the patched rootfs is flashed (install.sh adds it to post_init.sh).
# `start.sh restart` also restarts both even when they already run (used after an update).
# If mha_ir does not come up with the plugin, falls back to the stock encoder so the AC stays usable.
D=/data/esphome-ir
PLUG=${PLUG:-$D/libirplug.so}
STOCK_LIB=/lib/libha_ir.so

running() { ps | grep "$1" | grep -v grep >/dev/null; }
# mha_ir keeps running even when -r names a library it cannot load: check that the plugin is mapped.
plugin_up() {
  for pid in $(ps | grep "mha_ir -r $PLUG" | grep -v grep | awk '{print $1}'); do
    grep -q "$PLUG" /proc/$pid/maps 2>/dev/null && return 0
  done
  return 1
}

[ "$1" = restart ] && killall tcpsvd-lite 2>/dev/null
running tcpsvd-lite || { $D/web/httpd.sh 80 >/dev/null 2>&1 & }

[ "$1" = restart ] && killall mha_ir 2>/dev/null && sleep 2
n=0
while [ $n -lt 4 ]; do
  plugin_up && break
  # app_monitor.sh restarts mha_ir by itself (stock args on unpatched firmware); replace that one.
  killall mha_ir 2>/dev/null && sleep 2
  running "mha_ir -r $PLUG" || { mha_ir -r $PLUG >/dev/null 2>&1 & }
  sleep 3
  n=$((n + 1))
done
plugin_up && { echo "mha_ir runs with $PLUG"; exit 0; }

echo "mha_ir does not run with the plugin; falling back to the stock encoder" >&2
if [ -f $D/stock_r_id ]; then
  setprop persist.app.ir.r_id "$(cat $D/stock_r_id)"
  echo "remote id restored from $D/stock_r_id" >&2
else
  echo "no $D/stock_r_id: pick your remote again in the Aqara app, or use \"Use stock encoder\"" >&2
fi
killall mha_ir 2>/dev/null; sleep 2
running "mha_ir -r $STOCK_LIB" || { mha_ir -r $STOCK_LIB >/dev/null 2>&1 & }
exit 1
