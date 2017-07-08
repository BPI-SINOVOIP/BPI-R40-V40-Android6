/*
 ******************************************************************************
 *
 * top_reg.c
 *
 * CSIC - top_reg.c module
 *
 * Copyright (c) 2016 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Version		  Author         Date		    	Description
 *
 *   1.0		  Zhao Wei   	2016/06/13	      First Version
 *
 ******************************************************************************
 */

#include <linux/kernel.h>
#include "top_reg_i.h"
#include "top_reg.h"

#include "utility/vin_io.h"

volatile void __iomem *csic_top_base[MAX_CSIC_TOP_NUM];

int csic_top_set_base_addr(unsigned int sel, unsigned long addr)
{
	if (sel > MAX_CSIC_TOP_NUM - 1)
		return -1;
	csic_top_base[sel] = (volatile void __iomem *)addr;

	return 0;
}

void csic_top_enable(unsigned int sel)
{
	vin_reg_clr_set(csic_top_base[sel] + CSIC_TOP_EN_REG_OFF,
			CSIC_TOP_EN_MASK, 1 << CSIC_TOP_EN);
}

void csic_top_disable(unsigned int sel)
{
	vin_reg_clr_set(csic_top_base[sel] + CSIC_TOP_EN_REG_OFF,
			CSIC_TOP_EN_MASK, 0 << CSIC_TOP_EN);
}

void csic_top_wdr_mode(unsigned int sel, unsigned int en)
{
	vin_reg_clr_set(csic_top_base[sel] + CSIC_TOP_EN_REG_OFF,
			CSIC_WDR_MODE_EN_MASK, en << CSIC_WDR_MODE_EN);
}

void csic_top_sram_pwdn(unsigned int sel, unsigned int en)
{
	vin_reg_clr_set(csic_top_base[sel] + CSIC_TOP_EN_REG_OFF,
			CSIC_SRAM_PWDN_MASK, en << CSIC_SRAM_PWDN);
}

void csic_top_switch_ncsi(unsigned int sel, unsigned int en)
{
	vin_reg_clr_set(csic_top_base[sel] + CSIC_TOP_EN_REG_OFF,
			CSIC_NCSI_TOP_SWITCH_MASK, en << CSIC_NCSI_TOP_SWITCH);
}

void csic_top_version_read_en(unsigned int sel, unsigned int en)
{
	vin_reg_clr_set(csic_top_base[sel] + CSIC_TOP_EN_REG_OFF,
			CSIC_VER_EN_MASK, en << CSIC_VER_EN);
}

void csic_isp0_input0_select(unsigned int sel, enum isp0_input0 isp0_in0)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_ISP0_IN0_REG_OFF, isp0_in0);
}

void csic_isp0_input1_select(unsigned int sel, enum isp0_input1 isp0_in1)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_ISP0_IN1_REG_OFF, isp0_in1);
}

void csic_isp0_input2_select(unsigned int sel, enum isp0_input2 isp0_in2)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_ISP0_IN2_REG_OFF, isp0_in2);
}

void csic_isp0_input3_select(unsigned int sel, enum isp0_input3 isp0_in3)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_ISP0_IN3_REG_OFF, isp0_in3);
}

void csic_isp1_input0_select(unsigned int sel, enum isp1_input0 isp1_in0)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_ISP1_IN0_REG_OFF, isp1_in0);
}

void csic_isp1_input1_select(unsigned int sel, enum isp1_input1 isp1_in1)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_ISP1_IN1_REG_OFF, isp1_in1);
}

void csic_isp1_input2_select(unsigned int sel, enum isp1_input2 isp1_in2)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_ISP1_IN2_REG_OFF, isp1_in2);
}

void csic_isp1_input3_select(unsigned int sel, enum isp1_input3 isp1_in3)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_ISP1_IN3_REG_OFF, isp1_in3);
}

void csic_vipp0_input_select(unsigned int sel, enum vipp0_input vipp0_in)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_VIPP0_IN_REG_OFF, vipp0_in);
}

void csic_vipp1_input_select(unsigned int sel, enum vipp1_input vipp1_in)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_VIPP1_IN_REG_OFF, vipp1_in);
}

void csic_vipp2_input_select(unsigned int sel, enum vipp2_input vipp2_in)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_VIPP2_IN_REG_OFF, vipp2_in);
}

void csic_vipp3_input_select(unsigned int sel, enum vipp3_input vipp3_in)
{
	vin_reg_writel(csic_top_base[sel] + CSIC_VIPP3_IN_REG_OFF, vipp3_in);
}

void csic_isp_input_select(unsigned int sel, unsigned int isp, unsigned int in,
				unsigned int psr, unsigned int ch)
{
	if (0 == isp) {
		switch (in) {
		case 0:
			if ((psr == 0) && (ch == 0))
				csic_isp0_input0_select(sel, ISP_INPUT(0, 0, 0, 0));
			else if ((psr == 1) && (ch == 0))
				csic_isp0_input0_select(sel, ISP_INPUT(0, 0, 1, 0));
			else if ((psr == 1) && (ch == 0))
				csic_isp0_input0_select(sel, ISP_INPUT(0, 0, 1, 0));
			break;
		case 1:
			if ((psr == 0) && (ch == 1))
				csic_isp0_input1_select(sel, ISP_INPUT(0, 1, 0, 1));
			else if ((psr == 1) && (ch == 1))
				csic_isp0_input1_select(sel, ISP_INPUT(0, 1, 1, 1));
			break;

		case 2:
			if ((psr == 0) && (ch == 2))
				csic_isp0_input2_select(sel, ISP_INPUT(0, 2, 0, 2));
			else if ((psr == 1) && (ch == 2))
				csic_isp0_input2_select(sel, ISP_INPUT(0, 2, 1, 2));
			break;
		case 3:
			if ((psr == 0) && (ch == 3))
				csic_isp0_input3_select(sel, ISP_INPUT(0, 3, 0, 3));
			else if ((psr == 1) && (ch == 3))
				csic_isp0_input3_select(sel, ISP_INPUT(0, 3, 1, 3));
			break;
		default:
			break;
		}
	} else if (1 == isp) {
		switch (in) {
		case 0:
			if ((psr == 1) && (ch == 0))
				csic_isp1_input0_select(sel, ISP_INPUT(1, 0, 1, 0));
			else if ((psr == 0) && (ch == 0))
				csic_isp1_input0_select(sel, ISP_INPUT(1, 0, 0, 0));
			else if ((psr == 0) && (ch == 1))
				csic_isp1_input0_select(sel, ISP_INPUT(1, 0, 0, 1));
			break;
		case 1:
			if ((psr == 0) && (ch == 1))
				csic_isp1_input1_select(sel, ISP_INPUT(1, 1, 0, 1));
			else if ((psr == 1) && (ch == 1))
				csic_isp1_input1_select(sel, ISP_INPUT(1, 1, 1, 1));
			break;

		case 2:
			if ((psr == 0) && (ch == 2))
				csic_isp1_input2_select(sel, ISP_INPUT(1, 2, 0, 2));
			else if ((psr == 1) && (ch == 2))
				csic_isp1_input2_select(sel, ISP_INPUT(1, 2, 1, 2));
			break;
		case 3:
			if ((psr == 0) && (ch == 3))
				csic_isp1_input3_select(sel, ISP_INPUT(1, 3, 0, 3));
			else if ((psr == 1) && (ch == 3))
				csic_isp1_input3_select(sel, ISP_INPUT(1, 3, 1, 3));
			break;
		default:
			break;
		}
	}
}

void csic_vipp_input_select(unsigned int sel, unsigned int vipp,
				unsigned int isp, unsigned int ch)
{
	switch (vipp) {
	case 0:
		if ((isp == 0) && (ch == 0))
			csic_vipp0_input_select(sel, VIPP_INPUT(0, 0, 0));
		else if ((isp == 1) && (ch == 0))
			csic_vipp0_input_select(sel, VIPP_INPUT(0, 1, 0));
		break;
	case 1:
		if ((isp == 0) && (ch == 0))
			csic_vipp1_input_select(sel, VIPP_INPUT(1, 0, 0));
		else if ((isp == 1) && (ch == 0))
			csic_vipp1_input_select(sel, VIPP_INPUT(1, 1, 0));
		else if ((isp == 0) && (ch == 1))
			csic_vipp1_input_select(sel, VIPP_INPUT(1, 0, 1));
		else if ((isp == 1) && (ch == 1))
			csic_vipp1_input_select(sel, VIPP_INPUT(1, 1, 1));
		break;
	case 2:
		if ((isp == 0) && (ch == 0))
			csic_vipp2_input_select(sel, VIPP_INPUT(2, 0, 0));
		else if ((isp == 1) && (ch == 0))
			csic_vipp2_input_select(sel, VIPP_INPUT(2, 1, 0));
		else if ((isp == 0) && (ch == 2))
			csic_vipp2_input_select(sel, VIPP_INPUT(2, 0, 2));
		else if ((isp == 1) && (ch == 2))
			csic_vipp2_input_select(sel, VIPP_INPUT(2, 1, 2));
		break;
	case 3:
		if ((isp == 0) && (ch == 0))
			csic_vipp3_input_select(sel, VIPP_INPUT(3, 0, 0));
		else if ((isp == 1) && (ch == 0))
			csic_vipp3_input_select(sel, VIPP_INPUT(3, 1, 0));
		else if ((isp == 0) && (ch == 3))
			csic_vipp3_input_select(sel, VIPP_INPUT(3, 0, 3));
		else if ((isp == 1) && (ch == 3))
			csic_vipp3_input_select(sel, VIPP_INPUT(3, 1, 3));
		else if ((isp == 1) && (ch == 1))
			csic_vipp3_input_select(sel, VIPP_INPUT(3, 1, 1));
		break;
	default:
		break;
	}
}

void csic_feature_list_get(unsigned int sel, struct csic_feature_list *fl)
{
	unsigned int reg_val = 0;
	reg_val = vin_reg_readl(csic_top_base[sel] + CSIC_FEATURE_REG_OFF);
	fl->dma_num = (reg_val & CSIC_DMA_NUM_MASK) >> CSIC_DMA_NUM;
	fl->vipp_num = (reg_val & CSIC_VIPP_NUM_MASK) >> CSIC_VIPP_NUM;
	fl->isp_num = (reg_val & CSIC_ISP_NUM_MASK) >> CSIC_ISP_NUM;
	fl->ncsi_num = (reg_val & CSIC_NCSI_NUM_MASK) >> CSIC_NCSI_NUM;
	fl->mcsi_num = (reg_val & CSIC_MCSI_NUM_MASK) >> CSIC_MCSI_NUM;
	fl->parser_num = (reg_val & CSIC_PARSER_NUM_MASK) >> CSIC_PARSER_NUM;
}

void csic_version_get(unsigned int sel, struct csic_version *v)
{
	unsigned int reg_val = 0;
	reg_val = vin_reg_readl(csic_top_base[sel] + CSIC_VER_REG_OFF);
	v->ver_small = (reg_val & CSIC_VER_SMALL_MASK) >> CSIC_VER_SMALL;
	v->ver_big = (reg_val & CSIC_VER_BIG_MASK) >> CSIC_VER_BIG;
}

