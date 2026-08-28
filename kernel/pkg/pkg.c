/*
 * gwpkg — repository packages via single-file HTTP fetch.
 *
 * Repo serves:  GET {repo}/{name}.pkg
 * File format (text):
 *   ---MANIFEST---
 *   name: hello
 *   version: ...
 *   file: README.TXT
 *   ---FILE:README.TXT---
 *   <payload bytes>
 *   ---END---
 *
 * ESP is never a package store. Cache on C:/var/cache/gwpkg.
 */

#include <gw/pkg.h>
#include <gw/net.h>
#include <gw/vfs.h>
#include <gw/serial.h>
#include <stdint.h>
#include <stddef.h>

#define SOURCES_PATH  "C:/etc/gwpkg/sources"
#define CACHE_ROOT    "C:/var/cache/gwpkg"
#define DEFAULT_REPO  "http://10.0.2.100/v1"

static int starts(const char *s, const char *pfx) {
    while (*pfx) { if (*s++ != *pfx++) return 0; }
    return 1;
}
static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}
static void strcpy_l(char *d, const char *s, size_t max) {
    size_t i = 0;
    if (!s) { d[0] = 0; return; }
    while (s[i] && i + 1 < max) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static size_t strlen_l(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}
static void lower8(const char *name, char out[12]) {
    int i = 0;
    while (name[i] && i < 8) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        out[i++] = c;
    }
    out[i] = 0;
}
static void upper8(const char *name, char out[12]) {
    int i = 0;
    while (name[i] && i < 8) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[i++] = c;
    }
    out[i] = 0;
}

static void ensure_dirs(void) {
    vfs_chdir("C:/");
    vfs_mkdir("etc");
    vfs_mkdir("etc/gwpkg");
    vfs_mkdir("var");
    vfs_mkdir("var/cache");
    vfs_mkdir("var/cache/gwpkg");
    vfs_mkdir("Program Files");
}

void pkg_init(void) {
    ensure_dirs();
    const char *src =
        "# gwpkg repositories (HTTP). ESP is NOT a package store.\n"
        "http://10.0.2.100/v1\n";
    vfs_write(SOURCES_PATH, src, strlen_l(src));
    serial_write("gwpkg: repos in C:/etc/gwpkg/sources, cache C:/var/cache/gwpkg\n");
}

void pkg_repo_list(void) {
    char buf[512];
    size_t n = 0;
    serial_write("gwpkg repositories:\n");
    if (vfs_read(SOURCES_PATH, buf, sizeof(buf) - 1, &n) < 0) {
        serial_write("  (none)\n");
        return;
    }
    buf[n] = 0;
    serial_write(buf);
}

int pkg_repo_add(const char *url) {
    if (!url || !url[0]) return -1;
    char buf[512];
    size_t n = 0;
    vfs_read(SOURCES_PATH, buf, sizeof(buf) - 2, &n);
    if (n && buf[n - 1] != '\n') buf[n++] = '\n';
    size_t u = strlen_l(url);
    if (n + u + 2 >= sizeof(buf)) return -1;
    for (size_t i = 0; i < u; i++) buf[n++] = url[i];
    buf[n++] = '\n';
    vfs_write(SOURCES_PATH, buf, n);
    serial_write("gwpkg: added repo\n");
    return 0;
}

static int first_repo(char *out, size_t max) {
    char buf[512];
    size_t n = 0;
    if (vfs_read(SOURCES_PATH, buf, sizeof(buf) - 1, &n) < 0) {
        strcpy_l(out, DEFAULT_REPO, max);
        return 0;
    }
    buf[n] = 0;
    char *line = buf;
    while (*line) {
        char *eol = line;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        char save = *eol;
        *eol = 0;
        skip_ws((const char **)&line);
        if (line[0] && line[0] != '#') {
            strcpy_l(out, line, max);
            return 0;
        }
        *eol = save;
        line = eol;
        if (*line == '\r') line++;
        if (*line == '\n') line++;
    }
    strcpy_l(out, DEFAULT_REPO, max);
    return 0;
}

/* Parse .pkg blob into cache files */
static int unpack_pkg(const char *uname, char *blob, size_t len) {
    ensure_dirs();
    vfs_chdir(CACHE_ROOT);
    vfs_mkdir(uname);

    char base[96];
    strcpy_l(base, CACHE_ROOT, sizeof(base));
    size_t bl = strlen_l(base);
    if (bl + 1 < sizeof(base)) base[bl++] = '/';
    strcpy_l(base + bl, uname, sizeof(base) - bl);

    /* Write raw package too */
    char raw[112];
    strcpy_l(raw, base, sizeof(raw));
    bl = strlen_l(raw);
    strcpy_l(raw + bl, "/PKG.TXT", sizeof(raw) - bl);
    vfs_write(raw, blob, len);

    int files = 0;
    char *p = blob;
    char *end = blob + len;

    /* Extract manifest section */
    char *man = 0;
    if (starts(p, "---MANIFEST---")) {
        p += 14;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
        man = p;
        while (p < end && !starts(p, "---FILE:") && !starts(p, "---END---")) p++;
        size_t man_len = (size_t)(p - man);
        char mpath[112];
        strcpy_l(mpath, base, sizeof(mpath));
        bl = strlen_l(mpath);
        strcpy_l(mpath + bl, "/MANIFEST.TXT", sizeof(mpath) - bl);
        vfs_write(mpath, man, man_len);
    }

    while (p < end && starts(p, "---FILE:")) {
        p += 8;
        char fname[64];
        size_t fi = 0;
        while (p < end && *p != '-' && fi + 1 < sizeof(fname)) {
            if (*p != '\r' && *p != '\n') fname[fi++] = *p;
            p++;
        }
        fname[fi] = 0;
        while (p < end && *p != '\n') p++;
        if (p < end && *p == '\n') p++;
        char *fstart = p;
        while (p < end && !starts(p, "---FILE:") && !starts(p, "---END---")) p++;
        size_t flen = (size_t)(p - fstart);
        /* trim trailing newlines */
        while (flen && (fstart[flen - 1] == '\n' || fstart[flen - 1] == '\r')) flen--;

        char fpath[128];
        strcpy_l(fpath, base, sizeof(fpath));
        bl = strlen_l(fpath);
        if (bl + 1 < sizeof(fpath)) fpath[bl++] = '/';
        strcpy_l(fpath + bl, fname, sizeof(fpath) - bl);
        vfs_write(fpath, fstart, flen);
        serial_write("gwpkg: cached ");
        serial_write(fpath);
        serial_write("\n");
        files++;
    }
    return files;
}

int pkg_fetch(const char *name) {
    if (!name || !name[0]) return -1;
    char repo[128], url[192], lname[12], uname[12];
    first_repo(repo, sizeof(repo));
    lower8(name, lname);
    upper8(name, uname);

    /* {repo}/{name}.pkg */
    size_t i = 0;
    while (repo[i] && i + 1 < sizeof(url)) { url[i] = repo[i]; i++; }
    if (i && url[i - 1] != '/') url[i++] = '/';
    size_t j = 0;
    while (lname[j] && i + 1 < sizeof(url)) url[i++] = lname[j++];
    const char *sfx = ".pkg";
    while (*sfx && i + 1 < sizeof(url)) url[i++] = *sfx++;
    url[i] = 0;

    char blob[4096];
    size_t n = 0;
    if (net_http_get(url, blob, sizeof(blob) - 1, &n) < 0 || n == 0) {
        serial_write("gwpkg: fetch failed\n");
        return -1;
    }
    blob[n] = 0;
    int files = unpack_pkg(uname, blob, n);
    serial_write("gwpkg: fetched ");
    serial_write(uname);
    serial_write(" (");
    serial_write_dec((uint64_t)files);
    serial_write(" files)\n");
    return files > 0 ? 0 : -1;
}

void pkg_list(void) {
    serial_write("=== gwpkg ===\n");
    pkg_repo_list();
    serial_write("cache:\n");
    vfs_list(CACHE_ROOT);
}

int pkg_info(const char *name) {
    char uname[12];
    upper8(name, uname);
    char path[96];
    strcpy_l(path, CACHE_ROOT, sizeof(path));
    size_t cl = strlen_l(path);
    path[cl++] = '/';
    strcpy_l(path + cl, uname, sizeof(path) - cl);
    cl = strlen_l(path);
    strcpy_l(path + cl, "/MANIFEST.TXT", sizeof(path) - cl);
    char buf[512];
    size_t got = 0;
    if (vfs_read(path, buf, sizeof(buf) - 1, &got) < 0) {
        if (pkg_fetch(name) != 0) return -1;
        if (vfs_read(path, buf, sizeof(buf) - 1, &got) < 0) return -1;
    }
    buf[got] = 0;
    serial_write(buf);
    return 0;
}

int pkg_install(const char *name) {
    char uname[12];
    upper8(name, uname);
    char man[96];
    strcpy_l(man, CACHE_ROOT, sizeof(man));
    size_t cl = strlen_l(man);
    man[cl++] = '/';
    strcpy_l(man + cl, uname, sizeof(man) - cl);
    cl = strlen_l(man);
    strcpy_l(man + cl, "/MANIFEST.TXT", sizeof(man) - cl);

    char buf[512];
    size_t got = 0;
    if (vfs_read(man, buf, sizeof(buf) - 1, &got) < 0) {
        if (pkg_fetch(name) != 0) return -1;
        if (vfs_read(man, buf, sizeof(buf) - 1, &got) < 0) return -1;
    }
    buf[got] = 0;

    ensure_dirs();
    char inst[80];
    strcpy_l(inst, "Program Files/", sizeof(inst));
    cl = strlen_l(inst);
    strcpy_l(inst + cl, uname, sizeof(inst) - cl);
    vfs_chdir("C:/");
    vfs_mkdir("Program Files");
    vfs_mkdir(inst);

    char *line = buf;
    int files = 0;
    while (*line) {
        char *eol = line;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        char save = *eol;
        *eol = 0;
        if (starts(line, "file:")) {
            const char *fn = line + 5;
            skip_ws(&fn);
            if (*fn) {
                char src[96], dst[96];
                strcpy_l(src, CACHE_ROOT, sizeof(src));
                cl = strlen_l(src);
                src[cl++] = '/';
                strcpy_l(src + cl, uname, sizeof(src) - cl);
                cl = strlen_l(src);
                src[cl++] = '/';
                strcpy_l(src + cl, fn, sizeof(src) - cl);
                strcpy_l(dst, inst, sizeof(dst));
                cl = strlen_l(dst);
                dst[cl++] = '/';
                strcpy_l(dst + cl, fn, sizeof(dst) - cl);
                char fbuf[1024];
                size_t n = 0;
                if (vfs_read(src, fbuf, sizeof(fbuf), &n) >= 0 && vfs_write(dst, fbuf, n) >= 0) {
                    serial_write("gwpkg: installed ");
                    serial_write(dst);
                    serial_write("\n");
                    files++;
                }
            }
        }
        *eol = save;
        line = eol;
        if (*line == '\r') line++;
        if (*line == '\n') line++;
    }
    serial_write("gwpkg: install complete (");
    serial_write_dec((uint64_t)files);
    serial_write(" files)\n");
    return files > 0 ? 0 : -1;
}
