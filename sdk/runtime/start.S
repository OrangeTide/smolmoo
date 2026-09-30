| start.S - minimal m68k/ColdFire runtime for Linux user-mode.
|
| Provides _start (the ELF entry point), and thin wrappers for the two
| syscalls the test programs need: write(2) and exit(2). qemu-m68k uses
| the standard Linux/m68k syscall convention:
|
|   d0 = syscall number
|   d1..d5 = args (we only need d1, d2, d3)
|   trap #0 to invoke
|   return value in d0
|
| _start drops argc/argv (tests take no arguments), calls main, then
| passes main's return value to _exit.

    .text
    .globl _start
    .globl read
    .globl write
    .globl exit
    .globl _exit
    .globl __cont_capture
    .globl __cont_resume

_start:
    lea     stack_top, %sp      | switch to our own stack (qemu-m68k's
    sub.l   #16, %sp            | default stack is a single page; recursive
                                | tests overflow it within a few frames)
    jsr     main
    move.l  %d0, %d1            | exit status
    move.l  #1, %d0             | __NR_exit
    trap    #0
    | unreachable
1:  bra     1b

| int read(int fd, char *buf, int n);
read:
    movem.l %d2-%d3, -(%sp)
    move.l  12(%sp), %d1        | fd
    move.l  16(%sp), %d2        | buf
    move.l  20(%sp), %d3        | n
    move.l  #3, %d0             | __NR_read
    trap    #0
    movem.l (%sp)+, %d2-%d3
    rts

| int write(int fd, const char *buf, int n);
write:
    movem.l %d2-%d3, -(%sp)     | save callee-saved regs
    move.l  12(%sp), %d1        | fd
    move.l  16(%sp), %d2        | buf
    move.l  20(%sp), %d3        | n
    move.l  #4, %d0             | __NR_write
    trap    #0
    movem.l (%sp)+, %d2-%d3     | restore
    rts

| void exit(int status);  (alias _exit)
exit:
_exit:
    move.l  4(%sp), %d1
    move.l  #1, %d0              | __NR_exit
    trap    #0
1:  bra     1b

| __muldi3(a_hi, a_lo, b_hi, b_lo) -> d0:d1
|   64-bit multiply (low 64 bits of result).
|   Stack: 4(%sp)=a_hi, 8(%sp)=a_lo, 12(%sp)=b_hi, 16(%sp)=b_lo
|   Result: d0=hi, d1=lo
|
|   Algorithm using 16-bit halves to get the full 32x32->64 partial product:
|     a_lo = (a1 << 16) | a0,  b_lo = (b1 << 16) | b0
|     a_lo * b_lo = a1*b1 << 32 + (a1*b0 + a0*b1) << 16 + a0*b0
|   Then:  result_hi += a_hi*b_lo + a_lo*b_hi
    .globl __muldi3
__muldi3:
    movem.l %d2-%d7, -(%sp)
    | load args (offset by 24 for 6 saved regs)
    move.l  28(%sp), %d4        | d4 = a_hi
    move.l  32(%sp), %d5        | d5 = a_lo
    move.l  36(%sp), %d6        | d6 = b_hi
    move.l  40(%sp), %d7        | d7 = b_lo
    | Split a_lo and b_lo into 16-bit halves
    move.l  %d5, %d0
    and.l   #0xFFFF, %d0        | d0 = a0 (low 16 of a_lo)
    move.l  %d5, %d1
    lsr.l   #8, %d1
    lsr.l   #8, %d1             | d1 = a1 (high 16 of a_lo)
    move.l  %d7, %d2
    and.l   #0xFFFF, %d2        | d2 = b0 (low 16 of b_lo)
    move.l  %d7, %d3
    lsr.l   #8, %d3
    lsr.l   #8, %d3             | d3 = b1 (high 16 of b_lo)
    | p0 = a0 * b0
    move.l  %d0, %a0            | save a0
    mulu.l  %d2, %d0            | d0 = a0 * b0 (full 32-bit result)
    | result_lo = low 16 of p0
    | carry     = high 16 of p0
    move.l  %d0, %d5            | d5 = a0*b0 (will become result_lo)
    lsr.l   #8, %d0
    lsr.l   #8, %d0             | d0 = a0*b0 >> 16 (carry)
    | p1 = a1 * b0
    move.l  %d1, %a1            | save a1
    mulu.l  %d2, %d1            | d1 = a1 * b0
    add.l   %d1, %d0            | d0 = carry + a1*b0 (mid accumulator)
    | p2 = a0 * b1
    move.l  %a0, %d2            | d2 = a0 (restored)
    mulu.l  %d3, %d2            | d2 = a0 * b1
    add.l   %d2, %d0            | d0 = carry + a1*b0 + a0*b1
    | p3 = a1 * b1
    move.l  %a1, %d1            | d1 = a1 (restored)
    mulu.l  %d3, %d1            | d1 = a1 * b1
    | result_lo = (low16(p0)) | (low16(mid) << 16)
    move.l  %d0, %d2
    and.l   #0xFFFF, %d2
    lsl.l   #8, %d2
    lsl.l   #8, %d2             | d2 = low16(mid) << 16
    and.l   #0xFFFF, %d5        | keep only low 16 of a0*b0
    or.l    %d2, %d5            | d5 = result_lo
    | result_hi from partial products = a1*b1 + (mid >> 16)
    lsr.l   #8, %d0
    lsr.l   #8, %d0             | d0 = mid >> 16
    add.l   %d1, %d0            | d0 = a1*b1 + mid>>16
    | Add cross terms: a_hi*b_lo + a_lo*b_hi
    move.l  %d4, %d1            | d1 = a_hi
    mulu.l  %d7, %d1            | d1 = lo32(a_hi * b_lo)
    add.l   %d1, %d0
    move.l  32(%sp), %d1        | d1 = a_lo (from stack, d5 is clobbered)
    mulu.l  %d6, %d1            | d1 = lo32(a_lo * b_hi)
    add.l   %d1, %d0            | d0 = result_hi
    move.l  %d5, %d1            | d1 = result_lo
    movem.l (%sp)+, %d2-%d7
    rts

| __ashldi3(a_hi, a_lo, shift) -> d0:d1
|   64-bit left shift.
|   Stack: 4(%sp)=a_hi, 8(%sp)=a_lo, 12(%sp)=shift
    .globl __ashldi3
__ashldi3:
    movem.l %d2-%d4, -(%sp)
    move.l  16(%sp), %d0        | d0 = a_hi
    move.l  20(%sp), %d1        | d1 = a_lo
    move.l  24(%sp), %d2        | d2 = shift
    cmp.l   #32, %d2
    bge     .Lashl_ge32
    tst.l   %d2
    beq     .Lashl_done
    | shift < 32: hi = (hi << n) | (lo >> (32-n)), lo = lo << n
    move.l  %d1, %d3            | d3 = a_lo (copy)
    lsl.l   %d2, %d1            | d1 = a_lo << n
    lsl.l   %d2, %d0            | d0 = a_hi << n
    move.l  #32, %d4
    sub.l   %d2, %d4            | d4 = 32 - n
    lsr.l   %d4, %d3            | d3 = a_lo >> (32-n)
    or.l    %d3, %d0            | d0 = (a_hi << n) | (a_lo >> (32-n))
    bra     .Lashl_done
.Lashl_ge32:
    cmp.l   #64, %d2
    bge     .Lashl_zero
    sub.l   #32, %d2            | d2 = n - 32
    move.l  %d1, %d0
    lsl.l   %d2, %d0            | d0 = a_lo << (n-32)
    clr.l   %d1                 | lo = 0
    bra     .Lashl_done
.Lashl_zero:
    clr.l   %d0
    clr.l   %d1
.Lashl_done:
    movem.l (%sp)+, %d2-%d4
    rts

| __ashrdi3(a_hi, a_lo, shift) -> d0:d1
|   64-bit arithmetic right shift.
    .globl __ashrdi3
__ashrdi3:
    movem.l %d2-%d4, -(%sp)
    move.l  16(%sp), %d0        | d0 = a_hi
    move.l  20(%sp), %d1        | d1 = a_lo
    move.l  24(%sp), %d2        | d2 = shift
    cmp.l   #32, %d2
    bge     .Lashr_ge32
    tst.l   %d2
    beq     .Lashr_done
    | shift < 32: lo = (lo >> n) | (hi << (32-n)), hi = hi >> n (arith)
    move.l  %d0, %d3            | d3 = a_hi (copy)
    asr.l   %d2, %d0            | d0 = a_hi >> n (arithmetic)
    lsr.l   %d2, %d1            | d1 = a_lo >> n (logical)
    move.l  #32, %d4
    sub.l   %d2, %d4            | d4 = 32 - n
    lsl.l   %d4, %d3            | d3 = a_hi << (32-n)
    or.l    %d3, %d1            | d1 = (a_lo >> n) | (a_hi << (32-n))
    bra     .Lashr_done
.Lashr_ge32:
    cmp.l   #64, %d2
    bge     .Lashr_signfill
    sub.l   #32, %d2            | d2 = n - 32
    move.l  %d0, %d1
    asr.l   %d2, %d1            | d1 = a_hi >> (n-32) (arithmetic)
    asr.l   #8, %d0
    asr.l   #8, %d0
    asr.l   #8, %d0
    asr.l   #7, %d0             | d0 = sign extension of a_hi
    bra     .Lashr_done
.Lashr_signfill:
    asr.l   #8, %d0
    asr.l   #8, %d0
    asr.l   #8, %d0
    asr.l   #7, %d0             | sign-fill
    move.l  %d0, %d1
.Lashr_done:
    movem.l (%sp)+, %d2-%d4
    rts

| __lshrdi3(a_hi, a_lo, shift) -> d0:d1
|   64-bit logical right shift.
    .globl __lshrdi3
__lshrdi3:
    movem.l %d2-%d4, -(%sp)
    move.l  16(%sp), %d0        | d0 = a_hi
    move.l  20(%sp), %d1        | d1 = a_lo
    move.l  24(%sp), %d2        | d2 = shift
    cmp.l   #32, %d2
    bge     .Llshr_ge32
    tst.l   %d2
    beq     .Llshr_done
    | shift < 32: lo = (lo >> n) | (hi << (32-n)), hi = hi >> n
    move.l  %d0, %d3            | d3 = a_hi (copy)
    lsr.l   %d2, %d0            | d0 = a_hi >> n
    lsr.l   %d2, %d1            | d1 = a_lo >> n
    move.l  #32, %d4
    sub.l   %d2, %d4            | d4 = 32 - n
    lsl.l   %d4, %d3            | d3 = a_hi << (32-n)
    or.l    %d3, %d1            | d1 = (a_lo >> n) | (a_hi << (32-n))
    bra     .Llshr_done
.Llshr_ge32:
    cmp.l   #64, %d2
    bge     .Llshr_zero
    sub.l   #32, %d2            | d2 = n - 32
    move.l  %d0, %d1
    lsr.l   %d2, %d1            | d1 = a_hi >> (n-32)
    clr.l   %d0                 | hi = 0
    bra     .Llshr_done
.Llshr_zero:
    clr.l   %d0
    clr.l   %d1
.Llshr_done:
    movem.l (%sp)+, %d2-%d4
    rts

| __udivmoddi3(a_hi, a_lo, b_hi, b_lo) -> d0:d1 = quotient, d2:d3 = remainder
|   Unsigned 64-bit division via binary long division (shift-and-subtract).
|   Internal helper; __udivdi3/__umoddi3/__divdi3/__moddi3 are wrappers.
__udivmoddi3:
    movem.l %d4-%d7, -(%sp)
    | load args (offset by 16 for 4 saved regs + 4 for return addr)
    move.l  20(%sp), %d4        | d4 = a_hi (dividend high)
    move.l  24(%sp), %d5        | d5 = a_lo (dividend low)
    move.l  28(%sp), %d6        | d6 = b_hi (divisor high)
    move.l  32(%sp), %d7        | d7 = b_lo (divisor low)
    | check for division by zero (return 0 quotient, 0 remainder)
    move.l  %d6, %d0
    or.l    %d7, %d0
    beq     .Ludm_zero
    | quotient = 0
    clr.l   %d0                 | q_hi = 0
    clr.l   %d1                 | q_lo = 0
    | remainder = 0
    clr.l   %d2                 | r_hi = 0
    clr.l   %d3                 | r_lo = 0
    | iterate 64 bits
    move.l  #63, -(%sp)         | bit counter on stack (saves a reg)
.Ludm_loop:
    | shift remainder left by 1, shifting in top bit of dividend
    lsl.l   #1, %d3             | r_lo <<= 1
    addx.l  %d2, %d2            | r_hi <<= 1 (with carry from r_lo)
    | shift top bit of dividend (d4:d5) into remainder low bit
    tst.l   %d4
    bpl     .Ludm_no_bit        | branch if bit 31 of d4 is clear
    or.l    #1, %d3             | r_lo |= 1
.Ludm_no_bit:
    | shift dividend left by 1
    lsl.l   #1, %d5             | a_lo <<= 1
    addx.l  %d4, %d4            | a_hi <<= 1 (with carry from a_lo)
    | shift quotient left by 1
    lsl.l   #1, %d1             | q_lo <<= 1
    addx.l  %d0, %d0            | q_hi <<= 1
    | compare remainder >= divisor: (r_hi:r_lo) >= (b_hi:b_lo)?
    cmp.l   %d6, %d2            | compare r_hi vs b_hi
    bhi     .Ludm_sub           | r_hi > b_hi: subtract
    blo     .Ludm_next          | r_hi < b_hi: skip
    cmp.l   %d7, %d3            | r_hi == b_hi: compare r_lo vs b_lo
    blo     .Ludm_next          | r_lo < b_lo: skip
.Ludm_sub:
    | remainder -= divisor
    sub.l   %d7, %d3            | r_lo -= b_lo
    subx.l  %d6, %d2            | r_hi -= b_hi (with borrow)
    | quotient |= 1
    or.l    #1, %d1
.Ludm_next:
    subq.l  #1, (%sp)           | decrement bit counter
    bge     .Ludm_loop
    addq.l  #4, %sp             | pop bit counter
    movem.l (%sp)+, %d4-%d7
    rts
.Ludm_zero:
    clr.l   %d0
    clr.l   %d1
    clr.l   %d2
    clr.l   %d3
    movem.l (%sp)+, %d4-%d7
    rts

| __udivdi3(a_hi, a_lo, b_hi, b_lo) -> d0:d1 = quotient
|   Unsigned 64-bit division. d2/d3 are callee-saved in the C ABI and
|   __udivmoddi3 returns the remainder in them, so save them here.
    .globl __udivdi3
__udivdi3:
    movem.l %d2-%d3, -(%sp)
    move.l  24(%sp), -(%sp)     | push b_lo
    move.l  24(%sp), -(%sp)     | push b_hi
    move.l  24(%sp), -(%sp)     | push a_lo
    move.l  24(%sp), -(%sp)     | push a_hi
    bsr     __udivmoddi3
    lea     16(%sp), %sp        | pop args
    | d0:d1 = quotient (already there)
    movem.l (%sp)+, %d2-%d3
    rts

| __umoddi3(a_hi, a_lo, b_hi, b_lo) -> d0:d1 = remainder
|   Unsigned 64-bit modulo. Saves d2/d3 (C ABI callee-saved).
    .globl __umoddi3
__umoddi3:
    movem.l %d2-%d3, -(%sp)
    move.l  24(%sp), -(%sp)     | push b_lo
    move.l  24(%sp), -(%sp)     | push b_hi
    move.l  24(%sp), -(%sp)     | push a_lo
    move.l  24(%sp), -(%sp)     | push a_hi
    bsr     __udivmoddi3
    lea     16(%sp), %sp        | pop args
    move.l  %d2, %d0            | remainder hi
    move.l  %d3, %d1            | remainder lo
    movem.l (%sp)+, %d2-%d3
    rts

| __divdi3(a_hi, a_lo, b_hi, b_lo) -> d0:d1 = quotient
|   Signed 64-bit division. Negates operands as needed, calls __udivmoddi3,
|   negates result if signs differ. Saves d2-d5 (d2/d3 are C ABI
|   callee-saved; this wrapper and __udivmoddi3 use them as scratch).
    .globl __divdi3
__divdi3:
    movem.l %d2-%d5, -(%sp)
    | load args (offset by 16 for 4 saved regs)
    move.l  20(%sp), %d0        | a_hi
    move.l  24(%sp), %d1        | a_lo
    move.l  28(%sp), %d2        | b_hi
    move.l  32(%sp), %d3        | b_lo
    | d4 = sign flag (1 if result should be negated)
    clr.l   %d4
    tst.l   %d0
    bpl     .Ldiv_a_pos
    | negate a (two's complement: ~a + 1)
    not.l   %d1
    not.l   %d0
    addq.l  #1, %d1
    bcc     .Ldiv_a_neg_nc
    addq.l  #1, %d0
.Ldiv_a_neg_nc:
    eor.l   #1, %d4             | flip sign flag
.Ldiv_a_pos:
    tst.l   %d2
    bpl     .Ldiv_b_pos
    | negate b
    not.l   %d3
    not.l   %d2
    addq.l  #1, %d3
    bcc     .Ldiv_b_neg_nc
    addq.l  #1, %d2
.Ldiv_b_neg_nc:
    eor.l   #1, %d4             | flip sign flag
.Ldiv_b_pos:
    | push args for __udivmoddi3
    move.l  %d3, -(%sp)         | b_lo
    move.l  %d2, -(%sp)         | b_hi
    move.l  %d1, -(%sp)         | a_lo (abs)
    move.l  %d0, -(%sp)         | a_hi (abs)
    bsr     __udivmoddi3
    lea     16(%sp), %sp
    | d0:d1 = quotient; negate if d4 is set
    tst.l   %d4
    beq     .Ldiv_done
    not.l   %d1
    not.l   %d0
    addq.l  #1, %d1
    bcc     .Ldiv_done
    addq.l  #1, %d0
.Ldiv_done:
    movem.l (%sp)+, %d2-%d5
    rts

| __moddi3(a_hi, a_lo, b_hi, b_lo) -> d0:d1 = remainder
|   Signed 64-bit modulo. Remainder has same sign as dividend.
|   Saves d2-d5 like __divdi3.
    .globl __moddi3
__moddi3:
    movem.l %d2-%d5, -(%sp)
    move.l  20(%sp), %d0        | a_hi
    move.l  24(%sp), %d1        | a_lo
    move.l  28(%sp), %d2        | b_hi
    move.l  32(%sp), %d3        | b_lo
    | d4 = sign of dividend (remainder gets this sign)
    clr.l   %d4
    tst.l   %d0
    bpl     .Lmod_a_pos
    not.l   %d1
    not.l   %d0
    addq.l  #1, %d1
    bcc     .Lmod_a_neg_nc
    addq.l  #1, %d0
.Lmod_a_neg_nc:
    move.l  #1, %d4
.Lmod_a_pos:
    tst.l   %d2
    bpl     .Lmod_b_pos
    not.l   %d3
    not.l   %d2
    addq.l  #1, %d3
    bcc     .Lmod_b_pos
    addq.l  #1, %d2
.Lmod_b_pos:
    move.l  %d3, -(%sp)
    move.l  %d2, -(%sp)
    move.l  %d1, -(%sp)
    move.l  %d0, -(%sp)
    bsr     __udivmoddi3
    lea     16(%sp), %sp
    | d2:d3 = remainder; move to d0:d1
    move.l  %d2, %d0
    move.l  %d3, %d1
    | negate if dividend was negative
    tst.l   %d4
    beq     .Lmod_done
    not.l   %d1
    not.l   %d0
    addq.l  #1, %d1
    bcc     .Lmod_done
    addq.l  #1, %d0
.Lmod_done:
    movem.l (%sp)+, %d2-%d5
    rts

| __moo_arena_alloc(size)
|   Bump allocator for strings, lists, and other runtime values.
|   Aligns size up to 4 bytes.  Returns pointer in d0.
    .globl __moo_arena_alloc
__moo_arena_alloc:
    move.l  4(%sp), %d0         | d0 = requested size
    addq.l  #3, %d0
    and.l   #-4, %d0            | align to 4 bytes
    lea     __moo_arena_ptr, %a0
    move.l  (%a0), %d1          | d1 = current pointer
    add.l   %d0, %d1            | d1 = new pointer
    move.l  (%a0), %d0          | d0 = old pointer (return value)
    move.l  %d1, (%a0)          | store new pointer
    movea.l %d0, %a0            | a0 = return value (m68k ABI)
    rts

| __moo_arena_reset()
|   Resets the arena bump pointer to base.  Called after verb returns.
    .globl __moo_arena_reset
__moo_arena_reset:
    lea     __moo_arena_ptr, %a0
    lea     __moo_arena, %a1
    move.l  %a1, (%a0)
    rts

| __cont_capture()
|   Called by IR_CAPTURE inline code after pushing d2-d7.  The jsr return
|   address on the stack becomes the resume entry point (captured in the
|   stack segment; __cont_resume's rts pops it).
|
|   Reads the mark slot via __cont_mark_sp, bump-allocates a buffer,
|   copies the stack segment, then longjmps to the mark's re-entry label
|   with d0 = buffer address.
|
|   On entry:  d2-d7 are on the stack (pushed by IR_CAPTURE inline code)
|              (%sp) = return address to IR_CAPTURE resume point
|   Clobbers:  d0-d7, a0-a1 (callee-saves are in the captured segment)
__cont_capture:
    move.l  %sp, %d4                | d4 = capture_sp
    move.l  %fp, %d5                | d5 = capture_fp

    | Read the 12-byte mark slot pointed to by __cont_mark_sp
    lea     __cont_mark_sp, %a0
    move.l  (%a0), %a1              | a1 = mark slot pointer
    move.l  (%a1), %d2              | d2 = mark's saved_fp
    move.l  4(%a1), %d3             | d3 = mark's saved_sp
    move.l  8(%a1), %d7             | d7 = mark's re-entry PC

    | Stack segment size (mark_sp - capture_sp; stack grows down)
    move.l  %d3, %d6
    sub.l   %d4, %d6               | d6 = size

    | Bump-allocate: 12-byte header + size bytes of stack data
    lea     __cont_arena_ptr, %a0
    move.l  (%a0), %d1             | d1 = buf (start of buffer)

    | Write header [capture_sp, capture_fp, size]
    movea.l %d1, %a1
    move.l  %d4, (%a1)            | buf[0] = capture_sp
    move.l  %d5, 4(%a1)           | buf[4] = capture_fp
    move.l  %d6, 8(%a1)           | buf[8] = size

    | Advance arena pointer past this buffer
    move.l  %d1, %d0
    add.l   #12, %d0
    add.l   %d6, %d0
    lea     __cont_arena_ptr, %a0
    move.l  %d0, (%a0)

    | Copy stack data: capture_sp -> buf+12
    movea.l %d1, %a0
    lea     12(%a0), %a0          | a0 = dst (buf + 12)
    movea.l %d4, %a1              | a1 = src (capture_sp)
    move.l  %d6, %d0
    lsr.l   #2, %d0               | d0 = word count
    bra     2f
1:  move.l  (%a1), %d6
    move.l  %d6, (%a0)
    addq.l  #4, %a1
    addq.l  #4, %a0
2:  subq.l  #1, %d0
    bpl     1b

    | Longjmp to mark: restore FP/SP, d0 = buf, jump to re-entry label
    move.l  %d2, %fp
    movea.l %d3, %sp
    move.l  %d1, %d0              | d0 = buffer address (nonzero)
    movea.l %d7, %a0
    jmp     (%a0)                  | jump to IR_MARK's re-entry label

| __cont_resume(buf, value)
|   Restores the captured stack segment and returns into it via rts.
|   The rts pops the jsr __cont_capture return address, which lands at
|   the IR_CAPTURE inline code's resume point.
|
|   On entry:  4(%sp) = buf pointer, 8(%sp) = resume value
__cont_resume:
    move.l  4(%sp), %a1            | a1 = buf
    move.l  8(%sp), %d1            | d1 = resume value

    | Read buffer header
    move.l  (%a1), %d2             | d2 = capture_sp
    move.l  4(%a1), %d3            | d3 = capture_fp
    move.l  8(%a1), %d6            | d6 = size

    | Restore FP and SP to captured state
    move.l  %d3, %fp
    movea.l %d2, %sp

    | Copy stack data back: buf+12 -> capture_sp
    lea     12(%a1), %a1           | a1 = src (buf + 12)
    movea.l %d2, %a0              | a0 = dst (capture_sp)
    lsr.l   #2, %d6               | d6 = word count
    bra     4f
3:  move.l  (%a1), %d0
    move.l  %d0, (%a0)
    addq.l  #4, %a1
    addq.l  #4, %a0
4:  subq.l  #1, %d6
    bpl     3b

    | Return into the restored continuation
    move.l  %d1, %d0              | d0 = resume value
    rts

| ---- Excelsior sources (sources.md D4): a stackful coroutine ----
|
| struct exc_src (allocated by exc_host.c's __exc_src_new):
|    0  state: 0 new, 1 running, 2 done
|    4  body function pointer (takes one argument: the source struct)
|    8  suspended body sp          12  suspended body fp
|   16  pump sp (across a switch)  20  pump fp
|   24  private stack top          28.. argument words
|
| The body runs on the source's private stack, so its frames live at
| fixed addresses and a pump is safe from any caller depth (the reason
| this is a coroutine and not __cont_capture, whose restored segment is
| tied to the first pump's stack region). A switch saves the callee-saved
| register set the ColdFire backend allocates (d2-d7 and fp2-fp7; it
| never touches a2-a5) on the outgoing stack, and sp/fp in the struct.

| __exc_src_next(src) -> d0 = yielded (null-word maybe) value, 0 = done
    .globl __exc_src_next
__exc_src_next:
    move.l  4(%sp), %a0         | a0 = src
    move.l  (%a0), %d0
    subq.l  #2, %d0
    bne     1f
    move.l  #0, %d0             | exhausted stays exhausted
    rts
1:  | save the pump context on the pump stack
    movem.l %d2-%d7, -(%sp)
    lea     -48(%sp), %sp
    fmove.d %fp2, (%sp)
    fmove.d %fp3, 8(%sp)
    fmove.d %fp4, 16(%sp)
    fmove.d %fp5, 24(%sp)
    fmove.d %fp6, 32(%sp)
    fmove.d %fp7, 40(%sp)
    move.l  %sp, 16(%a0)        | pump sp
    move.l  %fp, 20(%a0)        | pump fp
    move.l  (%a0), %d0
    subq.l  #1, %d0
    beq     2f
    | first pump: launch the body on the private stack
    move.l  #1, (%a0)           | state = running
    move.l  24(%a0), %sp
    move.l  %a0, -(%sp)         | the body's one argument: the source
    move.l  4(%a0), %a1
    jsr     (%a1)               | returns only when the body is exhausted
    move.l  (%sp), %a0          | the pushed argument (caller-pops ABI)
    move.l  #2, (%a0)           | state = done
    move.l  16(%a0), %sp        | back to the pump stack
    move.l  20(%a0), %fp
    bra     3f
2:  | resume the suspended body where its yield left off
    move.l  8(%a0), %sp
    move.l  12(%a0), %fp
    fmove.d (%sp), %fp2
    fmove.d 8(%sp), %fp3
    fmove.d 16(%sp), %fp4
    fmove.d 24(%sp), %fp5
    fmove.d 32(%sp), %fp6
    fmove.d 40(%sp), %fp7
    lea     48(%sp), %sp
    movem.l (%sp)+, %d2-%d7
    rts                          | into the body, after its yield call
3:  | exhausted: restore the pump context, return 0
    fmove.d (%sp), %fp2
    fmove.d 8(%sp), %fp3
    fmove.d 16(%sp), %fp4
    fmove.d 24(%sp), %fp5
    fmove.d 32(%sp), %fp6
    fmove.d 40(%sp), %fp7
    lea     48(%sp), %sp
    movem.l (%sp)+, %d2-%d7
    move.l  #0, %d0
    rts

| __exc_src_yield(src, v): suspend the body, return v from the pump.
| Never returns to its caller directly; a later __exc_src_next resumes
| right after the caller's jsr.
    .globl __exc_src_yield
__exc_src_yield:
    move.l  4(%sp), %a0         | a0 = src
    move.l  8(%sp), %d1         | d1 = the yielded value
    | save the body context on the body stack (mirrors the resume path)
    movem.l %d2-%d7, -(%sp)
    lea     -48(%sp), %sp
    fmove.d %fp2, (%sp)
    fmove.d %fp3, 8(%sp)
    fmove.d %fp4, 16(%sp)
    fmove.d %fp5, 24(%sp)
    fmove.d %fp6, 32(%sp)
    fmove.d %fp7, 40(%sp)
    move.l  %sp, 8(%a0)         | body sp
    move.l  %fp, 12(%a0)        | body fp
    | switch to the pump and return the value from __exc_src_next
    move.l  16(%a0), %sp
    move.l  20(%a0), %fp
    fmove.d (%sp), %fp2
    fmove.d 8(%sp), %fp3
    fmove.d 16(%sp), %fp4
    fmove.d 24(%sp), %fp5
    fmove.d 32(%sp), %fp6
    fmove.d 40(%sp), %fp7
    lea     48(%sp), %sp
    movem.l (%sp)+, %d2-%d7
    move.l  %d1, %d0
    rts

    .data
    .align 4
    .globl __cont_mark_sp
__cont_mark_sp:
    .long 0
    .globl __cont_arena_ptr
__cont_arena_ptr:
    .long __cont_arena
__moo_arena_ptr:
    .long __moo_arena

    .bss
    .align 4
__cont_arena:
    .space 65536
__moo_arena:
    .space 65536
    .space 65536
stack_top:
