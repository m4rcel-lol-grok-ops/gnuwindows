#!/bin/bash
# GPT disk + FAT32 ESP: BOOTX64.EFI, kernel.elf, HELLO.ELF
set -euo pipefail

IMG="$1"
EFI="$2"
KERNEL="$3"
SIZE_MB=128
ESP_SIZE_MB=64
BUILD_DIR="$(dirname "$IMG")"
HELLO="${BUILD_DIR}/hello.elf"

rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=1M count=$SIZE_MB status=none

sgdisk -n 1:2048:$((ESP_SIZE_MB*2048 + 2047)) -t 1:ef00 -c 1:"EFI System" "$IMG" >/dev/null

ESP_IMG="${IMG}.esp"
dd if=/dev/zero of="$ESP_IMG" bs=1M count=$ESP_SIZE_MB status=none
mkfs.vfat -F 32 -s 1 -S 512 -n "GNUWINESP" "$ESP_IMG" >/dev/null 2>&1 || \
mkfs.vfat -F 32 -n "GNUWINESP" "$ESP_IMG" >/dev/null

export MTOOLS_SKIP_CHECK=1
mmd -i "$ESP_IMG" ::/EFI
mmd -i "$ESP_IMG" ::/EFI/BOOT
mcopy -i "$ESP_IMG" "$EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$ESP_IMG" "$KERNEL" ::/kernel.elf
if [ -f "$HELLO" ]; then
    mcopy -i "$ESP_IMG" "$HELLO" ::/HELLO.ELF
    echo "  + HELLO.ELF on ESP"
fi

dd if="$ESP_IMG" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
rm -f "$ESP_IMG"
echo "Created $IMG ($(stat -c%s "$IMG") bytes)"
