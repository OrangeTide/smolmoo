/* moo_syscall_rv.S : stack-convention syscall stubs for the MooScript verb
 * runtime.
 *
 * The MooScript backend and skj-cc-rv pass call arguments on the stack
 * (arg0 at 0(sp), arg1 at 4(sp), ...) and return the result in a0. These
 * stubs load the arguments into a0-a3, place the syscall number in a7, trap
 * with ecall, and return with the host's result already in a0. This differs
 * from verb_rt_rv.S, whose stubs follow the register (psABI) convention used
 * by the C verbs.
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

_exit:                                  /* 0(sp)=status */
    lw      a0, 0(sp)
    li      a7, 0
    ecall
1:  j       1b

open:                                   /* 0(sp)=name 4(sp)=flags -> a0=fd */
    lw      a0, 0(sp)
    lw      a1, 4(sp)
    li      a7, 1
    ecall
    ret

close:                                  /* 0(sp)=fd -> a0=result */
    lw      a0, 0(sp)
    li      a7, 2
    ecall
    ret

read:                                   /* 0(sp)=fd 4(sp)=buf 8(sp)=len */
    lw      a0, 0(sp)
    lw      a1, 4(sp)
    lw      a2, 8(sp)
    li      a7, 3
    ecall
    ret

write:                                  /* 0(sp)=fd 4(sp)=buf 8(sp)=len */
    lw      a0, 0(sp)
    lw      a1, 4(sp)
    lw      a2, 8(sp)
    li      a7, 4
    ecall
    ret

sys_broadcast:                          /* 0(sp)=room 4(sp)=msg -> a0=count */
    lw      a0, 0(sp)
    lw      a1, 4(sp)
    li      a7, 6
    ecall
    ret

sys_getprop:                            /* 0(sp)=obj 4=name 8=buf 12=bufsz */
    lw      a0, 0(sp)
    lw      a1, 4(sp)
    lw      a2, 8(sp)
    lw      a3, 12(sp)
    li      a7, 7
    ecall
    ret

sys_setprop:                            /* 0(sp)=obj 4=name 8=val */
    lw      a0, 0(sp)
    lw      a1, 4(sp)
    lw      a2, 8(sp)
    li      a7, 8
    ecall
    ret

sys_objfind:                            /* 0(sp)=name -> a0=objid */
    lw      a0, 0(sp)
    li      a7, 9
    ecall
    ret

sys_random:                             /* 0(sp)=max -> a0=int in [0,max) */
    lw      a0, 0(sp)
    li      a7, 12
    ecall
    ret

sys_move:                               /* 0(sp)=obj 4=dest */
    lw      a0, 0(sp)
    lw      a1, 4(sp)
    li      a7, 13
    ecall
    ret

sys_next:                               /* 0(sp)=container 4=after -> a0=child */
    lw      a0, 0(sp)
    lw      a1, 4(sp)
    li      a7, 14
    ecall
    ret

sys_create:                             /* 0(sp)=parent -> a0=new obj id */
    lw      a0, 0(sp)
    li      a7, 16
    ecall
    ret

sys_recycle:                            /* 0(sp)=obj -> a0=result */
    lw      a0, 0(sp)
    li      a7, 17
    ecall
    ret
