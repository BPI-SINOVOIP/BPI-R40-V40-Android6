/*
 * The driver of sunxi emce.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * zhouhuacai <zhouhuacai@allwinnertech.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/clk.h>
#include <linux/clk/sunxi.h>
#include "sunxi_emce.h"

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

static const struct of_device_id sunxi_emce_of_match[] = {
	{.compatible = "allwinner,sunxi-emce",},
	{},
};

MODULE_DEVICE_TABLE(of, sunxi_emce_of_match);
#endif

struct sunxi_emce_t *emce_dev;

void sunxi_emce_dump(void *addr, u32 size)
{
	int i, j;
	char *buf = (char *)addr;
	pr_debug("=========dump:size:0x%x=========\n", size);
	for (j = 0; j < size; j += 16) {
		for (i = 0; i < 16; i++)
			pr_debug("%02x ", buf[j + i] & 0xff);

		pr_debug("\n");
	}
	pr_debug("\n");

	return;
}
static DEFINE_MUTEX(emce_lock);

static void emce_dev_lock(void)
{
	mutex_lock(&emce_lock);
}

static void emce_dev_unlock(void)
{
	mutex_unlock(&emce_lock);
}

static int sunxi_emce_gen_salt(const u8 *mkey, u32 mklen, u8 *skey, u32 *sklen)
{
	int ret = 0;
	struct scatterlist sg = {0};
	struct crypto_hash *tfm = NULL;
	struct hash_desc desc = {0};
	EMCE_ENTER();

	tfm = crypto_alloc_hash("sha256", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		EMCE_ERR("Failed to alloc sha256 tfm. %p\n", tfm);
		goto out2;
	}

	desc.tfm = tfm;
	desc.flags = 0;

	sg_init_one(&sg, mkey, mklen);
	ret = crypto_hash_digest(&desc, &sg, mklen, skey);
	if (ret) {
		EMCE_ERR("Failed to do SHA256(), mklen: %d\n", mklen);
		goto out1;
	}
	*sklen = EMCE_SALT_KEY_LEN;

out1:
	crypto_free_hash(tfm);
out2:
	return ret;
}

static int sunxi_emce_set_cfg(enum cfg_cmd cmd, u32 para)
{
	int ret = 0;
	void __iomem *reg_addr = emce_dev->base_addr + EMCE_MODE;

	EMCE_ENTER();
	emce_dev_lock();
	switch (cmd) {
	case EMCE_SET_SECTOR_SIZE:
		sunxi_emce_set_bit(reg_addr, EMCE_SECTOR_SIZE_MASK, para);
		break;
	case EMCE_SET_CTR:
		sunxi_emce_set_bit(reg_addr, EMCE_CUR_CTR_MASK, para);
		break;
	case EMCE_SET_IV_LEN:
		sunxi_emce_set_bit(reg_addr, EMCE_IV_LEN_MASK, para);
		break;
	case EMCE_SET_KEY_LEN:
		sunxi_emce_set_bit(reg_addr, EMCE_KEY_LEN_MASK, para);
		break;
	case EMCE_SET_MODE:
		sunxi_emce_set_bit(reg_addr, EMCE_MODE_MASK, para);
		break;
	default:
		EMCE_ERR("Unsupport cmd %d\n", cmd);
		break;
	}
	emce_dev_unlock();

	return ret;
}

void sunxi_emce_set_sector_size(u32 para)
{
	sunxi_emce_set_cfg(EMCE_SET_SECTOR_SIZE, para);
}
EXPORT_SYMBOL_GPL(sunxi_emce_set_sector_size);

void sunxi_emce_set_cur_controller(u32 para)
{
	sunxi_emce_set_cfg(EMCE_SET_CTR, para);
}
EXPORT_SYMBOL_GPL(sunxi_emce_set_cur_controller);

static void sunxi_emce_set_masterkey(const u8 *mkey, u32 klen)
{
	int i = 0;
	void __iomem *reg_addr = emce_dev->base_addr + EMCE_MASTER_KEY_OFFSET;

	if ((NULL == mkey) || (klen <= 0))
		EMCE_ERR("sunxi emce masterkey error\n ");

	for (i = 0; i < klen; i = i + 4)
		writel(*(u32 *)(mkey + i) , (reg_addr + i));
}

static void sunxi_emce_set_saltkey(const u8 *skey, u32 klen)
{
	int i = 0;
	void __iomem *reg_addr = emce_dev->base_addr + EMCE_SALT_KEY_OFFSET;

	if ((NULL == skey) || (klen <= 0))
		EMCE_ERR("sunxi emce saltkey error\n ");

	for (i = 0; i < klen; i = i + 4)
		writel((*(u32 *)(skey + i)), reg_addr + i);
}

int sunxi_emce_set_key(const u8 *mkey, u32 len)
{
	u8 saltkey[EMCE_SALT_KEY_LEN];
	u32 skey_len = 0;
	int ret = 0;

	EMCE_ENTER();
	if ((NULL == mkey) || (len <= 0))
		EMCE_ERR("sunxi emce key error\n ");

	ret = sunxi_emce_gen_salt(mkey, len, saltkey, &skey_len);
	if (!ret) {
		sunxi_emce_set_saltkey(saltkey, skey_len);
		sunxi_emce_set_masterkey(mkey, len);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sunxi_emce_set_key);

static int sunxi_emce_get_version(void)
{
	int ver = 0;
	ver = readl(emce_dev->base_addr + EMCE_VER);
	pr_info("[EMCE]:VER:0x%x\n", ver);
	return ver;
}

static int sunxi_emce_hw_init(struct sunxi_emce_t *emce)
{
	struct device_node *pnode = emce->pdev->dev.of_node;

	emce->mclk = of_clk_get(pnode, 0);
	if (IS_ERR_OR_NULL(emce->mclk)) {
		EMCE_ERR("get mclk fail: ret = %x\n", PTR_RET(emce->mclk));
		return PTR_RET(emce->mclk);
	}

	EMCE_DBG("EMCE mclk %luMHz\n", clk_get_rate(emce->mclk) / 1000000);

	if (clk_prepare_enable(emce->mclk)) {
		EMCE_ERR("Couldn't enable module clock\n");
		return -EBUSY;
	}

	return 0;
}

static int sunxi_emce_hw_exit(struct sunxi_emce_t *emce)
{
	EMCE_EXIT();
	clk_disable_unprepare(emce->mclk);
	clk_put(emce->mclk);
	emce->mclk = NULL;
	return 0;
}

static int sunxi_emce_res_request(struct platform_device *pdev)
{
	struct device_node *np = NULL;
	struct sunxi_emce_t *emce = platform_get_drvdata(pdev);

	np = pdev->dev.of_node;
	if (!of_device_is_available(np)) {
		EMCE_ERR("sunxi emce is disable\n");
		return -EPERM;
	}
	emce->base_addr = of_iomap(np, 0);
	if (emce->base_addr == 0) {
		EMCE_ERR("Failed to ioremap() io memory region.\n");
		return -EBUSY;
	} else
		EMCE_DBG("sunxi emce reg base: %p !\n", emce->base_addr);

	return 0;
}
static int sunxi_emce_res_release(struct sunxi_emce_t *emce)
{
	iounmap(emce->base_addr);
	return 0;
}

static int sunxi_emce_probe(struct platform_device *pdev)
{
	u32 ret = 0;
	struct sunxi_emce_t *emce = NULL;

	emce = devm_kzalloc(&pdev->dev, sizeof(emce), GFP_KERNEL);
	if (emce == NULL) {
		EMCE_ERR("Unable to allocate emce_data\n");
		return -ENOMEM;
	}
	snprintf(emce->dev_name, sizeof(emce->dev_name), SUNXI_EMCE_DEV_NAME);
	platform_set_drvdata(pdev, emce);

	ret = sunxi_emce_res_request(pdev);
	if (ret != 0)
		goto err0;

	emce->pdev = pdev;

	ret = sunxi_emce_hw_init(emce);
	if (ret != 0) {
		EMCE_ERR("emce hw init failed!\n");
		goto err1;
	}
	emce_dev = emce;

	ret = sunxi_emce_get_version();
	if (!ret) {
		EMCE_ERR("sunxi emce version error\n");
		ret = -ENXIO;
		goto err1;
	}

	return 0;

err1:
	sunxi_emce_res_release(emce);
err0:
	platform_set_drvdata(pdev, NULL);
	return ret;

}

static int sunxi_emce_remove(struct platform_device *pdev)
{
	struct sunxi_emce_t *emce = platform_get_drvdata(pdev);

	sunxi_emce_hw_exit(emce);
	sunxi_emce_res_release(emce);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int sunxi_emce_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_emce_t *emce = platform_get_drvdata(pdev);

	EMCE_ENTER();
	sunxi_emce_hw_exit(emce);

	return 0;
}

static int sunxi_emce_resume(struct device *dev)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_emce_t *emce = platform_get_drvdata(pdev);

	EMCE_ENTER();
	ret = sunxi_emce_hw_init(emce);

	return ret;
}

static const struct dev_pm_ops sunxi_emce_dev_pm_ops = {
	.suspend = sunxi_emce_suspend,
	.resume  = sunxi_emce_resume,
};

#define SUNXI_EMCE_DEV_PM_OPS (&sunxi_emce_dev_pm_ops)
#else
#define SUNXI_EMCE_DEV_PM_OPS NULL
#endif /* CONFIG_PM */

static struct platform_driver sunxi_emce_driver = {
	.probe   = sunxi_emce_probe,
	.remove  = sunxi_emce_remove,
	.driver = {
		.name	= SUNXI_EMCE_DEV_NAME,
		.owner	= THIS_MODULE,
		.pm		= SUNXI_EMCE_DEV_PM_OPS,
		.of_match_table = sunxi_emce_of_match,
	},
};

static int __init sunxi_emce_init(void)
{
	int ret = 0;
	EMCE_DBG("Sunxi EMCE init ...\n");
	ret = platform_driver_register(&sunxi_emce_driver);
	if (ret < 0)
		EMCE_ERR("platform_driver_register() failed, return %d\n", ret);

	return ret;
}

static void __exit sunxi_emce_exit(void)
{
	platform_driver_unregister(&sunxi_emce_driver);
}

module_init(sunxi_emce_init);
module_exit(sunxi_emce_exit);

MODULE_AUTHOR("zhouhuacai");
MODULE_DESCRIPTION("SUNXI EMCE Driver");
MODULE_ALIAS("platform:"SUNXI_EMCE_DEV_NAME);
MODULE_LICENSE("GPL");

