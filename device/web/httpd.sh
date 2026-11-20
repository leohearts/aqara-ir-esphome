#!/bin/sh
# Minimal HTTP server for the P3: busybox nc serves one connection per loop iteration,
# handler.sh reads the request from nc and writes the response back through a fifo.
PORT=${1:-80}
W=/data/esphome-ir/web
F=/tmp/esphome-ir-web.fifo
rm -f $F && mkfifo $F
while :; do
  nc -l -p "$PORT" < $F | $W/handler.sh > $F
done
