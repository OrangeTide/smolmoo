/* start_rv.S - minimal RISC-V (RV32IM) runtime for Linux user-mode.
 *
 * Provides _start (the ELF entry point), and thin wrappers for the two
 * syscalls the test programs need: write(2) and exit(2).  qemu-riscv32
 * uses the standard Linux/RISC-V syscall convention:
 *
 *   a7 = syscall number
 *   a0..a5 = args
 *   ecall to invoke
 *   return value in a0
 *
 * The backend emits the RISC-V ILP32 platform psABI, so arguments arrive
 * in a0..a7.  The syscall wrappers pass them straight through: write's
 * (fd, buf, n) are already in a0, a1, a2, which is the ecall order too.
 *
 * _start drops argc/argv (tests take no arguments), calls main, then
 * passes main's return value to _exit.
 */

	.text
	.globl _start
	.globl write
	.globl read
	.globl exit
	.globl _exit

_start:
	la	sp, stack_top
	addi	sp, sp, -16
	jal	ra, main
	li	a7, 93
	ecall
1:	j	1b

/* int write(int fd, const char *buf, int n); */
write:
	li	a7, 64
	ecall
	ret

/* int read(int fd, char *buf, int n); */
read:
	li	a7, 63
	ecall
	ret

/* void exit(int status); */
exit:
_exit:
	li	a7, 93
	ecall
1:	j	1b

/* __muldi3(a_lo, a_hi, b_lo, b_hi) -> a0:a1
 *   64-bit multiply (low 64 bits of result).
 *   Stack: 8(sp)=a_lo, 12(sp)=a_hi, 0(sp)=b_lo, 4(sp)=b_hi
 *   Result: a0=lo, a1=hi */
	.globl __muldi3
__muldi3:
	lw	a0, 8(sp)		/* a0 = a_lo */
	lw	a1, 12(sp)		/* a1 = a_hi */
	lw	a2, 0(sp)		/* a2 = b_lo */
	lw	a3, 4(sp)		/* a3 = b_hi */
	/* result_lo = a_lo * b_lo (low 32 bits) */
	/* result_hi = mulhu(a_lo, b_lo) + a_hi*b_lo + a_lo*b_hi */
	mulhu	t0, a0, a2		/* t0 = hi32(a_lo * b_lo) */
	mul	t1, a1, a2		/* t1 = lo32(a_hi * b_lo) */
	add	t0, t0, t1
	mul	t1, a0, a3		/* t1 = lo32(a_lo * b_hi) */
	add	a1, t0, t1		/* a1 = result_hi */
	mul	a0, a0, a2		/* a0 = result_lo */
	ret

/* __ashldi3(a_lo, a_hi, shift) -> a0:a1
 *   64-bit left shift.
 *   Stack: 0(sp)=shift, 4(sp)=a_lo, 8(sp)=a_hi */
	.globl __ashldi3
__ashldi3:
	lw	a2, 0(sp)		/* a2 = shift */
	lw	a0, 4(sp)		/* a0 = a_lo */
	lw	a1, 8(sp)		/* a1 = a_hi */
	li	t0, 32
	bge	a2, t0, .Lashl_ge32
	beqz	a2, .Lashl_done
	/* shift < 32 */
	sll	a1, a1, a2		/* a1 = a_hi << n */
	sub	t1, t0, a2		/* t1 = 32 - n */
	srl	t2, a0, t1		/* t2 = a_lo >> (32-n) */
	or	a1, a1, t2		/* a1 |= carry bits */
	sll	a0, a0, a2		/* a0 = a_lo << n */
	ret
.Lashl_ge32:
	li	t0, 64
	bge	a2, t0, .Lashl_zero
	addi	a2, a2, -32
	sll	a1, a0, a2		/* a1 = a_lo << (n-32) */
	li	a0, 0
	ret
.Lashl_zero:
	li	a0, 0
	li	a1, 0
.Lashl_done:
	ret

/* __ashrdi3(a_lo, a_hi, shift) -> a0:a1
 *   64-bit arithmetic right shift. */
	.globl __ashrdi3
__ashrdi3:
	lw	a2, 0(sp)		/* a2 = shift */
	lw	a0, 4(sp)		/* a0 = a_lo */
	lw	a1, 8(sp)		/* a1 = a_hi */
	li	t0, 32
	bge	a2, t0, .Lashr_ge32
	beqz	a2, .Lashr_done
	/* shift < 32 */
	srl	a0, a0, a2		/* a0 = a_lo >> n */
	sub	t1, t0, a2		/* t1 = 32 - n */
	sll	t2, a1, t1		/* t2 = a_hi << (32-n) */
	or	a0, a0, t2		/* a0 |= carry bits */
	sra	a1, a1, a2		/* a1 = a_hi >> n (arithmetic) */
	ret
.Lashr_ge32:
	li	t0, 64
	bge	a2, t0, .Lashr_sign
	addi	a2, a2, -32
	sra	a0, a1, a2		/* a0 = a_hi >> (n-32) */
	srai	a1, a1, 31		/* sign extension */
	ret
.Lashr_sign:
	srai	a1, a1, 31
	mv	a0, a1
.Lashr_done:
	ret

/* __lshrdi3(a_lo, a_hi, shift) -> a0:a1
 *   64-bit logical right shift. */
	.globl __lshrdi3
__lshrdi3:
	lw	a2, 0(sp)		/* a2 = shift */
	lw	a0, 4(sp)		/* a0 = a_lo */
	lw	a1, 8(sp)		/* a1 = a_hi */
	li	t0, 32
	bge	a2, t0, .Llshr_ge32
	beqz	a2, .Llshr_done
	/* shift < 32 */
	srl	a0, a0, a2		/* a0 = a_lo >> n */
	sub	t1, t0, a2		/* t1 = 32 - n */
	sll	t2, a1, t1		/* t2 = a_hi << (32-n) */
	or	a0, a0, t2
	srl	a1, a1, a2		/* a1 = a_hi >> n */
	ret
.Llshr_ge32:
	li	t0, 64
	bge	a2, t0, .Llshr_zero
	addi	a2, a2, -32
	srl	a0, a1, a2		/* a0 = a_hi >> (n-32) */
	li	a1, 0
	ret
.Llshr_zero:
	li	a0, 0
	li	a1, 0
.Llshr_done:
	ret

/* void *__moo_arena_alloc(int size);
 *   Bump allocator.  Aligns size up to 4 bytes.  Returns pointer in a0. */
	.globl __moo_arena_alloc
__moo_arena_alloc:
	addi	a0, a0, 3
	andi	a0, a0, -4
	la	t0, __moo_arena_ptr
	lw	t1, 0(t0)		/* t1 = old pointer (return value) */
	add	t2, t1, a0		/* t2 = new pointer */
	sw	t2, 0(t0)
	mv	a0, t1
	ret

/* void __moo_arena_reset(void); */
	.globl __moo_arena_reset
__moo_arena_reset:
	la	t0, __moo_arena_ptr
	la	t1, __moo_arena
	sw	t1, 0(t0)
	ret

/* __cont_capture()
 *   Called by IR_CAPTURE inline code after pushing s1-s11.
 *   The jal return address (ra) is on the stack as part of the
 *   captured segment (pushed by the callee-save block in IR_CAPTURE).
 *
 *   Reads the 12-byte mark slot via __cont_mark_sp, bump-allocates
 *   a buffer in __cont_arena, copies the stack segment from
 *   capture_sp to mark_sp, then longjmps to the mark's re-entry
 *   label with a0 = buffer address (nonzero).
 *
 *   On entry:  s1-s11 on stack (pushed by IR_CAPTURE inline code)
 *              ra = return address to IR_CAPTURE resume point
 *   Clobbers:  all regs (callee-saves are in the captured segment) */
	.globl __cont_capture
__cont_capture:
	addi	sp, sp, -4
	sw	ra, 0(sp)		/* save return address on stack */
	mv	t2, sp			/* t2 = capture_sp */
	mv	t3, s0			/* t3 = capture_fp */

	/* read 12-byte mark slot */
	la	t0, __cont_mark_sp
	lw	t0, 0(t0)		/* t0 = mark slot pointer */
	lw	t4, 0(t0)		/* t4 = mark's saved_fp */
	lw	t5, 4(t0)		/* t5 = mark's saved_sp */
	lw	t6, 8(t0)		/* t6 = mark's re-entry PC */

	/* size = mark_sp - capture_sp (stack grows down) */
	sub	a2, t5, t2		/* a2 = size */

	/* bump-allocate: 12-byte header + size bytes */
	la	t0, __cont_arena_ptr
	lw	a0, 0(t0)		/* a0 = buf */

	/* write buffer header [capture_sp, capture_fp, size] */
	sw	t2, 0(a0)		/* buf[0] = capture_sp */
	sw	t3, 4(a0)		/* buf[4] = capture_fp */
	sw	a2, 8(a0)		/* buf[8] = size */

	/* advance arena pointer past this buffer */
	add	t1, a0, a2
	addi	t1, t1, 12
	sw	t1, 0(t0)

	/* copy stack segment: capture_sp -> buf+12 */
	addi	a3, a0, 12		/* a3 = dst (buf + 12) */
	mv	a4, t2			/* a4 = src (capture_sp) */
	mv	a5, a2			/* a5 = remaining bytes */
	beqz	a5, .Lcap_done
.Lcap_loop:
	lw	t1, 0(a4)
	sw	t1, 0(a3)
	addi	a4, a4, 4
	addi	a3, a3, 4
	addi	a5, a5, -4
	bnez	a5, .Lcap_loop
.Lcap_done:

	/* longjmp to mark: restore FP/SP, jump to re-entry label */
	mv	s0, t4			/* restore mark's FP */
	mv	sp, t5			/* restore mark's SP */
	jr	t6			/* a0 = buf (nonzero) */

/* __cont_resume(buf, value)
 *   Restores the captured stack segment and returns into it.
 *   The saved ra on the stack (pushed by __cont_capture) is popped
 *   by the epilogue, landing at the IR_CAPTURE resume point.
 *
 *   On entry:  0(sp) = buf pointer, 4(sp) = resume value */
	.globl __cont_resume
__cont_resume:
	lw	a3, 0(sp)		/* a3 = buf */
	lw	a1, 4(sp)		/* a1 = resume value */

	/* read buffer header */
	lw	t2, 0(a3)		/* t2 = capture_sp */
	lw	t3, 4(a3)		/* t3 = capture_fp */
	lw	a2, 8(a3)		/* a2 = size */

	/* restore FP and SP to captured state */
	mv	s0, t3
	mv	sp, t2

	/* copy stack data back: buf+12 -> capture_sp */
	addi	a4, a3, 12		/* a4 = src (buf + 12) */
	mv	a5, sp			/* a5 = dst (capture_sp) */
	beqz	a2, .Lres_done
.Lres_loop:
	lw	t1, 0(a4)
	sw	t1, 0(a5)
	addi	a4, a4, 4
	addi	a5, a5, 4
	addi	a2, a2, -4
	bnez	a2, .Lres_loop
.Lres_done:

	mv	a0, a1			/* a0 = resume value */
	lw	ra, 0(sp)		/* restore ra (saved by __cont_capture) */
	addi	sp, sp, 4
	ret				/* return to IR_CAPTURE resume point */

	.data
	.align	4
	.globl __cont_mark_sp
__cont_mark_sp:
	.word	0
	.globl	__cont_arena_ptr
__cont_arena_ptr:
	.word	__cont_arena
__moo_arena_ptr:
	.word	__moo_arena

	.bss
	.align	4
__cont_arena:
	.space	65536
__moo_arena:
	.space	65536
	.space	65536
stack_top:
