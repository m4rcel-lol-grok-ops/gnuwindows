#!/bin/bash
# GNU/Windows build orchestrator
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

CMD="${1:-all}"

case "$CMD" in
    clean)
        make clean
        rm -f build/OVMF_VARS.fd
        echo "Clean complete."
        ;;
    toolchain)
        echo "Toolchain check:"
        clang --version | head -1
        ld.lld --version | head -1
        qemu-system-x86_64 --version | head -1
        echo "OVMF: $(ls /usr/share/OVMF/OVMF_CODE_4M.fd 2>/dev/null || ls /usr/share/ovmf/OVMF.fd)"
        echo "Toolchain OK."
        ;;
    kernel)
        make kernel
        ;;
    bootloader)
        make bootloader
        ;;
    image)
        make image
        ;;
    iso)
        echo "ISO generation not yet implemented (Phase 1 uses raw disk image)."
        make image
        ;;
    run)
        make run
        ;;
    debug)
        make debug
        ;;
    test)
        echo "Running Phase 1 boot test..."
        make image
        # Capture serial output with a timeout
        timeout 15s ./scripts/run_qemu.sh build/gnuwindows.img 2>&1 | tee build/test_serial.log || true
        if grep -q "GWKernel initialized successfully" build/test_serial.log; then
            echo "TEST PASSED: Kernel message found."
            exit 0
        else
            echo "TEST FAILED: Expected kernel message not found."
            exit 1
        fi
        ;;
    all)
        make all
        ;;
    *)
        echo "Usage: $0 {clean|toolchain|kernel|bootloader|image|iso|run|debug|test|all}"
        exit 1
        ;;
esac
