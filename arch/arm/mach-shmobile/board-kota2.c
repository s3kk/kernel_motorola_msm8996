/*
 * kota2 board support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 * Copyright (C) 2010  Takashi Yoshii <yoshii.takashi.zj@renesas.com>
 * Copyright (C) 2009  Yoshihiro Shimoda <shimoda.yoshihiro@renesas.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/smsc911x.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/input/sh_keysc.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <mach/hardware.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/traps.h>

static struct resource smsc9220_resources[] = {
	[0] = {
		.start		= 0x14000000, /* CS5A */
		.end		= 0x140000ff, /* A1->A7 */
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= gic_spi(33), /* PINTA2 @ PORT144 */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc9220_platdata = {
	.flags		= SMSC911X_USE_32BIT, /* 32-bit SW on 16-bit HW bus */
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device eth_device = {
	.name		= "smsc911x",
	.id		= 0,
	.dev  = {
		.platform_data = &smsc9220_platdata,
	},
	.resource	= smsc9220_resources,
	.num_resources	= ARRAY_SIZE(smsc9220_resources),
};

static struct sh_keysc_info keysc_platdata = {
	.mode		= SH_KEYSC_MODE_6,
	.scan_timing	= 3,
	.delay		= 100,
	.keycodes	= {
		KEY_NUMERIC_STAR, KEY_NUMERIC_0, KEY_NUMERIC_POUND,
		0, 0, 0, 0, 0,
		KEY_NUMERIC_7, KEY_NUMERIC_8, KEY_NUMERIC_9,
		0, KEY_DOWN, 0, 0, 0,
		KEY_NUMERIC_4, KEY_NUMERIC_5, KEY_NUMERIC_6,
		KEY_LEFT, KEY_ENTER, KEY_RIGHT, 0, 0,
		KEY_NUMERIC_1, KEY_NUMERIC_2, KEY_NUMERIC_3,
		0, KEY_UP, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
	},
};

static struct resource keysc_resources[] = {
	[0] = {
		.name	= "KEYSC",
		.start	= 0xe61b0000,
		.end	= 0xe61b0098 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(71),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device keysc_device = {
	.name		= "sh_keysc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(keysc_resources),
	.resource	= keysc_resources,
	.dev		= {
		.platform_data	= &keysc_platdata,
	},
};

#define GPIO_KEY(c, g, d) { .code = c, .gpio = g, .desc = d, .active_low = 1 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_VOLUMEUP, GPIO_PORT56, "+"), /* S2: VOL+ [IRQ9] */
	GPIO_KEY(KEY_VOLUMEDOWN, GPIO_PORT54, "-"), /* S3: VOL- [IRQ10] */
	GPIO_KEY(KEY_MENU, GPIO_PORT27, "Menu"), /* S4: MENU [IRQ30] */
	GPIO_KEY(KEY_HOMEPAGE, GPIO_PORT26, "Home"), /* S5: HOME [IRQ31] */
	GPIO_KEY(KEY_BACK, GPIO_PORT11, "Back"), /* S6: BACK [IRQ0] */
	GPIO_KEY(KEY_PHONE, GPIO_PORT238, "Tel"), /* S7: TEL [IRQ11] */
	GPIO_KEY(KEY_POWER, GPIO_PORT239, "C1"), /* S8: CAM [IRQ13] */
	GPIO_KEY(KEY_MAIL, GPIO_PORT224, "Mail"), /* S9: MAIL [IRQ3] */
	/* Omitted button "C3?": GPIO_PORT223 - S10: CUST [IRQ8] */
	GPIO_KEY(KEY_CAMERA, GPIO_PORT164, "C2"), /* S11: CAM_HALF [IRQ25] */
	/* Omitted button "?": GPIO_PORT152 - S12: CAM_FULL [No IRQ] */
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons        = gpio_buttons,
	.nbuttons       = ARRAY_SIZE(gpio_buttons),
	.poll_interval  = 250, /* polled for now */
};

static struct platform_device gpio_keys_device = {
	.name   = "gpio-keys-polled", /* polled for now */
	.id     = -1,
	.dev    = {
		.platform_data  = &gpio_key_info,
	},
};

#define GPIO_LED(n, g) { .name = n, .gpio = g }

static struct gpio_led gpio_leds[] = {
	GPIO_LED("V2513", GPIO_PORT153), /* PORT153 [TPU1T02] -> V2513 */
	GPIO_LED("V2514", GPIO_PORT199), /* PORT199 [TPU4TO1] -> V2514 */
	GPIO_LED("V2515", GPIO_PORT197), /* PORT197 [TPU2TO1] -> V2515 */
	GPIO_LED("KEYLED", GPIO_PORT163), /* PORT163 [TPU3TO0] -> KEYLED */
	GPIO_LED("G", GPIO_PORT20), /* PORT20 [GPO0] -> LED7 -> "G" */
	GPIO_LED("H", GPIO_PORT21), /* PORT21 [GPO1] -> LED8 -> "H" */
	GPIO_LED("J", GPIO_PORT22), /* PORT22 [GPO2] -> LED9 -> "J" */
};

static struct gpio_led_platform_data gpio_leds_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device gpio_leds_device = {
	.name   = "leds-gpio",
	.id     = -1,
	.dev    = {
		.platform_data  = &gpio_leds_info,
	},
};

static struct resource mmcif_resources[] = {
	[0] = {
		.name   = "MMCIF",
		.start  = 0xe6bd0000,
		.end    = 0xe6bd00ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(140),
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		.start  = gic_spi(141),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct sh_mmcif_plat_data mmcif_info = {
	.ocr            = MMC_VDD_165_195,
	.caps           = MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
};

static struct platform_device mmcif_device = {
	.name           = "sh_mmcif",
	.id             = 0,
	.dev            = {
		.platform_data          = &mmcif_info,
	},
	.num_resources  = ARRAY_SIZE(mmcif_resources),
	.resource       = mmcif_resources,
};

static struct platform_device *kota2_devices[] __initdata = {
	&eth_device,
	&keysc_device,
	&gpio_keys_device,
	&gpio_leds_device,
	&mmcif_device,
};

static struct map_desc kota2_io_desc[] __initdata = {
	/* create a 1:1 entity map for 0xe6xxxxxx
	 * used by CPGA, INTC and PFC.
	 */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 256 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
};

static void __init kota2_map_io(void)
{
	iotable_init(kota2_io_desc, ARRAY_SIZE(kota2_io_desc));

	/* setup early devices and console here as well */
	sh73a0_add_early_devices();
	shmobile_setup_console();
}

#define PINTER0A	0xe69000a0
#define PINTCR0A	0xe69000b0

void __init kota2_init_irq(void)
{
	sh73a0_init_irq();

	/* setup PINT: enable PINTA2 as active low */
	__raw_writel(1 << 29, PINTER0A);
	__raw_writew(2 << 10, PINTCR0A);
}

static void __init kota2_init(void)
{
	sh73a0_pinmux_init();

	/* SCIFA2 (UART2) */
	gpio_request(GPIO_FN_SCIFA2_TXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RTS1_, NULL);
	gpio_request(GPIO_FN_SCIFA2_CTS1_, NULL);

	/* SCIFA4 (UART1) */
	gpio_request(GPIO_FN_SCIFA4_TXD, NULL);
	gpio_request(GPIO_FN_SCIFA4_RXD, NULL);
	gpio_request(GPIO_FN_SCIFA4_RTS_, NULL);
	gpio_request(GPIO_FN_SCIFA4_CTS_, NULL);

	/* SMSC911X */
	gpio_request(GPIO_FN_D0_NAF0, NULL);
	gpio_request(GPIO_FN_D1_NAF1, NULL);
	gpio_request(GPIO_FN_D2_NAF2, NULL);
	gpio_request(GPIO_FN_D3_NAF3, NULL);
	gpio_request(GPIO_FN_D4_NAF4, NULL);
	gpio_request(GPIO_FN_D5_NAF5, NULL);
	gpio_request(GPIO_FN_D6_NAF6, NULL);
	gpio_request(GPIO_FN_D7_NAF7, NULL);
	gpio_request(GPIO_FN_D8_NAF8, NULL);
	gpio_request(GPIO_FN_D9_NAF9, NULL);
	gpio_request(GPIO_FN_D10_NAF10, NULL);
	gpio_request(GPIO_FN_D11_NAF11, NULL);
	gpio_request(GPIO_FN_D12_NAF12, NULL);
	gpio_request(GPIO_FN_D13_NAF13, NULL);
	gpio_request(GPIO_FN_D14_NAF14, NULL);
	gpio_request(GPIO_FN_D15_NAF15, NULL);
	gpio_request(GPIO_FN_CS5A_, NULL);
	gpio_request(GPIO_FN_WE0__FWE, NULL);
	gpio_request(GPIO_PORT144, NULL); /* PINTA2 */
	gpio_direction_input(GPIO_PORT144);
	gpio_request(GPIO_PORT145, NULL); /* RESET */
	gpio_direction_output(GPIO_PORT145, 1);

	/* KEYSC */
	gpio_request(GPIO_FN_KEYIN0_PU, NULL);
	gpio_request(GPIO_FN_KEYIN1_PU, NULL);
	gpio_request(GPIO_FN_KEYIN2_PU, NULL);
	gpio_request(GPIO_FN_KEYIN3_PU, NULL);
	gpio_request(GPIO_FN_KEYIN4_PU, NULL);
	gpio_request(GPIO_FN_KEYIN5_PU, NULL);
	gpio_request(GPIO_FN_KEYIN6_PU, NULL);
	gpio_request(GPIO_FN_KEYIN7_PU, NULL);
	gpio_request(GPIO_FN_KEYOUT0, NULL);
	gpio_request(GPIO_FN_KEYOUT1, NULL);
	gpio_request(GPIO_FN_KEYOUT2, NULL);
	gpio_request(GPIO_FN_KEYOUT3, NULL);
	gpio_request(GPIO_FN_KEYOUT4, NULL);
	gpio_request(GPIO_FN_KEYOUT5, NULL);
	gpio_request(GPIO_FN_PORT59_KEYOUT6, NULL);
	gpio_request(GPIO_FN_PORT58_KEYOUT7, NULL);
	gpio_request(GPIO_FN_KEYOUT8, NULL);

	/* MMCIF */
	gpio_request(GPIO_FN_MMCCLK0, NULL);
	gpio_request(GPIO_FN_MMCD0_0, NULL);
	gpio_request(GPIO_FN_MMCD0_1, NULL);
	gpio_request(GPIO_FN_MMCD0_2, NULL);
	gpio_request(GPIO_FN_MMCD0_3, NULL);
	gpio_request(GPIO_FN_MMCD0_4, NULL);
	gpio_request(GPIO_FN_MMCD0_5, NULL);
	gpio_request(GPIO_FN_MMCD0_6, NULL);
	gpio_request(GPIO_FN_MMCD0_7, NULL);
	gpio_request(GPIO_FN_MMCCMD0, NULL);
	gpio_request(GPIO_PORT208, NULL); /* Reset */
	gpio_direction_output(GPIO_PORT208, 1);

	/* SCIFB (BT) */
	gpio_request(GPIO_FN_PORT159_SCIFB_SCK, NULL);
	gpio_request(GPIO_FN_PORT160_SCIFB_TXD, NULL);
	gpio_request(GPIO_FN_PORT161_SCIFB_CTS_, NULL);
	gpio_request(GPIO_FN_PORT162_SCIFB_RXD, NULL);
	gpio_request(GPIO_FN_PORT163_SCIFB_RTS_, NULL);

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 64K*8way */
	l2x0_init(__io(0xf0100000), 0x40460000, 0x82000fff);
#endif
	sh73a0_add_standard_devices();
	platform_add_devices(kota2_devices, ARRAY_SIZE(kota2_devices));
}

static void __init kota2_timer_init(void)
{
	sh73a0_clock_init();
	shmobile_timer.init();
	return;
}

struct sys_timer kota2_timer = {
	.init	= kota2_timer_init,
};

MACHINE_START(KOTA2, "kota2")
	.map_io		= kota2_map_io,
	.init_irq	= kota2_init_irq,
	.handle_irq	= shmobile_handle_irq_gic,
	.init_machine	= kota2_init,
	.timer		= &kota2_timer,
MACHINE_END
