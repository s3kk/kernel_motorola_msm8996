/*
 * pinmux driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/bitops.h>

#define DRIVER_NAME "pinmux-sirf"

#define SIRFSOC_NUM_PADS    622
#define SIRFSOC_GPIO_PAD_EN(g) ((g)*0x100 + 0x84)
#define SIRFSOC_RSC_PIN_MUX 0x4

/*
 * pad list for the pinmux subsystem
 * refer to CS-131858-DC-6A.xls
 */
static const struct pinctrl_pin_desc __refdata sirfsoc_pads[] = {
	PINCTRL_PIN(4, "pwm0"),
	PINCTRL_PIN(5, "pwm1"),
	PINCTRL_PIN(6, "pwm2"),
	PINCTRL_PIN(7, "pwm3"),
	PINCTRL_PIN(8, "warm_rst_b"),
	PINCTRL_PIN(9, "odo_0"),
	PINCTRL_PIN(10, "odo_1"),
	PINCTRL_PIN(11, "dr_dir"),
	PINCTRL_PIN(13, "scl_1"),
	PINCTRL_PIN(15, "sda_1"),
	PINCTRL_PIN(16, "x_ldd[16]"),
	PINCTRL_PIN(17, "x_ldd[17]"),
	PINCTRL_PIN(18, "x_ldd[18]"),
	PINCTRL_PIN(19, "x_ldd[19]"),
	PINCTRL_PIN(20, "x_ldd[20]"),
	PINCTRL_PIN(21, "x_ldd[21]"),
	PINCTRL_PIN(22, "x_ldd[22]"),
	PINCTRL_PIN(23, "x_ldd[23], lcdrom_frdy"),
	PINCTRL_PIN(24, "gps_sgn"),
	PINCTRL_PIN(25, "gps_mag"),
	PINCTRL_PIN(26, "gps_clk"),
	PINCTRL_PIN(27,	"sd_cd_b_1"),
	PINCTRL_PIN(28, "sd_vcc_on_1"),
	PINCTRL_PIN(29, "sd_wp_b_1"),
	PINCTRL_PIN(30, "sd_clk_3"),
	PINCTRL_PIN(31, "sd_cmd_3"),

	PINCTRL_PIN(32, "x_sd_dat_3[0]"),
	PINCTRL_PIN(33, "x_sd_dat_3[1]"),
	PINCTRL_PIN(34, "x_sd_dat_3[2]"),
	PINCTRL_PIN(35, "x_sd_dat_3[3]"),
	PINCTRL_PIN(36, "x_sd_clk_4"),
	PINCTRL_PIN(37, "x_sd_cmd_4"),
	PINCTRL_PIN(38, "x_sd_dat_4[0]"),
	PINCTRL_PIN(39, "x_sd_dat_4[1]"),
	PINCTRL_PIN(40, "x_sd_dat_4[2]"),
	PINCTRL_PIN(41, "x_sd_dat_4[3]"),
	PINCTRL_PIN(42, "x_cko_1"),
	PINCTRL_PIN(43, "x_ac97_bit_clk"),
	PINCTRL_PIN(44, "x_ac97_dout"),
	PINCTRL_PIN(45, "x_ac97_din"),
	PINCTRL_PIN(46, "x_ac97_sync"),
	PINCTRL_PIN(47, "x_txd_1"),
	PINCTRL_PIN(48, "x_txd_2"),
	PINCTRL_PIN(49, "x_rxd_1"),
	PINCTRL_PIN(50, "x_rxd_2"),
	PINCTRL_PIN(51, "x_usclk_0"),
	PINCTRL_PIN(52, "x_utxd_0"),
	PINCTRL_PIN(53, "x_urxd_0"),
	PINCTRL_PIN(54, "x_utfs_0"),
	PINCTRL_PIN(55, "x_urfs_0"),
	PINCTRL_PIN(56, "x_usclk_1"),
	PINCTRL_PIN(57, "x_utxd_1"),
	PINCTRL_PIN(58, "x_urxd_1"),
	PINCTRL_PIN(59, "x_utfs_1"),
	PINCTRL_PIN(60, "x_urfs_1"),
	PINCTRL_PIN(61, "x_usclk_2"),
	PINCTRL_PIN(62, "x_utxd_2"),
	PINCTRL_PIN(63, "x_urxd_2"),

	PINCTRL_PIN(64, "x_utfs_2"),
	PINCTRL_PIN(65, "x_urfs_2"),
	PINCTRL_PIN(66, "x_df_we_b"),
	PINCTRL_PIN(67, "x_df_re_b"),
	PINCTRL_PIN(68, "x_txd_0"),
	PINCTRL_PIN(69, "x_rxd_0"),
	PINCTRL_PIN(78, "x_cko_0"),
	PINCTRL_PIN(79, "x_vip_pxd[7]"),
	PINCTRL_PIN(80, "x_vip_pxd[6]"),
	PINCTRL_PIN(81, "x_vip_pxd[5]"),
	PINCTRL_PIN(82, "x_vip_pxd[4]"),
	PINCTRL_PIN(83, "x_vip_pxd[3]"),
	PINCTRL_PIN(84, "x_vip_pxd[2]"),
	PINCTRL_PIN(85, "x_vip_pxd[1]"),
	PINCTRL_PIN(86, "x_vip_pxd[0]"),
	PINCTRL_PIN(87, "x_vip_vsync"),
	PINCTRL_PIN(88, "x_vip_hsync"),
	PINCTRL_PIN(89, "x_vip_pxclk"),
	PINCTRL_PIN(90, "x_sda_0"),
	PINCTRL_PIN(91, "x_scl_0"),
	PINCTRL_PIN(92, "x_df_ry_by"),
	PINCTRL_PIN(93, "x_df_cs_b[1]"),
	PINCTRL_PIN(94, "x_df_cs_b[0]"),
	PINCTRL_PIN(95, "x_l_pclk"),

	PINCTRL_PIN(96, "x_l_lck"),
	PINCTRL_PIN(97, "x_l_fck"),
	PINCTRL_PIN(98, "x_l_de"),
	PINCTRL_PIN(99, "x_ldd[0]"),
	PINCTRL_PIN(100, "x_ldd[1]"),
	PINCTRL_PIN(101, "x_ldd[2]"),
	PINCTRL_PIN(102, "x_ldd[3]"),
	PINCTRL_PIN(103, "x_ldd[4]"),
	PINCTRL_PIN(104, "x_ldd[5]"),
	PINCTRL_PIN(105, "x_ldd[6]"),
	PINCTRL_PIN(106, "x_ldd[7]"),
	PINCTRL_PIN(107, "x_ldd[8]"),
	PINCTRL_PIN(108, "x_ldd[9]"),
	PINCTRL_PIN(109, "x_ldd[10]"),
	PINCTRL_PIN(110, "x_ldd[11]"),
	PINCTRL_PIN(111, "x_ldd[12]"),
	PINCTRL_PIN(112, "x_ldd[13]"),
	PINCTRL_PIN(113, "x_ldd[14]"),
	PINCTRL_PIN(114, "x_ldd[15]"),
};

/**
 * @dev: a pointer back to containing device
 * @virtbase: the offset to the controller in virtual memory
 */
struct sirfsoc_pmx {
	struct device *dev;
	struct pinctrl_dev *pmx;
	void __iomem *gpio_virtbase;
	void __iomem *rsc_virtbase;
};

/* SIRFSOC_GPIO_PAD_EN set */
struct sirfsoc_muxmask {
	unsigned long group;
	unsigned long mask;
};

struct sirfsoc_padmux {
	unsigned long muxmask_counts;
	const struct sirfsoc_muxmask *muxmask;
	/* RSC_PIN_MUX set */
	unsigned long funcmask;
	unsigned long funcval;
};

 /**
 * struct sirfsoc_pin_group - describes a SiRFprimaII pin group
 * @name: the name of this specific pin group
 * @pins: an array of discrete physical pins used in this group, taken
 *	from the driver-local pin enumeration space
 * @num_pins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 */
struct sirfsoc_pin_group {
	const char *name;
	const unsigned int *pins;
	const unsigned num_pins;
};

static const struct sirfsoc_muxmask lcd_16bits_sirfsoc_muxmask[] = {
	{
		.group = 3,
		.mask = BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(8) |
			BIT(9) | BIT(10) | BIT(11) | BIT(12) | BIT(13) | BIT(14) | BIT(15) | BIT(16) |
			BIT(17) | BIT(18),
	}, {
		.group = 2,
		.mask = BIT(31),
	},
};

static const struct sirfsoc_padmux lcd_16bits_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcd_16bits_sirfsoc_muxmask),
	.muxmask = lcd_16bits_sirfsoc_muxmask,
	.funcmask = BIT(4),
	.funcval = 0,
};

static const unsigned lcd_16bits_pins[] = { 95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
	105, 106, 107, 108, 109, 110, 111, 112, 113, 114 };

static const struct sirfsoc_muxmask lcd_18bits_muxmask[] = {
	{
		.group = 3,
		.mask = BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(8) |
			BIT(9) | BIT(10) | BIT(11) | BIT(12) | BIT(13) | BIT(14) | BIT(15) | BIT(16) |
			BIT(17) | BIT(18),
	}, {
		.group = 2,
		.mask = BIT(31),
	}, {
		.group = 0,
		.mask = BIT(16) | BIT(17),
	},
};

static const struct sirfsoc_padmux lcd_18bits_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcd_18bits_muxmask),
	.muxmask = lcd_18bits_muxmask,
	.funcmask = BIT(4),
	.funcval = 0,
};

static const unsigned lcd_18bits_pins[] = { 16, 17, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
	105, 106, 107, 108, 109, 110, 111, 112, 113, 114};

static const struct sirfsoc_muxmask lcd_24bits_muxmask[] = {
	{
		.group = 3,
		.mask = BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(8) |
			BIT(9) | BIT(10) | BIT(11) | BIT(12) | BIT(13) | BIT(14) | BIT(15) | BIT(16) |
			BIT(17) | BIT(18),
	}, {
		.group = 2,
		.mask = BIT(31),
	}, {
		.group = 0,
		.mask = BIT(16) | BIT(17) | BIT(18) | BIT(19) | BIT(20) | BIT(21) | BIT(22) | BIT(23),
	},
};

static const struct sirfsoc_padmux lcd_24bits_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcd_24bits_muxmask),
	.muxmask = lcd_24bits_muxmask,
	.funcmask = BIT(4),
	.funcval = 0,
};

static const unsigned lcd_24bits_pins[] = { 16, 17, 18, 19, 20, 21, 22, 23, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
	105, 106, 107, 108, 109, 110, 111, 112, 113, 114 };

static const struct sirfsoc_muxmask lcdrom_muxmask[] = {
	{
		.group = 3,
		.mask = BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(8) |
			BIT(9) | BIT(10) | BIT(11) | BIT(12) | BIT(13) | BIT(14) | BIT(15) | BIT(16) |
			BIT(17) | BIT(18),
	}, {
		.group = 2,
		.mask = BIT(31),
	}, {
		.group = 0,
		.mask = BIT(23),
	},
};

static const struct sirfsoc_padmux lcdrom_padmux = {
	.muxmask_counts = ARRAY_SIZE(lcdrom_muxmask),
	.muxmask = lcdrom_muxmask,
	.funcmask = BIT(4),
	.funcval = BIT(4),
};

static const unsigned lcdrom_pins[] = { 23, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
	105, 106, 107, 108, 109, 110, 111, 112, 113, 114 };

static const struct sirfsoc_muxmask uart0_muxmask[] = {
	{
		.group = 2,
		.mask = BIT(4) | BIT(5),
	}, {
		.group = 1,
		.mask = BIT(23) | BIT(28),
	},
};

static const struct sirfsoc_padmux uart0_padmux = {
	.muxmask_counts = ARRAY_SIZE(uart0_muxmask),
	.muxmask = uart0_muxmask,
	.funcmask = BIT(9),
	.funcval = BIT(9),
};

static const unsigned uart0_pins[] = { 55, 60, 68, 69 };

static const struct sirfsoc_muxmask uart0_nostreamctrl_muxmask[] = {
	{
		.group = 2,
		.mask = BIT(4) | BIT(5),
	},
};

static const struct sirfsoc_padmux uart0_nostreamctrl_padmux = {
	.muxmask_counts = ARRAY_SIZE(uart0_nostreamctrl_muxmask),
	.muxmask = uart0_nostreamctrl_muxmask,
};

static const unsigned uart0_nostreamctrl_pins[] = { 68, 39 };

static const struct sirfsoc_muxmask uart1_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(15) | BIT(17),
	},
};

static const struct sirfsoc_padmux uart1_padmux = {
	.muxmask_counts = ARRAY_SIZE(uart1_muxmask),
	.muxmask = uart1_muxmask,
};

static const unsigned uart1_pins[] = { 47, 49 };

static const struct sirfsoc_muxmask uart2_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(16) | BIT(18) | BIT(24) | BIT(27),
	},
};

static const struct sirfsoc_padmux uart2_padmux = {
	.muxmask_counts = ARRAY_SIZE(uart2_muxmask),
	.muxmask = uart2_muxmask,
	.funcmask = BIT(10),
	.funcval = BIT(10),
};

static const unsigned uart2_pins[] = { 48, 50, 56, 59 };

static const struct sirfsoc_muxmask uart2_nostreamctrl_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(16) | BIT(18),
	},
};

static const struct sirfsoc_padmux uart2_nostreamctrl_padmux = {
	.muxmask_counts = ARRAY_SIZE(uart2_nostreamctrl_muxmask),
	.muxmask = uart2_nostreamctrl_muxmask,
};

static const unsigned uart2_nostreamctrl_pins[] = { 48, 50 };

static const struct sirfsoc_muxmask sdmmc3_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(30) | BIT(31),
	}, {
		.group = 1,
		.mask = BIT(0) | BIT(1) | BIT(2) | BIT(3),
	},
};

static const struct sirfsoc_padmux sdmmc3_padmux = {
	.muxmask_counts = ARRAY_SIZE(sdmmc3_muxmask),
	.muxmask = sdmmc3_muxmask,
	.funcmask = BIT(7),
	.funcval = 0,
};

static const unsigned sdmmc3_pins[] = { 30, 31, 32, 33, 34, 35 };

static const struct sirfsoc_muxmask spi0_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(0) | BIT(1) | BIT(2) | BIT(3),
	},
};

static const struct sirfsoc_padmux spi0_padmux = {
	.muxmask_counts = ARRAY_SIZE(spi0_muxmask),
	.muxmask = spi0_muxmask,
	.funcmask = BIT(7),
	.funcval = BIT(7),
};

static const unsigned spi0_pins[] = { 32, 33, 34, 35 };

static const struct sirfsoc_muxmask sdmmc4_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(8) | BIT(9),
	},
};

static const struct sirfsoc_padmux sdmmc4_padmux = {
	.muxmask_counts = ARRAY_SIZE(sdmmc4_muxmask),
	.muxmask = sdmmc4_muxmask,
};

static const unsigned sdmmc4_pins[] = { 36, 37, 38, 39, 40, 41 };

static const struct sirfsoc_muxmask cko1_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(10),
	},
};

static const struct sirfsoc_padmux cko1_padmux = {
	.muxmask_counts = ARRAY_SIZE(cko1_muxmask),
	.muxmask = cko1_muxmask,
	.funcmask = BIT(3),
	.funcval = 0,
};

static const unsigned cko1_pins[] = { 42 };

static const struct sirfsoc_muxmask i2s_muxmask[] = {
	{
		.group = 1,
		.mask =
			BIT(10) | BIT(11) | BIT(12) | BIT(13) | BIT(14) | BIT(19)
				| BIT(23) | BIT(28),
	},
};

static const struct sirfsoc_padmux i2s_padmux = {
	.muxmask_counts = ARRAY_SIZE(i2s_muxmask),
	.muxmask = i2s_muxmask,
	.funcmask = BIT(3) | BIT(9),
	.funcval = BIT(3),
};

static const unsigned i2s_pins[] = { 42, 43, 44, 45, 46, 51, 55, 60 };

static const struct sirfsoc_muxmask ac97_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(11) | BIT(12) | BIT(13) | BIT(14),
	},
};

static const struct sirfsoc_padmux ac97_padmux = {
	.muxmask_counts = ARRAY_SIZE(ac97_muxmask),
	.muxmask = ac97_muxmask,
	.funcmask = BIT(8),
	.funcval = 0,
};

static const unsigned ac97_pins[] = { 33, 34, 35, 36 };

static const struct sirfsoc_muxmask spi1_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(11) | BIT(12) | BIT(13) | BIT(14),
	},
};

static const struct sirfsoc_padmux spi1_padmux = {
	.muxmask_counts = ARRAY_SIZE(spi1_muxmask),
	.muxmask = spi1_muxmask,
	.funcmask = BIT(8),
	.funcval = BIT(8),
};

static const unsigned spi1_pins[] = { 33, 34, 35, 36 };

static const struct sirfsoc_muxmask sdmmc1_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(27) | BIT(28) | BIT(29),
	},
};

static const struct sirfsoc_padmux sdmmc1_padmux = {
	.muxmask_counts = ARRAY_SIZE(sdmmc1_muxmask),
	.muxmask = sdmmc1_muxmask,
};

static const unsigned sdmmc1_pins[] = { 27, 28, 29 };

static const struct sirfsoc_muxmask gps_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(24) | BIT(25) | BIT(26),
	},
};

static const struct sirfsoc_padmux gps_padmux = {
	.muxmask_counts = ARRAY_SIZE(gps_muxmask),
	.muxmask = gps_muxmask,
	.funcmask = BIT(12) | BIT(13) | BIT(14),
	.funcval = BIT(12),
};

static const unsigned gps_pins[] = { 24, 25, 26 };

static const struct sirfsoc_muxmask sdmmc5_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(24) | BIT(25) | BIT(26),
	}, {
		.group = 1,
		.mask = BIT(29),
	}, {
		.group = 2,
		.mask = BIT(0) | BIT(1),
	},
};

static const struct sirfsoc_padmux sdmmc5_padmux = {
	.muxmask_counts = ARRAY_SIZE(sdmmc5_muxmask),
	.muxmask = sdmmc5_muxmask,
	.funcmask = BIT(13) | BIT(14),
	.funcval = BIT(13) | BIT(14),
};

static const unsigned sdmmc5_pins[] = { 24, 25, 26, 61, 64, 65 };

static const struct sirfsoc_muxmask usp0_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(19) | BIT(20) | BIT(21) | BIT(22) | BIT(23),
	},
};

static const struct sirfsoc_padmux usp0_padmux = {
	.muxmask_counts = ARRAY_SIZE(usp0_muxmask),
	.muxmask = usp0_muxmask,
	.funcmask = BIT(1) | BIT(2) | BIT(6) | BIT(9),
	.funcval = 0,
};

static const unsigned usp0_pins[] = { 51, 52, 53, 54, 55 };

static const struct sirfsoc_muxmask usp1_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(24) | BIT(25) | BIT(26) | BIT(27) | BIT(28),
	},
};

static const struct sirfsoc_padmux usp1_padmux = {
	.muxmask_counts = ARRAY_SIZE(usp1_muxmask),
	.muxmask = usp1_muxmask,
	.funcmask = BIT(1) | BIT(9) | BIT(10) | BIT(11),
	.funcval = 0,
};

static const unsigned usp1_pins[] = { 56, 57, 58, 59, 60 };

static const struct sirfsoc_muxmask usp2_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(29) | BIT(30) | BIT(31),
	}, {
		.group = 2,
		.mask = BIT(0) | BIT(1),
	},
};

static const struct sirfsoc_padmux usp2_padmux = {
	.muxmask_counts = ARRAY_SIZE(usp2_muxmask),
	.muxmask = usp2_muxmask,
	.funcmask = BIT(13) | BIT(14),
	.funcval = 0,
};

static const unsigned usp2_pins[] = { 61, 62, 63, 64, 65 };

static const struct sirfsoc_muxmask nand_muxmask[] = {
	{
		.group = 2,
		.mask = BIT(2) | BIT(3) | BIT(28) | BIT(29) | BIT(30),
	},
};

static const struct sirfsoc_padmux nand_padmux = {
	.muxmask_counts = ARRAY_SIZE(nand_muxmask),
	.muxmask = nand_muxmask,
	.funcmask = BIT(5),
	.funcval = 0,
};

static const unsigned nand_pins[] = { 64, 65, 92, 93, 94 };

static const struct sirfsoc_padmux sdmmc0_padmux = {
	.muxmask_counts = 0,
	.funcmask = BIT(5),
	.funcval = 0,
};

static const unsigned sdmmc0_pins[] = { };

static const struct sirfsoc_muxmask sdmmc2_muxmask[] = {
	{
		.group = 2,
		.mask = BIT(2) | BIT(3),
	},
};

static const struct sirfsoc_padmux sdmmc2_padmux = {
	.muxmask_counts = ARRAY_SIZE(sdmmc2_muxmask),
	.muxmask = sdmmc2_muxmask,
	.funcmask = BIT(5),
	.funcval = BIT(5),
};

static const unsigned sdmmc2_pins[] = { 66, 67 };

static const struct sirfsoc_muxmask cko0_muxmask[] = {
	{
		.group = 2,
		.mask = BIT(14),
	},
};

static const struct sirfsoc_padmux cko0_padmux = {
	.muxmask_counts = ARRAY_SIZE(cko0_muxmask),
	.muxmask = cko0_muxmask,
};

static const unsigned cko0_pins[] = { 78 };

static const struct sirfsoc_muxmask vip_muxmask[] = {
	{
		.group = 2,
		.mask = BIT(15) | BIT(16) | BIT(17) | BIT(18) | BIT(19)
			| BIT(20) | BIT(21) | BIT(22) | BIT(23) | BIT(24) |
			BIT(25),
	},
};

static const struct sirfsoc_padmux vip_padmux = {
	.muxmask_counts = ARRAY_SIZE(vip_muxmask),
	.muxmask = vip_muxmask,
	.funcmask = BIT(0),
	.funcval = 0,
};

static const unsigned vip_pins[] = { 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89 };

static const struct sirfsoc_muxmask i2c0_muxmask[] = {
	{
		.group = 2,
		.mask = BIT(26) | BIT(27),
	},
};

static const struct sirfsoc_padmux i2c0_padmux = {
	.muxmask_counts = ARRAY_SIZE(i2c0_muxmask),
	.muxmask = i2c0_muxmask,
};

static const unsigned i2c0_pins[] = { 90, 91 };

static const struct sirfsoc_muxmask i2c1_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(13) | BIT(15),
	},
};

static const struct sirfsoc_padmux i2c1_padmux = {
	.muxmask_counts = ARRAY_SIZE(i2c1_muxmask),
	.muxmask = i2c1_muxmask,
};

static const unsigned i2c1_pins[] = { 13, 15 };

static const struct sirfsoc_muxmask viprom_muxmask[] = {
	{
		.group = 2,
		.mask = BIT(15) | BIT(16) | BIT(17) | BIT(18) | BIT(19)
			| BIT(20) | BIT(21) | BIT(22) | BIT(23) | BIT(24) |
			BIT(25),
	}, {
		.group = 0,
		.mask = BIT(12),
	},
};

static const struct sirfsoc_padmux viprom_padmux = {
	.muxmask_counts = ARRAY_SIZE(viprom_muxmask),
	.muxmask = viprom_muxmask,
	.funcmask = BIT(0),
	.funcval = BIT(0),
};

static const unsigned viprom_pins[] = { 12, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89 };

static const struct sirfsoc_muxmask pwm0_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(4),
	},
};

static const struct sirfsoc_padmux pwm0_padmux = {
	.muxmask_counts = ARRAY_SIZE(pwm0_muxmask),
	.muxmask = pwm0_muxmask,
	.funcmask = BIT(12),
	.funcval = 0,
};

static const unsigned pwm0_pins[] = { 4 };

static const struct sirfsoc_muxmask pwm1_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(5),
	},
};

static const struct sirfsoc_padmux pwm1_padmux = {
	.muxmask_counts = ARRAY_SIZE(pwm1_muxmask),
	.muxmask = pwm1_muxmask,
};

static const unsigned pwm1_pins[] = { 5 };

static const struct sirfsoc_muxmask pwm2_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(6),
	},
};

static const struct sirfsoc_padmux pwm2_padmux = {
	.muxmask_counts = ARRAY_SIZE(pwm2_muxmask),
	.muxmask = pwm2_muxmask,
};

static const unsigned pwm2_pins[] = { 6 };

static const struct sirfsoc_muxmask pwm3_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(7),
	},
};

static const struct sirfsoc_padmux pwm3_padmux = {
	.muxmask_counts = ARRAY_SIZE(pwm3_muxmask),
	.muxmask = pwm3_muxmask,
};

static const unsigned pwm3_pins[] = { 7 };

static const struct sirfsoc_muxmask warm_rst_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(8),
	},
};

static const struct sirfsoc_padmux warm_rst_padmux = {
	.muxmask_counts = ARRAY_SIZE(warm_rst_muxmask),
	.muxmask = warm_rst_muxmask,
};

static const unsigned warm_rst_pins[] = { 8 };

static const struct sirfsoc_muxmask usb0_utmi_drvbus_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(22),
	},
};
static const struct sirfsoc_padmux usb0_utmi_drvbus_padmux = {
	.muxmask_counts = ARRAY_SIZE(usb0_utmi_drvbus_muxmask),
	.muxmask = usb0_utmi_drvbus_muxmask,
	.funcmask = BIT(6),
	.funcval = BIT(6), /* refer to PAD_UTMI_DRVVBUS0_ENABLE */
};

static const unsigned usb0_utmi_drvbus_pins[] = { 54 };

static const struct sirfsoc_muxmask usb1_utmi_drvbus_muxmask[] = {
	{
		.group = 1,
		.mask = BIT(27),
	},
};

static const struct sirfsoc_padmux usb1_utmi_drvbus_padmux = {
	.muxmask_counts = ARRAY_SIZE(usb1_utmi_drvbus_muxmask),
	.muxmask = usb1_utmi_drvbus_muxmask,
	.funcmask = BIT(11),
	.funcval = BIT(11), /* refer to PAD_UTMI_DRVVBUS1_ENABLE */
};

static const unsigned usb1_utmi_drvbus_pins[] = { 59 };

static const struct sirfsoc_muxmask pulse_count_muxmask[] = {
	{
		.group = 0,
		.mask = BIT(9) | BIT(10) | BIT(11),
	},
};

static const struct sirfsoc_padmux pulse_count_padmux = {
	.muxmask_counts = ARRAY_SIZE(pulse_count_muxmask),
	.muxmask = pulse_count_muxmask,
};

static const unsigned pulse_count_pins[] = { 9, 10, 11 };

#define SIRFSOC_PIN_GROUP(n, p)  \
	{			\
		.name = n,	\
		.pins = p,	\
		.num_pins = ARRAY_SIZE(p),	\
	}

static const struct sirfsoc_pin_group sirfsoc_pin_groups[] = {
	SIRFSOC_PIN_GROUP("lcd_16bitsgrp", lcd_16bits_pins),
	SIRFSOC_PIN_GROUP("lcd_18bitsgrp", lcd_18bits_pins),
	SIRFSOC_PIN_GROUP("lcd_24bitsgrp", lcd_24bits_pins),
	SIRFSOC_PIN_GROUP("lcdrom_grp", lcdrom_pins),
	SIRFSOC_PIN_GROUP("uart0grp", uart0_pins),
	SIRFSOC_PIN_GROUP("uart1grp", uart1_pins),
	SIRFSOC_PIN_GROUP("uart2grp", uart2_pins),
	SIRFSOC_PIN_GROUP("uart2_nostreamctrlgrp", uart2_nostreamctrl_pins),
	SIRFSOC_PIN_GROUP("usp0grp", usp0_pins),
	SIRFSOC_PIN_GROUP("usp1grp", usp1_pins),
	SIRFSOC_PIN_GROUP("usp2grp", usp2_pins),
	SIRFSOC_PIN_GROUP("i2c0grp", i2c0_pins),
	SIRFSOC_PIN_GROUP("i2c1grp", i2c1_pins),
	SIRFSOC_PIN_GROUP("pwm0grp", pwm0_pins),
	SIRFSOC_PIN_GROUP("pwm1grp", pwm1_pins),
	SIRFSOC_PIN_GROUP("pwm2grp", pwm2_pins),
	SIRFSOC_PIN_GROUP("pwm3grp", pwm3_pins),
	SIRFSOC_PIN_GROUP("vipgrp", vip_pins),
	SIRFSOC_PIN_GROUP("vipromgrp", viprom_pins),
	SIRFSOC_PIN_GROUP("warm_rstgrp", warm_rst_pins),
	SIRFSOC_PIN_GROUP("cko0_rstgrp", cko0_pins),
	SIRFSOC_PIN_GROUP("cko1_rstgrp", cko1_pins),
	SIRFSOC_PIN_GROUP("sdmmc0grp", sdmmc0_pins),
	SIRFSOC_PIN_GROUP("sdmmc1grp", sdmmc1_pins),
	SIRFSOC_PIN_GROUP("sdmmc2grp", sdmmc2_pins),
	SIRFSOC_PIN_GROUP("sdmmc3grp", sdmmc3_pins),
	SIRFSOC_PIN_GROUP("sdmmc4grp", sdmmc4_pins),
	SIRFSOC_PIN_GROUP("sdmmc5grp", sdmmc5_pins),
	SIRFSOC_PIN_GROUP("usb0_utmi_drvbusgrp", usb0_utmi_drvbus_pins),
	SIRFSOC_PIN_GROUP("usb1_utmi_drvbusgrp", usb1_utmi_drvbus_pins),
	SIRFSOC_PIN_GROUP("pulse_countgrp", pulse_count_pins),
	SIRFSOC_PIN_GROUP("i2sgrp", i2s_pins),
	SIRFSOC_PIN_GROUP("ac97grp", ac97_pins),
	SIRFSOC_PIN_GROUP("nandgrp", nand_pins),
	SIRFSOC_PIN_GROUP("spi0grp", spi0_pins),
	SIRFSOC_PIN_GROUP("spi1grp", spi1_pins),
	SIRFSOC_PIN_GROUP("gpsgrp", gps_pins),
};

static int sirfsoc_list_groups(struct pinctrl_dev *pctldev, unsigned selector)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pin_groups))
		return -EINVAL;
	return 0;
}

static const char *sirfsoc_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned selector)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pin_groups))
		return NULL;
	return sirfsoc_pin_groups[selector].name;
}

static int sirfsoc_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
			       unsigned ** const pins,
			       unsigned * const num_pins)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pin_groups))
		return -EINVAL;
	*pins = (unsigned *) sirfsoc_pin_groups[selector].pins;
	*num_pins = sirfsoc_pin_groups[selector].num_pins;
	return 0;
}

static void sirfsoc_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
		   unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}

static struct pinctrl_ops sirfsoc_pctrl_ops = {
	.list_groups = sirfsoc_list_groups,
	.get_group_name = sirfsoc_get_group_name,
	.get_group_pins = sirfsoc_get_group_pins,
	.pin_dbg_show = sirfsoc_pin_dbg_show,
};

struct sirfsoc_pmx_func {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;
	const struct sirfsoc_padmux *padmux;
};

static const char * const lcd_16bitsgrp[] = { "lcd_16bitsgrp" };
static const char * const lcd_18bitsgrp[] = { "lcd_18bitsgrp" };
static const char * const lcd_24bitsgrp[] = { "lcd_24bitsgrp" };
static const char * const lcdromgrp[] = { "lcdromgrp" };
static const char * const uart0grp[] = { "uart0grp" };
static const char * const uart1grp[] = { "uart1grp" };
static const char * const uart2grp[] = { "uart2grp" };
static const char * const uart2_nostreamctrlgrp[] = { "uart2_nostreamctrlgrp" };
static const char * const usp0grp[] = { "usp0grp" };
static const char * const usp1grp[] = { "usp1grp" };
static const char * const usp2grp[] = { "usp2grp" };
static const char * const i2c0grp[] = { "i2c0grp" };
static const char * const i2c1grp[] = { "i2c1grp" };
static const char * const pwm0grp[] = { "pwm0grp" };
static const char * const pwm1grp[] = { "pwm1grp" };
static const char * const pwm2grp[] = { "pwm2grp" };
static const char * const pwm3grp[] = { "pwm3grp" };
static const char * const vipgrp[] = { "vipgrp" };
static const char * const vipromgrp[] = { "vipromgrp" };
static const char * const warm_rstgrp[] = { "warm_rstgrp" };
static const char * const cko0grp[] = { "cko0grp" };
static const char * const cko1grp[] = { "cko1grp" };
static const char * const sdmmc0grp[] = { "sdmmc0grp" };
static const char * const sdmmc1grp[] = { "sdmmc1grp" };
static const char * const sdmmc2grp[] = { "sdmmc2grp" };
static const char * const sdmmc3grp[] = { "sdmmc3grp" };
static const char * const sdmmc4grp[] = { "sdmmc4grp" };
static const char * const sdmmc5grp[] = { "sdmmc5grp" };
static const char * const usb0_utmi_drvbusgrp[] = { "usb0_utmi_drvbusgrp" };
static const char * const usb1_utmi_drvbusgrp[] = { "usb1_utmi_drvbusgrp" };
static const char * const pulse_countgrp[] = { "pulse_countgrp" };
static const char * const i2sgrp[] = { "i2sgrp" };
static const char * const ac97grp[] = { "ac97grp" };
static const char * const nandgrp[] = { "nandgrp" };
static const char * const spi0grp[] = { "spi0grp" };
static const char * const spi1grp[] = { "spi1grp" };
static const char * const gpsgrp[] = { "gpsgrp" };

#define SIRFSOC_PMX_FUNCTION(n, g, m)		\
	{					\
		.name = n,			\
		.groups = g,			\
		.num_groups = ARRAY_SIZE(g),	\
		.padmux = &m,			\
	}

static const struct sirfsoc_pmx_func sirfsoc_pmx_functions[] = {
	SIRFSOC_PMX_FUNCTION("lcd_16bits", lcd_16bitsgrp, lcd_16bits_padmux),
	SIRFSOC_PMX_FUNCTION("lcd_18bits", lcd_18bitsgrp, lcd_18bits_padmux),
	SIRFSOC_PMX_FUNCTION("lcd_24bits", lcd_24bitsgrp, lcd_24bits_padmux),
	SIRFSOC_PMX_FUNCTION("lcdrom", lcdromgrp, lcdrom_padmux),
	SIRFSOC_PMX_FUNCTION("uart0", uart0grp, uart0_padmux),
	SIRFSOC_PMX_FUNCTION("uart1", uart1grp, uart1_padmux),
	SIRFSOC_PMX_FUNCTION("uart2", uart2grp, uart2_padmux),
	SIRFSOC_PMX_FUNCTION("uart2_nostreamctrl", uart2_nostreamctrlgrp, uart2_nostreamctrl_padmux),
	SIRFSOC_PMX_FUNCTION("usp0", usp0grp, usp0_padmux),
	SIRFSOC_PMX_FUNCTION("usp1", usp1grp, usp1_padmux),
	SIRFSOC_PMX_FUNCTION("usp2", usp2grp, usp2_padmux),
	SIRFSOC_PMX_FUNCTION("i2c0", i2c0grp, i2c0_padmux),
	SIRFSOC_PMX_FUNCTION("i2c1", i2c1grp, i2c1_padmux),
	SIRFSOC_PMX_FUNCTION("pwm0", pwm0grp, pwm0_padmux),
	SIRFSOC_PMX_FUNCTION("pwm1", pwm1grp, pwm1_padmux),
	SIRFSOC_PMX_FUNCTION("pwm2", pwm2grp, pwm2_padmux),
	SIRFSOC_PMX_FUNCTION("pwm3", pwm3grp, pwm3_padmux),
	SIRFSOC_PMX_FUNCTION("vip", vipgrp, vip_padmux),
	SIRFSOC_PMX_FUNCTION("viprom", vipromgrp, viprom_padmux),
	SIRFSOC_PMX_FUNCTION("warm_rst", warm_rstgrp, warm_rst_padmux),
	SIRFSOC_PMX_FUNCTION("cko0", cko0grp, cko0_padmux),
	SIRFSOC_PMX_FUNCTION("cko1", cko1grp, cko1_padmux),
	SIRFSOC_PMX_FUNCTION("sdmmc0", sdmmc0grp, sdmmc0_padmux),
	SIRFSOC_PMX_FUNCTION("sdmmc1", sdmmc1grp, sdmmc1_padmux),
	SIRFSOC_PMX_FUNCTION("sdmmc2", sdmmc2grp, sdmmc2_padmux),
	SIRFSOC_PMX_FUNCTION("sdmmc3", sdmmc3grp, sdmmc3_padmux),
	SIRFSOC_PMX_FUNCTION("sdmmc4", sdmmc4grp, sdmmc4_padmux),
	SIRFSOC_PMX_FUNCTION("sdmmc5", sdmmc5grp, sdmmc5_padmux),
	SIRFSOC_PMX_FUNCTION("usb0_utmi_drvbus", usb0_utmi_drvbusgrp, usb0_utmi_drvbus_padmux),
	SIRFSOC_PMX_FUNCTION("usb1_utmi_drvbus", usb1_utmi_drvbusgrp, usb1_utmi_drvbus_padmux),
	SIRFSOC_PMX_FUNCTION("pulse_count", pulse_countgrp, pulse_count_padmux),
	SIRFSOC_PMX_FUNCTION("i2s", i2sgrp, i2s_padmux),
	SIRFSOC_PMX_FUNCTION("ac97", ac97grp, ac97_padmux),
	SIRFSOC_PMX_FUNCTION("nand", nandgrp, nand_padmux),
	SIRFSOC_PMX_FUNCTION("spi0", spi0grp, spi0_padmux),
	SIRFSOC_PMX_FUNCTION("spi1", spi1grp, spi1_padmux),
	SIRFSOC_PMX_FUNCTION("gps", gpsgrp, gps_padmux),
};

static void sirfsoc_pinmux_endisable(struct sirfsoc_pmx *spmx, unsigned selector,
	bool enable)
{
	int i;
	const struct sirfsoc_padmux *mux = sirfsoc_pmx_functions[selector].padmux;
	const struct sirfsoc_muxmask *mask = mux->muxmask;

	for (i = 0; i < mux->muxmask_counts; i++) {
		u32 muxval;
		muxval = readl(spmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(mask[i].group));
		if (enable)
			muxval = muxval & ~mask[i].mask;
		else
			muxval = muxval | mask[i].mask;
		writel(muxval, spmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(mask[i].group));
	}

	if (mux->funcmask && enable) {
		u32 func_en_val;
		func_en_val =
			readl(spmx->rsc_virtbase + SIRFSOC_RSC_PIN_MUX);
		func_en_val =
			(func_en_val & ~mux->funcmask) | (mux->
				funcval);
		writel(func_en_val, spmx->rsc_virtbase + SIRFSOC_RSC_PIN_MUX);
	}
}

static int sirfsoc_pinmux_enable(struct pinctrl_dev *pmxdev, unsigned selector,
	unsigned group)
{
	struct sirfsoc_pmx *spmx;

	spmx = pinctrl_dev_get_drvdata(pmxdev);
	sirfsoc_pinmux_endisable(spmx, selector, true);

	return 0;
}

static void sirfsoc_pinmux_disable(struct pinctrl_dev *pmxdev, unsigned selector,
	unsigned group)
{
	struct sirfsoc_pmx *spmx;

	spmx = pinctrl_dev_get_drvdata(pmxdev);
	sirfsoc_pinmux_endisable(spmx, selector, false);
}

static int sirfsoc_pinmux_list_funcs(struct pinctrl_dev *pmxdev, unsigned selector)
{
	if (selector >= ARRAY_SIZE(sirfsoc_pmx_functions))
		return -EINVAL;
	return 0;
}

static const char *sirfsoc_pinmux_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	return sirfsoc_pmx_functions[selector].name;
}

static int sirfsoc_pinmux_get_groups(struct pinctrl_dev *pctldev, unsigned selector,
			       const char * const **groups,
			       unsigned * const num_groups)
{
	*groups = sirfsoc_pmx_functions[selector].groups;
	*num_groups = sirfsoc_pmx_functions[selector].num_groups;
	return 0;
}

static int sirfsoc_pinmux_request_gpio(struct pinctrl_dev *pmxdev,
	struct pinctrl_gpio_range *range, unsigned offset)
{
	struct sirfsoc_pmx *spmx;

	int group = range->id;

	u32 muxval;

	spmx = pinctrl_dev_get_drvdata(pmxdev);

	muxval = readl(spmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(group));
	muxval = muxval | (1 << offset);
	writel(muxval, spmx->gpio_virtbase + SIRFSOC_GPIO_PAD_EN(group));

	return 0;
}

static struct pinmux_ops sirfsoc_pinmux_ops = {
	.list_functions = sirfsoc_pinmux_list_funcs,
	.enable = sirfsoc_pinmux_enable,
	.disable = sirfsoc_pinmux_disable,
	.get_function_name = sirfsoc_pinmux_get_func_name,
	.get_function_groups = sirfsoc_pinmux_get_groups,
	.gpio_request_enable = sirfsoc_pinmux_request_gpio,
};

static struct pinctrl_desc sirfsoc_pinmux_desc = {
	.name = DRIVER_NAME,
	.pins = sirfsoc_pads,
	.npins = ARRAY_SIZE(sirfsoc_pads),
	.maxpin = SIRFSOC_NUM_PADS - 1,
	.pctlops = &sirfsoc_pctrl_ops,
	.pmxops = &sirfsoc_pinmux_ops,
	.owner = THIS_MODULE,
};

/*
 * Todo: bind irq_chip to every pinctrl_gpio_range
 */
static struct pinctrl_gpio_range sirfsoc_gpio_ranges[] = {
	{
		.name = "sirfsoc-gpio*",
		.id = 0,
		.base = 0,
		.npins = 32,
	}, {
		.name = "sirfsoc-gpio*",
		.id = 1,
		.base = 32,
		.npins = 32,
	}, {
		.name = "sirfsoc-gpio*",
		.id = 2,
		.base = 64,
		.npins = 32,
	}, {
		.name = "sirfsoc-gpio*",
		.id = 3,
		.base = 96,
		.npins = 19,
	},
};

static void __iomem *sirfsoc_rsc_of_iomap(void)
{
	const struct of_device_id rsc_ids[]  = {
		{ .compatible = "sirf,prima2-rsc" },
		{}
	};
	struct device_node *np;

	np = of_find_matching_node(NULL, rsc_ids);
	if (!np)
		panic("unable to find compatible rsc node in dtb\n");

	return of_iomap(np, 0);
}

static int __devinit sirfsoc_pinmux_probe(struct platform_device *pdev)
{
	int ret;
	struct sirfsoc_pmx *spmx;
	struct device_node *np = pdev->dev.of_node;
	int i;

	/* Create state holders etc for this driver */
	spmx = devm_kzalloc(&pdev->dev, sizeof(*spmx), GFP_KERNEL);
	if (!spmx)
		return -ENOMEM;

	spmx->dev = &pdev->dev;

	platform_set_drvdata(pdev, spmx);

	spmx->gpio_virtbase = of_iomap(np, 0);
	if (!spmx->gpio_virtbase) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "can't map gpio registers\n");
		goto out_no_gpio_remap;
	}

	spmx->rsc_virtbase = sirfsoc_rsc_of_iomap();
	if (!spmx->rsc_virtbase) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "can't map rsc registers\n");
		goto out_no_rsc_remap;
	}

	/* Now register the pin controller and all pins it handles */
	spmx->pmx = pinctrl_register(&sirfsoc_pinmux_desc, &pdev->dev, spmx);
	if (!spmx->pmx) {
		dev_err(&pdev->dev, "could not register SIRFSOC pinmux driver\n");
		ret = -EINVAL;
		goto out_no_pmx;
	}

	for (i = 0; i < ARRAY_SIZE(sirfsoc_gpio_ranges); i++)
		pinctrl_add_gpio_range(spmx->pmx, &sirfsoc_gpio_ranges[i]);

	dev_info(&pdev->dev, "initialized SIRFSOC pinmux driver\n");

	return 0;

out_no_pmx:
	iounmap(spmx->rsc_virtbase);
out_no_rsc_remap:
	iounmap(spmx->gpio_virtbase);
out_no_gpio_remap:
	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, spmx);
	return ret;
}

static const struct of_device_id pinmux_ids[]  = {
	{ .compatible = "sirf,prima2-gpio-pinmux" },
	{}
};

static struct platform_driver sirfsoc_pinmux_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = pinmux_ids,
	},
	.probe = sirfsoc_pinmux_probe,
};

static int __init sirfsoc_pinmux_init(void)
{
	return platform_driver_register(&sirfsoc_pinmux_driver);
}
arch_initcall(sirfsoc_pinmux_init);

MODULE_AUTHOR("Rongjun Ying <rongjun.ying@csr.com>, "
	"Barry Song <baohua.song@csr.com>");
MODULE_DESCRIPTION("SIRFSOC pin control driver");
MODULE_LICENSE("GPL");
