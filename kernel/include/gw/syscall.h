#ifndef GW_SYSCALL_H
#define GW_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/*
 * GWKernel System Call ABI (NOT Linux numbers)
 *
 * Entry: int 0x80
 *   RAX = syscall number
 *   RDI = arg0
 *   RSI = arg1
 *   RDX = arg2
 *   RCX = arg3
 * Return: RAX = result (negative = error)
 */

#define SYS_EXIT      1
#define SYS_WRITE     2
#define SYS_READ      3
#define SYS_OPEN      4
#define SYS_CLOSE     5
#define SYS_GETPID    6
#define SYS_YIELD     7
#define SYS_SLEEP     8
#define SYS_ALLOC     9
#define SYS_FREE      10
#define SYS_SPAWN     11
#define SYS_WAIT      12

#define GW_STDERR     2
#define GW_STDOUT     1
#define GW_STDIN      0

void syscall_init(void);

/* Kernel-callable wrappers (also usable before full userspace) */
int64_t gw_syscall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);

#endif
