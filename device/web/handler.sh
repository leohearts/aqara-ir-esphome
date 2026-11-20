#!/bin/sh
# Handles one HTTP request: stdin is the request from nc, stdout the response (see httpd.sh).
# Heavy work (ESPHome codegen, C++ compile) happens in the browser; this only moves files.
D=/data/esphome-ir
MAGIC_HEX=3131383939393838313900   # "1189998819": persist.app.ir.* values are hex strings + 00

# No CORS headers: only the page this server serves may read /api/*. Every request that changes
# something is a PUT, which other sites cannot send without a CORS preflight this server never answers.
reply() { # status content-type body-file
  printf 'HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %s\r\nConnection: close\r\n\r\n' \
    "$1" "$2" "$(wc -c < "$3")"
  cat "$3"
}
OUT=/tmp/web.$$.out IN=/tmp/web.$$.in   # one handler process per connection (tcpsvd-lite)
trap 'rm -f $OUT $IN' EXIT
text() { printf '%s\n' "$2" > $OUT; reply "$1" text/plain $OUT; }

# Browsers open spare connections that never send a request; close them after 3 s.
read -t 3 -r method target proto || exit 0
target=${target%%\?*}   # routes ignore query strings (cache busters)
len=0
while read -t 3 -r line; do
  line=$(printf '%s' "$line" | tr -d '\r')
  [ -z "$line" ] && break
  case "$line" in [Cc]ontent-[Ll]ength:*) len=${line#*:}; len=${len# } ;; esac
done
body() { head -c "$len" > "$1"; }

case "$method $target" in
"GET /" | "GET /index.html")
  reply "200 OK" "text/html; charset=utf-8" $D/web/index.html ;;
"GET /api/status")
  {
    printf '{"r_id_hex":"%s","magic_hex":"%s",' "$(getprop persist.app.ir.r_id)" "$MAGIC_HEX"
    printf '"mha_ir":"%s",' "$(ps | grep 'mha_ir -r' | grep -v grep | sed 's/.*-r //')"
    printf '"irgen_md5":"%s",' "$(md5sum $D/irgen 2>/dev/null | cut -d' ' -f1)"
    printf '"ir_uart_tx":"%s"}\n' "$(grep '^2:' /proc/tty/driver/serial | sed 's/.*tx:\([0-9]*\).*/\1/')"
  } > $OUT
  reply "200 OK" application/json $OUT ;;
"GET /api/config")
  reply "200 OK" "text/yaml; charset=utf-8" $D/irgen.yaml ;;
"PUT /api/config")
  body $D/irgen.yaml.$$ && mv $D/irgen.yaml.$$ $D/irgen.yaml && text "200 OK" saved ;;
"PUT /api/irgen")
  # Accept the new binary only if it runs and prints timings for a test state.
  body $IN && chmod +x $IN
  n=$(ESPHOME_PREFDIR=/tmp AQARA_AC=0,0,26,0,0 $IN 2>/dev/null | tr ',' '\n' | grep -c '^[0-9]')
  if [ "$n" -gt 0 ] && [ $((n % 2)) -eq 0 ]; then
    cp $IN $D/irgen.$$ && mv $D/irgen.$$ $D/irgen
    text "200 OK" "installed, test frame has $n timings, md5 $(md5sum $D/irgen | cut -d' ' -f1)"
  else
    text "400 Bad Request" "rejected: test run printed $n timings"
  fi ;;
"PUT /api/use-esphome")
  setprop persist.app.ir.r_id $MAGIC_HEX; killall mha_ir; text "200 OK" "r_id set to 1189998819, mha_ir restarting" ;;
"PUT /api/use-stock")
  body $IN; setprop persist.app.ir.r_id "$(cat $IN)"; killall mha_ir
  text "200 OK" "r_id set to $(cat $IN), mha_ir restarting" ;;
*)
  text "404 Not Found" "no route: $method $target" ;;
esac
