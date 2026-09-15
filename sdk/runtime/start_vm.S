| start_vm.S : ColdFire entry point for smolmoo verb programs.
|
| Calls verb() as the user entry point, then exits via LINE_A trap.
| Also provides the bump arena used by the MooScript runtime.
|
| Copyright (c) 2026 Jon Mayo
| SPDX-License-Identifier: 0BSD OR CC0-1.0

    .text
    .globl _start
    .globl __moo_arena_alloc
    .globl __moo_arena_reset

_start:
    lea     stack_top, %sp
    sub.l   #16, %sp
    jsr     verb
    move.l  %d0, %d0
    .short  0xA000              | sys_exit
1:  bra     1b

| void *__moo_arena_alloc(int size);
|   Bump allocator for strings, lists, and other runtime values.
|   Aligns size up to 4 bytes. Returns pointer in d0/a0.
__moo_arena_alloc:
    move.l  4(%sp), %d0
    addq.l  #3, %d0
    and.l   #-4, %d0
    lea     __moo_arena_ptr, %a0
    move.l  (%a0), %d1
    add.l   %d0, %d1
    move.l  (%a0), %d0
    move.l  %d1, (%a0)
    movea.l %d0, %a0
    rts

| void __moo_arena_reset(void);
|   Resets the arena bump pointer to base.
__moo_arena_reset:
    lea     __moo_arena_ptr, %a0
    lea     __moo_arena, %a1
    move.l  %a1, (%a0)
    rts

    .data
    .align 4
__moo_arena_ptr:
    .long __moo_arena

    .bss
    .align 4
__moo_arena:
    .space 65536
    .space 65536
stack_top:
