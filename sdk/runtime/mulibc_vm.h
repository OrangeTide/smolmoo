/* mulibc_vm.h : C library for smolmoo verb programs compiled with skj-cc.
 *
 * Unlike mulibc.h (which uses GCC inline asm), this header declares extern
 * functions implemented in hypercall.S and linked at build time.
 *
 * Copyright (c) 2026 Jon Mayo
 * SPDX-License-Identifier: 0BSD OR CC0-1.0 */

#ifndef MULIBC_VM_H
#define MULIBC_VM_H

typedef unsigned int size_t;
typedef int ssize_t;

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

extern void _exit(int status);
extern int open(const char *name, int flags);
extern int close(int fd);
extern ssize_t read(int fd, void *buf, size_t len);
extern ssize_t write(int fd, const void *buf, size_t len);
extern int sys_wait(int nevents, short *events, void *event_out,
                    long long timeout_usec);
extern int sys_broadcast(int room, const char *msg);
extern int sys_getprop(int obj, const char *name, char *buf, int bufsz);
extern int sys_setprop(int obj, const char *name, const char *val);
extern int sys_objfind(const char *name);
extern int sys_spawn(const char *hash, int delay_ms, const char *args);
extern int sys_suspend(int delay_ms);
extern int sys_random(int max);

#endif
