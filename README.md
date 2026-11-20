> Warning: Step 4 flashes a modified rootfs. That can brick your P3. Only flash an image whose md5 matches, and keep telnet working.

# aqara-ir-esphome

**Drive any AC from the Aqara P3 with an [ESPHome `climate_ir`](https://esphome.io/components/climate/climate_ir.html) encoder. Offline. HomeKit and Mi Home keep working.**

## Why

The Aqara Air Conditioning Companion P3 (KTBL12LM) is a nice IR blaster with power metering and HomeKit.
It is also a cloud product pretending to be a local one.

The P3 can only build IR codes on the device for 7 brands (Gree, Midea, Haier, Aux, Chigo, Panasonic,
Mitsubishi, see `protocol_ac_judge` in `libha_ir.so`), and only for a handful of remotes. Every other
remote lives in the vendor cloud: when you pick a remote, the P3 asks the cloud (resource `200.2.14`) for a
download URL and fetches the code file from there.

My P3 runs in LAN mode with telnet and no internet, because IoT devices don't get to go online here.
Result: no new remote can ever be paired. Even the built-in Gree encoder doesn't speak my Gree **YAP0F**
remote.

Meanwhile ESPHome already has open encoders for dozens of AC brands (YAP0F included, via
[ryanh7/esphome-custom-components](https://github.com/ryanh7/esphome-custom-components)). So this project
runs ESPHome *on the P3* and lets it do the one job the cloud was needed for.

## What it does

Everything stock stays: HomeKit, Mi Home, automations, power metering, the IR queue and module.
Only "AC state <-> IR timings" is replaced, and only when the remote id is the magic **1189998819**
(0118 999 881 9, cut to fit an int32). Any other id = stock behaviour. Both directions: the app sends
through ESPHome's encoder, and presses on the physical remote are decoded by ESPHome so the app shows
the real AC state.

- **`irgen`**: a normal ESPHome project built for the `host` platform, cross-compiled to static MIPS32r2.
  One `climate_ir` entity (your AC), plus `aqara_ir` (turns `$AQARA_AC` into a `ClimateCall`, or decodes
  `$AQARA_RX`), a `remote_transmitter` that prints timings to stdout instead of toggling a GPIO and a
  `remote_receiver` that feeds received timings to the climate's decoder.
- **`libirplug.so`**: loaded by `mha_ir -r` in place of `/lib/libha_ir.so` (and linked against it).
  Its `ha_ir_open` redirects libha_ir's GOT slots for `protocol_ac_product_ircode` (send) and
  `protocol_ac_sync` (receive) to itself, then calls the stock one. (uClibc 0.9.33 ignores `LD_PRELOAD`
  and MIPS binds local symbols, so plain interposition doesn't work.) The last received frame is kept in
  `/tmp/irplug.rx` for debugging.
- **Firmware patch** (`src/firmware/rootfs.patch`): makes `rcS` run `/data/scripts/post_init.sh` and
  makes `fw_manager.sh` / `app_monitor.sh` start `mha_ir` with the plugin. Optional; without it the
  plugin lasts until `mha_ir` restarts or the P3 reboots.

`AQARA_AC=P,M,T,S,D`: P power (1 = off), M mode (0 cool, 1 heat, 2 auto, 3 fan, 4 dry),
T target °C, S fan (0 auto, 1 low, 2 med, 3 high), D swing (0 = on).

## Requirements

- P3 on firmware **4.5.20_0033.0652**, Mi Home mode, telnet as `admin` (empty password), e.g. via
  [niceboygithub/AqaraM1SM2fw](https://github.com/niceboygithub/AqaraM1SM2fw).
- Linux x86_64 with Python 3.13, `fakeroot`, `squashfs-tools`, `ncat`.
- `irplug.c` offsets match `/lib/libha_ir.so` md5 `2b73769db0b7569344ea5ca58fd925cc`. Other firmware: see [Porting](#porting).

## How to use

### 1. Pick your AC and build

Edit `climate:` in `src/irgen/irgen.yaml` (keep `id: ac`, `transmitter_id: ir_out`, and
`receiver_id: ir_in` if the platform can decode its remote; drop that line if it can't):

```yaml
climate:
  - platform: coolix          # any climate_ir platform, or an external one like gree_ext
    id: ac
    name: AC
    transmitter_id: ir_out
    receiver_id: ir_in
```

`remote_transmitter: send_times: N` sends every command N times in total (default 1). Measured with a
microphone next to the AC (Gree YAP0F), the AC missed about 30-50% of single frames, regardless of
content; with `send_times: 2`, 29 of 40 commands got through. The AC beeps once per copy it catches. A
command must fit in 998 timings (`send_times` × frame length; a Gree frame is 280, so at most 3).

```sh
git clone https://github.com/leohearts/aqara-ir-esphome && cd aqara-ir-esphome
python3.13 -m venv venv && venv/bin/pip install esphome ziglang
export PATH=$PWD/venv/bin:$PATH
./src/irgen/build.sh          # -> src/irgen/.esphome/build/irgen/mips/irgen
# copy /lib/libha_ir.so from the P3 into src/irplug/ first
./src/irplug/build.sh         # -> src/irplug/libirplug.so
```

### 2. Copy to the P3

```sh
# P3
mkdir -p /data/esphome-ir && nc -l -p 9092 > /data/esphome-ir/irgen
# PC
ncat --send-only <p3-ip> 9092 < src/irgen/.esphome/build/irgen/mips/irgen
# same for libirplug.so, then on the P3:
chmod +x /data/esphome-ir/irgen
ESPHOME_PREFDIR=/tmp AQARA_AC=0,1,26,0,0 /data/esphome-ir/irgen   # prints timings
```

(`DEVICE=<p3-ip> src/irgen/build.sh upload` does the irgen part.) Changing the AC model later = rebuild
and copy `irgen`; `mha_ir` runs it per command, no restart needed.

### 3. Switch to the ESPHome encoder

```sh
getprop persist.app.ir.r_id                          # remember this to switch back
setprop persist.app.ir.r_id 3131383939393838313900   # "1189998819" as hex + 00
killall mha_ir; mha_ir -r /data/esphome-ir/libirplug.so >/dev/null 2>&1 &
```

Use the app as usual. The `tx:` counter in `grep '^2:' /proc/tty/driver/serial` goes up when a frame is sent.

Switch back: `setprop persist.app.ir.r_id <old value>` (or `rm /data/esphome-ir/libirplug.so`), then `killall mha_ir`.

### 4. (Optional) Survive reboots

<details><summary>Build and flash the patched rootfs</summary>

Base: niceboygithub `modified/P3/4.5.20_0033.0652/rootfs_4.5.20_0033.0652_modified.bin`
(md5 `a209d9b3d253b75afa87d01437230911`).

```sh
tail -c +17 rootfs_4.5.20_0033.0652_modified.bin > mod.sqfs
fakeroot -s fr.state unsquashfs -d root mod.sqfs            # fakeroot keeps /dev nodes
(cd root && patch -p1 < ../src/firmware/rootfs.patch)
fakeroot -i fr.state mksquashfs root new.sqfs -comp xz -b 131072 -noappend
python3 src/firmware/mkimg.py new.sqfs rootfs_ir.bin        # r6cr header + checksum
```

Expect md5 `1bae852b5cd57b2388507fafefa60ab0`. (Only a 16-bit checksum, no signature; `root_sum_check: off`.)

With the patch `rcS` runs `post_init.sh` **instead of** `fw_manager.sh -r`, so create it first:

```sh
printf '#!/bin/sh\n\nfw_manager.sh -r\nfw_manager.sh -t -k\n' > /data/scripts/post_init.sh
chmod +x /data/scripts/post_init.sh
```

Copy the image to `/tmp` (RAM; `/data` is too small) and flash:

```sh
fw_update /tmp/rootfs_ir.bin    # ends with "crc ok" / "Success"
boot_ctrl show                  # inactive slot now has the new size and is selected
reboot
```

`fw_update` writes the inactive slot; the old system stays in the other one. An official OTA wipes the patch.

</details>

### 5. (Optional) Web UI

Edit `irgen.yaml`, rebuild and install `irgen` from a browser, switch encoders. The P3 only serves a
tiny page and moves files; ESPHome codegen (Pyodide) and the MIPS compile (Clang/LLD in WebAssembly, see
[toolchain/](toolchain/README.md)) run in your browser, loaded from GitHub Pages. The P3 stays offline.
A build takes about 90 s; the first one downloads ~150 MB.

```sh
./device/web/build.sh                       # -> device/web/tcpsvd-lite (busybox has no httpd/inetd)
# copy device/web/{handler.sh,httpd.sh,index.html,tcpsvd-lite} to /data/esphome-ir/web/
# and src/irgen/irgen.yaml to /data/esphome-ir/irgen.yaml, then on the P3:
chmod +x /data/esphome-ir/web/*.sh /data/esphome-ir/web/tcpsvd-lite
/data/esphome-ir/web/httpd.sh 80 >/dev/null 2>&1 &   # also add this line to post_init.sh
```

Open `http://<p3-ip>/`. An uploaded `irgen` is only installed if a test run prints IR timings.

## Porting

<details><summary>Offsets for another firmware's <code>libha_ir.so</code></summary>

- `OFF_SYNC`, `OFF_OPEN`: `st_value` of `protocol_ac_sync` and `ha_ir_open` in `.dynsym`.
- `OFF_GOT_PRODUCT`: `DT_PLTGOT + 4 * (DT_MIPS_LOCAL_GOTNO + symidx - DT_MIPS_GOTSYM)`, `symidx` = `.dynsym`
  index of `protocol_ac_product_ircode` (must be `>= DT_MIPS_GOTSYM`).
- Check `protocol_ac_product_ircode` still takes the AC state as 4th arg with the same layout
  (power +0, mode +4, temperature +9, fan +12, swing +16).

</details>

## Notes

- Don't overwrite `libirplug.so` while `mha_ir` has it loaded (SIGBUS). Stop `mha_ir` first.
- Inside `mha_ir`, `pclose()` doesn't return 0 even on success (probably ignored SIGCHLD, not verified),
  so the plugin trusts the parsed output instead.
- `persist.app.ir.*` values are hex strings with a trailing `00`.
- Local IPC: `/tmp/miio_agent.socket` (SOCK_SEQPACKET), bind with `{"method":"bind","address":N}`; `mha_ir` is 512, e.g.
  `{"id":1,"_to":512,"method":"local.ir_write","params":[{"did":"<did>","prop":"on_off_status","value":"1"}]}`.
- One frame, or one 998-timing command, goes to the IR module as a single UART packet:
  `FF FE 00 <cmd> <len hi> <len lo> <u16 timings> <crc16-x25>`. No carrier field; the module picks it.

## Credits

[ESPHome](https://esphome.io), [ryanh7/esphome-custom-components](https://github.com/ryanh7/esphome-custom-components) (`gree_ext`),
[niceboygithub/AqaraM1SM2fw](https://github.com/niceboygithub/AqaraM1SM2fw) (firmware images and tools).

MIT, see [LICENSE](LICENSE). Not affiliated with Aqara, Lumi or Xiaomi.
