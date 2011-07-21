/*
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/gpio.h>

struct pch_regs {
	u32	ien;
	u32	istatus;
	u32	idisp;
	u32	iclr;
	u32	imask;
	u32	imaskclr;
	u32	po;
	u32	pi;
	u32	pm;
	u32	im0;
	u32	im1;
	u32	reserved[3];
	u32	gpio_use_sel;
	u32	reset;
};

enum pch_type_t {
	INTEL_EG20T_PCH,
	OKISEMI_ML7223m_IOH, /* OKISEMI ML7223 IOH PCIe Bus-m */
	OKISEMI_ML7223n_IOH  /* OKISEMI ML7223 IOH PCIe Bus-n */
};

/* Specifies number of GPIO PINS */
static int gpio_pins[] = {
	[INTEL_EG20T_PCH] = 12,
	[OKISEMI_ML7223m_IOH] = 8,
	[OKISEMI_ML7223n_IOH] = 8,
};

/**
 * struct pch_gpio_reg_data - The register store data.
 * @po_reg:	To store contents of PO register.
 * @pm_reg:	To store contents of PM register.
 * @im0_reg:	To store contents of IM0 register.
 * @im1_reg:	To store contents of IM1 register.
 * @gpio_use_sel_reg : To store contents of GPIO_USE_SEL register.
 *		       (Only ML7223 Bus-n)
 */
struct pch_gpio_reg_data {
	u32 po_reg;
	u32 pm_reg;
	u32 im0_reg;
	u32 im1_reg;
	u32 gpio_use_sel_reg;
};

/**
 * struct pch_gpio - GPIO private data structure.
 * @base:			PCI base address of Memory mapped I/O register.
 * @reg:			Memory mapped PCH GPIO register list.
 * @dev:			Pointer to device structure.
 * @gpio:			Data for GPIO infrastructure.
 * @pch_gpio_reg:		Memory mapped Register data is saved here
 *				when suspend.
 * @ioh:		IOH ID
 * @spinlock:		Used for register access protection in
 *				interrupt context pch_irq_mask,
 *				pch_irq_unmask and pch_irq_type;
 */
struct pch_gpio {
	void __iomem *base;
	struct pch_regs __iomem *reg;
	struct device *dev;
	struct gpio_chip gpio;
	struct pch_gpio_reg_data pch_gpio_reg;
	struct mutex lock;
	enum pch_type_t ioh;
	spinlock_t spinlock;
};

static void pch_gpio_set(struct gpio_chip *gpio, unsigned nr, int val)
{
	u32 reg_val;
	struct pch_gpio *chip =	container_of(gpio, struct pch_gpio, gpio);

	mutex_lock(&chip->lock);
	reg_val = ioread32(&chip->reg->po);
	if (val)
		reg_val |= (1 << nr);
	else
		reg_val &= ~(1 << nr);

	iowrite32(reg_val, &chip->reg->po);
	mutex_unlock(&chip->lock);
}

static int pch_gpio_get(struct gpio_chip *gpio, unsigned nr)
{
	struct pch_gpio *chip =	container_of(gpio, struct pch_gpio, gpio);

	return ioread32(&chip->reg->pi) & (1 << nr);
}

static int pch_gpio_direction_output(struct gpio_chip *gpio, unsigned nr,
				     int val)
{
	struct pch_gpio *chip =	container_of(gpio, struct pch_gpio, gpio);
	u32 pm;
	u32 reg_val;

	mutex_lock(&chip->lock);
	pm = ioread32(&chip->reg->pm) & ((1 << gpio_pins[chip->ioh]) - 1);
	pm |= (1 << nr);
	iowrite32(pm, &chip->reg->pm);

	reg_val = ioread32(&chip->reg->po);
	if (val)
		reg_val |= (1 << nr);
	else
		reg_val &= ~(1 << nr);
	iowrite32(reg_val, &chip->reg->po);

	mutex_unlock(&chip->lock);

	return 0;
}

static int pch_gpio_direction_input(struct gpio_chip *gpio, unsigned nr)
{
	struct pch_gpio *chip =	container_of(gpio, struct pch_gpio, gpio);
	u32 pm;

	mutex_lock(&chip->lock);
	pm = ioread32(&chip->reg->pm) & ((1 << gpio_pins[chip->ioh]) - 1);
	pm &= ~(1 << nr);
	iowrite32(pm, &chip->reg->pm);
	mutex_unlock(&chip->lock);

	return 0;
}

/*
 * Save register configuration and disable interrupts.
 */
static void pch_gpio_save_reg_conf(struct pch_gpio *chip)
{
	chip->pch_gpio_reg.po_reg = ioread32(&chip->reg->po);
	chip->pch_gpio_reg.pm_reg = ioread32(&chip->reg->pm);
	chip->pch_gpio_reg.im0_reg = ioread32(&chip->reg->im0);
	if (chip->ioh == INTEL_EG20T_PCH)
		chip->pch_gpio_reg.im1_reg = ioread32(&chip->reg->im1);
	if (chip->ioh == OKISEMI_ML7223n_IOH)
		chip->pch_gpio_reg.gpio_use_sel_reg =\
					    ioread32(&chip->reg->gpio_use_sel);
}

/*
 * This function restores the register configuration of the GPIO device.
 */
static void pch_gpio_restore_reg_conf(struct pch_gpio *chip)
{
	/* to store contents of PO register */
	iowrite32(chip->pch_gpio_reg.po_reg, &chip->reg->po);
	/* to store contents of PM register */
	iowrite32(chip->pch_gpio_reg.pm_reg, &chip->reg->pm);
	iowrite32(chip->pch_gpio_reg.im0_reg, &chip->reg->im0);
	if (chip->ioh == INTEL_EG20T_PCH)
		iowrite32(chip->pch_gpio_reg.im1_reg, &chip->reg->im1);
	if (chip->ioh == OKISEMI_ML7223n_IOH)
		iowrite32(chip->pch_gpio_reg.gpio_use_sel_reg,
			  &chip->reg->gpio_use_sel);
}

static void pch_gpio_setup(struct pch_gpio *chip)
{
	struct gpio_chip *gpio = &chip->gpio;

	gpio->label = dev_name(chip->dev);
	gpio->owner = THIS_MODULE;
	gpio->direction_input = pch_gpio_direction_input;
	gpio->get = pch_gpio_get;
	gpio->direction_output = pch_gpio_direction_output;
	gpio->set = pch_gpio_set;
	gpio->dbg_show = NULL;
	gpio->base = -1;
	gpio->ngpio = gpio_pins[chip->ioh];
	gpio->can_sleep = 0;
}

static int __devinit pch_gpio_probe(struct pci_dev *pdev,
				    const struct pci_device_id *id)
{
	s32 ret;
	struct pch_gpio *chip;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s : pci_enable_device FAILED", __func__);
		goto err_pci_enable;
	}

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_regions FAILED-%d", ret);
		goto err_request_regions;
	}

	chip->base = pci_iomap(pdev, 1, 0);
	if (chip->base == 0) {
		dev_err(&pdev->dev, "%s : pci_iomap FAILED", __func__);
		ret = -ENOMEM;
		goto err_iomap;
	}

	if (pdev->device == 0x8803)
		chip->ioh = INTEL_EG20T_PCH;
	else if (pdev->device == 0x8014)
		chip->ioh = OKISEMI_ML7223m_IOH;
	else if (pdev->device == 0x8043)
		chip->ioh = OKISEMI_ML7223n_IOH;

	chip->reg = chip->base;
	pci_set_drvdata(pdev, chip);
	mutex_init(&chip->lock);
	pch_gpio_setup(chip);
	ret = gpiochip_add(&chip->gpio);
	if (ret) {
		dev_err(&pdev->dev, "PCH gpio: Failed to register GPIO\n");
		goto err_gpiochip_add;
	}

	return 0;

err_gpiochip_add:
	pci_iounmap(pdev, chip->base);

err_iomap:
	pci_release_regions(pdev);

err_request_regions:
	pci_disable_device(pdev);

err_pci_enable:
	kfree(chip);
	dev_err(&pdev->dev, "%s Failed returns %d\n", __func__, ret);
	return ret;
}

static void __devexit pch_gpio_remove(struct pci_dev *pdev)
{
	int err;
	struct pch_gpio *chip = pci_get_drvdata(pdev);

	err = gpiochip_remove(&chip->gpio);
	if (err)
		dev_err(&pdev->dev, "Failed gpiochip_remove\n");

	pci_iounmap(pdev, chip->base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(chip);
}

#ifdef CONFIG_PM
static int pch_gpio_suspend(struct pci_dev *pdev, pm_message_t state)
{
	s32 ret;
	struct pch_gpio *chip = pci_get_drvdata(pdev);
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	pch_gpio_save_reg_conf(chip);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	ret = pci_save_state(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_save_state Failed-%d\n", ret);
		return ret;
	}
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D0);
	ret = pci_enable_wake(pdev, PCI_D0, 1);
	if (ret)
		dev_err(&pdev->dev, "pci_enable_wake Failed -%d\n", ret);

	return 0;
}

static int pch_gpio_resume(struct pci_dev *pdev)
{
	s32 ret;
	struct pch_gpio *chip = pci_get_drvdata(pdev);
	unsigned long flags;

	ret = pci_enable_wake(pdev, PCI_D0, 0);

	pci_set_power_state(pdev, PCI_D0);
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device Failed-%d ", ret);
		return ret;
	}
	pci_restore_state(pdev);

	spin_lock_irqsave(&chip->spinlock, flags);
	iowrite32(0x01, &chip->reg->reset);
	iowrite32(0x00, &chip->reg->reset);
	pch_gpio_restore_reg_conf(chip);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}
#else
#define pch_gpio_suspend NULL
#define pch_gpio_resume NULL
#endif

#define PCI_VENDOR_ID_ROHM             0x10DB
static DEFINE_PCI_DEVICE_TABLE(pch_gpio_pcidev_id) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x8803) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ROHM, 0x8014) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ROHM, 0x8043) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pch_gpio_pcidev_id);

static struct pci_driver pch_gpio_driver = {
	.name = "pch_gpio",
	.id_table = pch_gpio_pcidev_id,
	.probe = pch_gpio_probe,
	.remove = __devexit_p(pch_gpio_remove),
	.suspend = pch_gpio_suspend,
	.resume = pch_gpio_resume
};

static int __init pch_gpio_pci_init(void)
{
	return pci_register_driver(&pch_gpio_driver);
}
module_init(pch_gpio_pci_init);

static void __exit pch_gpio_pci_exit(void)
{
	pci_unregister_driver(&pch_gpio_driver);
}
module_exit(pch_gpio_pci_exit);

MODULE_DESCRIPTION("PCH GPIO PCI Driver");
MODULE_LICENSE("GPL");
