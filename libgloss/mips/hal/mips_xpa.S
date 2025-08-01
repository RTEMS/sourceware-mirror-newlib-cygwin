/*
 * Copyright (C) 2015-2018 MIPS Tech, LLC
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
*/

#include <mips/regdef.h>
#include <mips/cpu.h>
#include <mips/asm.h>
#include <mips/hal.h>

MIPS_NOMIPS16

/*
 * FUNCTION:	_xpa_save
 * DESCRIPTION:	save the XPA version of badvaddr.
 * RETURNS:	int
 *		0:	No context saved
 *		CTX_*:	Type of conext stored
 */
LEAF(_xpa_save)
	move	a1, a0
	PTR_S	zero, LINKCTX_NEXT(a1)
	/* Test for LPA support */
	mfc0	t0, C0_CONFIG3
	ext	t0, t0, CFG3_LPA_SHIFT, 1
	beqz	t0, 1f
	/* Test for LPA enabled */
	mfc0	t0, C0_PAGEGRAIN
	ext	t0, t0, PAGEGRAIN_ELPA_SHIFT, PAGEGRAIN_ELPA_BITS
	bnez	t0, 2f

	/* LPA either unavailable or not enabled - return 0 */
1:	move	va0, zero
	jr	ra

2:	lui	va0, %hi(LINKCTX_TYPE_XPA)
	addiu	va0, va0, %lo(LINKCTX_TYPE_XPA)
	mfc0	t0, C0_BADVADDR
	.set push
	.set xpa
	mfhc0	t1, C0_BADVADDR
	.set pop
#if BYTE_ORDER == BIG_ENDIAN
	sw	t0, XPACTX_BADVADDR(a1)
	sw	t1, (XPACTX_BADVADDR+4)(a1)
#else /* BYTE ORDER == LITTLE_ENDIAN */
	sw	t1, XPACTX_BADVADDR(a1)
	sw	t0, (XPACTX_BADVADDR+4)(a1)
#endif
	REG_S	va0, LINKCTX_ID(a1)
	jr ra
END(_xpa_save)
