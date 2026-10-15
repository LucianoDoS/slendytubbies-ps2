#!/usr/bin/env bash
set -euo pipefail
tools_dir="$HOME/slendytubbies-tools"
sdk_dir="${PS2DEV:-$tools_dir/ps2dev}"
if [[ -x "$sdk_dir/ee/bin/mips64r5900el-ps2-elf-gcc" ]]; then
    echo "PS2 toolchain already installed: $sdk_dir"
    exit 0
fi
mkdir -p "$tools_dir" "$sdk_dir"
archive="$tools_dir/ps2dev.tar.gz"
if [[ ! -f "$archive" ]]; then
    curl --fail --location --retry 2 https://github.com/ps2dev/ps2dev/releases/download/latest/ps2dev-ubuntu-latest.tar.gz -o "$archive"
fi
tar -xzf "$archive" --strip-components=1 -C "$sdk_dir"
sha256sum "$archive" > "$tools_dir/ps2dev.sha256"
"$sdk_dir/ee/bin/mips64r5900el-ps2-elf-gcc" --version
