# GNU/Windows (GWKernel)

An independent hobby operating system for **x86_64 UEFI**.

**This is not Linux.** It uses a custom kernel (GWKernel), not the Linux kernel, not a distro, and not a compatibility layer on another OS kernel.

## Honest status

| Target | Status |
|--------|--------|
| Build from source (clang/lld) | Yes |
| Boot in QEMU + OVMF (UEFI) | Yes |
| Interactive serial shell | Yes |
| VGA text + PS/2 keyboard | Yes |
| FAT32 (ESP) R/W, RAMFS | Yes |
| Ring-3 ELF, syscalls | Minimal |
| Network (NE2000, TCP/HTTP, DNS) | Lab/QEMU |
| Package fetch (`gwpkg` `.pkg`) | Lab/QEMU guestfwd |
| **Real hardware install** | **Experimental only** |

### Real hardware

The tree **compiles** as a freestanding kernel and UEFI bootloader. On real machines you would need:

- x86_64 with UEFI
- Hardware that matches current drivers (IDE/ATA PIO, optional NE2000-class NIC, standard PS/2, CMOS RTC, PCI)
- Manual ESP layout (`EFI/BOOT/BOOTX64.EFI`, `KERNEL.ELF`)

There is **no installer**, no secure-boot signing flow, no AHCI/NVMe, no USB stack, no SMP, and no production driver set. Treat real-hardware boots as a bring-up experiment, not a supported product.

Primary development and test platform: **QEMU `pc` + OVMF + IDE + `ne2k_isa`**.

## Build (Linux host)

```bash
# Dependencies (Debian/Ubuntu-ish)
sudo apt install clang lld gdisk dosfstools mtools qemu-system-x86 ovmf

./build.sh          # or: make && scripts/create_image.sh ...
./build.sh run      # QEMU boot with serial console
```

See `scripts/run_qemu.sh` and `scripts/create_image.sh`.

## Architecture (simplified)

```
UEFI (OVMF) → BOOTX64.EFI → GWKernel (ELF @ 0x100000)
  → GDT/IDT, PMM, heap, VMM
  → ATA + FAT32 (D:), RAMFS (C:)
  → threads (cooperative), syscalls, ring-3 ELF
  → NE2000 / IPv4 / TCP / HTTP / DNS → gwpkg
  → serial + VGA text + PS/2 + CMOS RTC + PCI scan
```

## License

See `LICENSE`.
