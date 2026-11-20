#!/bin/sh
# Handles one HTTP request: stdin is the request from nc, stdout the response (see httpd.sh).
# Heavy work (ESPHome codegen, C++ compile) happens in the browser; this only moves files.
D=/data/esphome-ir
MAGIC_HEX=3131383939393838313900   # "1189998819": persist.app.ir.* values are hex strings + 00

reply() { # status content-type body-file
  printf 'HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %s\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n' \
    "$1" "$2" "$(wc -c < "$3")"
  cat "$3"
}
text() { printf '%s\n' "$2" > /tmp/web.out; reply "$1" text/plain /tmp/web.out; }

read -r method target proto
len=0
while read -r line; do
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
  } > /tmp/web.out
  reply "200 OK" application/json /tmp/web.out ;;
"GET /api/config")
  reply "200 OK" "text/yaml; charset=utf-8" $D/irgen.yaml ;;
"PUT /api/config")
  body $D/irgen.yaml.new && mv $D/irgen.yaml.new $D/irgen.yaml && text "200 OK" saved ;;
"PUT /api/irgen")
  # Accept the new binary only if it runs and prints timings for a test state.
  body /tmp/irgen.new && chmod +x /tmp/irgen.new
  n=$(ESPHOME_PREFDIR=/tmp AQARA_AC=0,0,26,0,0 /tmp/irgen.new 2>/dev/null | tr ',' '\n' | grep -c '^[0-9]')
  if [ "$n" -gt 0 ] && [ $((n % 2)) -eq 0 ]; then
    cp /tmp/irgen.new $D/irgen.tmp && mv $D/irgen.tmp $D/irgen && rm /tmp/irgen.new
    text "200 OK" "installed, test frame has $n timings, md5 $(md5sum $D/irgen | cut -d' ' -f1)"
  else
    rm -f /tmp/irgen.new; text "400 Bad Request" "rejected: test run printed $n timings"
  fi ;;
"POST /api/use-esphome")
  setprop persist.app.ir.r_id $MAGIC_HEX; killall mha_ir; text "200 OK" "r_id set to 1189998819, mha_ir restarting" ;;
"POST /api/use-stock")
  body /tmp/web.in; setprop persist.app.ir.r_id "$(cat /tmp/web.in)"; killall mha_ir
  text "200 OK" "r_id set to $(cat /tmp/web.in), mha_ir restarting" ;;
"OPTIONS "*)
  printf 'HTTP/1.0 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, PUT, POST\r\nConnection: close\r\n\r\n' ;;
*)
  text "404 Not Found" "no route: $method $target" ;;
esac
