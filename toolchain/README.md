# In-browser toolchain

The web UI compiles `irgen` in the browser. Two release assets (tag `toolchain-clang22-mips`) hold the
large parts; the Pages workflow unpacks them next to `docs/`.

| Asset | Contents | How it was made |
|---|---|---|
| `clang-mips-wasm.tar.gz` | `bundle.js`, `llvm.js`, `llvm.core*.wasm`, `llvm-resources.*`: Clang/LLD 22 as WebAssembly (WASI) with the **Mips** backend | [YoWASP/clang](https://github.com/YoWASP/clang) at `944dd7c` (llvm-src `97196c8`), `yowasp-clang-mips.patch` applied, `./build.sh` then `./package-npmjs.sh`, archive of `npmjs/gen/` |
| `sysroot.tar.gz` | musl + libc++ headers and static libs for mipsel, MIPS32r2, soft-float | `make-sysroot.sh` from a native `zig c++` build of irgen (ziglang from PyPI) |

`docs/cc1-args.json` and `docs/link-args.json` are the exact `clang -cc1` and `ld.lld` arguments
`zig c++` uses for this target (`-###` and `ZIG_VERBOSE_LINK=1`), with paths moved to `/sysroot` and `/work`.
A browser build and the native zig build produce the same IR timings (checked over 240 AC states).

Upstream YoWASP only builds the WebAssembly backend (`No available targets are compatible with triple
"mipsel-unknown-linux-musl"`), hence the patch. The build takes about 50 minutes on 16 cores; set
`CMAKE_BUILD_PARALLEL_LEVEL`, otherwise `cmake --build` runs a single job. `package-npmjs.sh` needs an
`llvmorg-*` tag on `llvm-src` (a shallow clone has none: `git -C llvm-src tag llvmorg-22.0.0`).

Licenses: LLVM (Apache-2.0 WITH LLVM-exception), YoWASP runtime (ISC), musl (MIT), zig's bundled
libc++/libunwind/compiler-rt (Apache-2.0 WITH LLVM-exception).
