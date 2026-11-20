#!/bin/sh
# Collects the mipsel (MIPS32r2, soft-float, musl, libc++) headers and static libraries that the
# in-browser build compiles and links against, from a native `zig c++` build of irgen.
# Usage: toolchain/make-sysroot.sh <dir with the .o files of a zig-built irgen> <out.tar.gz>
# (src/irgen/build.sh leaves them in src/irgen/.esphome/build/irgen/mips/)
set -e
OBJDIR=$1; OUT=$2
Z=$(python3 -c "import ziglang,os;print(os.path.dirname(ziglang.__file__))")/lib
T=$(mktemp -d); S=$T/sysroot; mkdir -p $S/include $S/lib
for d in libcxx/include:cxx libcxxabi/include:cxxabi include:clang libc/include/mips-linux-musl:musl-mips \
         libc/include/generic-musl:musl-generic libc/include/mips-linux-any:linux-mips \
         libc/include/any-linux-any:linux-any libunwind/include:unwind; do
  cp -r "$Z/${d%%:*}" "$S/include/${d##*:}"
done
# Ask zig which runtime libraries it links (it builds them into its cache on first use).
ZIG_VERBOSE_LINK=1 python3 -m ziglang c++ -target mipsel-linux-musleabi -mcpu=mips32r2 -msoft-float -static -Os \
  -o $T/irgen "$OBJDIR"/*.o 2>&1 | tr ' ' '\n' | grep -e '\.a$' -e 'crt1\.o$' | grep -v "^$OBJDIR" | xargs -I{} cp {} $S/lib/
tar czf "$OUT" -C $T sysroot
rm -rf $T
ls -l "$OUT"
