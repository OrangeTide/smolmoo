/* moo_syscall_rv.S : syscall stubs for the MooScript verb runtime.
 * SPDX-License-Identifier: 0BSD OR CC0-1.0
 *
 * As of skjegg v0.7.0 the RV32 stack calling convention was retired: the
 * MooScript backend and skj-cc-rv both pass call arguments in registers
 * (arg0 in a0, arg1 in a1, ...) and return the result in a0. These stubs
 * place the syscall number in a7, trap with ecall, and return with the
 * host's result already in a0, the same register convention as the C-verb
 * stubs in verb_rt_rv.S.
 *
 * Assembled with skj-as-rv.
 */

    .text
    .globl _exit
    .globl open
    .globl close
    .globl read
    .globl write
    .globl sys_broadcast
    .globl sys_getprop
    .globl sys_setprop
    .globl sys_objfind
    .globl sys_random
    .globl sys_move
    .globl sys_next
    .globl sys_create
    .globl sys_recycle
    .globl sys_call
    .globl sys_hasverb
    .globl sys_setpriv

_exit:                                  /* a0=status */
    li      a7, 0
    ecall
1:  j       1b

open:                                   /* a0=name, a1=flags -> a0=fd */
    li      a7, 1
    ecall
    ret

close:                                  /* a0=fd -> a0=result */
    li      a7, 2
    ecall
    ret

read:                                   /* a0=fd, a1=buf, a2=len */
    li      a7, 3
    ecall
    ret

write:                                  /* a0=fd, a1=buf, a2=len */
    li      a7, 4
    ecall
    ret

sys_broadcast:                          /* a0=room, a1=msg -> a0=count */
    li      a7, 6
    ecall
    ret

sys_getprop:                            /* a0=obj, a1=name, a2=buf, a3=bufsz */
    li      a7, 7
    ecall
    ret

sys_setprop:                            /* a0=obj, a1=name, a2=val */
    li      a7, 8
    ecall
    ret

sys_objfind:                            /* a0=name -> a0=objid */
    li      a7, 9
    ecall
    ret

sys_random:                             /* a0=max -> a0=int in [0,max) */
    li      a7, 12
    ecall
    ret

sys_move:                               /* a0=obj, a1=dest */
    li      a7, 13
    ecall
    ret

sys_next:                               /* a0=container, a1=after -> a0=child */
    li      a7, 14
    ecall
    ret

sys_create:                             /* a0=parent -> a0=new obj id */
    li      a7, 16
    ecall
    ret

sys_recycle:                            /* a0=obj -> a0=result */
    li      a7, 17
    ecall
    ret

sys_call:                               /* a0=target, a1=verb, a2=argstr,
                                         * a3=dobj, a4=iobj */
    li      a7, 18
    ecall
    ret

sys_hasverb:                            /* a0=target, a1=verb -> a0=0/1 */
    li      a7, 19
    ecall
    ret

sys_setpriv:                            /* a0=on -> a0=0 ok, negative denied */
    li      a7, 20
    ecall
    ret
