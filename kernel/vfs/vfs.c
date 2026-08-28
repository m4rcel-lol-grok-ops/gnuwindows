/*
 * VFS: C: = RAMFS, D: = FAT32 ESP
 */

#include <gw/vfs.h>
#include <gw/fat.h>
#include <gw/heap.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

static vfs_node_t nodes[VFS_MAX_NODES];
static int node_count;
static vfs_node_t *root;
static vfs_node_t *cwd;
static vfs_drive_t cur_drive = VFS_DRIVE_C;

static void *memset_l(void *s, int c, size_t n) {
    uint8_t *p = s; while (n--) *p++ = (uint8_t)c; return s;
}
static void strcpy_l(char *d, const char *s, size_t max) {
    size_t i = 0;
    if (!s) { d[0] = 0; return; }
    while (s[i] && i + 1 < max) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}
static size_t strlen_l(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}

static vfs_node_t *alloc_node(const char *name, vfs_type_t type, vfs_node_t *parent) {
    if (node_count >= VFS_MAX_NODES) return NULL;
    vfs_node_t *n = &nodes[node_count++];
    memset_l(n, 0, sizeof(*n));
    strcpy_l(n->name, name, VFS_NAME_MAX);
    n->type = type;
    n->parent = parent;
    if (parent) {
        n->sibling = parent->child;
        parent->child = n;
    }
    return n;
}

/* Returns drive and advances path past "X:" and slashes. */
static vfs_drive_t parse_drive(const char **path_io) {
    const char *p = *path_io;
    if (!p || !p[0]) return cur_drive;
    if ((p[0] == 'C' || p[0] == 'c') && p[1] == ':') {
        p += 2;
        while (*p == '/' || *p == '\\') p++;
        *path_io = p;
        return VFS_DRIVE_C;
    }
    if ((p[0] == 'D' || p[0] == 'd') && p[1] == ':') {
        p += 2;
        while (*p == '/' || *p == '\\') p++;
        *path_io = p;
        return VFS_DRIVE_D;
    }
    return cur_drive;
}

static const char *strip_c(const char *path) {
    if ((path[0] == 'C' || path[0] == 'c') && path[1] == ':')
        path += 2;
    while (*path == '/' || *path == '\\') path++;
    return path;
}

static vfs_node_t *lookup_rel(vfs_node_t *base, const char *path) {
    if (!path || !*path) return base;
    path = strip_c(path);
    vfs_node_t *cur = base;
    char part[VFS_NAME_MAX];
    while (*path) {
        size_t i = 0;
        while (*path && *path != '/' && *path != '\\' && i + 1 < VFS_NAME_MAX)
            part[i++] = *path++;
        part[i] = 0;
        while (*path == '/' || *path == '\\') path++;
        if (!part[0] || streq(part, ".")) continue;
        if (streq(part, "..")) {
            if (cur->parent) cur = cur->parent;
            continue;
        }
        if (cur->type != VFS_TYPE_DIR) return NULL;
        vfs_node_t *c = cur->child;
        while (c && !streq(c->name, part)) c = c->sibling;
        if (!c) return NULL;
        cur = c;
    }
    return cur;
}

vfs_drive_t vfs_current_drive(void) { return cur_drive; }

vfs_node_t *vfs_cwd(void) { return cwd; }

void vfs_getcwd(char *buf, size_t maxlen) {
    if (cur_drive == VFS_DRIVE_D) {
        char fatp[128];
        fat_getcwd(fatp, sizeof(fatp));
        /* D: + fat path */
        size_t i = 0;
        if (i + 1 < maxlen) buf[i++] = 'D';
        if (i + 1 < maxlen) buf[i++] = ':';
        size_t j = 0;
        while (fatp[j] && i + 1 < maxlen) buf[i++] = fatp[j++];
        buf[i] = 0;
        return;
    }
    /* C: walk parents */
    char parts[8][VFS_NAME_MAX];
    int n = 0;
    vfs_node_t *p = cwd;
    while (p && n < 8) {
        if (p->name[0]) {
            strcpy_l(parts[n], p->name, VFS_NAME_MAX);
            n++;
        }
        p = p->parent;
    }
    size_t i = 0;
    if (i + 1 < maxlen) buf[i++] = 'C';
    if (i + 1 < maxlen) buf[i++] = ':';
    if (i + 1 < maxlen) buf[i++] = '/';
    for (int k = n - 1; k >= 0; k--) {
        size_t j = 0;
        while (parts[k][j] && i + 1 < maxlen) buf[i++] = parts[k][j++];
        if (k && i + 1 < maxlen) buf[i++] = '/';
    }
    buf[i] = 0;
}

vfs_node_t *vfs_lookup(const char *path) {
    if (!path || !*path) return cwd;
    const char *p = path;
    vfs_drive_t d = parse_drive(&p);
    if (d == VFS_DRIVE_D)
        return NULL; /* FAT has no vfs_node; callers use drive-aware ops */
    if ((path[0] == 'C' || path[0] == 'c') && path[1] == ':')
        return lookup_rel(root, path);
    if (path[0] == '/' || path[0] == '\\')
        return lookup_rel(root, path);
    if (cur_drive == VFS_DRIVE_D)
        return NULL;
    return lookup_rel(cwd, path);
}

int vfs_chdir(const char *path) {
    if (!path) return -1;
    const char *p = path;
    vfs_drive_t d = parse_drive(&p);

    if (d == VFS_DRIVE_D || ((path[0] == 'D' || path[0] == 'd') && path[1] == ':')) {
        if (fat_chdir(*p ? p : "/") != 0 && fat_chdir(*p ? p : "") != 0)
            return -1;
        cur_drive = VFS_DRIVE_D;
        return 0;
    }

    if ((path[0] == 'C' || path[0] == 'c') && path[1] == ':') {
        vfs_node_t *n = lookup_rel(root, path);
        if (!n || n->type != VFS_TYPE_DIR) return -1;
        cwd = n;
        cur_drive = VFS_DRIVE_C;
        return 0;
    }

    if (cur_drive == VFS_DRIVE_D) {
        if (fat_chdir(path) != 0) return -1;
        return 0;
    }

    vfs_node_t *n = vfs_lookup(path);
    if (!n || n->type != VFS_TYPE_DIR) return -1;
    cwd = n;
    cur_drive = VFS_DRIVE_C;
    return 0;
}

static int split_parent(const char *path, char *parent_out, char *name_out) {
    char tmp[VFS_PATH_MAX];
    strcpy_l(tmp, path, VFS_PATH_MAX);
    int last = -1;
    for (int i = 0; tmp[i]; i++)
        if (tmp[i] == '/' || tmp[i] == '\\') last = i;
    if (last < 0) {
        parent_out[0] = 0;
        strcpy_l(name_out, tmp, VFS_NAME_MAX);
        return 0;
    }
    tmp[last] = 0;
    strcpy_l(parent_out, tmp, VFS_PATH_MAX);
    strcpy_l(name_out, tmp + last + 1, VFS_NAME_MAX);
    return 0;
}

int vfs_mkdir(const char *path) {
    const char *p = path;
    vfs_drive_t d = parse_drive(&p);
    if (d == VFS_DRIVE_D || cur_drive == VFS_DRIVE_D) {
        if ((path[0] == 'D' || path[0] == 'd') && path[1] == ':')
            return fat_mkdir(p);
        return fat_mkdir(path);
    }
    char parent[VFS_PATH_MAX], name[VFS_NAME_MAX];
    split_parent(path, parent, name);
    if (!name[0]) return -1;
    vfs_node_t *par = parent[0] ? vfs_lookup(parent) : cwd;
    if (!par || par->type != VFS_TYPE_DIR) return -1;
    if (lookup_rel(par, name)) return -1;
    if (!alloc_node(name, VFS_TYPE_DIR, par)) return -1;
    return 0;
}

int vfs_create_file(const char *path) {
    const char *p = path;
    vfs_drive_t d = parse_drive(&p);
    if (d == VFS_DRIVE_D || cur_drive == VFS_DRIVE_D)
        return fat_write_file((d == VFS_DRIVE_D) ? p : path, "", 0);

    char parent[VFS_PATH_MAX], name[VFS_NAME_MAX];
    split_parent(path, parent, name);
    if (!name[0]) return -1;
    vfs_node_t *par = parent[0] ? vfs_lookup(parent) : cwd;
    if (!par || par->type != VFS_TYPE_DIR) return -1;
    if (lookup_rel(par, name)) return -1;
    vfs_node_t *f = alloc_node(name, VFS_TYPE_FILE, par);
    if (!f) return -1;
    f->capacity = 512;
    f->data = kmalloc(f->capacity);
    if (!f->data) return -1;
    f->size = 0;
    return 0;
}

int vfs_write(const char *path, const void *buf, size_t len) {
    const char *p = path;
    vfs_drive_t d = parse_drive(&p);
    if (d == VFS_DRIVE_D)
        return fat_write_file(p, buf, len);
    if (cur_drive == VFS_DRIVE_D && !((path[0] == 'C' || path[0] == 'c') && path[1] == ':'))
        return fat_write_file(path, buf, len);

    vfs_node_t *n = vfs_lookup(path);
    if (!n) {
        if (vfs_create_file(path) != 0) return -1;
        n = vfs_lookup(path);
    }
    if (!n || n->type != VFS_TYPE_FILE) return -1;
    if (len > VFS_MAX_FILE_SIZE) len = VFS_MAX_FILE_SIZE;
    if (len > n->capacity) {
        size_t nc = len + 256;
        if (nc > VFS_MAX_FILE_SIZE) nc = VFS_MAX_FILE_SIZE;
        uint8_t *nd = kmalloc(nc);
        if (!nd) return -1;
        for (size_t i = 0; i < n->size; i++) nd[i] = n->data[i];
        n->data = nd;
        n->capacity = nc;
    }
    const uint8_t *s = buf;
    for (size_t i = 0; i < len; i++) n->data[i] = s[i];
    n->size = len;
    return (int)len;
}

int vfs_read(const char *path, void *buf, size_t maxlen, size_t *out_len) {
    const char *p = path;
    vfs_drive_t d = parse_drive(&p);
    if (d == VFS_DRIVE_D)
        return fat_read_file(p, buf, maxlen, out_len);
    if (cur_drive == VFS_DRIVE_D && !((path[0] == 'C' || path[0] == 'c') && path[1] == ':'))
        return fat_read_file(path, buf, maxlen, out_len);

    vfs_node_t *n = vfs_lookup(path);
    if (!n || n->type != VFS_TYPE_FILE) return -1;
    size_t ncopy = n->size < maxlen ? n->size : maxlen;
    uint8_t *dst = buf;
    for (size_t i = 0; i < ncopy; i++) dst[i] = n->data[i];
    if (out_len) *out_len = ncopy;
    return (int)ncopy;
}

int vfs_unlink(const char *path) {
    const char *p = path;
    vfs_drive_t d = parse_drive(&p);
    if (d == VFS_DRIVE_D)
        return fat_unlink(p);
    if (cur_drive == VFS_DRIVE_D && !((path[0] == 'C' || path[0] == 'c') && path[1] == ':'))
        return fat_unlink(path);

    vfs_node_t *n = vfs_lookup(path);
    if (!n || n == root) return -1;
    if (n->type == VFS_TYPE_DIR && n->child) return -1;
    vfs_node_t *par = n->parent;
    if (!par) return -1;
    vfs_node_t **pp = &par->child;
    while (*pp && *pp != n) pp = &(*pp)->sibling;
    if (*pp) *pp = n->sibling;
    n->type = 0;
    n->name[0] = 0;
    return 0;
}

void vfs_list(const char *path) {
    const char *p = path ? path : "";
    vfs_drive_t d = cur_drive;
    if (p[0]) d = parse_drive(&p);

    if (d == VFS_DRIVE_D) {
        serial_write("Directory of D:");
        if (p[0]) {
            serial_write("/");
            serial_write(p);
        }
        serial_write("\n");
        fat_list(p[0] ? p : "");
        return;
    }

    vfs_node_t *n = path && path[0] ? vfs_lookup(path) : cwd;
    if (!n) {
        serial_write("not found\n");
        return;
    }
    if (n->type == VFS_TYPE_FILE) {
        serial_write(n->name);
        serial_write("  (file) ");
        serial_write_dec(n->size);
        serial_write(" bytes\n");
        return;
    }
    serial_write("Directory of C:/");
    serial_write(n->name[0] ? n->name : "");
    serial_write("\n");
    for (vfs_node_t *c = n->child; c; c = c->sibling) {
        serial_write(c->type == VFS_TYPE_DIR ? " <DIR>  " : "        ");
        serial_write(c->name);
        if (c->type == VFS_TYPE_FILE) {
            serial_write("  ");
            serial_write_dec(c->size);
        }
        serial_write("\n");
    }
}

int vfs_init(void) {
    node_count = 0;
    memset_l(nodes, 0, sizeof(nodes));
    root = alloc_node("", VFS_TYPE_DIR, NULL);
    cwd = root;
    cur_drive = VFS_DRIVE_C;

    vfs_mkdir("Windows");
    vfs_mkdir("Users");
    vfs_mkdir("Users/root");
    vfs_mkdir("Program Files");
    vfs_mkdir("System");
    vfs_mkdir("Temp");
    vfs_chdir("Users/root");
    vfs_write("readme.txt", "Welcome to GNU/Windows\nGWKernel RAMFS on C:\n", 44);

    serial_write("VFS: C: = RAMFS, D: = FAT ESP (after mount)\n");
    (void)strlen_l;
    return 0;
}
