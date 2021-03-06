/*
 * Copyright (C) 2016 Marcelina Kościelnicka <mwk@0x04.net>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pgraph.h"
#include "pgraph_mthd.h"
#include "pgraph_class.h"
#include "nva.h"

namespace hwtest {
namespace pgraph {

void MthdOperation::emulate_mthd() {
	pgraph_grobj_set_operation(&exp, egrobj, val);
}

void MthdDither::emulate_mthd() {
	uint32_t rval = 0;
	if ((val & 3) == 0)
		rval = 1;
	if ((val & 3) == 1)
		rval = 2;
	if ((val & 3) == 2)
		rval = 3;
	if ((val & 3) == 3)
		rval = 3;
	pgraph_grobj_set_dither(&exp, egrobj, rval);
}

void MthdPatch::emulate_mthd() {
	if (chipset.chipset < 5) {
		if (val < 2) {
			if (extr(exp.debug_b, 8, 1) && val == 0) {
				exp.ctx_cache_a[subc] = exp.ctx_switch_a;
				insrt(exp.ctx_cache_a[subc], 24, 1, 0);
				if (extr(exp.debug_b, 20, 1))
					exp.ctx_switch_a = exp.ctx_cache_a[subc];
			} else {
				exp.intr |= 0x10;
				exp.fifo_enable = 0;
			}
		}
		if (!extr(exp.nsource, 1, 1) && !extr(exp.intr, 4, 1)) {
			if (!nv04_pgraph_is_nv25p(&chipset))
				insrt(egrobj[0], 24, 8, extr(exp.ctx_switch_a, 24, 8));
			else
				egrobj[0] = exp.ctx_switch_a;
			insrt(egrobj[0], 24, 1, 0);
		}
	} else {
		exp.intr |= 0x10;
		exp.fifo_enable = 0;
	}
}

void MthdDmaNotify::emulate_mthd() {
	uint32_t rval = val;
	int dcls = extr(pobj[0], 0, 12);
	if (dcls == 0x30)
		rval = 0;
	bool bad = false;
	if (dcls != 0x30 && dcls != 0x3d && dcls != 3)
		bad = true;
	if (bad && extr(exp.debug_d, 23, 1) && chipset.chipset != 5)
		nv04_pgraph_blowup(&exp, 2);
	pgraph_grobj_set_dma_pre(&exp, egrobj, 0, rval, false);
	if (extr(exp.notify, 24, 1))
		nv04_pgraph_blowup(&exp, 0x2000);
	if (bad && extr(exp.debug_d, 23, 1))
		nv04_pgraph_blowup(&exp, 2);
	pgraph_grobj_set_dma_post(&exp, 0, rval, false);
	bool check_prot = true;
	if (chipset.card_type >= 0x10)
		check_prot = dcls != 0x30;
	else if (chipset.chipset >= 5)
		check_prot = dcls == 2 || dcls == 0x3d;
	unsigned min = 0xf;
	if (pgraph_class(&exp) == 0x39)
		min = 0x1f;
	if (pgraph_3d_class(&exp) >= PGRAPH_3D_CELSIUS)
		min = 0x1f;
	if (check_prot && pobj[1] < min)
		nv04_pgraph_blowup(&exp, 4);
}

void MthdDmaGrobj::emulate_mthd() {
	uint32_t rval = val;
	int dcls = extr(pobj[0], 0, 12);
	if (dcls == 0x30)
		rval = 0;
	bool bad = false;
	if (dcls != 0x30 && dcls != 0x3d && dcls != ecls)
		bad = true;
	if (bad && extr(exp.debug_d, 23, 1) && chipset.chipset != 5)
		nv04_pgraph_blowup(&exp, 2);
	pgraph_grobj_set_dma_pre(&exp, egrobj, 1 + which, rval, clr);
	if (bad && extr(exp.debug_d, 23, 1))
		nv04_pgraph_blowup(&exp, 2);
	pgraph_grobj_set_dma_post(&exp, 1 + which, rval, clr);
	bool prot_err = false;
	bool check_prot = true;
	if (chipset.card_type >= 0x10)
		check_prot = dcls != 0x30;
	else if (chipset.chipset >= 5)
		check_prot = dcls == 2 || dcls == 0x3d;
	if (check_prot) {
		if (align) {
			if (extr(pobj[1], 0, 8) != 0xff)
				prot_err = true;
			if (cls != 0x48 && extr(pobj[0], 20, 8))
				prot_err = true;
		}
		if (fence) {
			if (pobj[1] & ~0xfff)
				prot_err = true;
			if (extr(pobj[0], 20, 12))
				prot_err = true;
		}
	}
	if (prot_err)
		nv04_pgraph_blowup(&exp, 4);
	if (check_prev && !pgraph_grobj_get_dma(&exp, 1))
		pgraph_state_error(&exp);
}

static void nv04_pgraph_set_ctx(struct pgraph_state *state, uint32_t grobj[4], uint32_t pobj[4], int ecls, int bit) {
	int ccls = extr(pobj[0], 0, 12);
	bool bad = ccls != ecls && ccls != 0x30;
	if (state->chipset.card_type < 0x10 && !extr(state->nsource, 1, 1)) {
		insrt(grobj[0], 8, 24, extr(state->ctx_switch_a, 8, 24));
		insrt(grobj[0], bit, 1, ccls != 0x30);
	}
	if (bad && extr(state->debug_d, 23, 1))
		nv04_pgraph_blowup(state, 2);
	if (!extr(state->nsource, 1, 1)) {
		if (state->chipset.card_type >= 0x10) {
			if (!nv04_pgraph_is_nv25p(&state->chipset))
				insrt(grobj[0], 8, 24, extr(state->ctx_switch_a, 8, 24));
			else
				grobj[0] = state->ctx_switch_a;
			insrt(grobj[0], bit, 1, ccls != 0x30);
		}
		int subc = extr(state->ctx_user, 13, 3);
		state->ctx_cache_a[subc] = state->ctx_switch_a;
		insrt(state->ctx_cache_a[subc], bit, 1, ccls != 0x30);
		if (extr(state->debug_b, 20, 1))
			state->ctx_switch_a = state->ctx_cache_a[subc];
	}
}

void MthdCtxChroma::emulate_mthd() {
	int ecls = (is_new || chipset.card_type < 0x10 ? 0x57 : 0x17);
	nv04_pgraph_set_ctx(&exp, egrobj, pobj, ecls, 12);
}

void MthdCtxClip::emulate_mthd() {
	nv04_pgraph_set_ctx(&exp, egrobj, pobj, 0x19, 13);
}

void MthdCtxPattern::emulate_mthd() {
	int ecls = (is_new ? 0x44 : 0x18);
	nv04_pgraph_set_ctx(&exp, egrobj, pobj, ecls, 27);
}

void MthdCtxRop::emulate_mthd() {
	nv04_pgraph_set_ctx(&exp, egrobj, pobj, 0x43, 28);
}

void MthdCtxBeta::emulate_mthd() {
	nv04_pgraph_set_ctx(&exp, egrobj, pobj, 0x12, 29);
}

void MthdCtxBeta4::emulate_mthd() {
	nv04_pgraph_set_ctx(&exp, egrobj, pobj, 0x72, 30);
}

void MthdCtxSurf::emulate_mthd() {
	int ccls = extr(pobj[0], 0, 12);
	bool bad = false;
	int ecls = 0x58 + which;
	bad = ccls != ecls && ccls != 0x30;
	bool isswz = ccls == 0x52;
	if (nv04_pgraph_is_nv15p(&chipset) && ccls == 0x9e)
		isswz = true;
	if (chipset.card_type == 0x30 && ccls == 0x39e)
		isswz = true;
	if (chipset.card_type < 0x10 && !extr(exp.nsource, 1, 1)) {
		insrt(egrobj[0], 8, 24, extr(exp.ctx_switch_a, 8, 24));
		insrt(egrobj[0], 25 + (which & 1), 1, ccls != 0x30);
		if (which == 0 || which == 2)
			insrt(egrobj[0], 14, 1, isswz);
	}
	if (bad && extr(exp.debug_d, 23, 1))
		nv04_pgraph_blowup(&exp, 2);
	if (!extr(exp.nsource, 1, 1)) {
		if (chipset.card_type >= 0x10) {
			if (!nv04_pgraph_is_nv25p(&chipset))
				insrt(egrobj[0], 8, 24, extr(exp.ctx_switch_a, 8, 24));
			else
				egrobj[0] = exp.ctx_switch_a;
			insrt(egrobj[0], 25 + (which & 1), 1, ccls != 0x30);
			if (which == 0 || which == 2)
				insrt(egrobj[0], 14, 1, isswz);
		}
		exp.ctx_cache_a[subc] = exp.ctx_switch_a;
		insrt(exp.ctx_cache_a[subc], 25 + (which & 1), 1, ccls != 0x30);
		if (which == 0 || which == 2)
			insrt(exp.ctx_cache_a[subc], 14, 1, isswz);
		if (extr(exp.debug_b, 20, 1))
			exp.ctx_switch_a = exp.ctx_cache_a[subc];
	}
}

void MthdCtxSurf2D::emulate_mthd() {
	int ccls = extr(pobj[0], 0, 12);
	bool bad = false;
	bad = ccls != 0x42 && ccls != 0x52 && ccls != 0x30;
	if (ccls == (extr(exp.debug_d, 16, 1) ? 0x82 : 0x62) && chipset.card_type >= 0x10 && new_ok)
		bad = false;
	if (nv04_pgraph_is_nv15p(&chipset) && ccls == 0x9e && new_ok)
		bad = false;
	if (chipset.card_type == 0x30 && (ccls == 0x362 || ccls == 0x39e) && (new_ok || !swz_ok))
		bad = false;
	bool isswz = ccls == 0x52;
	if (nv04_pgraph_is_nv15p(&chipset) && ccls == 0x9e)
		isswz = true;
	if (chipset.card_type == 0x30 && ccls == 0x39e)
		isswz = true;
	if (isswz && !swz_ok)
		bad = true;
	if (chipset.chipset >= 5 && chipset.card_type < 0x10 && !extr(exp.nsource, 1, 1)) {
		insrt(egrobj[0], 8, 24, extr(exp.ctx_switch_a, 8, 24));
		insrt(egrobj[0], 25, 1, ccls != 0x30);
		insrt(egrobj[0], 14, 1, isswz);
	}
	if (bad && extr(exp.debug_d, 23, 1))
		nv04_pgraph_blowup(&exp, 2);
	if (!extr(exp.nsource, 1, 1)) {
		if (chipset.card_type >= 0x10) {
			if (!nv04_pgraph_is_nv25p(&chipset))
				insrt(egrobj[0], 8, 24, extr(exp.ctx_switch_a, 8, 24));
			else
				egrobj[0] = exp.ctx_switch_a;
			insrt(egrobj[0], 25, 1, ccls != 0x30);
			insrt(egrobj[0], 14, 1, isswz);
		}
		exp.ctx_cache_a[subc] = exp.ctx_switch_a;
		insrt(exp.ctx_cache_a[subc], 25, 1, ccls != 0x30);
		insrt(exp.ctx_cache_a[subc], 14, 1, isswz);
		if (extr(exp.debug_b, 20, 1))
			exp.ctx_switch_a = exp.ctx_cache_a[subc];
	}
}

void MthdCtxSurf3D::emulate_mthd() {
	int ccls = extr(pobj[0], 0, 12);
	bool bad = false;
	bad = ccls != 0x53 && ccls != 0x30;
	if (ccls == 0x93 && chipset.card_type >= 0x10 && new_ok)
		bad = false;
	bool isswz = ccls == 0x52;
	if (nv04_pgraph_is_nv15p(&chipset) && ccls == 0x9e)
		isswz = true;
	if (chipset.chipset >= 5 && chipset.card_type < 0x10 && !extr(exp.nsource, 1, 1)) {
		insrt(egrobj[0], 8, 24, extr(exp.ctx_switch_a, 8, 24));
		insrt(egrobj[0], 25, 1, ccls != 0x30);
		insrt(egrobj[0], 14, 1, isswz);
	}
	if (bad && extr(exp.debug_d, 23, 1))
		nv04_pgraph_blowup(&exp, 2);
	if (chipset.chipset < 5) {
		int subc = extr(exp.ctx_user, 13, 3);
		exp.ctx_cache_a[subc] = exp.ctx_switch_a;
		insrt(exp.ctx_cache_a[subc], 25, 1, ccls == 0x53);
		if (extr(exp.debug_b, 20, 1))
			exp.ctx_switch_a = exp.ctx_cache_a[subc];
		if (!extr(exp.nsource, 1, 1))
			insrt(egrobj[0], 24, 8, extr(exp.ctx_cache_a[subc], 24, 8));
	} else {
		if (!extr(exp.nsource, 1, 1)) {
			if (chipset.card_type >= 0x10) {
				if (!nv04_pgraph_is_nv25p(&chipset))
					insrt(egrobj[0], 8, 24, extr(exp.ctx_switch_a, 8, 24));
				else
					egrobj[0] = exp.ctx_switch_a;
				insrt(egrobj[0], 25, 1, ccls != 0x30);
				insrt(egrobj[0], 14, 1, isswz);
			}
			int subc = extr(exp.ctx_user, 13, 3);
			exp.ctx_cache_a[subc] = exp.ctx_switch_a;
			insrt(exp.ctx_cache_a[subc], 25, 1, ccls != 0x30);
			insrt(exp.ctx_cache_a[subc], 14, 1, isswz);
			if (extr(exp.debug_b, 20, 1))
				exp.ctx_switch_a = exp.ctx_cache_a[subc];
		}
	}
}

}
}
