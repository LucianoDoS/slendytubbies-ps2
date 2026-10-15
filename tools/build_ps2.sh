#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/.." && pwd)"
export PS2DEV="${PS2DEV:-$HOME/slendytubbies-tools/ps2dev}"
export PS2SDK="$PS2DEV/ps2sdk"
export GSKIT="$PS2DEV/gsKit"
export PATH="$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/bin:$PATH"
if [[ ! -x "$PS2DEV/ee/bin/mips64r5900el-ps2-elf-gcc" ]]; then
    echo "Missing PS2 toolchain. Set PS2DEV to an installed ps2dev release." >&2
    exit 1
fi
make -C "$project/ps2" -j2
mkdir -p "$project/dist"
cp "$project/ps2/build/SLENDY.ELF" "$project/dist/SLENDY.ELF"
mips64r5900el-ps2-elf-strip --strip-debug "$project/dist/SLENDY.ELF"
mips64r5900el-ps2-elf-readelf -h "$project/dist/SLENDY.ELF"
mips64r5900el-ps2-elf-size "$project/dist/SLENDY.ELF"
