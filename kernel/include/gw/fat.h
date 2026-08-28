#ifndef GW_FAT_H
#define GW_FAT_H

#include <stdint.h>
#include <stddef.h>

int  fat_mount(void);
void fat_list(const char *path);
int  fat_chdir(const char *path);
void fat_getcwd(char *buf, size_t maxlen);
int  fat_mkdir(const char *path);
int  fat_read_file(const char *path, void *buf, size_t maxlen, size_t *out_len);
int  fat_write_file(const char *path, const void *data, size_t len);
int  fat_unlink(const char *path);

static inline void fat_list_root(void) { fat_list(""); }

#endif
