#!/bin/sh
# Install the ESPHome IR encoder on the Aqara P3. Run on the P3 from the extracted release tarball.
# Idempotent. Does not touch firmware; irgen.yaml is only created when missing.
set -e
cd "$(dirname "$0")"
D=/data/esphome-ir
MAGIC_HEX=3131383939393838313900   # "1189998819" as hex + 00

mkdir -p $D/web
put() { # src dest: write a new file and move it over, so a loaded/running binary is never overwritten
  cp "$1" "$2.new" && chmod 755 "$2.new" && mv "$2.new" "$2"
}
put libirplug.so $D/libirplug.so
for f in handler.sh httpd.sh tcpsvd-lite; do put web/$f $D/web/$f; done
cp web/index.html $D/web/index.html
put start.sh $D/start.sh
[ -f $D/irgen.yaml ] || cp irgen.yaml $D/irgen.yaml

cur=$(getprop persist.app.ir.r_id)
if [ "$cur" != "$MAGIC_HEX" ] && [ ! -f $D/stock_r_id ]; then
  echo "$cur" > $D/stock_r_id   # hex string, restore with: setprop persist.app.ir.r_id $(cat stock_r_id)
  h=${cur%00}; id=
  while [ -n "$h" ]; do p=${h%${h#??}}; h=${h#??}; id=$id$(printf "\\$(printf %o 0x$p)"); done
  echo "saved stock remote id $id to $D/stock_r_id (hex); \"Use stock encoder\" in the web UI needs $id"
fi
setprop persist.app.ir.r_id $MAGIC_HEX || true   # exit status is not reliable here

# Patched firmware (rcS runs post_init.sh): start the web UI at boot too.
if grep -q '^if \[ -x \${CUSTOM_POST_INIT}' /etc/init.d/rcS 2>/dev/null; then
  mkdir -p /data/scripts
  [ -f /data/scripts/post_init.sh ] ||
    printf '#!/bin/sh\n\nfw_manager.sh -r\nfw_manager.sh -t -k\n' > /data/scripts/post_init.sh
  grep -q "$D/start.sh" /data/scripts/post_init.sh ||
    echo "$D/start.sh >/dev/null 2>&1 &" >> /data/scripts/post_init.sh
  chmod +x /data/scripts/post_init.sh
fi

$D/start.sh restart
ip=$(ifconfig wlan0 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p')
echo "Done. Open http://${ip:-<p3-ip>}/ in a browser to set up your AC."
