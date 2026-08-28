# Booting GNU/Windows on real hardware

## Requirements

- x86_64 CPU with UEFI firmware (CSM optional; pure UEFI preferred)
- Disable **Secure Boot** (unsigned bootloader)
- Prefer firmware **AHCI** mode for SATA (not RAID)
- USB stick or disk with GPT + FAT32 ESP

## ESP layout

```
/EFI/BOOT/BOOTX64.EFI   ← UEFI bootloader
/KERNEL.ELF             ← GWKernel
```

Copy those files from a successful QEMU `scripts/create_image.sh` build (or extract from the image).

## Firmware settings

1. Secure Boot: **Off**
2. SATA mode: **AHCI**
3. Fast Boot: Off (first bring-up)
4. If no PS/2 keyboard, use a USB keyboard in **legacy USB** mode if available, or a serial console (COM1)

## What works on modern machines (goal of this branch)

| Component | Mechanism |
|-----------|-----------|
| Display | UEFI **GOP** framebuffer (not legacy VGA text alone) |
| Disk | **AHCI** if present, else legacy ATA PIO |
| Keyboard | PS/2 IRQ1 (USB HID not yet) |
| Timer | PIT (many firmwares still virtual-wire IRQ0) |

## What does **not** work yet

- NVMe, USB mass storage, USB HID
- IOAPIC/LAPIC-only systems with no PIC virtual wire (rare after UEFI)
- Secure Boot / signed images
- Wi-Fi / modern NICs (NE2000 is ISA/lab only)
- Full SMP

## Build and write a USB stick (Linux host)

```bash
./build.sh
# identify USB device carefully, e.g. /dev/sdX — THIS WIPES THE DEVICE
sudo dd if=build/gnuwindows.img of=/dev/sdX bs=4M status=progress conv=fsync
```

Or format a USB as FAT32 ESP and copy `BOOTX64.EFI` + `KERNEL.ELF` as above.

## First boot success criteria

- Firmware loads `BOOTX64.EFI`
- Message: GOP framebuffer geometry
- Kernel: `fb: GOP ...`, `ahci:` or `ATA:`, shell prompt on screen

If the screen stays blank but the machine is running, try a serial header (115200 8N1) — the kernel still logs to COM1 when present.
