# esphome-ir: ESPHome IR encoders on the Aqara AC Companion P3 (KTBL12LM)

The stock firmware has no encoder for some remotes (here: Gree YAP0F). This directory lets
`mha_ir` (the stock IR daemon) hand AC state to an ESPHome `climate_ir` platform instead.

## Files in /data/esphome-ir

| File | What it is |
|---|---|
| `irgen` | ESPHome built for this CPU (static MIPS32r2 binary). Reads `$AQARA_AC`, prints mark/space timings. The AC model is compiled in from `src/irgen/irgen.yaml`. |
| `libirplug.so` | Wrapper module loaded by `mha_ir -r`. Pulls in the stock `/lib/libha_ir.so`. |
| `src.tar.gz` | Everything needed to rebuild both files and the firmware change. |
| `README.md` | This file. |

## How it fits together

1. Boot: `rcS` runs `/data/scripts/post_init.sh`, which runs `fw_manager.sh -r`.
2. `fw_manager.sh` (and `app_monitor.sh` when it restarts a crashed `mha_ir`) start
   `mha_ir -r /data/esphome-ir/libirplug.so` if that file exists, else the stock `/lib/libha_ir.so`.
3. In `ha_ir_open`, `libirplug.so` redirects libha_ir's GOT slot of `protocol_ac_product_ircode`
   to its own function, then calls the stock `ha_ir_open`.
4. When the remote id `r_id` is `1189998819` (the IT Crowd emergency number, 0118 999 881 9,
   cut to fit a 32-bit int), the plugin runs
   `AQARA_AC=P,M,T,S,D /data/esphome-ir/irgen` and sends its output.
   Any other `r_id` goes to the stock encoder, unchanged.

`AQARA_AC` fields: P power (1 = off), M mode (0 cool, 1 heat, 2 auto, 3 fan, 4 dry),
T target temperature (C), S fan (0 auto, 1 low, 2 medium, 3 high), D swing (0 = on).

## Everyday tasks

### Check it is active

    ps | grep 'mha_ir -r'                  # should show -r /data/esphome-ir/libirplug.so
    getprop persist.app.ir.r_id            # 3131383939393838313900 = "1189998819" (hex + NUL)
    ESPHOME_PREFDIR=/tmp AQARA_AC=0,0,26,1,0 /data/esphome-ir/irgen   # prints timings

`persist.app.ir.*` values are hex-encoded strings with a trailing `00`.

### Change the AC model

On a PC (Linux x86_64):

    tar xzf src.tar.gz && cd src
    python3.13 -m venv venv && venv/bin/pip install esphome ziglang   # esphome wheels failed on 3.15
    # edit irgen/irgen.yaml: replace the climate entry, e.g. a stock platform
    #   - platform: coolix        (or daikin, midea_ir, toshiba, ... any climate_ir platform)
    #     id: ac
    #     name: AC
    #     transmitter_id: ir_out
    # keep `id: ac` (or update aqara_ir.climate_id) and keep transmitter_id: ir_out
    PATH=$PWD/venv/bin:$PATH ./irgen/build.sh upload

`build.sh upload` copies the binary to the device over telnet (`admin`, empty password) and nc.
No restart is needed: `irgen` is started fresh for every IR command.

### Switch between ESPHome and the stock encoder

    setprop persist.app.ir.r_id 3131383939393838313900   # ESPHome (1189998819)
    setprop persist.app.ir.r_id 3138383132383531353300   # stock Gree id 1881285153 (original value)
    killall mha_ir                                       # app_monitor.sh restarts it

To drop the plugin entirely: delete `libirplug.so`, then `killall mha_ir`.

## Rebuilding libirplug.so

    cp <device:/lib/libha_ir.so> src/irplug/ && ./src/irplug/build.sh

The `OFF_*` constants in `irplug.c` are for `/lib/libha_ir.so` md5 `2b73769db0b7569344ea5ca58fd925cc`
(firmware 4.5.20_0033.0652). A different firmware needs new offsets:
`protocol_ac_sync` and `ha_ir_open` values from `.dynsym`, and the GOT slot of
`protocol_ac_product_ircode` = `DT_PLTGOT + 4 * (DT_MIPS_LOCAL_GOTNO + symidx - DT_MIPS_GOTSYM)`.

Do not overwrite `libirplug.so` while `mha_ir` has it loaded (it crashes with SIGBUS):
`killall mha_ir` first, or write to a new name and `mv`.

## The firmware change

Base: niceboygithub/AqaraM1SM2fw `modified/P3/4.5.20_0033.0652/rootfs_4.5.20_0033.0652_modified.bin`
(md5 `a209d9b3d253b75afa87d01437230911`). `src/firmware/rootfs.patch` changes three scripts:
`rcS` runs `post_init.sh` again; `fw_manager.sh` and `app_monitor.sh` prefer `libirplug.so`.

To rebuild the image (Linux PC):

    tail -c +17 rootfs_4.5.20_0033.0652_modified.bin > mod.sqfs
    fakeroot -s fr.state unsquashfs -d root mod.sqfs        # fakeroot keeps /dev nodes
    (cd root && patch -p1 < ../src/firmware/rootfs.patch)
    fakeroot -i fr.state mksquashfs root new.sqfs -comp xz -b 131072 -noappend
    python3 src/firmware/mkimg.py new.sqfs rootfs_ir.bin

`mkimg.py` adds the 16-byte Realtek `r6cr` header and the 16-bit checksum the image format uses.
There is no signature, and `boot_ctrl show` reports `root_sum_check: off`.
The installed image has md5 `1bae852b5cd57b2388507fafefa60ab0`.

Flash from the device: copy the image to `/tmp` (RAM; `/data` is too small), then `fw_update /tmp/<image>`.
It wrote the inactive rootfs slot and switched to it; `boot_ctrl show` shows the slots
(as installed: `rootfs_0` = this image, `rootfs_1` = the unmodified community image).
An official OTA would replace this change.

## Known facts that cost time

- uClibc 0.9.33 here ignores `LD_PRELOAD`, and its MIPS ld.so binds a library's own defined
  symbols to itself, so overriding `protocol_ac_product_ircode` by name does not work. Hence the GOT redirect.
- Inside `mha_ir`, `pclose()` does not return 0 even when `irgen` succeeds (likely SIGCHLD is ignored;
  not verified), so the plugin checks the parsed output instead of the exit status.
- The IR module is on `/dev/ttyS2`; `grep '^2:' /proc/tty/driver/serial` before and after a command shows
  whether a frame was sent (one Gree YAP0F command = 568 bytes).
- Local IPC is `/tmp/miio_agent.socket` (SOCK_SEQPACKET). Bind with `{"method":"bind","address":N}`;
  `mha_ir` is address 512 and accepts e.g.
  `{"id":1,"_to":512,"method":"local.ir_write","params":[{"did":"<did>","prop":"on_off_status","value":"1"}]}`.
