/*
 * FAT32 R/W with root + simple subdirectories (8.3 names)
 */

#include <gw/fat.h>
#include <gw/ata.h>
#include <gw/ahci.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

static uint32_t part_lba, fat_start, data_start, root_cluster;
static uint16_t bytes_per_sec;
static uint8_t  sec_per_cluster, num_fats;
static uint32_t fat_size_sectors, total_clusters;
static int mounted;

static uint32_t cwd_cluster;
static char cwd_path[128];

static uint8_t sector[512];

static void memset_l(void *d, int c, size_t n) {
    uint8_t *p = d; while (n--) *p++ = (uint8_t)c;
}
static void memcpy_l(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s; while (n--) *dd++ = *ss++;
}
static size_t strlen_l(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}

static void to_83(const char *a, char name83[11]) {
    int i = 0, j = 0;
    while (a[i] && a[i] != '.' && a[i] != '/' && a[i] != '\\' && j < 8) {
        char c = a[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        name83[j++] = c;
    }
    while (j < 8) name83[j++] = ' ';
    if (a[i] == '.') i++;
    int k = 0;
    while (a[i] && a[i] != '/' && a[i] != '\\' && k < 3) {
        char c = a[i++];
        if (c >= 'a' && c <= 'z') c -= 32;
        name83[8 + k++] = c;
    }
    while (k < 3) name83[8 + k++] = ' ';
}

static int eq83(const char *a, const char name83[11]) {
    char n[11]; to_83(a, n);
    for (int i = 0; i < 11; i++) if (n[i] != name83[i]) return 0;
    return 1;
}

static void print_83(const char name[11]) {
    for (int i = 0; i < 8 && name[i] != ' '; i++) serial_putc(name[i]);
    if (name[8] != ' ') {
        serial_putc('.');
        for (int i = 8; i < 11 && name[i] != ' '; i++) serial_putc(name[i]);
    }
}

static int use_ahci;

static int read_abs(uint32_t lba, void *buf) {
    if (use_ahci) return ahci_read(lba, 1, buf);
    return ata_read_sectors(lba, 1, buf);
}
static int write_abs(uint32_t lba, const void *buf) {
    if (use_ahci) return ahci_write(lba, 1, buf);
    return ata_write_sectors(lba, 1, buf);
}
static uint32_t cluster_to_lba(uint32_t cl) {
    return data_start + (cl - 2) * sec_per_cluster;
}

static uint32_t fat_get(uint32_t cl) {
    uint32_t off = cl * 4;
    uint32_t sec = fat_start + (off / bytes_per_sec);
    uint32_t idx = off % bytes_per_sec;
    if (read_abs(sec, sector) != 0) return 0x0FFFFFF7;
    uint32_t v = sector[idx] | (sector[idx+1]<<8) | (sector[idx+2]<<16) | (sector[idx+3]<<24);
    return v & 0x0FFFFFFF;
}

static int fat_set(uint32_t cl, uint32_t val) {
    uint32_t off = cl * 4;
    uint32_t sec = fat_start + (off / bytes_per_sec);
    uint32_t idx = off % bytes_per_sec;
    if (read_abs(sec, sector) != 0) return -1;
    uint32_t old = sector[idx] | (sector[idx+1]<<8) | (sector[idx+2]<<16) | (sector[idx+3]<<24);
    uint32_t neu = (old & 0xF0000000) | (val & 0x0FFFFFFF);
    sector[idx]=(uint8_t)neu; sector[idx+1]=(uint8_t)(neu>>8);
    sector[idx+2]=(uint8_t)(neu>>16); sector[idx+3]=(uint8_t)(neu>>24);
    return write_abs(sec, sector);
}

static uint32_t fat_alloc_cluster(void) {
    for (uint32_t c = 3; c < total_clusters + 2; c++) {
        if (fat_get(c) == 0) {
            if (fat_set(c, 0x0FFFFFFF) != 0) return 0;
            return c;
        }
    }
    return 0;
}

static int fat_free_chain(uint32_t cl) {
    while (cl >= 2 && cl < 0x0FFFFFF8) {
        uint32_t n = fat_get(cl);
        if (fat_set(cl, 0) != 0) return -1;
        cl = n;
    }
    return 0;
}

/* Find name in directory cluster chain. Returns 1 if found.
 * out_cluster = first cluster, out_size = size, out_isdir, out_lba/off of entry */
static int dir_find(uint32_t dir_cl, const char *name,
                    uint32_t *out_cl, uint32_t *out_sz, int *out_dir,
                    uint32_t *ent_lba, int *ent_off) {
    while (dir_cl >= 2 && dir_cl < 0x0FFFFFF8) {
        for (uint8_t s = 0; s < sec_per_cluster; s++) {
            uint32_t lba = cluster_to_lba(dir_cl) + s;
            if (read_abs(lba, sector) != 0) return 0;
            for (int i = 0; i < 512; i += 32) {
                uint8_t *e = &sector[i];
                if (e[0] == 0x00) return 0;
                if (e[0] == 0xE5 || (e[11] & 0x08)) continue;
                if (!eq83(name, (char *)e)) continue;
                uint32_t cl = e[26] | (e[27]<<8) | (e[20]<<16) | (e[21]<<24);
                uint32_t sz = e[28] | (e[29]<<8) | (e[30]<<16) | (e[31]<<24);
                if (out_cl) *out_cl = cl;
                if (out_sz) *out_sz = sz;
                if (out_dir) *out_dir = (e[11] & 0x10) ? 1 : 0;
                if (ent_lba) *ent_lba = lba;
                if (ent_off) *ent_off = i;
                return 1;
            }
        }
        dir_cl = fat_get(dir_cl);
    }
    return 0;
}

/* Find free dir entry slot; may extend directory chain */
static int dir_alloc_slot(uint32_t dir_cl, uint32_t *ent_lba, int *ent_off) {
    uint32_t cur = dir_cl;
    for (;;) {
        for (uint8_t s = 0; s < sec_per_cluster; s++) {
            uint32_t lba = cluster_to_lba(cur) + s;
            if (read_abs(lba, sector) != 0) return -1;
            for (int i = 0; i < 512; i += 32) {
                if (sector[i] == 0x00 || sector[i] == 0xE5) {
                    *ent_lba = lba;
                    *ent_off = i;
                    return 0;
                }
            }
        }
        uint32_t n = fat_get(cur);
        if (n >= 0x0FFFFFF8) {
            uint32_t nc = fat_alloc_cluster();
            if (!nc) return -1;
            if (fat_set(cur, nc) != 0) return -1;
            memset_l(sector, 0, 512);
            for (uint8_t s = 0; s < sec_per_cluster; s++)
                if (write_abs(cluster_to_lba(nc) + s, sector) != 0) return -1;
            cur = nc;
            continue;
        }
        cur = n;
    }
}

/* Resolve path to parent dir cluster + final component name.
 * absolute if starts with / or \ ; else relative to cwd.
 * For "" returns cwd with name empty. */
static int resolve_parent(const char *path, uint32_t *parent_cl, char *final, int final_max) {
    uint32_t cl = cwd_cluster;
    const char *p = path;
    if (!p) p = "";
    if (*p == '/' || *p == '\\') {
        cl = root_cluster;
        p++;
    }
    final[0] = 0;
    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        char part[16];
        int i = 0;
        while (*p && *p != '/' && *p != '\\' && i < 15) part[i++] = *p++;
        part[i] = 0;
        /* peek if more components */
        const char *q = p;
        while (*q == '/' || *q == '\\') q++;
        if (!*q) {
            /* final component */
            int j = 0;
            while (part[j] && j + 1 < final_max) { final[j] = part[j]; j++; }
            final[j] = 0;
            *parent_cl = cl;
            return 0;
        }
        /* intermediate: must be directory */
        if (part[0] == '.' && part[1] == 0) continue;
        if (part[0] == '.' && part[1] == '.' && part[2] == 0) {
            /* limited: go to root on .. from anywhere (no parent tracking) */
            cl = root_cluster;
            continue;
        }
        uint32_t ncl; int isdir = 0;
        if (!dir_find(cl, part, &ncl, 0, &isdir, 0, 0) || !isdir)
            return -1;
        cl = ncl;
    }
    *parent_cl = cl;
    final[0] = 0;
    return 0;
}

static int resolve_full(const char *path, uint32_t *cl, uint32_t *sz, int *isdir) {
    if (!path || !path[0] || (path[0] == '/' && !path[1]) || (path[0] == '\\' && !path[1])) {
        *cl = (path && (path[0] == '/' || path[0] == '\\')) ? root_cluster : cwd_cluster;
        if (sz) *sz = 0;
        if (isdir) *isdir = 1;
        return 0;
    }
    uint32_t parent;
    char name[16];
    if (resolve_parent(path, &parent, name, sizeof(name)) != 0) return -1;
    if (!name[0]) {
        *cl = parent;
        if (isdir) *isdir = 1;
        if (sz) *sz = 0;
        return 0;
    }
    if (!dir_find(parent, name, cl, sz, isdir, 0, 0)) return -1;
    return 0;
}

int fat_mount(void) {
    mounted = 0;
    use_ahci = 0;
    /* Prefer AHCI on modern hardware; fall back to legacy ATA PIO */
    if (ahci_init() == 0) {
        use_ahci = 1;
        if (read_abs(0, sector) == 0) {
            serial_write("FAT: using AHCI backend\n");
            goto have_mbr;
        }
        serial_write("FAT: AHCI present but LBA0 read failed, trying ATA\n");
        use_ahci = 0;
    }
    if (ata_init() != 0) return -1;
    serial_write("FAT: using ATA PIO backend\n");
    if (read_abs(0, sector) != 0) return -1;
have_mbr:
    if (sector[510] != 0x55 || sector[511] != 0xAA) return -1;

    if (sector[0x1BE + 4] == 0xEE) {
        if (read_abs(1, sector) != 0) return -1;
        uint32_t pe_lba = sector[72]|(sector[73]<<8)|(sector[74]<<16)|(sector[75]<<24);
        if (read_abs(pe_lba, sector) != 0) return -1;
        part_lba = sector[32]|(sector[33]<<8)|(sector[34]<<16)|(sector[35]<<24);
        serial_write("FAT: GPT partition LBA=");
        serial_write_dec(part_lba);
        serial_write("\n");
    } else {
        part_lba = sector[0x1BE + 8]|(sector[0x1BE + 9]<<8)|(sector[0x1BE + 10]<<16)|(sector[0x1BE + 11]<<24);
    }

    if (read_abs(part_lba, sector) != 0) return -1;
    bytes_per_sec = sector[11]|(sector[12]<<8);
    sec_per_cluster = sector[13];
    uint16_t reserved = sector[14]|(sector[15]<<8);
    num_fats = sector[16];
    uint16_t root_ents = sector[17]|(sector[18]<<8);
    uint16_t fatsz16 = sector[22]|(sector[23]<<8);
    uint32_t fatsz32 = sector[36]|(sector[37]<<8)|(sector[38]<<16)|(sector[39]<<24);
    root_cluster = sector[44]|(sector[45]<<8)|(sector[46]<<16)|(sector[47]<<24);
    uint32_t totsec = sector[32]|(sector[33]<<8)|(sector[34]<<16)|(sector[35]<<24);
    if (!totsec) totsec = sector[19]|(sector[20]<<8);
    if (bytes_per_sec != 512) return -1;

    fat_size_sectors = fatsz16 ? fatsz16 : fatsz32;
    fat_start = part_lba + reserved;
    uint32_t root_secs = ((root_ents * 32) + 511) / 512;
    data_start = fat_start + num_fats * fat_size_sectors + root_secs;
    uint32_t data_secs = totsec > (data_start - part_lba) ? totsec - (data_start - part_lba) : 0;
    total_clusters = sec_per_cluster ? data_secs / sec_per_cluster : 0;
    uint32_t fat_entries = (fat_size_sectors * 512) / 4;
    if (total_clusters == 0 || total_clusters > fat_entries)
        total_clusters = fat_entries > 2 ? fat_entries - 2 : 1000;

    cwd_cluster = root_cluster;
    cwd_path[0] = '/'; cwd_path[1] = 0;

    serial_write("FAT32: mount OK (R/W + subdirs), root=");
    serial_write_dec(root_cluster);
    serial_write("\n");
    mounted = 1;
    return 0;
}

void fat_getcwd(char *buf, size_t maxlen) {
    size_t i = 0;
    while (cwd_path[i] && i + 1 < maxlen) { buf[i] = cwd_path[i]; i++; }
    buf[i] = 0;
}

int fat_chdir(const char *path) {
    if (!mounted) return -1;
    uint32_t cl; int isdir = 0;
    if (resolve_full(path, &cl, 0, &isdir) != 0 || !isdir) return -1;
    cwd_cluster = cl;
    /* crude path display */
    if (path && (path[0] == '/' || path[0] == '\\')) {
        size_t n = strlen_l(path);
        if (n >= sizeof(cwd_path)) n = sizeof(cwd_path) - 1;
        memcpy_l(cwd_path, path, n);
        cwd_path[n] = 0;
        if (n == 0) { cwd_path[0] = '/'; cwd_path[1] = 0; }
    } else if (path && path[0] == '.' && path[1] == '.' && path[2] == 0) {
        cwd_path[0] = '/'; cwd_path[1] = 0;
        cwd_cluster = root_cluster;
    } else if (path && path[0]) {
        size_t n = strlen_l(cwd_path);
        if (n > 1 && cwd_path[n-1] != '/') {
            if (n + 1 < sizeof(cwd_path)) cwd_path[n++] = '/';
        }
        size_t m = strlen_l(path);
        if (n + m < sizeof(cwd_path)) {
            memcpy_l(cwd_path + n, path, m);
            cwd_path[n+m] = 0;
        }
    }
    return 0;
}

void fat_list(const char *path) {
    if (!mounted) { serial_write("FAT: not mounted\n"); return; }
    uint32_t cl; int isdir = 0;
    if (resolve_full(path ? path : "", &cl, 0, &isdir) != 0 || !isdir) {
        serial_write("FAT: not a directory\n");
        return;
    }
    serial_write("FAT directory:\n");
    uint32_t cur = cl;
    while (cur >= 2 && cur < 0x0FFFFFF8) {
        for (uint8_t s = 0; s < sec_per_cluster; s++) {
            if (read_abs(cluster_to_lba(cur) + s, sector) != 0) return;
            for (int i = 0; i < 512; i += 32) {
                uint8_t *e = &sector[i];
                if (e[0] == 0x00) return;
                if (e[0] == 0xE5 || (e[11] & 0x08)) continue;
                if (e[11] & 0x10) serial_write(" <DIR>  ");
                else serial_write("        ");
                print_83((char *)e);
                if (!(e[11] & 0x10)) {
                    uint32_t sz = e[28]|(e[29]<<8)|(e[30]<<16)|(e[31]<<24);
                    serial_write("  ");
                    serial_write_dec(sz);
                }
                serial_write("\n");
            }
        }
        cur = fat_get(cur);
    }
}

int fat_mkdir(const char *path) {
    if (!mounted || !path || !path[0]) return -1;
    uint32_t parent;
    char name[16];
    if (resolve_parent(path, &parent, name, sizeof(name)) != 0 || !name[0]) return -1;
    if (dir_find(parent, name, 0, 0, 0, 0, 0)) return -1; /* exists */

    uint32_t new_cl = fat_alloc_cluster();
    if (!new_cl) return -1;
    /* zero cluster and write . and .. */
    memset_l(sector, 0, 512);
    /* . */
    memcpy_l(sector, ".          ", 11);
    sector[11] = 0x10;
    sector[26] = (uint8_t)(new_cl);
    sector[27] = (uint8_t)(new_cl >> 8);
    sector[20] = (uint8_t)(new_cl >> 16);
    sector[21] = (uint8_t)(new_cl >> 24);
    /* .. */
    memcpy_l(sector + 32, "..         ", 11);
    sector[32+11] = 0x10;
    sector[32+26] = (uint8_t)(parent);
    sector[32+27] = (uint8_t)(parent >> 8);
    sector[32+20] = (uint8_t)(parent >> 16);
    sector[32+21] = (uint8_t)(parent >> 24);
    if (write_abs(cluster_to_lba(new_cl), sector) != 0) return -1;
    for (uint8_t s = 1; s < sec_per_cluster; s++) {
        memset_l(sector, 0, 512);
        if (write_abs(cluster_to_lba(new_cl) + s, sector) != 0) return -1;
    }

    uint32_t elba; int eoff;
    if (dir_alloc_slot(parent, &elba, &eoff) != 0) return -1;
    if (read_abs(elba, sector) != 0) return -1;
    uint8_t *e = &sector[eoff];
    memset_l(e, 0, 32);
    char n83[11]; to_83(name, n83);
    memcpy_l(e, n83, 11);
    e[11] = 0x10;
    e[26] = (uint8_t)(new_cl);
    e[27] = (uint8_t)(new_cl >> 8);
    e[20] = (uint8_t)(new_cl >> 16);
    e[21] = (uint8_t)(new_cl >> 24);
    if (write_abs(elba, sector) != 0) return -1;
    ata_flush();
    serial_write("FAT: mkdir ");
    serial_write(path);
    serial_write("\n");
    return 0;
}

int fat_read_file(const char *path, void *buf, size_t maxlen, size_t *out_len) {
    if (!mounted) return -1;
    uint32_t file_cl, file_sz; int isdir = 0;
    if (resolve_full(path, &file_cl, &file_sz, &isdir) != 0 || isdir) return -1;
    size_t done = 0;
    uint8_t *dst = buf;
    uint32_t cl = file_cl;
    while (cl >= 2 && cl < 0x0FFFFFF8 && done < file_sz && done < maxlen) {
        for (uint8_t s = 0; s < sec_per_cluster && done < file_sz && done < maxlen; s++) {
            if (read_abs(cluster_to_lba(cl) + s, sector) != 0) return -1;
            for (int i = 0; i < 512 && done < file_sz && done < maxlen; i++)
                dst[done++] = sector[i];
        }
        cl = fat_get(cl);
    }
    if (out_len) *out_len = done;
    return (int)done;
}

int fat_write_file(const char *path, const void *data, size_t len) {
    if (!mounted || !path || !path[0] || len > 512 * 64) return -1;
    uint32_t parent;
    char name[16];
    if (resolve_parent(path, &parent, name, sizeof(name)) != 0 || !name[0]) return -1;

    uint32_t old_cl = 0, elba = 0; int eoff = -1, isdir = 0;
    int exists = dir_find(parent, name, &old_cl, 0, &isdir, &elba, &eoff);
    if (exists && isdir) return -1;
    if (exists && old_cl >= 2) fat_free_chain(old_cl);
    if (!exists) {
        if (dir_alloc_slot(parent, &elba, &eoff) != 0) return -1;
    }

    uint32_t first_cl = 0, prev = 0;
    const uint8_t *src = data;
    size_t remaining = len;
    if (len == 0) first_cl = 0;
    else {
        while (remaining > 0 || first_cl == 0) {
            uint32_t c = fat_alloc_cluster();
            if (!c) return -1;
            if (!first_cl) first_cl = c;
            else if (fat_set(prev, c) != 0) return -1;
            prev = c;
            for (uint8_t s = 0; s < sec_per_cluster; s++) {
                memset_l(sector, 0, 512);
                size_t chunk = remaining > 512 ? 512 : remaining;
                for (size_t i = 0; i < chunk; i++) sector[i] = src[i];
                src += chunk; remaining -= chunk;
                if (write_abs(cluster_to_lba(c) + s, sector) != 0) return -1;
                if (remaining == 0) {
                    for (uint8_t z = s + 1; z < sec_per_cluster; z++) {
                        memset_l(sector, 0, 512);
                        write_abs(cluster_to_lba(c) + z, sector);
                    }
                    break;
                }
            }
            if (remaining == 0) break;
        }
    }

    if (read_abs(elba, sector) != 0) return -1;
    uint8_t *e = &sector[eoff];
    memset_l(e, 0, 32);
    char n83[11]; to_83(name, n83);
    memcpy_l(e, n83, 11);
    e[11] = 0x20;
    e[20] = (uint8_t)(first_cl >> 16); e[21] = (uint8_t)(first_cl >> 24);
    e[26] = (uint8_t)(first_cl); e[27] = (uint8_t)(first_cl >> 8);
    e[28] = (uint8_t)(len); e[29] = (uint8_t)(len >> 8);
    e[30] = (uint8_t)(len >> 16); e[31] = (uint8_t)(len >> 24);
    if (write_abs(elba, sector) != 0) return -1;
    ata_flush();
    serial_write("FAT: wrote ");
    serial_write(path);
    serial_write(" (");
    serial_write_dec(len);
    serial_write(" bytes)\n");
    return (int)len;
}

int fat_unlink(const char *path) {
    if (!mounted || !path || !path[0]) return -1;
    uint32_t parent;
    char name[16];
    if (resolve_parent(path, &parent, name, sizeof(name)) != 0 || !name[0])
        return -1;

    uint32_t cl = 0, elba = 0;
    int eoff = -1, isdir = 0;
    if (!dir_find(parent, name, &cl, 0, &isdir, &elba, &eoff))
        return -1;

    if (isdir) {
        /* only allow empty dirs: check for entries beyond . and .. */
        uint32_t cur = cl;
        while (cur >= 2 && cur < 0x0FFFFFF8) {
            for (uint8_t s = 0; s < sec_per_cluster; s++) {
                if (read_abs(cluster_to_lba(cur) + s, sector) != 0) return -1;
                for (int i = 0; i < 512; i += 32) {
                    uint8_t *e = &sector[i];
                    if (e[0] == 0x00) goto empty_ok;
                    if (e[0] == 0xE5) continue;
                    if (e[0] == '.' && (e[1] == ' ' || (e[1] == '.' && e[2] == ' ')))
                        continue;
                    return -1; /* non-empty */
                }
            }
            cur = fat_get(cur);
        }
    empty_ok:
        (void)0;
    }

    if (cl >= 2)
        fat_free_chain(cl);

    if (read_abs(elba, sector) != 0) return -1;
    sector[eoff] = 0xE5; /* mark deleted */
    if (write_abs(elba, sector) != 0) return -1;
    ata_flush();
    serial_write("FAT: unlinked ");
    serial_write(path);
    serial_write("\n");
    return 0;
}
