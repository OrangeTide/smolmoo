/* verb_rt_rv.S : RISC-V RV32 runtime linked with every smolmoo verb.
 * Copyright (c) 2026 Jon Mayo
 * SPDX-License-Identifier: 0BSD OR CC0-1.0
 *
 * Verbs are self-contained: each defines its own _start and includes
 * mulibc.h, which declares these syscall wrappers extern. This object
 * supplies them as ecall stubs, plus the 64-bit integer helpers the RV32
 * backend calls (their arguments arrive on the stack even under the psABI).
 *
 * Built with skj-as-rv for the standard RISC-V ILP32 psABI. The C caller has
 * placed the arguments in a0-a5, so each stub adds only the syscall number in
 * a7. The host reads the number from a7, the arguments from a0-a5, and
 * returns the result in a0. A 64-bit argument occupies an aligned register
 * pair, so sys_wait's timeout lands in a4 (low) and a5 (high).
 */

    .text
    .globl _exit
    .globl open
    .globl close
    .globl read
    .globl write
    .globl sys_broadcast
    .globl sys_wait
    .globl sys_getprop
    .globl sys_setprop
    .globl sys_objfind
    .globl sys_spawn
    .globl sys_suspend
    .globl sys_random
    .globl sys_move
    .globl sys_next
    .globl sys_rollup
    .globl sys_create
    .globl sys_recycle
    .globl sys_call
    .globl sys_hasverb

_exit:                                  /* void _exit(int status)  a0=status */
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

read:                                   /* a0=fd, a1=buf, a2=len -> a0=bytes */
    li      a7, 3
    ecall
    ret

write:                                  /* a0=fd, a1=buf, a2=len -> a0=bytes */
    li      a7, 4
    ecall
    ret

sys_wait:                               /* a0=nevents, a1=events, a2=event_out,
                                         * a4=timeout_lo, a5=timeout_hi */
    li      a7, 5
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

sys_spawn:                              /* a0=hash, a1=delay_ms, a2=args */
    li      a7, 10
    ecall
    ret

sys_suspend:                            /* a0=delay_ms */
    li      a7, 11
    ecall
    ret

sys_random:                             /* a0=max -> a0=uniform int in [0,max) */
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

sys_rollup:                             /* a0=obj, a1=field -> a0=subtree sum */
    li      a7, 15
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

sys_call:                               /* a0=target, a1=verb, a2=argstr */
    li      a7, 18
    ecall
    ret

sys_hasverb:                            /* a0=target, a1=verb -> a0=0/1 */
    li      a7, 19
    ecall
    ret

/* 64-bit integer helpers. Arguments arrive on the stack (backend-internal
 * ABI), result in a0:a1. */

    .globl __muldi3
__muldi3:                               /* 8(sp)=a_lo 12(sp)=a_hi 0(sp)=b_lo 4(sp)=b_hi */
    lw      a0, 8(sp)
    lw      a1, 12(sp)
    lw      a2, 0(sp)
    lw      a3, 4(sp)
    mulhu   t0, a0, a2
    mul     t1, a1, a2
    add     t0, t0, t1
    mul     t1, a0, a3
    add     a1, t0, t1
    mul     a0, a0, a2
    ret

    .globl __ashldi3
__ashldi3:                              /* 0(sp)=shift 4(sp)=a_lo 8(sp)=a_hi */
    lw      a2, 0(sp)
    lw      a0, 4(sp)
    lw      a1, 8(sp)
    li      t0, 32
    bge     a2, t0, .Lashl_ge32
    beqz    a2, .Lashl_done
    sll     a1, a1, a2
    sub     t1, t0, a2
    srl     t2, a0, t1
    or      a1, a1, t2
    sll     a0, a0, a2
    ret
.Lashl_ge32:
    li      t0, 64
    bge     a2, t0, .Lashl_zero
    addi    a2, a2, -32
    sll     a1, a0, a2
    li      a0, 0
    ret
.Lashl_zero:
    li      a0, 0
    li      a1, 0
.Lashl_done:
    ret

    .globl __ashrdi3
__ashrdi3:                              /* 0(sp)=shift 4(sp)=a_lo 8(sp)=a_hi */
    lw      a2, 0(sp)
    lw      a0, 4(sp)
    lw      a1, 8(sp)
    li      t0, 32
    bge     a2, t0, .Lashr_ge32
    beqz    a2, .Lashr_done
    srl     a0, a0, a2
    sub     t1, t0, a2
    sll     t2, a1, t1
    or      a0, a0, t2
    sra     a1, a1, a2
    ret
.Lashr_ge32:
    li      t0, 64
    bge     a2, t0, .Lashr_sign
    addi    a2, a2, -32
    sra     a0, a1, a2
    srai    a1, a1, 31
    ret
.Lashr_sign:
    srai    a1, a1, 31
    mv      a0, a1
.Lashr_done:
    ret

    .globl __lshrdi3
__lshrdi3:                              /* 0(sp)=shift 4(sp)=a_lo 8(sp)=a_hi */
    lw      a2, 0(sp)
    lw      a0, 4(sp)
    lw      a1, 8(sp)
    li      t0, 32
    bge     a2, t0, .Llshr_ge32
    beqz    a2, .Llshr_done
    srl     a0, a0, a2
    sub     t1, t0, a2
    sll     t2, a1, t1
    or      a0, a0, t2
    srl     a1, a1, a2
    ret
.Llshr_ge32:
    li      t0, 64
    bge     a2, t0, .Llshr_zero
    addi    a2, a2, -32
    srl     a0, a1, a2
    li      a1, 0
    ret
.Llshr_zero:
    li      a0, 0
    li      a1, 0
.Llshr_done:
    ret
