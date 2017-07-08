/*
 * Allwinner SoCs display driver.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

/******************************************************************************
 *    All Winner Tech, All Right Reserved. 2014-2016 Copyright (c)
 *
 *    File name   :    de_ccsc.c
 *
 *    Description :    display engine 3.0 channel csc basic function definition
 *
 *    History     :    2016/03/29  vito cheng  v0.1  Initial version
 ******************************************************************************/

#include "de_rtmx.h"
#include "de_csc_type.h"
#include "de_vep_table.h"
#include "de_csc.h"
#include "de_enhance.h"

static struct __ccsc_reg_t *ccsc_dev[DE_NUM][CHN_NUM];
static struct __icsc_reg_t *icsc_dev[DE_NUM][CHN_NUM];
static struct __fcc_csc_reg_t *fcc_csc_dev[DE_NUM][CHN_NUM];
static struct de_reg_blocks csc_block[DE_NUM][CHN_NUM];
static struct de_reg_blocks icsc_block[DE_NUM][CHN_NUM];
static struct de_reg_blocks fcc_csc_block[DE_NUM][CHN_NUM];

static unsigned int vi_num[DE_NUM];
static unsigned int chn_num[DE_NUM];
static unsigned int vep_support[DE_NUM][CHN_NUM];

static int de_ccsc_set_reg_base(unsigned int sel, unsigned int chno, void *base)
{
	__inf("sel=%d, chno=%d, base=0x%p\n", sel, chno, base);
	ccsc_dev[sel][chno] = (struct __ccsc_reg_t *) base;

	return 0;
}

static int de_icsc_set_reg_base(unsigned int sel, unsigned int chno, void *base)
{
	__inf("sel=%d, chno=%d, base=0x%p\n", sel, chno, base);
	icsc_dev[sel][chno] = (struct __icsc_reg_t *) base;

	return 0;
}

static int de_fcsc_set_reg_base(unsigned int sel, unsigned int chno, void *base)
{
	__inf("sel=%d, chno=%d, base=0x%p\n", sel, chno, base);
	fcc_csc_dev[sel][chno] = (struct __fcc_csc_reg_t *) base;

	return 0;
}

static int de_set_icsc_coef(unsigned int sel, unsigned int ch_id,
			    int icsc_coeff[16])
{
	unsigned int temp0, temp1, temp2;

	temp0 = (icsc_coeff[12] >= 0) ? (icsc_coeff[12] & 0x3ff) :
		(0x400 - (unsigned int)(icsc_coeff[12] & 0x3ff));
	temp1 = (icsc_coeff[13] >= 0) ? (icsc_coeff[13] & 0x3ff) :
		(0x400 - (unsigned int)(icsc_coeff[13] & 0x3ff));
	temp2 = (icsc_coeff[14] >= 0) ? (icsc_coeff[14] & 0x3ff) :
		(0x400 - (unsigned int)(icsc_coeff[14] & 0x3ff));

	icsc_dev[sel][ch_id]->en.dwval = 0x1;
	icsc_dev[sel][ch_id]->d[0].dwval = temp0;
	icsc_dev[sel][ch_id]->d[1].dwval = temp1;
	icsc_dev[sel][ch_id]->d[2].dwval = temp2;

	icsc_dev[sel][ch_id]->c0[0].dwval = icsc_coeff[0];
	icsc_dev[sel][ch_id]->c0[1].dwval = icsc_coeff[1];
	icsc_dev[sel][ch_id]->c0[2].dwval = icsc_coeff[2];
	icsc_dev[sel][ch_id]->c03.dwval = icsc_coeff[3];

	icsc_dev[sel][ch_id]->c1[0].dwval = icsc_coeff[4];
	icsc_dev[sel][ch_id]->c1[1].dwval = icsc_coeff[5];
	icsc_dev[sel][ch_id]->c1[2].dwval = icsc_coeff[6];
	icsc_dev[sel][ch_id]->c13.dwval = icsc_coeff[7];

	icsc_dev[sel][ch_id]->c2[0].dwval = icsc_coeff[8];
	icsc_dev[sel][ch_id]->c2[1].dwval = icsc_coeff[9];
	icsc_dev[sel][ch_id]->c2[2].dwval = icsc_coeff[10];
	icsc_dev[sel][ch_id]->c23.dwval = icsc_coeff[11];

	icsc_block[sel][ch_id].dirty = 1;

	return 0;
}

static int de_set_ccsc_coef(unsigned int sel, unsigned int ch_id,
			    int csc_coeff[16])
{
	int c, d;

	ccsc_dev[sel][ch_id]->c0[0].dwval = *(csc_coeff);
	ccsc_dev[sel][ch_id]->c0[1].dwval = *(csc_coeff + 1);
	ccsc_dev[sel][ch_id]->c0[2].dwval = *(csc_coeff + 2);
	c = *(csc_coeff + 3) & 0x7ff;
	d = *(csc_coeff + 12) & 0x7ff;
	ccsc_dev[sel][ch_id]->d0.dwval = c | (d << 16);

	ccsc_dev[sel][ch_id]->c1[0].dwval = *(csc_coeff + 4);
	ccsc_dev[sel][ch_id]->c1[1].dwval = *(csc_coeff + 5);
	ccsc_dev[sel][ch_id]->c1[2].dwval = *(csc_coeff + 6);
	c = *(csc_coeff + 7) & 0x7ff;
	d = *(csc_coeff + 13) & 0x7ff;
	ccsc_dev[sel][ch_id]->d1.dwval = c | (d << 16);

	ccsc_dev[sel][ch_id]->c2[0].dwval = *(csc_coeff + 8);
	ccsc_dev[sel][ch_id]->c2[1].dwval = *(csc_coeff + 9);
	ccsc_dev[sel][ch_id]->c2[2].dwval = *(csc_coeff + 10);
	c = *(csc_coeff + 11) & 0x7ff;
	d = *(csc_coeff + 14) & 0x7ff;
	ccsc_dev[sel][ch_id]->d2.dwval = c | (d << 16);

	csc_block[sel][ch_id].dirty = 1;

	return 0;
}

static int de_set_fcc_csc_coef(unsigned int sel, unsigned int ch_id,
			       int in_csc_coeff[16], int out_csc_coeff[16])
{
	unsigned int temp0, temp1, temp2;

	temp0 = (in_csc_coeff[12] >= 0) ? (in_csc_coeff[12] & 0x3ff) :
		(0x400 - (unsigned int)(in_csc_coeff[12] & 0x3ff));
	temp1 = (in_csc_coeff[13] >= 0) ? (in_csc_coeff[13] & 0x3ff) :
		(0x400 - (unsigned int)(in_csc_coeff[13] & 0x3ff));
	temp2 = (in_csc_coeff[14] >= 0) ? (in_csc_coeff[14] & 0x3ff) :
		(0x400 - (unsigned int)(in_csc_coeff[14] & 0x3ff));

	fcc_csc_dev[sel][ch_id]->in_d[0].dwval = temp0;
	fcc_csc_dev[sel][ch_id]->in_d[1].dwval = temp1;
	fcc_csc_dev[sel][ch_id]->in_d[2].dwval = temp2;

	fcc_csc_dev[sel][ch_id]->in_c0[0].dwval = in_csc_coeff[0];
	fcc_csc_dev[sel][ch_id]->in_c0[1].dwval = in_csc_coeff[1];
	fcc_csc_dev[sel][ch_id]->in_c0[2].dwval = in_csc_coeff[2];
	fcc_csc_dev[sel][ch_id]->in_c03.dwval = in_csc_coeff[3];

	fcc_csc_dev[sel][ch_id]->in_c1[0].dwval = in_csc_coeff[4];
	fcc_csc_dev[sel][ch_id]->in_c1[1].dwval = in_csc_coeff[5];
	fcc_csc_dev[sel][ch_id]->in_c1[2].dwval = in_csc_coeff[6];
	fcc_csc_dev[sel][ch_id]->in_c13.dwval = in_csc_coeff[7];

	fcc_csc_dev[sel][ch_id]->in_c2[0].dwval = in_csc_coeff[8];
	fcc_csc_dev[sel][ch_id]->in_c2[1].dwval = in_csc_coeff[9];
	fcc_csc_dev[sel][ch_id]->in_c2[2].dwval = in_csc_coeff[10];
	fcc_csc_dev[sel][ch_id]->in_c23.dwval = in_csc_coeff[11];

	temp0 = (out_csc_coeff[12] >= 0) ? (out_csc_coeff[12] & 0x3ff) :
		(0x400 - (unsigned int)(out_csc_coeff[12] & 0x3ff));
	temp1 = (out_csc_coeff[13] >= 0) ? (out_csc_coeff[13] & 0x3ff) :
		(0x400 - (unsigned int)(out_csc_coeff[13] & 0x3ff));
	temp2 = (out_csc_coeff[14] >= 0) ? (out_csc_coeff[14] & 0x3ff) :
		(0x400 - (unsigned int)(out_csc_coeff[14] & 0x3ff));

	fcc_csc_dev[sel][ch_id]->out_d[0].dwval = temp0;
	fcc_csc_dev[sel][ch_id]->out_d[1].dwval = temp1;
	fcc_csc_dev[sel][ch_id]->out_d[2].dwval = temp2;

	fcc_csc_dev[sel][ch_id]->out_c0[0].dwval = out_csc_coeff[0];
	fcc_csc_dev[sel][ch_id]->out_c0[1].dwval = out_csc_coeff[1];
	fcc_csc_dev[sel][ch_id]->out_c0[2].dwval = out_csc_coeff[2];
	fcc_csc_dev[sel][ch_id]->out_c03.dwval = out_csc_coeff[3];

	fcc_csc_dev[sel][ch_id]->out_c1[0].dwval = out_csc_coeff[4];
	fcc_csc_dev[sel][ch_id]->out_c1[1].dwval = out_csc_coeff[5];
	fcc_csc_dev[sel][ch_id]->out_c1[2].dwval = out_csc_coeff[6];
	fcc_csc_dev[sel][ch_id]->out_c13.dwval = out_csc_coeff[7];

	fcc_csc_dev[sel][ch_id]->out_c2[0].dwval = out_csc_coeff[8];
	fcc_csc_dev[sel][ch_id]->out_c2[1].dwval = out_csc_coeff[9];
	fcc_csc_dev[sel][ch_id]->out_c2[2].dwval = out_csc_coeff[10];
	fcc_csc_dev[sel][ch_id]->out_c23.dwval = out_csc_coeff[11];

	fcc_csc_block[sel][ch_id].dirty = 1;

	return 0;
}

int de_ccsc_apply(unsigned int sel, unsigned int ch_id,
		  struct disp_csc_config *config)
{
	int ccsc_coeff[16], icsc_coeff[16], fcc_incsc_coeff[16],
	    fcc_outcsc_coeff[16];
	unsigned int in_fmt, in_mode, in_color_range, out_fmt, out_mode,
		     out_color_range;
	unsigned int i_in_fmt, i_in_mode, i_in_color_range, i_out_fmt,
		     i_out_mode, i_out_color_range;
	unsigned int fcc_in_fmt, fcc_in_mode, fcc_in_color_range, fcc_out_fmt,
		     fcc_out_mode, fcc_out_color_range;

	if (vep_support[sel][ch_id]) {
		/* FCE */
		if (config->in_fmt == DE_RGB) {
			i_in_fmt = config->in_fmt;
			i_in_mode = config->in_mode;
			i_in_color_range = config->in_color_range;
			i_out_fmt = DE_YUV;
			i_out_mode = DE_BT709;
			i_out_color_range = DISP_COLOR_RANGE_0_255;
		} else {
			i_in_fmt = config->in_fmt;
			i_in_mode = config->in_mode;
			i_in_color_range = config->in_color_range;
			i_out_fmt = config->in_fmt;
			i_out_mode = config->in_mode;
			i_out_color_range = DISP_COLOR_RANGE_0_255;
		}

		de_csc_coeff_calc(i_in_fmt, i_in_mode, i_in_color_range,
				  i_out_fmt, i_out_mode, i_out_color_range,
				  icsc_coeff);

		de_set_icsc_coef(sel, ch_id, icsc_coeff);

		/* CSC in FCC */
		fcc_in_fmt = i_out_fmt;
		fcc_in_mode = i_out_mode;
		fcc_in_color_range = i_out_color_range;
		fcc_out_fmt = DE_RGB;
		fcc_out_mode = DE_BT709;
		fcc_out_color_range = DISP_COLOR_RANGE_0_255;

		de_csc_coeff_calc(fcc_in_fmt, fcc_in_mode, fcc_in_color_range,
				  fcc_out_fmt, fcc_out_mode,
				  fcc_out_color_range, fcc_incsc_coeff);

		de_csc_coeff_calc(fcc_out_fmt, fcc_out_mode,
				  fcc_out_color_range, fcc_in_fmt, fcc_in_mode,
				  fcc_in_color_range, fcc_outcsc_coeff);

		de_set_fcc_csc_coef(sel, ch_id, fcc_incsc_coeff,
				    fcc_outcsc_coeff);

		/* CSC in BLD */
		in_fmt = i_out_fmt;
		in_mode = i_out_mode;
		in_color_range = i_out_color_range;
		out_fmt = config->out_fmt;
		out_mode = config->out_mode;
		out_color_range = config->out_color_range;

	} else {
		/* CSC in BLD */
		in_fmt = config->in_fmt;
		in_mode = config->in_mode;
		in_color_range = config->in_color_range;
		out_fmt = config->out_fmt;
		out_mode = config->out_mode;
		out_color_range = config->out_color_range;
	}

	de_csc_coeff_calc(in_fmt, in_mode, in_color_range, out_fmt, out_mode,
			  out_color_range, ccsc_coeff);

	de_set_ccsc_coef(sel, ch_id, ccsc_coeff);

	return 0;
}

int de_ccsc_update_regs(unsigned int sel)
{
	int ch_id;

	for (ch_id = 0; ch_id < vi_num[sel]; ch_id++) {
		if (vep_support[sel][ch_id]) {
			if (icsc_block[sel][ch_id].dirty == 0x1) {
				memcpy((void *)icsc_block[sel][ch_id].off,
				       icsc_block[sel][ch_id].val,
				       icsc_block[sel][ch_id].size);
				icsc_block[sel][ch_id].dirty = 0x0;
			}

			if (fcc_csc_block[sel][ch_id].dirty == 0x1) {
				memcpy((void *)fcc_csc_block[sel][ch_id].off,
				       fcc_csc_block[sel][ch_id].val,
				       fcc_csc_block[sel][ch_id].size);
				fcc_csc_block[sel][ch_id].dirty = 0x0;
			}
		}
	}

	for (ch_id = 0; ch_id < chn_num[sel]; ch_id++) {
		if (csc_block[sel][ch_id].dirty == 0x1) {
			memcpy((void *)csc_block[sel][ch_id].off,
			       csc_block[sel][ch_id].val,
			       csc_block[sel][ch_id].size);
			csc_block[sel][ch_id].dirty = 0x0;
		}
	}
	return 0;
}

int de_ccsc_init(disp_bsp_init_para *para)
{
	uintptr_t base, base_ofst;
	void *memory;
	int screen_id, ch_id, device_num;

	device_num = de_feat_get_num_screens();

	for (screen_id = 0; screen_id < device_num; screen_id++) {
		vi_num[screen_id] = de_feat_get_num_vi_chns(screen_id);
		chn_num[screen_id] = de_feat_get_num_chns(screen_id);

		for (ch_id = 0; ch_id < vi_num[screen_id]; ch_id++) {
			vep_support[screen_id][ch_id] =
			    de_feat_is_support_vep_by_chn(screen_id, ch_id);
			if (vep_support[screen_id][ch_id]) {
				/* fce csc */
				base = para->reg_base[DISP_MOD_DE] +
				       (screen_id + 1) * 0x00100000
				       + FCE_OFST + 0x40;

				__inf("sel%d, icsc_base[%d]=0x%p\n", screen_id,
				      ch_id, (void *)base);

				memory = kmalloc(sizeof(struct __icsc_reg_t),
						 GFP_KERNEL | __GFP_ZERO);
				if (memory == NULL) {
					__wrn("alloc icsc[%d][%d] mm fail!",
					      screen_id, ch_id);
					__wrn("size=0x%x\n", (unsigned int)
					sizeof(struct __icsc_reg_t));
					return -1;
				}
				icsc_block[screen_id][ch_id].off = base;
				icsc_block[screen_id][ch_id].val = memory;
				icsc_block[screen_id][ch_id].size = 0x40;
				icsc_block[screen_id][ch_id].dirty = 0;

				de_icsc_set_reg_base(screen_id, ch_id, memory);

				/* fcc csc */
				base = para->reg_base[DISP_MOD_DE] +
				       (screen_id + 1) * 0x00100000
				       + FCC_OFST + 0x60;

				__inf("sel%d, fcc_csc_base[%d]=0x%p\n",
				      screen_id, ch_id, (void *)base);

				memory = kmalloc(sizeof(struct __fcc_csc_reg_t),
						 GFP_KERNEL | __GFP_ZERO);
				if (memory == NULL) {
					__wrn("alloc fcc_csc[%d][%d] mm fail!",
					      screen_id, ch_id);
					__wrn("size=0x%x\n", (unsigned int)
					sizeof(struct __fcc_csc_reg_t));
					return -1;
				}
				fcc_csc_block[screen_id][ch_id].off = base;
				fcc_csc_block[screen_id][ch_id].val = memory;
				fcc_csc_block[screen_id][ch_id].size = 0x80;
				fcc_csc_block[screen_id][ch_id].dirty = 0;

				de_fcsc_set_reg_base(screen_id, ch_id, memory);
			}
		}

		for (ch_id = 0; ch_id < chn_num[screen_id]; ch_id++) {
			base_ofst = 0x0910 + ch_id * 0x30;
			base = para->reg_base[DISP_MOD_DE] + (screen_id + 1)
				* 0x00100000 + base_ofst;

			memory = kmalloc(sizeof(struct __ccsc_reg_t),
					 GFP_KERNEL | __GFP_ZERO);
			if (memory == NULL) {
				__wrn("alloc Ccsc[%d][%d] mm fail!size=0x%x\n",
				     screen_id, ch_id,
				     (unsigned int)sizeof(struct __ccsc_reg_t));
				return -1;
			}

			csc_block[screen_id][ch_id].off = base;
			csc_block[screen_id][ch_id].val = memory;
			csc_block[screen_id][ch_id].size = 0x30;
			csc_block[screen_id][ch_id].dirty = 0;

			de_ccsc_set_reg_base(screen_id, ch_id, memory);
		}
	}
	return 0;
}

int de_ccsc_double_init(disp_bsp_init_para *para)
{
	uintptr_t base, base_ofst;
	int screen_id, ch_id, device_num;

	device_num = de_feat_get_num_screens();

	for (screen_id = 0; screen_id < device_num; screen_id++) {
		vi_num[screen_id] = de_feat_get_num_vi_chns(screen_id);
		chn_num[screen_id] = de_feat_get_num_chns(screen_id);

		for (ch_id = 0; ch_id < vi_num[screen_id]; ch_id++) {
			vep_support[screen_id][ch_id] =
			    de_feat_is_support_vep_by_chn(screen_id, ch_id);
			if (vep_support[screen_id][ch_id]) {
				/* fce csc */
				base = para->reg_base[DISP_MOD_DE] +
				       (screen_id + 1) * 0x00100000
				       + FCE_OFST + 0x40;

				de_icsc_set_reg_base(screen_id, ch_id,
						    (void *)base);

				/* fcc csc */
				base = para->reg_base[DISP_MOD_DE] +
				       (screen_id + 1) * 0x00100000
				       + FCC_OFST + 0x60;

				de_fcsc_set_reg_base(screen_id, ch_id,
						     (void *)base);
			}
		}

		for (ch_id = 0; ch_id < chn_num[screen_id]; ch_id++) {
			base_ofst = 0x0910 + ch_id * 0x30;
			base = para->reg_base[DISP_MOD_DE] + (screen_id + 1)
				* 0x00100000 + base_ofst;

			de_ccsc_set_reg_base(screen_id, ch_id, (void *)base);
		}
	}
	return 0;
}

int de_ccsc_exit(void)
{
	int screen_id, ch_id, device_num;

	device_num = de_feat_get_num_screens();

	for (screen_id = 0; screen_id < device_num; screen_id++) {
		vi_num[screen_id] = de_feat_get_num_vi_chns(screen_id);
		chn_num[screen_id] = de_feat_get_num_chns(screen_id);

		for (ch_id = 0; ch_id < vi_num[screen_id]; ch_id++) {
			vep_support[screen_id][ch_id] =
			    de_feat_is_support_vep_by_chn(screen_id, ch_id);
			if (vep_support[screen_id][ch_id]) {
				kfree(icsc_block[screen_id][ch_id].val);
				kfree(fcc_csc_block[screen_id][ch_id].val);
			}
		}

		for (ch_id = 0; ch_id < chn_num[screen_id]; ch_id++)
			kfree(csc_block[screen_id][ch_id].val);
	}
	return 0;
}

int de_ccsc_double_exit(void)
{
	return 0;
}

static unsigned int get_cscmod_idx(unsigned int cscmod)
{
	unsigned int idx;

	switch (cscmod) {
	case DE_BT601:
	case DE_BT470BG:
		idx = 0; break;
	case DE_BT709:
		idx = 1; break;
	case DE_BT2020NC:
	case DE_BT2020C:
		idx = 2; break;
	case DE_FCC:
		idx = 3; break;
	case DE_SMPTE240M:
		idx = 4; break;
	case DE_YCGCO:
		idx = 5; break;
	case DE_GBR:
		idx = 6; break;
	default:
		idx = 0; break;
	}
	return idx;
}

int de_csc_coeff_calc(unsigned int infmt, unsigned int incscmod,
		      unsigned int inrange, unsigned int outfmt,
		      unsigned int outcscmod, unsigned int outrange,
		      int *csc_coeff)
{
	unsigned int inidx; /* index for incscmod */
	unsigned int outidx; /* index for outcscmod */
	int *ptr_coeff;

	inidx = get_cscmod_idx(incscmod);
	outidx = get_cscmod_idx(outcscmod);

	if (infmt == DE_RGB) {
		/* only support inrange is DISP_COLOR_RANGE_0_255 */
		if (outfmt == DE_YUV)
			if (outrange == DISP_COLOR_RANGE_0_255)
				ptr_coeff = &r2y[7+outidx][0];
			else /* outrange == DISP_COLOR_RANGE_16_235 */
				ptr_coeff = &r2y[outidx][0];
		else /*outfmt == DE_RGB */
			if (outrange == DISP_COLOR_RANGE_0_255)
				ptr_coeff = &r2r[0][0];
			else /* outrange == DISP_COLOR_RANGE_16_235 */
				ptr_coeff = &r2r[1][0];
	} else { /* infmt == DE_YUV */
		if (outfmt == DE_YUV) {
			if (inrange == DISP_COLOR_RANGE_0_255)
				if (outrange == DISP_COLOR_RANGE_0_255)
					ptr_coeff = &y2yf2f[inidx*3+outidx][0];
				else /* outrange == DISP_COLOR_RANGE_16_235 */
					ptr_coeff = &y2yf2l[inidx*3+outidx][0];
			else /* inrange == DISP_COLOR_RANGE_16_235 */
				if (outrange == DISP_COLOR_RANGE_0_255)
					ptr_coeff = &y2yl2f[inidx*3+outidx][0];
				else /* outrange == DISP_COLOR_RANGE_16_235 */
					ptr_coeff = &y2yl2l[inidx*3+outidx][0];
		} else { /*outfmt == DE_RGB */
			if (inrange == DISP_COLOR_RANGE_0_255)
				if (outrange == DISP_COLOR_RANGE_0_255)
					ptr_coeff = &y2rf2f[inidx][0];
				else
					ptr_coeff = &y2rf2l[inidx][0];
			else
				if (outrange == DISP_COLOR_RANGE_0_255)
					ptr_coeff = &y2rl2f[inidx][0];
				else
					ptr_coeff = &y2rl2l[inidx][0];
		}
	}

	memcpy((void *)csc_coeff, (void *)ptr_coeff, 64);

	return 0;
}
