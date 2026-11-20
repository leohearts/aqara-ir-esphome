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

ESPHome already has open encoders for dozens of AC brands (YAP0F included, via
[ryanh7/esphome-custom-components](https://github.com/ryanh7/esphome-custom-components)). So this project
runs ESPHome *on the P3* and lets it do the one job the cloud was needed for. Everything stock stays:
HomeKit, Mi Home, automations, power metering. Both directions work: the app sends through ESPHome's
encoder, and presses on the physical remote are decoded by ESPHome so the app shows the real AC state.

## Requirements

- Aqara P3 (KTBL12LM) on the community firmware **4.5.20_0033.0652** ([niceboygithub/AqaraM1SM2fw](https://github.com/niceboygithub/AqaraM1SM2fw)), in Mi Home mode, with telnet (`admin`, empty password). The offsets in `irplug.c` match `/lib/libha_ir.so` md5 `2b73769db0b7569344ea5ca58fd925cc`; for other firmware see [Contributing](#contributing).
- A PC on the same network with `curl`, `telnet` and `ncat` (or `nc`).
- A browser to build for your AC: a current Chromium-based browser or Firefox. No Python or compiler is needed.
- Only for flashing the optional firmware image (Install, step 1): `fakeroot`, `squashfs-tools`, `python3`, `curl`, `patch`.

## Install

**Step 1: plugin-capable firmware (optional, risky).** The stock firmware runs nothing from `/data` at boot; this image starts the plugin and the web UI by itself after every reboot. A failed flash or a wrong image can leave the P3 unbootable. **Skip it** if your firmware already runs `/data/scripts/post_init.sh` at boot, or if you will run `/data/esphome-ir/start.sh` after each reboot.

```sh
# PC: download the community image, patch it, build rootfs_ir.bin
git clone https://github.com/leohearts/aqara-ir-esphome && cd aqara-ir-esphome
sh src/firmware/make-rootfs.sh

# P3 (telnet <p3-ip>, login admin, empty password): wait for the file; /tmp is RAM, /data is too small
nc -l -p 9092 > /tmp/rootfs_ir.bin
# PC, in a second terminal:
ncat --send-only <p3-ip> 9092 < rootfs_ir.bin
# P3: flash into the inactive slot, then reboot
md5sum /tmp/rootfs_ir.bin    # compare with the PC
fw_update /tmp/rootfs_ir.bin && reboot
```

**Step 2: the encoder and the web UI.** The release tarball is 16 KB. Send it to the P3 and run `install.sh`; it is safe to run again.

```sh
# PC
curl -LO https://github.com/leohearts/aqara-ir-esphome/releases/latest/download/aqara-ir-esphome-p3.tar.gz

# P3 (telnet <p3-ip>): wait for the file
nc -l -p 9092 > /tmp/p3.tgz
# PC, in a second terminal:
ncat --send-only <p3-ip> 9092 < aqara-ir-esphome-p3.tar.gz
# P3
cd /tmp && tar xzf p3.tgz && sh aqara-ir-esphome/install.sh
```

`install.sh` copies everything to `/data/esphome-ir/`, keeps your `irgen.yaml` if there is one, saves the current remote id to `/data/esphome-ir/stock_r_id` (and prints it, you need it for "Use stock encoder"), switches to the ESPHome encoder, starts the plugin and the web UI, and prints the address to open.

If you skipped Step 1, the plugin and the web UI stop when the P3 reboots. Start them again over telnet:

```sh
/data/esphome-ir/start.sh
```

## Usage

1. Open `http://<p3-ip>/`. The page is served by the P3 and shows the current encoder.
2. Edit the YAML (it is `/data/esphome-ir/irgen.yaml`). The default is a Gree YAP0F remote:

   ```yaml
   esphome:
     name: irgen

   host:

   external_components:
     - source: github://leohearts/aqara-ir-esphome
       components: [remote_transmitter, remote_receiver, aqara_ir]
     - source: github://ryanh7/esphome-custom-components
       components: [gree_ext]

   remote_transmitter:
     id: ir_out
     send_times: 2          # copies per command, see below

   remote_receiver:
     id: ir_in

   climate:
     - platform: gree_ext
       id: ac
       name: AC
       model: yap0f
       transmitter_id: ir_out
       receiver_id: ir_in   # decode the physical remote (optional)
       supports_horizontal_swing: false

   aqara_ir:
     climate_id: ac
     receiver_id: ir_in
   ```

   For another brand, use any [`climate_ir` platform](https://esphome.io/components/climate/climate_ir.html)
   and drop the `gree_ext` entry from `external_components`. Keep `id: ac`, `transmitter_id: ir_out`, and
   `receiver_id: ir_in` (drop that line if the platform can't decode its own remote):

   ```yaml
   climate:
     - platform: coolix      # or daikin, ...
       id: ac
       name: AC
       transmitter_id: ir_out
       receiver_id: ir_in
   ```

   `send_times: N` sends every command N times in total (default 1). The AC beeps once per copy it catches.
   A command must fit in 998 timings (`send_times` x frame length; a Gree frame is 280, so at most 3).
3. Click **Build in browser and install**. ESPHome codegen and the MIPS compile run in your browser; the P3 only
   receives the result and accepts it if a test run prints IR timings. The first build takes about
   90-190 s and downloads ~150 MB; later builds take 20-60 s.
4. Make sure the encoder says **ESPHome**. If not, click **Use ESPHome encoder**. **Use stock encoder** switches
   back (it needs the old remote id that `install.sh` printed; it is prefilled when the P3 runs the stock encoder).
5. Test: change mode, temperature or fan in Mi Home or Apple Home. The `ir_uart_tx` counter in the status panel goes up per
   sent frame. Press a button on the physical remote: the app follows. The last received frame is in
   `/tmp/irplug.rx` on the P3.

The browser keeps all downloads and compiled objects, so after one build the page also works offline.
"Clear build cache (N MB)" empties it.

## Contributing

Layout:

- `components/`: ESPHome external components, loaded by any config with `source: github://leohearts/aqara-ir-esphome`.
  `aqara_ir` turns `$AQARA_AC` into a `ClimateCall` or decodes `$AQARA_RX`; `remote_transmitter` prints timings to stdout
  instead of toggling a GPIO; `remote_receiver` feeds received timings to the climate's decoder.
- `src/irgen/`: `irgen.yaml` (default config) and `build.sh`, a native build of the ESPHome `host` project to static MIPS32r2.
- `src/irplug/`: `irplug.c`, `libirplug.so` source. Loaded by `mha_ir -r` in place of `/lib/libha_ir.so`; its `ha_ir_open`
  redirects libha_ir's GOT slots for `protocol_ac_product_ircode` (send) and `protocol_ac_sync` (receive) to itself, then
  calls the stock one. (uClibc 0.9.33 ignores `LD_PRELOAD` and MIPS binds local symbols, so plain interposition doesn't work.)
- `device/`: `install.sh`, `start.sh`, `pack.sh` (release tarball); `device/web/`: `tcpsvd-lite.c` (busybox has no httpd/inetd),
  `httpd.sh`, `handler.sh` (API), `index.html`.
- `docs/`: the web UI on GitHub Pages ([leohearts.github.io/aqara-ir-esphome](https://leohearts.github.io/aqara-ir-esphome/)).
  Toolchain: see [toolchain/README.md](toolchain/README.md).
- `src/firmware/`: `rootfs.patch`, `mkimg.py`, `make-rootfs.sh`.
- `.github/workflows/`: `pages.yml` (web UI), `release.yml` (tarball on `v*` tags).

Native build (Linux x86_64, Python 3.13):

```sh
python3.13 -m venv venv && venv/bin/pip install esphome ziglang
export PATH=$PWD/venv/bin:$PATH
./src/irgen/build.sh                 # -> src/irgen/.esphome/build/irgen/mips/irgen; DEVICE=<p3-ip> ... upload sends it
./src/irplug/build.sh                # -> src/irplug/libirplug.so (links a stub libha_ir.so, the real one is not needed)
./device/pack.sh                     # -> aqara-ir-esphome-p3.tar.gz
node test/build-node.mjs <config.yaml> <out-binary> [clang bundle.js]   # the web build under node (needs pyodide in node_modules)
```

Releases: push a `v*` tag; `release.yml` builds and attaches `aqara-ir-esphome-p3.tar.gz`.

Porting to another `libha_ir.so`: set the offsets in `irplug.c`:

- `OFF_SYNC`, `OFF_OPEN`: `st_value` of `protocol_ac_sync` and `ha_ir_open` in `.dynsym`.
- `OFF_GOT_PRODUCT`: `DT_PLTGOT + 4 * (DT_MIPS_LOCAL_GOTNO + symidx - DT_MIPS_GOTSYM)`, `symidx` = `.dynsym`
  index of `protocol_ac_product_ircode` (must be `>= DT_MIPS_GOTSYM`).
- Check `protocol_ac_product_ircode` still takes the AC state as 4th arg with the same layout
  (power +0, mode +4, temperature +9, fan +12, swing +16).

## Notes

- The ESPHome encoder is used only when `persist.app.ir.r_id` is the magic **1189998819** (0118 999 881 9, cut to fit an int32);
  any other id is stock behaviour. `persist.app.ir.*` values are hex strings with a trailing `00`:
  `setprop persist.app.ir.r_id 3131383939393838313900`.
- `AQARA_AC=P,M,T,S,D`: P power (1 = off), M mode (0 cool, 1 heat, 2 auto, 3 fan, 4 dry), T target °C,
  S fan (0 auto, 1 low, 2 med, 3 high), D swing (0 = on). Test on the P3:
  `ESPHOME_PREFDIR=/tmp AQARA_AC=0,1,26,0,0 /data/esphome-ir/irgen` prints timings.
- Measured with a microphone next to the AC (Gree YAP0F): it missed about 30-50% of single frames, regardless of
  content; with `send_times: 2`, 29 of 40 commands got through.
- Don't overwrite `libirplug.so` while `mha_ir` has it loaded (SIGBUS). `install.sh` writes a new file and moves it over; stop `mha_ir` first for manual copies.
- `app_monitor.sh` restarts `mha_ir` when it dies (with the stock library on unpatched firmware). `start.sh` replaces that process; if two
  start at once, one logs "Home Automation IR is running, exit now!!!".
- The patched rootfs makes `rcS` run `post_init.sh` **instead of** `fw_manager.sh -r`, and `fw_manager.sh` / `app_monitor.sh` start
  `mha_ir` with `/data/esphome-ir/libirplug.so` when present. `install.sh` creates `post_init.sh` (with `fw_manager.sh -r` and `-t -k`)
  if missing and appends the `start.sh` line. `make-rootfs.sh` builds a reproducible image (md5 `592fd665937cf26b9bdc812c1ad48424` with squashfs-tools 4.7.4)
  from niceboygithub's `rootfs_4.5.20_0033.0652_modified.bin` (md5 `a209d9b3d253b75afa87d01437230911`). The format has only a 16-bit checksum, no signature (`root_sum_check: off`).
  `fw_update` writes the inactive slot and selects it; `boot_ctrl show` lists the slots, and the old system stays in the other one.
  An official OTA wipes the patch.
- Switching encoders back: `setprop persist.app.ir.r_id $(cat /data/esphome-ir/stock_r_id)`, then `killall mha_ir`.
- Inside `mha_ir`, `pclose()` doesn't return 0 even on success (probably ignored SIGCHLD, not verified), so the plugin trusts the parsed output instead.
- Local IPC: `/tmp/miio_agent.socket` (SOCK_SEQPACKET), bind with `{"method":"bind","address":N}`; `mha_ir` is 512, e.g.
  `{"id":1,"_to":512,"method":"local.ir_write","params":[{"did":"<did>","prop":"on_off_status","value":"1"}]}`.
- One frame, or one 998-timing command, goes to the IR module as a single UART packet:
  `FF FE 00 <cmd> <len hi> <len lo> <u16 timings> <crc16-x25>`. No carrier field; the module picks it. The `tx:` counter in `grep '^2:' /proc/tty/driver/serial` goes up per packet.
- BusyBox on the P3 has no `curl`, no `https` in `wget`, and its `nc` has no `-e`; hence the PC sends files with `nc`.

## Credits

[ESPHome](https://esphome.io), [ryanh7/esphome-custom-components](https://github.com/ryanh7/esphome-custom-components) (`gree_ext`),
[niceboygithub/AqaraM1SM2fw](https://github.com/niceboygithub/AqaraM1SM2fw) (firmware images and tools),
[YoWASP/clang](https://github.com/YoWASP/clang) (the in-browser compiler).

MIT, see [LICENSE](LICENSE). Not affiliated with Aqara, Lumi or Xiaomi.
