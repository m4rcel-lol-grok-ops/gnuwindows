#ifndef GW_PROCESS_H
#define GW_PROCESS_H

#include <stdint.h>

#define PROCESS_MAX 8

typedef struct {
    int active;
    int pid;
    int exit_code;
    uint64_t kernel_rsp;
    uint64_t resume_rip;
    char name[32];
} process_t;

process_t *process_current(void);
int  process_exec(const char *path);
int  user_enter(void);
int  process_handle_exit(void *frame, int64_t code);
void process_install_hello_elf(void);
int  process_list(void); /* print to serial */

#endif
