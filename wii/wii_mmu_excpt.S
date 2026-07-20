/* ---------------------------------------------------------------------------
 * wii_mmu_excpt.S – DSI/ISI exception entry stub for wii_mmu (tuxedo libogc)
 *
 * Installed into __ppc_excpt_table[] via PPCExcptSetHandler() (see
 * WMMU_OnMemMapped in wii_mmu.cpp). libogc's shared dispatcher
 * (PPCExcptEntryGeneral, exception_asm.o) rfi's straight into us with:
 *
 *   - r11        = exception id (PPC_EXCPT_DSI=3 / PPC_EXCPT_ISI=4)
 *   - r12        = &__ppc_excpt_table[id]         (not useful to us)
 *   - MSR        = original MSR | 0x30 (IR/DR on; translation enabled)
 *   - r1         = the interrupted code's stack pointer, unchanged
 *   - r0,r2..r10,r13..r31, LR, CTR, XER  = exactly the interrupted values
 *     (the dispatcher only ever touches r11/r12/CR/SRR0/SRR1)
 *   - original r11/r12                  saved by the dispatcher in SPRG0/1
 *   - original SRR0(pc)/SRR1(msr)/CR    saved by the dispatcher at the fixed
 *     low-memory buffer __ppc_excpt_buf (+0x0/+0x4/+0x8) — this protocol
 *     isn't in any public tuxedo header; verified by disassembling
 *     libogc.a's exception_asm.o (PPCExcptEntryGeneral) / exception.o
 *     (PPCExcptInit, which patches each vector's copy of the trampoline
 *     with its own exception id at what becomes our entry r11).
 *   - DAR: untouched hardware SPR, still valid — read fresh here.
 *
 * We build a frame_context (layout in wii_mmu.h, shared with wii_mmu.cpp) on
 * the interrupted stack, call the C dispatcher, then restore everything and
 * rfi back. r1 itself is restored implicitly by popping the frame (never
 * stored in/reloaded from the GPR array — see the ra==1 guard in
 * wmmu_handle_dsi()).
 *
 * NOTE: MSR.RI stays 0 the whole time except when wmmu_msr_open() reopens it
 * around the C dispatch — any exception raised outside that window goes
 * straight to libogc's panic screen (no nesting).
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
	stwu	1,-WMMU_CTX_SIZE(1)		/* frame_context on the interrupted stack */

	stw	11,WMMU_CTX_EXCPT(1)		/* exception id, still in r11             */
	stw	4,WMMU_CTX_GPR+16(1)		/* GPR[4] — save before using r4 as scratch */

	/* original SRR0/SRR1/CR, saved by the dispatcher before it rfi'd here */
	lis	12,__ppc_excpt_buf@ha
	addi	12,12,__ppc_excpt_buf@l
	lwz	4,0(12)
	stw	4,WMMU_CTX_SRR0(1)
	lwz	4,4(12)
	stw	4,WMMU_CTX_SRR1(1)
	stw	4,WMMU_CTX_MSR(1)		/* duplicate: dump/printf convenience      */
	lwz	4,8(12)
	stw	4,WMMU_CTX_CR(1)

	mfdar	4
	stw	4,WMMU_CTX_DAR(1)
	mflr	4
	stw	4,WMMU_CTX_LR(1)
	mfctr	4
	stw	4,WMMU_CTX_CTR(1)
	mfxer	4
	stw	4,WMMU_CTX_XER(1)

	/* original r11/r12, stashed by the dispatcher in SPRG0/SPRG1            */
	mfsprg	4,0
	stw	4,WMMU_CTX_GPR+44(1)		/* GPR[11] */
	mfsprg	4,1
	stw	4,WMMU_CTX_GPR+48(1)		/* GPR[12] */

	/* everything else is still exactly the interrupted value                */
	stw	0,WMMU_CTX_GPR+0(1)
	stw	2,WMMU_CTX_GPR+8(1)
	stw	3,WMMU_CTX_GPR+12(1)
	stw	5,WMMU_CTX_GPR+20(1)
	stw	6,WMMU_CTX_GPR+24(1)
	stw	7,WMMU_CTX_GPR+28(1)
	stw	8,WMMU_CTX_GPR+32(1)
	stw	9,WMMU_CTX_GPR+36(1)
	stw	10,WMMU_CTX_GPR+40(1)
	stmw	13,WMMU_CTX_GPR+52(1)		/* r13..r31 */

	/* int WMMU_HandleExcpt(frame_context* ctx)  — ctx = our frame == r1     */
	addi	3,1,0
	bl	WMMU_HandleExcpt

	/* ---- restore ---------------------------------------------------- */
	lwz	4,WMMU_CTX_CR(1)
	mtcr	4
	lwz	4,WMMU_CTX_LR(1)
	mtlr	4
	lwz	4,WMMU_CTX_CTR(1)
	mtctr	4
	lwz	4,WMMU_CTX_XER(1)
	mtxer	4
	lwz	4,WMMU_CTX_SRR0(1)		/* C may have advanced it (skip the insn) */
	mtsrr0	4
	lwz	4,WMMU_CTX_SRR1(1)
	mtsrr1	4

	lwz	0,WMMU_CTX_GPR+0(1)
	lwz	2,WMMU_CTX_GPR+8(1)
	lwz	3,WMMU_CTX_GPR+12(1)
	lwz	5,WMMU_CTX_GPR+20(1)
	lwz	6,WMMU_CTX_GPR+24(1)
	lwz	7,WMMU_CTX_GPR+28(1)
	lwz	8,WMMU_CTX_GPR+32(1)
	lwz	9,WMMU_CTX_GPR+36(1)
	lwz	10,WMMU_CTX_GPR+40(1)
	lmw	13,WMMU_CTX_GPR+52(1)		/* also reloads r11/r12 */
	lwz	4,WMMU_CTX_GPR+16(1)		/* GPR[4] — reload last, was our scratch */

	addi	1,1,WMMU_CTX_SIZE		/* pop frame — restores r1 implicitly  */
	rfi

#endif /* WMMU_ENABLE */
