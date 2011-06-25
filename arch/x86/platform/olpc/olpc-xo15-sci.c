/*
 * Support for OLPC XO-1.5 System Control Interrupts (SCI)
 *
 * Copyright (C) 2009-2010 One Laptop per Child
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <asm/olpc.h>

#define DRV_NAME			"olpc-xo15-sci"
#define PFX				DRV_NAME ": "
#define XO15_SCI_CLASS			DRV_NAME
#define XO15_SCI_DEVICE_NAME		"OLPC XO-1.5 SCI"

static unsigned long xo15_sci_gpe;

static void battery_status_changed(void)
{
	struct power_supply *psy = power_supply_get_by_name("olpc-battery");

	if (psy) {
		power_supply_changed(psy);
		put_device(psy->dev);
	}
}

static void ac_status_changed(void)
{
	struct power_supply *psy = power_supply_get_by_name("olpc-ac");

	if (psy) {
		power_supply_changed(psy);
		put_device(psy->dev);
	}
}

static void process_sci_queue(void)
{
	u16 data;
	int r;

	do {
		r = olpc_ec_sci_query(&data);
		if (r || !data)
			break;

		pr_debug(PFX "SCI 0x%x received\n", data);

		switch (data) {
		case EC_SCI_SRC_BATERR:
		case EC_SCI_SRC_BATSOC:
		case EC_SCI_SRC_BATTERY:
		case EC_SCI_SRC_BATCRIT:
			battery_status_changed();
			break;
		case EC_SCI_SRC_ACPWR:
			ac_status_changed();
			break;
		}
	} while (data);

	if (r)
		pr_err(PFX "Failed to clear SCI queue");
}

static void process_sci_queue_work(struct work_struct *work)
{
	process_sci_queue();
}

static DECLARE_WORK(sci_work, process_sci_queue_work);

static u32 xo15_sci_gpe_handler(acpi_handle gpe_device, u32 gpe, void *context)
{
	schedule_work(&sci_work);
	return ACPI_INTERRUPT_HANDLED | ACPI_REENABLE_GPE;
}

static int xo15_sci_add(struct acpi_device *device)
{
	unsigned long long tmp;
	acpi_status status;

	if (!device)
		return -EINVAL;

	strcpy(acpi_device_name(device), XO15_SCI_DEVICE_NAME);
	strcpy(acpi_device_class(device), XO15_SCI_CLASS);

	/* Get GPE bit assignment (EC events). */
	status = acpi_evaluate_integer(device->handle, "_GPE", NULL, &tmp);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	xo15_sci_gpe = tmp;
	status = acpi_install_gpe_handler(NULL, xo15_sci_gpe,
					  ACPI_GPE_EDGE_TRIGGERED,
					  xo15_sci_gpe_handler, device);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	dev_info(&device->dev, "Initialized, GPE = 0x%lx\n", xo15_sci_gpe);

	/* Flush queue, and enable all SCI events */
	process_sci_queue();
	olpc_ec_mask_write(EC_SCI_SRC_ALL);

	acpi_enable_gpe(NULL, xo15_sci_gpe);

	/* Enable wake-on-EC */
	if (device->wakeup.flags.valid)
		device_set_wakeup_enable(&device->dev, true);

	return 0;
}

static int xo15_sci_remove(struct acpi_device *device, int type)
{
	acpi_disable_gpe(NULL, xo15_sci_gpe);
	acpi_remove_gpe_handler(NULL, xo15_sci_gpe, xo15_sci_gpe_handler);
	cancel_work_sync(&sci_work);
	return 0;
}

static int xo15_sci_resume(struct acpi_device *device)
{
	/* Enable all EC events */
	olpc_ec_mask_write(EC_SCI_SRC_ALL);

	/* Power/battery status might have changed */
	battery_status_changed();
	ac_status_changed();

	return 0;
}

static const struct acpi_device_id xo15_sci_device_ids[] = {
	{"XO15EC", 0},
	{"", 0},
};

static struct acpi_driver xo15_sci_drv = {
	.name = DRV_NAME,
	.class = XO15_SCI_CLASS,
	.ids = xo15_sci_device_ids,
	.ops = {
		.add = xo15_sci_add,
		.remove = xo15_sci_remove,
		.resume = xo15_sci_resume,
	},
};

static int __init xo15_sci_init(void)
{
	return acpi_bus_register_driver(&xo15_sci_drv);
}
device_initcall(xo15_sci_init);
