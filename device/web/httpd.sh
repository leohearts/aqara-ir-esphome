#!/bin/sh
# HTTP server for the P3 web UI: tcpsvd-lite runs handler.sh once per connection
# (busybox here has no httpd/tcpsvd/inetd).
W=/data/esphome-ir/web
exec $W/tcpsvd-lite "${1:-80}" $W/handler.sh
