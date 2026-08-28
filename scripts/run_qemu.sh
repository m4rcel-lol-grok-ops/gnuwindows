#!/bin/bash
set -euo pipefail
IMG="$1"
DEBUG="${2:-}"

OVMF_CODE="/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS_TEMPLATE="/usr/share/OVMF/OVMF_VARS_4M.fd"
BUILD_DIR="$(dirname "$IMG")"
OVMF_VARS="${BUILD_DIR}/OVMF_VARS.fd"
REPO_DIR="${BUILD_DIR}/repo"

[ -f "$OVMF_CODE" ] || OVMF_CODE="/usr/share/ovmf/OVMF.fd"
if [ ! -f "$OVMF_VARS" ] && [ -f "$OVMF_VARS_TEMPLATE" ]; then
    cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS"
fi

# Host-side package repo for guestfwd
mkdir -p "$REPO_DIR/v1"
cat > "$REPO_DIR/v1/hello.pkg" << 'PKG'
---MANIFEST---
name: hello
version: 0.2.0
desc: Hello from real HTTP single-file package
file: README.TXT
---FILE:README.TXT---
GNU/Windows package: hello
Served as one .pkg over HTTP from the host.
---END---
PKG

# Start python http server if not running
if ! curl -s -o /dev/null -w '' --connect-timeout 1 http://127.0.0.1:8765/v1/hello/MANIFEST.TXT 2>/dev/null; then
    ( cd "$REPO_DIR" && python3 -m http.server 8765 >/dev/null 2>&1 & )
    sleep 0.5
    echo "Host repo: http://127.0.0.1:8765/ (guest sees http://10.0.2.100/)"
fi

QEMU_ARGS=(
    -machine pc,accel=tcg
    -cpu qemu64
    -m 256M
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE"
)
if [ -f "$OVMF_VARS" ]; then
    QEMU_ARGS+=(-drive if=pflash,format=raw,file="$OVMF_VARS")
fi

QEMU_ARGS+=(
    -drive file="$IMG",format=raw,if=ide,index=0,media=disk,cache=writeback
    -boot order=c
    -netdev user,id=net0,guestfwd=tcp:10.0.2.100:80-tcp:127.0.0.1:8765
    -device ne2k_isa,netdev=net0,iobase=0x300,irq=3
    -serial stdio
    -display none -vga std
    -no-reboot
)

if [ "$DEBUG" = "--debug" ]; then
    QEMU_ARGS+=(-s -S)
fi

echo "Starting QEMU (IDE + ne2k + HTTP guestfwd)..."
exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
