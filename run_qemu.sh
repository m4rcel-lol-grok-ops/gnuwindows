#!/bin/bash
set -euo pipefail
IMG="$1"
MODE="${2:-}"   # --debug | --ahci | --ahci --debug

OVMF_CODE="/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS_TEMPLATE="/usr/share/OVMF/OVMF_VARS_4M.fd"
BUILD_DIR="$(dirname "$IMG")"
OVMF_VARS="${BUILD_DIR}/OVMF_VARS.fd"
REPO_DIR="${BUILD_DIR}/repo"

[ -f "$OVMF_CODE" ] || OVMF_CODE="/usr/share/ovmf/OVMF.fd"
if [ ! -f "$OVMF_VARS" ] && [ -f "$OVMF_VARS_TEMPLATE" ]; then
    cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS"
fi

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

if ! curl -s -o /dev/null -w '' --connect-timeout 1 http://127.0.0.1:8765/v1/hello.pkg 2>/dev/null; then
    ( cd "$REPO_DIR" && python3 -m http.server 8765 >/dev/null 2>&1 & )
    sleep 0.5
    echo "Host repo: http://127.0.0.1:8765/ (guest sees http://10.0.2.100/)"
fi

USE_AHCI=0
DEBUG=
for a in "$@"; do
    case "$a" in
        --ahci) USE_AHCI=1 ;;
        --debug) DEBUG=1 ;;
    esac
done
# also if second arg is --ahci
[ "${2:-}" = "--ahci" ] && USE_AHCI=1
[ "${2:-}" = "--debug" ] && DEBUG=1
[ "${3:-}" = "--ahci" ] && USE_AHCI=1
[ "${3:-}" = "--debug" ] && DEBUG=1

QEMU_ARGS=(
    -machine pc,accel=tcg
    -cpu qemu64
    -m 256M
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE"
)
if [ -f "$OVMF_VARS" ]; then
    QEMU_ARGS+=(-drive if=pflash,format=raw,file="$OVMF_VARS")
fi

USE_NVME=0
for a in "$@"; do
    [ "$a" = "--nvme" ] && USE_NVME=1
done
[ "${2:-}" = "--nvme" ] && USE_NVME=1
[ "${3:-}" = "--nvme" ] && USE_NVME=1

if [ "$USE_NVME" = "1" ]; then
    QEMU_ARGS+=(
        -drive file="$IMG",format=raw,if=none,id=gwdisk,cache=writeback
        -device nvme,drive=gwdisk,serial=gw0
    )
    echo "Starting QEMU (NVMe + ne2k + HTTP guestfwd)..."
elif [ "$USE_AHCI" = "1" ]; then
    QEMU_ARGS+=(
        -device ahci,id=ahci0
        -drive file="$IMG",format=raw,if=none,id=gwdisk,cache=writeback
        -device ide-hd,drive=gwdisk,bus=ahci0.0
    )
    echo "Starting QEMU (AHCI SATA + ne2k + HTTP guestfwd)..."
else
    QEMU_ARGS+=(
        -drive file="$IMG",format=raw,if=ide,index=0,media=disk,cache=writeback
    )
    echo "Starting QEMU (IDE + ne2k + HTTP guestfwd)..."
fi

QEMU_ARGS+=(
    -boot order=c
    -netdev user,id=net0,guestfwd=tcp:10.0.2.100:80-tcp:127.0.0.1:8765
    -device ne2k_isa,netdev=net0,iobase=0x300,irq=3
    -usb
    -device usb-kbd
    -serial stdio
    -display none -vga std
    -no-reboot
)

if [ "$DEBUG" = "1" ]; then
    QEMU_ARGS+=(-s -S)
fi

exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
