#ifndef GW_VFS_H
#define GW_VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_NAME_MAX  64
#define VFS_PATH_MAX  256
#define VFS_MAX_NODES 128
#define VFS_MAX_FILE_SIZE (16 * 1024)

typedef enum {
    VFS_TYPE_DIR = 1,
    VFS_TYPE_FILE = 2
} vfs_type_t;

typedef enum {
    VFS_DRIVE_C = 0,  /* RAMFS */
    VFS_DRIVE_D = 1   /* FAT ESP */
} vfs_drive_t;

typedef struct vfs_node {
    char name[VFS_NAME_MAX];
    vfs_type_t type;
    struct vfs_node *parent;
    struct vfs_node *child;
    struct vfs_node *sibling;
    uint8_t *data;
    size_t size;
    size_t capacity;
} vfs_node_t;

int  vfs_init(void);

vfs_node_t *vfs_lookup(const char *path);
vfs_node_t *vfs_cwd(void);
vfs_drive_t vfs_current_drive(void);
void        vfs_getcwd(char *buf, size_t maxlen);

int  vfs_chdir(const char *path);
int  vfs_mkdir(const char *path);
int  vfs_create_file(const char *path);
int  vfs_write(const char *path, const void *buf, size_t len);
int  vfs_read(const char *path, void *buf, size_t maxlen, size_t *out_len);
int  vfs_unlink(const char *path);
void vfs_list(const char *path);

#endif
