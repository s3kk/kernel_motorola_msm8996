/*
 * arch/arm/mach-ep93xx/edb9302.c
 * Cirrus Logic EDB9302 support.
 *
 * Copyright (C) 2006 George Kashperko <george@chas.com.ua>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

static struct physmap_flash_data edb9302_flash_data = {
	.width		= 2,
};

static struct resource edb9302_flash_resource = {
	.start		= EP93XX_CS6_PHYS_BASE,
	.end		= EP93XX_CS6_PHYS_BASE + SZ_16M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device edb9302_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &edb9302_flash_data,
	},
	.num_resources	= 1,
	.resource	= &edb9302_flash_resource,
};

static struct ep93xx_eth_data edb9302_eth_data = {
	.phy_id		= 1,
};

static void __init edb9302_init_machine(void)
{
	ep93xx_init_devices();
	platform_device_register(&edb9302_flash);

	ep93xx_register_eth(&edb9302_eth_data, 1);
}

MACHINE_START(EDB9302, "Cirrus Logic EDB9302 Evaluation Board")
	/* Maintainer: George Kashperko <george@chas.com.ua> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_SYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= edb9302_init_machine,
MACHINE_END
