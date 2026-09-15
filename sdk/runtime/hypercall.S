| hypercall.S : LINE_A hypercall stubs for smolmoo verb programs.
|
| Each function loads arguments from the C stack into the registers
| expected by the ColdFire VM's LINE_A dispatcher, executes the trap,
| and returns D0.
|
| Copyright (c) 2026 Jon Mayo
| SPDX-License-Identifier: 0BSD OR CC0-1.0

    .text
    .globl _exit
    .globl open
    .globl close
    .globl read
    .globl write
    .globl sys_wait
    .globl sys_broadcast
    .globl sys_getprop
    .globl sys_setprop
    .globl sys_objfind
    .globl sys_spawn
    .globl sys_suspend
    .globl sys_random

| void _exit(int status);
|   D0=status
_exit:
    move.l  4(%sp), %d0
    .short  0xA000
1:  bra     1b

| int open(const char *name, int flags);
|   A0=name, D0=flags -> D0=fd
open:
    movea.l 4(%sp), %a0
    move.l  8(%sp), %d0
    .short  0xA001
    rts

| int close(int fd);
|   D0=fd -> D0=result
close:
    move.l  4(%sp), %d0
    .short  0xA002
    rts

| int read(int fd, void *buf, int len);
|   D0=fd, A0=buf, D1=len -> D0=bytes
read:
    move.l  4(%sp), %d0
    movea.l 8(%sp), %a0
    move.l  12(%sp), %d1
    .short  0xA003
    rts

| int write(int fd, const void *buf, int len);
|   D0=fd, A0=buf, D1=len -> D0=bytes
write:
    move.l  4(%sp), %d0
    movea.l 8(%sp), %a0
    move.l  12(%sp), %d1
    .short  0xA004
    rts

| int sys_wait(int nevents, short *events, void *event_out,
|              long long timeout_usec);
|   D0=nevents, A0=events, D1=timeout_hi, D2=timeout_lo -> D0=result
|   event_out at 16(%sp) is unused by the VM.
sys_wait:
    move.l  %d2, -(%sp)
    move.l  8(%sp), %d0
    movea.l 12(%sp), %a0
    move.l  20(%sp), %d1
    move.l  24(%sp), %d2
    .short  0xA005
    move.l  (%sp)+, %d2
    rts

| int sys_broadcast(int room, const char *msg);
|   D0=room, A0=msg -> D0=count
sys_broadcast:
    move.l  4(%sp), %d0
    movea.l 8(%sp), %a0
    .short  0xA006
    rts

| int sys_getprop(int obj, const char *name, char *buf, int bufsz);
|   D0=obj, A0=name, A1=buf, D1=bufsz -> D0=len
sys_getprop:
    move.l  4(%sp), %d0
    movea.l 8(%sp), %a0
    movea.l 12(%sp), %a1
    move.l  16(%sp), %d1
    .short  0xA007
    rts

| int sys_setprop(int obj, const char *name, const char *val);
|   D0=obj, A0=name, A1=val -> D0=result
sys_setprop:
    move.l  4(%sp), %d0
    movea.l 8(%sp), %a0
    movea.l 12(%sp), %a1
    .short  0xA008
    rts

| int sys_objfind(const char *name);
|   A0=name -> D0=objid
sys_objfind:
    movea.l 4(%sp), %a0
    .short  0xA009
    rts

| int sys_spawn(const char *hash, int delay_ms, const char *args);
|   A0=hash, D0=delay_ms, A1=args -> D0=task_id
sys_spawn:
    movea.l 4(%sp), %a0
    move.l  8(%sp), %d0
    movea.l 12(%sp), %a1
    .short  0xA00A
    rts

| int sys_suspend(int delay_ms);
|   D0=delay_ms -> D0=result
sys_suspend:
    move.l  4(%sp), %d0
    .short  0xA00B
    rts

| int sys_random(int max);
|   D0=max -> D0=uniform int in [0,max)
sys_random:
    move.l  4(%sp), %d0
    .short  0xA00C
    rts
