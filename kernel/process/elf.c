/*
 * Minimal ELF64 PT_LOAD loader for GWKernel
 */

#include <gw/elf.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

#define EI_MAG0 0x7F
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1

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
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) Elf64_Phdr;

static void memcpy_l(void *d, const void *s, size_t n) {
    uint8_t *dd = d;
    const uint8_t *ss = s;
    while (n--) *dd++ = *ss++;
}

static void memset_l(void *d, int c, size_t n) {
    uint8_t *dd = d;
    while (n--) *dd++ = (uint8_t)c;
}

int elf_load(const void *data, size_t size, elf_info_t *out) {
    if (!data || size < sizeof(Elf64_Ehdr) || !out)
        return -1;

    const Elf64_Ehdr *eh = data;
    if (eh->e_ident[0] != EI_MAG0 || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        serial_write("elf: bad magic\n");
        return -1;
    }
    if (eh->e_ident[4] != 2) { /* ELFCLASS64 */
        serial_write("elf: not 64-bit\n");
        return -1;
    }
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64) {
        serial_write("elf: not x86_64 EXEC\n");
        return -1;
    }
    if (eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > size)
        return -1;

    uint64_t min_v = ~0ULL, max_v = 0;
    const uint8_t *base = data;
    const Elf64_Phdr *ph = (const Elf64_Phdr *)(base + eh->e_phoff);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_vaddr < min_v) min_v = ph[i].p_vaddr;
        uint64_t end = ph[i].p_vaddr + ph[i].p_memsz;
        if (end > max_v) max_v = end;
    }
    if (min_v == ~0ULL) {
        serial_write("elf: no PT_LOAD\n");
        return -1;
    }

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_offset + ph[i].p_filesz > size) return -1;

        uint8_t *dst = (uint8_t *)(uintptr_t)ph[i].p_vaddr;
        memset_l(dst, 0, (size_t)ph[i].p_memsz);
        memcpy_l(dst, base + ph[i].p_offset, (size_t)ph[i].p_filesz);
    }

    out->entry = eh->e_entry;
    out->load_base = min_v;
    out->load_size = max_v - min_v;

    serial_write("elf: loaded entry=");
    serial_write_hex(out->entry);
    serial_write(" size=");
    serial_write_dec(out->load_size);
    serial_write("\n");
    return 0;
}
