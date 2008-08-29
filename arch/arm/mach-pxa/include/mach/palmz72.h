/*
 * GPIOs and interrupts for Palm Zire72 Handheld Computer
 *
 * Authors:	Alex Osborne <bobofdoom@gmail.com>
 *		Jan Herman <2hp@seznam.cz>
 *		Sergey Lapin <slapin@ossfans.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _INCLUDE_PALMZ72_H_
#define _INCLUDE_PALMZ72_H_

/* Power and control */
#define GPIO_NR_PALMZ72_GPIO_RESET		1
#define GPIO_NR_PALMZ72_POWER_DETECT		0

/* SD/MMC */
#define GPIO_NR_PALMZ72_SD_DETECT_N		14
#define GPIO_NR_PALMZ72_SD_POWER_N		98
#define GPIO_NR_PALMZ72_SD_RO 115

/* Touchscreen */
#define GPIO_NR_PALMZ72_WM9712_IRQ		27

/* IRDA -  disable GPIO connected to SD pin of tranceiver (TFBS4710?) ? */
#define GPIO_NR_PALMZ72_IR_DISABLE		49

/* USB */
#define GPIO_NR_PALMZ72_USB_DETECT_N		15
#define GPIO_NR_PALMZ72_USB_POWER		95
#define GPIO_NR_PALMZ72_USB_PULLUP		12

/* LCD/Backlight */
#define GPIO_NR_PALMZ72_BL_POWER		20
#define GPIO_NR_PALMZ72_LCD_POWER		96

/* LED */
#define GPIO_NR_PALMZ72_LED_GREEN		88

/* Bluetooth */
#define GPIO_NR_PALMZ72_BT_POWER		17
#define GPIO_NR_PALMZ72_BT_RESET		83

/** Initial values **/

/* Battery */
#define PALMZ72_BAT_MAX_VOLTAGE		4000	/* 4.00v current voltage */
#define PALMZ72_BAT_MIN_VOLTAGE		3550	/* 3.55v critical voltage */
#define PALMZ72_BAT_MAX_CURRENT		0	/* unknokn */
#define PALMZ72_BAT_MIN_CURRENT		0	/* unknown */
#define PALMZ72_BAT_MAX_CHARGE		1	/* unknown */
#define PALMZ72_BAT_MIN_CHARGE		1	/* unknown */
#define PALMZ72_MAX_LIFE_MINS		360	/* on-life in minutes */

/* Backlight */
#define PALMZ72_MAX_INTENSITY		0xFE
#define PALMZ72_DEFAULT_INTENSITY	0x7E
#define PALMZ72_LIMIT_MASK		0x7F
#define PALMZ72_PRESCALER		0x3F
#define PALMZ72_PERIOD_NS		3500

#endif

