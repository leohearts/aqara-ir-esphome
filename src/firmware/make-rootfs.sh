#!/bin/sh
# Build rootfs_ir.bin: niceboygithub's community P3 rootfs 4.5.20_0033.0652 plus rootfs.patch.
# Needs: curl, fakeroot, squashfs-tools, python3. Output: ./rootfs_ir.bin (md5 592fd665937cf26b9bdc812c1ad48424 with squashfs-tools 4.7.4; the build is reproducible).
set -e
H=$(cd "$(dirname "$0")" && pwd)
BASE=rootfs_4.5.20_0033.0652_modified.bin
URL=https://github.com/niceboygithub/AqaraM1SM2fw/raw/main/modified/P3/4.5.20_0033.0652/$BASE
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
curl -fL -o "$W/$BASE" "$URL"
[ "$(md5sum < "$W/$BASE" | cut -d' ' -f1)" = a209d9b3d253b75afa87d01437230911 ] || { echo "base image md5 mismatch" >&2; exit 1; }
tail -c +17 "$W/$BASE" > "$W/mod.sqfs"   # strip the 16-byte r6cr header
cd "$W"
fakeroot -s fr.state unsquashfs -d root mod.sqfs   # fakeroot keeps /dev nodes
(cd root && patch -p1 < "$H/rootfs.patch")
fakeroot -i fr.state mksquashfs root new.sqfs -comp xz -b 131072 -noappend -mkfs-time 1700000000 -all-time 1700000000
python3 "$H/mkimg.py" new.sqfs "$OLDPWD/rootfs_ir.bin"
cd "$OLDPWD"
sum=$(md5sum < rootfs_ir.bin | cut -d' ' -f1)
echo "rootfs_ir.bin md5 $sum"
[ "$sum" = 592fd665937cf26b9bdc812c1ad48424 ] && echo "md5 matches the reference build" ||
  echo "md5 differs from the reference build (other squashfs-tools version?); the image is still valid" >&2
