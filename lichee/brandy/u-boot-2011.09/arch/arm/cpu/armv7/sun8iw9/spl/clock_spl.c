/*
**********************************************************************************************************************
*
*						           the Embedded Secure Bootloader System
*
*
*						       Copyright(C), 2006-2014, Allwinnertech Co., Ltd.
*                                           All Rights Reserved
*
* File    :
*
* By      :
*
* Version : V2.00
*
* Date	  :
*
* Descript:
**********************************************************************************************************************
*/
#include "common.h"
#include "asm/io.h"
#include "asm/armv7.h"
#include "asm/arch/cpu.h"
#include "asm/arch/ccmu.h"
#include "asm/arch/timer.h"
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
static int clk_set_divd(void)
{
	unsigned int reg_val;
        

	//config axi
	reg_val = readl(CCM_CPU_L2_AXI_CTRL);
	reg_val &= ~(0x03 << 8);
	reg_val |=  (0x01 << 8);
	reg_val |=  (0x01 << 0);
	writel(reg_val, CCM_CPU_L2_AXI_CTRL);
	
	reg_val = readl(CCM_AHB1_APB1_CTRL);;
	reg_val &= ~((0x03 << 12) | (0x03 << 8) |(0x03 << 4));
	reg_val |=  (0x01 << 12);
	writel(reg_val, CCM_AHB1_APB1_CTRL);

        writel(readl(CCM_PLL6_MOD_CTRL) | (1U << 31), CCM_PLL6_MOD_CTRL);
	//config ahb
	reg_val = readl(CCM_AHB1_APB1_CTRL);;
	reg_val &= ~((0x03 << 12) | (0x03 << 8) |(0x03 << 4));
	reg_val |=  (0x02 << 12);
	reg_val |=  (2 << 6);
	reg_val |=  (1 << 8);

	writel(reg_val, CCM_AHB1_APB1_CTRL);

	return 0;
}
/*******************************************************************************
*函数名称: set_pll
*函数原型：void set_pll( void )
*函数功能: 调整CPU频率
*入口参数: void
*返 回 值: void
*备    注:
*******************************************************************************/
void set_pll( void )
{
    unsigned int reg_val;
    unsigned int i;
    //设置时钟为默认408M

    //切换到24M
    reg_val = readl(CCM_CPU_L2_AXI_CTRL);
    reg_val &= ~((0x1 << 12) | (0x1 <<28) );
    reg_val |=  ((0x00 << 12) | (0x00 << 28));
    reg_val |=  ((0x01 << 0) | (0x01 << 16));
    writel(reg_val, CCM_CPU_L2_AXI_CTRL);
    //延时，等待时钟稳定
    for(i=0; i<0x400; i++);
	//回写PLL1
    reg_val = (0x01<<12)|(0x01<<31);
    writel(reg_val, CCM_PLL1_C0CPUX_CTRL);
    //延时，等待时钟稳定
#ifndef CONFIG_FPGA
	do
	{
		reg_val = readl(CCM_PLL1_C0CPUX_CTRL);
	}
	while(!(reg_val & (0x1 << 28)));
#endif
    reg_val = (0x01<<12)|(0x01<<31);
    writel(reg_val, CCM_PLL2_C1CPUX_CTRL);
    //延时，等待时钟稳定
#ifndef CONFIG_FPGA
	do
	{
		reg_val = readl(CCM_PLL2_C1CPUX_CTRL);
	}
	while(!(reg_val & (0x1 << 28)));
#endif
    //修改AXI,AHB,APB分频
    clk_set_divd();
		//dma reset
	writel(readl(CCM_AHB1_RESET_CTRL)  | (1 << 6), CCM_AHB1_RESET_CTRL);
	for(i=0;i<100;i++);
	//gating clock for dma pass
	writel(readl(CCM_AHB1_GATE0_CTRL) | (1 << 6), CCM_AHB1_GATE0_CTRL);
	writel(7, (0x01c20000+0x20));
	//打开MBUS,clk src is pll6
	writel(0x80000000, CCM_MBUS_RESET_CTRL);       //Assert mbus domain
	__usdelay(100);
	writel(0x00000002, CCM_MBUS_SCLK_CTRL0);
	__usdelay(1);//设置MBUS的分频因子
	writel(0x01000002, CCM_MBUS_SCLK_CTRL0);
	__usdelay(1);//选择MBUS的源头
	writel(0x81000002, CCM_MBUS_SCLK_CTRL0); 
	__usdelay(1);//开MBUS时钟

    //切换时钟到COREPLL上
    reg_val = readl(CCM_CPU_L2_AXI_CTRL);
    reg_val &= ~((0x01 << 12) | (0x3 << 0));
    reg_val |=  ((0x01 << 12) | (0x01 << 0));
    writel(reg_val, CCM_CPU_L2_AXI_CTRL);
    reg_val = readl(CCM_CPU_L2_AXI_CTRL);
    reg_val &= ~((0x01 << 28) | (0x3 << 16));
    reg_val |=  ((0x01 << 28) | (0x01 << 16) | (0x01 << 24));
    writel(reg_val, CCM_CPU_L2_AXI_CTRL);
	__usdelay(1000);
	CP15DMB;
	CP15ISB;
        //CCI-400

        reg_val = readl(CCM_CCI400_CFG_REG);
        //if(!(reg_val & ( 0x3 << 24)))
       // {
        //        writel(0x0,CCM_CCI400_CFG_REG);
        //        __usdelay(50);
       // }
        reg_val &= ~(0x3 << 0);
        reg_val |= 0x02 << 0;
        writel(reg_val , CCM_CCI400_CFG_REG);
        __usdelay(10);
        reg_val &= ~(0x3 << 24);
        reg_val |= 0x01 << 24;
        writel(reg_val ,CCM_CCI400_CFG_REG);
	//打开GPIO
	writel(readl(CCM_APB1_GATE0_CTRL)		|	(1 << 5), CCM_APB1_GATE0_CTRL);
	writel(readl(SUNXI_RPRCM_BASE + 0x28)   | 		0x01, SUNXI_RPRCM_BASE + 0x28);
    return  ;
}
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
void reset_pll( void )
{
	writel(0x00000000, CCM_CPU_L2_AXI_CTRL);
	writel(0x00001000, CCM_PLL1_C0CPUX_CTRL);

	return ;
}
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
void set_gpio_gate(void)
{
	writel(readl(CCM_APB1_GATE0_CTRL)		|	(1 << 5), CCM_APB1_GATE0_CTRL);
	writel(readl(SUNXI_RPRCM_BASE + 0x28)	|		0x01, SUNXI_RPRCM_BASE + 0x28);
}
/*
************************************************************************************************************
*
*                                             function
*
*    函数名称：
*
*    参数列表：
*
*    返回值  ：
*
*    说明    ：
*
*
************************************************************************************************************
*/
void set_ccmu_normal(void)
{
	writel(7, CCM_SECURITY_REG);
}

