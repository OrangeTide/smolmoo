/* mulibc.h : tiny C library for smolmoo RISC-V RV32 verb programs */
/* Copyright (c) 2026 Jon Mayo <jon@rm-f.net>
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#ifndef MULIBC_H
#define MULIBC_H

typedef unsigned int size_t;
typedef int ssize_t;

/* Syscall numbers (the number the ecall stub puts in a7) */
#define SYS_EXIT     0
#define SYS_OPEN     1
#define SYS_CLOSE    2
#define SYS_READ     3
#define SYS_WRITE    4
#define SYS_WAIT     5
#define SYS_FCNTL    6
#define SYS_GETPROP  7
#define SYS_SETPROP  8
#define SYS_OBJFIND  9
#define SYS_SPAWN   10
#define SYS_SUSPEND 11
#define SYS_RANDOM  12
#define SYS_MOVE    13
#define SYS_NEXT    14
#define SYS_ROLLUP  15
#define SYS_CREATE  16
#define SYS_RECYCLE 17
#define SYS_CALL    18
#define SYS_HASVERB 19

/* Verb arguments passed by host at fixed address */
struct vm_args {
    int player;
    int room;
    char *argstr;
    int arglen;
    int this_obj;
    int dobj;
    int iobj;
    char *dobjstr;
    char *iobjstr;
    char *prepstr;
    char *verb;
};

#define vm_args ((volatile struct vm_args *)0x380)

#define O_VERB 0x100

#define WAIT_TIMEOUT ((int)-2)

struct verb_event {
    int player;
    int room;
    int this_obj;
    int dobj;
    int iobj;
    unsigned int argstr;
    unsigned int dobjstr;
    unsigned int iobjstr;
    unsigned int prepstr;
    unsigned int verb;
};

/* Syscall wrappers. Each is an ecall stub implemented in verb_rt_rv.S; under
 * the RISC-V psABI the compiler passes the arguments in a0-a5 and the stub
 * only supplies the number in a7. */
extern void _exit(int status);
extern int open(const char *name, int flags);
extern int close(int fd);
extern ssize_t read(int fd, void *buf, size_t len);
extern ssize_t write(int fd, const void *buf, size_t len);
extern int sys_broadcast(int room, const char *msg);
extern int sys_wait(unsigned nevents, short *events, void *event_out,
                    long long timeout_usec);
extern int sys_getprop(int obj, const char *name, char *buf, int bufsz);
extern int sys_setprop(int obj, const char *name, const char *val);
extern int sys_objfind(const char *name);
extern int sys_spawn(const char *hash, int delay_ms, const char *args);
extern int sys_suspend(int delay_ms);
extern int sys_random(int max);
extern int sys_move(int obj, int dest);
extern int sys_next(int container, int after);
extern int sys_rollup(int obj, const char *field);
extern int sys_create(int parent);
extern int sys_recycle(int obj);
extern int sys_call(int target, const char *verb, const char *argstr);
extern int sys_hasverb(int target, const char *verb);

/* puts — write string + newline to stdout */
static int
puts(const char *s)
{
    const char *p = s;
    while (*p) p++;
    write(1, s, p - s);
    write(1, "\n", 1);
    return 0;
}

/* strlen */
static size_t
strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}

/* Freestanding memory routines. The C verbs link only their own object plus
 * verb_rt_rv.o, and the compiler emits implicit calls to memset/memcpy for
 * aggregate initialization and struct copies. Those implicit calls bind to
 * the external symbol, so these are given external linkage and left
 * out-of-line rather than being static. */
void *
memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;

    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void *
memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    while (n--) *d++ = *s++;
    return dst;
}

void *
memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int
memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;

    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++; y++;
    }
    return 0;
}

#endif
