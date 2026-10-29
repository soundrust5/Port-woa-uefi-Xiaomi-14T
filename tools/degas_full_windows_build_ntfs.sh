#!/usr/bin/env bash
# Offline Android ARM64 NTFS mount helper from the already pinned tool source.
# Builds only: no mount, formatting, installation or phone access.
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
out="$root/artifacts/degas/full-windows/host-tools"
archive="$root/artifacts/oneplus12/host-tools/ntfs-3g_ntfsprogs-2026.7.7.tgz"
mkdir -p "$out"
printf '%s  %s\n' d67b769025d32860549d35c2147e45024d172f81c540d750390ce3602c059dab "$archive" | sha256sum -c -
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tar -xzf "$archive" -C "$work"
mkdir "$work/build"
cd "$work/build"
export CC=aarch64-linux-gnu-gcc AR=aarch64-linux-gnu-ar RANLIB=aarch64-linux-gnu-ranlib
export CFLAGS="-O2 -ffile-prefix-map=$work=/usr/src/ntfs-3g-2026.7.7"
"$work/ntfs-3g-2026.7.7/configure" --host=aarch64-linux-gnu --build=x86_64-linux-gnu \
 --disable-shared --enable-static --disable-plugins --without-uuid --with-fuse=internal \
 > "$out/configure.log" 2>&1
make -j4 LDFLAGS=-all-static > "$out/build.log" 2>&1
cp src/ntfs-3g "$out/ntfs-3g-arm64-static"
aarch64-linux-gnu-strip --strip-all "$out/ntfs-3g-arm64-static"
file "$out/ntfs-3g-arm64-static" > "$out/file.txt"
aarch64-linux-gnu-readelf -d "$out/ntfs-3g-arm64-static" > "$out/dynamic.txt"
grep -q 'statically linked' "$out/file.txt"
grep -q 'There is no dynamic section' "$out/dynamic.txt"
sha256sum "$out/ntfs-3g-arm64-static" > "$out/sha256.txt"
cat "$out/file.txt" "$out/sha256.txt"
