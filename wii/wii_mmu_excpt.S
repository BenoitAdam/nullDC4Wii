/* ---------------------------------------------------------------------------
 * wii_mmu_excpt.S – DSI/ISI exception entry stub for wii_mmu (classic libogc)
 *
 * Installed into _exceptionhandlertable[] via __exception_sethandler(), so we
 * are entered the same way libogc's own default_exceptionhandler is: the
 * low-level vector (exceptionhandler_start, see libogc exception_handler.S)
 * has already, with translation OFF, written a 728-byte frame at the
 * PHYSICAL alias of (r1 - 728) containing:
 *
 *   +0x08 EXCPT_Number   +0x0C SRR0        +0x10 SRR1
 *   +0x14 GPR0  +0x18 GPR1  +0x1C GPR2  +0x20 GPR3  +0x24 GPR4  +0x28 GPR5
 *   +0xB4 CR    +0xB8 LR    +0xBC CTR   +0xC0 XER   +0xC4 MSR   +0xC8 DAR
 *
 * then rfi'd to us with:
 *   - MSR: IR/DR on (translation enabled), EE off, RI off
 *   - r1  = the interrupted code's stack pointer (unchanged, virtual)
 *   - r0,r2,r3,r4,r5 already saved in the frame (r3/r4/r5 clobbered)
 *   - CR/LR/CTR/XER values saved; LR/CTR/XER registers still intact
 *
 * Since the 0x8000_0000 BAT is a 1:1 offset mapping of physical MEM1, the
 * frame the vector wrote physically at (r1&0x3FFFFFFF)-728 is the same
 * memory our virtual r1-728 points at — so we just stwu and keep filling
 * the same frame, exactly like default_exceptionhandler does.
 *
 * We complete the GPR save (r6-r31), call the C dispatcher with a
 * frame_context* (= frame+8), then restore everything and rfi back.
 * The C side never touches FPU/paired-singles, so FPRs/GQRs are not saved.
 *
 * NOTE: MSR.RI stays 0 the whole time — any exception raised inside the C
 * dispatcher goes straight to libogc's panic screen (no nesting).
 * ------------------------------------------------------------------------- */

/* NOTE: .S files are assembled with ASFLAGS (no project -I paths), but a
 * quoted include always searches the including file's own directory first,
 * so this resolves to wii/wii_mmu.h without any -I. */
#include "wii_mmu.h"

#if WMMU_ENABLE

	.text
	.balign	4
	.globl	wmmu_excpt_stub
wmmu_excpt_stub:
	stwu	1,-728(1)		/* same frame the vector populated       */

	/* complete the GPR save: r6..r15 singly, r16..r31 in one stmw */
	stw	6,44(1)
	stw	7,48(1)
	stw	8,52(1)
	stw	9,56(1)
	stw	10,60(1)
	stw	11,64(1)
	stw	12,68(1)
	stw	13,72(1)
	stw	14,76(1)
	stw	15,80(1)
	stmw	16,84(1)

	/* int WMMU_HandleExcpt(frame_context* ctx)  — ctx = frame + 8       */
	/* (bl clobbers LR; original LR value is in the frame at +0xB8)      */
	addi	3,1,8
	bl	WMMU_HandleExcpt

	/* ---- restore ---------------------------------------------------- */
	lwz	4,180(1)		/* CR   */
	mtcr	4
	lwz	4,184(1)		/* LR   */
	mtlr	4
	lwz	4,188(1)		/* CTR  */
	mtctr	4
	lwz	4,192(1)		/* XER  */
	mtxer	4
	lwz	4,12(1)			/* SRR0 (C may have advanced it)         */
	mtspr	26,4
	lwz	4,16(1)			/* SRR1 */
	mtspr	27,4

	lwz	0,20(1)
	lwz	2,28(1)
	lwz	5,40(1)
	lwz	6,44(1)
	lwz	7,48(1)
	lwz	8,52(1)
	lwz	9,56(1)
	lwz	10,60(1)
	lwz	11,64(1)
	lwz	12,68(1)
	lwz	13,72(1)
	lwz	14,76(1)
	lwz	15,80(1)
	lmw	16,84(1)
	lwz	3,32(1)
	lwz	4,36(1)

	addi	1,1,728
	rfi

#endif /* WMMU_ENABLE */
