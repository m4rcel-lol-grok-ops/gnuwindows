/*
 * GNU/Windows UEFI Bootloader - Phase 1
 *
 * Loads GWKernel ELF from the ESP, obtains the memory map,
 * exits boot services, and transfers control to the custom kernel.
 *
 * This is a real UEFI application (PE/COFF), not a Linux loader.
 */

#include "efi.h"

/* Boot information passed to GWKernel */
typedef struct {
    uint64_t magic;                 /* 'GWB1' */
    uint64_t kernel_physical_base;
    uint64_t kernel_entry;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_map_desc_size;
    uint64_t memory_map_desc_version;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    uint64_t rsdp;                  /* ACPI RSDP if present */
} GW_BOOT_INFO;

#define GW_BOOT_MAGIC 0x31425747ULL  /* 'GWB1' little-endian */

/* Minimal ELF64 structures */
#define ELF_MAGIC 0x464C457F

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#define PT_LOAD 1

static void efi_print(EFI_SYSTEM_TABLE *st, const CHAR16 *s) {
    if (st && st->ConOut && st->ConOut->OutputString) {
        st->ConOut->OutputString(st->ConOut, (CHAR16 *)s);
    }
}

static void efi_print_hex(EFI_SYSTEM_TABLE *st, uint64_t val) {
    CHAR16 buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int nibble = (val >> (60 - i * 4)) & 0xF;
        buf[2 + i] = (CHAR16)(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    buf[18] = 0;
    efi_print(st, buf);
}

static void efi_print_dec(EFI_SYSTEM_TABLE *st, uint64_t val) {
    CHAR16 buf[24];
    int i = 22;
    buf[23] = 0;
    if (val == 0) {
        buf[22] = '0';
        efi_print(st, &buf[22]);
        return;
    }
    while (val > 0 && i >= 0) {
        buf[i--] = (CHAR16)('0' + (val % 10));
        val /= 10;
    }
    efi_print(st, &buf[i + 1]);
}

/* Simple memory copy */
static void *memcpy(void *dest, const void *src, UINTN n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static void *memset(void *s, int c, UINTN n) {
    uint8_t *p = s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

/* Load the kernel ELF from the ESP */
static EFI_STATUS load_kernel(EFI_SYSTEM_TABLE *st, EFI_HANDLE image,
                              uint64_t *entry_out, uint64_t *base_out,
                              uint64_t *size_out) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    UINTN file_size = 0;
    void *kernel_buffer = NULL;
    Elf64_Ehdr *ehdr;
    Elf64_Phdr *phdr;
    uint64_t kernel_phys = 0;
    UINTN pages = 0;

    /* Get LoadedImage to find the device we were loaded from */
    status = st->BootServices->OpenProtocol(
        image,
        (EFI_GUID *)&EFI_LOADED_IMAGE_PROTOCOL_GUID,
        (void **)&loaded_image,
        image,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(status) || !loaded_image) {
        efi_print(st, L"Failed to get LoadedImage protocol\r\n");
        return status;
    }

    /* Open SimpleFileSystem on the same device */
    status = st->BootServices->OpenProtocol(
        loaded_image->DeviceHandle,
        (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
        (void **)&fs,
        image,
        NULL,
        EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(status) || !fs) {
        efi_print(st, L"Failed to get SimpleFileSystem\r\n");
        return status;
    }

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || !root) {
        efi_print(st, L"Failed to open volume\r\n");
        return status;
    }

    /* Open \kernel.elf (or /kernel.elf) */
    status = root->Open(root, &file, L"\\kernel.elf", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        status = root->Open(root, &file, L"kernel.elf", EFI_FILE_MODE_READ, 0);
    }
    if (EFI_ERROR(status) || !file) {
        efi_print(st, L"Failed to open kernel.elf\r\n");
        return status;
    }

    /* Get file size: for Phase 1 allocate a generous buffer and read all */
    /* (avoids GetInfo GUID/struct layout issues on some firmwares) */
    UINTN max_pages = 512; /* 2 MiB max for early kernel */
    status = st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                             max_pages, (uint64_t *)&kernel_buffer);
    if (EFI_ERROR(status)) {
        efi_print(st, L"Failed to allocate kernel buffer\r\n");
        return status;
    }

    UINTN read_size = max_pages * 4096;
    status = file->Read(file, &read_size, kernel_buffer);
    if (EFI_ERROR(status)) {
        efi_print(st, L"Failed to read kernel.elf\r\n");
        return status;
    }
    file_size = read_size;
    efi_print(st, L"kernel.elf size: ");
    efi_print_dec(st, file_size);
    efi_print(st, L" bytes\r\n");

    file->Close(file);
    root->Close(root);

    /* Parse ELF */
    ehdr = (Elf64_Ehdr *)kernel_buffer;
    if (*(uint32_t *)ehdr->e_ident != ELF_MAGIC) {
        efi_print(st, L"Invalid ELF magic\r\n");
        return EFI_LOAD_ERROR;
    }
    if (ehdr->e_machine != 0x3E) { /* EM_X86_64 */
        efi_print(st, L"Not an x86_64 ELF\r\n");
        return EFI_LOAD_ERROR;
    }

    efi_print(st, L"ELF entry: ");
    efi_print_hex(st, ehdr->e_entry);
    efi_print(st, L"\r\n");

    /* Find the highest load address to size the final image, and lowest paddr */
    uint64_t min_paddr = ~0ULL;
    uint64_t max_paddr = 0;
    phdr = (Elf64_Phdr *)((uint8_t *)kernel_buffer + ehdr->e_phoff);
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_paddr < min_paddr) min_paddr = phdr[i].p_paddr;
        uint64_t end = phdr[i].p_paddr + phdr[i].p_memsz;
        if (end > max_paddr) max_paddr = end;
    }

    if (min_paddr == ~0ULL) {
        efi_print(st, L"No PT_LOAD segments\r\n");
        return EFI_LOAD_ERROR;
    }

    uint64_t image_size = max_paddr - min_paddr;
    pages = (image_size + 4095) / 4096;

    /* Allocate final kernel at its linked address (0x100000) so that
       absolute addressing used by -fno-pic code works. */
    kernel_phys = min_paddr;  /* usually 0x100000 */
    status = st->BootServices->AllocatePages(AllocateAddress, EfiLoaderData,
                                             pages, &kernel_phys);
    if (EFI_ERROR(status)) {
        efi_print(st, L"Failed to allocate kernel at linked address, trying any\r\n");
        kernel_phys = 0;
        status = st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                                 pages, &kernel_phys);
        if (EFI_ERROR(status)) {
            efi_print(st, L"Failed to allocate final kernel pages\r\n");
            return status;
        }
        efi_print(st, L"WARNING: relocated kernel - absolute refs may break\r\n");
    }

    memset((void *)kernel_phys, 0, pages * 4096);

    /* Copy LOAD segments */
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint64_t dest = kernel_phys + (phdr[i].p_paddr - min_paddr);
        memcpy((void *)dest,
               (uint8_t *)kernel_buffer + phdr[i].p_offset,
               phdr[i].p_filesz);
        /* BSS already zeroed */
    }

    *entry_out = kernel_phys + (ehdr->e_entry - min_paddr);
    *base_out = kernel_phys;
    *size_out = image_size;

    efi_print(st, L"Kernel loaded at phys ");
    efi_print_hex(st, kernel_phys);
    efi_print(st, L", entry ");
    efi_print_hex(st, *entry_out);
    efi_print(st, L"\r\n");

    /* Free temporary buffer (optional) */
    st->BootServices->FreePages((uint64_t)kernel_buffer, max_pages);

    return EFI_SUCCESS;
}

/* Obtain memory map (caller must free or keep after ExitBootServices) */
static EFI_STATUS get_memory_map(EFI_SYSTEM_TABLE *st,
                                 EFI_MEMORY_DESCRIPTOR **map_out,
                                 UINTN *map_size_out,
                                 UINTN *map_key_out,
                                 UINTN *desc_size_out,
                                 uint32_t *desc_ver_out) {
    EFI_STATUS status;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    uint32_t desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN pages = 32; /* 128 KiB should be plenty for the map */

    status = st->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                             pages, (uint64_t *)&map);
    if (EFI_ERROR(status)) {
        efi_print(st, L"mmap page alloc failed\r\n");
        return status;
    }

    map_size = pages * 4096;
    status = st->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        efi_print(st, L"GetMemoryMap failed status=\r\n");
        efi_print_hex(st, (uint64_t)status);
        efi_print(st, L"\r\n");
        return status;
    }

    *map_out = map;
    *map_size_out = map_size;
    *map_key_out = map_key;
    *desc_size_out = desc_size;
    *desc_ver_out = desc_ver;
    return EFI_SUCCESS;
}


EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS status;
    uint64_t kernel_entry = 0;
    uint64_t kernel_base = 0;
    uint64_t kernel_size = 0;
    EFI_MEMORY_DESCRIPTOR *mmap = NULL;
    UINTN mmap_size = 0, mmap_key = 0, desc_size = 0;
    uint32_t desc_ver = 0;
    GW_BOOT_INFO *boot_info = NULL;

    if (!SystemTable || !SystemTable->ConOut) {
        return EFI_INVALID_PARAMETER;
    }

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    efi_print(SystemTable, L"GNU/Windows UEFI Bootloader\r\n");
    efi_print(SystemTable, L"Loading GWKernel...\r\n");

    status = load_kernel(SystemTable, ImageHandle, &kernel_entry, &kernel_base, &kernel_size);
    if (EFI_ERROR(status)) {
        efi_print(SystemTable, L"Kernel load failed\r\n");
        for (;;) {}
        return status;
    }

    /* Allocate a page for boot_info that survives ExitBootServices */
    status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                                      1, (uint64_t *)&boot_info);
    if (EFI_ERROR(status)) {
        efi_print(SystemTable, L"Failed to allocate boot_info\r\n");
        for (;;) {}
        return status;
    }
    memset(boot_info, 0, 4096);
    boot_info->magic = GW_BOOT_MAGIC;
    boot_info->kernel_physical_base = kernel_base;
    boot_info->kernel_entry = kernel_entry;

    /* Graphics Output Protocol — required for real hardware display after ExitBootServices */
    {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
        status = SystemTable->BootServices->LocateProtocol(
            (EFI_GUID *)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, NULL, (void **)&gop);
        if (!EFI_ERROR(status) && gop && gop->Mode) {
            boot_info->framebuffer_base = gop->Mode->FrameBufferBase;
            boot_info->framebuffer_size = (uint64_t)gop->Mode->FrameBufferSize;
            if (gop->Mode->Info) {
                boot_info->framebuffer_width = gop->Mode->Info->HorizontalResolution;
                boot_info->framebuffer_height = gop->Mode->Info->VerticalResolution;
                boot_info->framebuffer_pitch = gop->Mode->Info->PixelsPerScanLine * 4;
                boot_info->framebuffer_bpp = 32;
            }
            efi_print(SystemTable, L"GOP framebuffer: ");
            efi_print_hex(SystemTable, boot_info->framebuffer_base);
            efi_print(SystemTable, L" ");
            efi_print_dec(SystemTable, boot_info->framebuffer_width);
            efi_print(SystemTable, L"x");
            efi_print_dec(SystemTable, boot_info->framebuffer_height);
            efi_print(SystemTable, L"\r\n");
        } else {
            efi_print(SystemTable, L"GOP not available — kernel will try VGA text\r\n");
        }
    }

    /* Get memory map */
    status = get_memory_map(SystemTable, &mmap, &mmap_size, &mmap_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        efi_print(SystemTable, L"GetMemoryMap failed\r\n");
        for (;;) {}
        return status;
    }

    boot_info->memory_map = (uint64_t)mmap;
    boot_info->memory_map_size = mmap_size;
    boot_info->memory_map_desc_size = desc_size;
    boot_info->memory_map_desc_version = desc_ver;

    efi_print(SystemTable, L"Memory map obtained (");
    efi_print_dec(SystemTable, mmap_size / desc_size);
    efi_print(SystemTable, L" descriptors)\r\n");
    efi_print(SystemTable, L"Exiting boot services...\r\n");

    /* ExitBootServices - after this ConOut is invalid */
    status = SystemTable->BootServices->ExitBootServices(ImageHandle, mmap_key);
    if (EFI_ERROR(status)) {
        /* Retry once with fresh map */
        status = get_memory_map(SystemTable, &mmap, &mmap_size, &mmap_key, &desc_size, &desc_ver);
        if (!EFI_ERROR(status)) {
            boot_info->memory_map = (uint64_t)mmap;
            boot_info->memory_map_size = mmap_size;
            boot_info->memory_map_desc_size = desc_size;
            status = SystemTable->BootServices->ExitBootServices(ImageHandle, mmap_key);
        }
    }

    if (EFI_ERROR(status)) {
        /* Cannot print reliably; just hang */
        for (;;) {}
    }

    /* Transfer control to GWKernel.
     * Calling convention: System V AMD64 for the kernel.
     * First argument: pointer to GW_BOOT_INFO
     */
    typedef void (*kernel_entry_t)(GW_BOOT_INFO *);
    kernel_entry_t entry = (kernel_entry_t)kernel_entry;
    entry(boot_info);

    /* Should never return */
    for (;;) {
        __asm__ volatile ("hlt");
    }

    return EFI_SUCCESS;
}
