/*
 * pps_parport.c -- kernel parallel port PPS client
 *
 *
 * Copyright (C) 2009   Alexander Gordeev <lasaine@lvk.cs.msu.su>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


/*
 * TODO:
 * implement echo over SEL pin
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irqnr.h>
#include <linux/time.h>
#include <linux/parport.h>
#include <linux/pps_kernel.h>

#define DRVDESC "parallel port PPS client"

/* module parameters */

#define CLEAR_WAIT_MAX		100
#define CLEAR_WAIT_MAX_ERRORS	5

static unsigned int clear_wait = 100;
MODULE_PARM_DESC(clear_wait,
	"Maximum number of port reads when polling for signal clear,"
	" zero turns clear edge capture off entirely");
module_param(clear_wait, uint, 0);


/* internal per port structure */
struct pps_client_pp {
	struct pardevice *pardev;	/* parport device */
	struct pps_device *pps;		/* PPS device */
	unsigned int cw;		/* port clear timeout */
	unsigned int cw_err;		/* number of timeouts */
};

static inline int signal_is_set(struct parport *port)
{
	return (port->ops->read_status(port) & PARPORT_STATUS_ACK) != 0;
}

/* parport interrupt handler */
static void parport_irq(void *handle)
{
	struct pps_event_time ts_assert, ts_clear;
	struct pps_client_pp *dev = handle;
	struct parport *port = dev->pardev->port;
	unsigned int i;
	unsigned long flags;

	/* first of all we get the time stamp... */
	pps_get_ts(&ts_assert);

	if (dev->cw == 0)
		/* clear edge capture disabled */
		goto out_assert;

	/* try capture the clear edge */

	/* We have to disable interrupts here. The idea is to prevent
	 * other interrupts on the same processor to introduce random
	 * lags while polling the port. Reading from IO port is known
	 * to take approximately 1us while other interrupt handlers can
	 * take much more potentially.
	 *
	 * Interrupts won't be disabled for a long time because the
	 * number of polls is limited by clear_wait parameter which is
	 * kept rather low. So it should never be an issue.
	 */
	local_irq_save(flags);
	/* check the signal (no signal means the pulse is lost this time) */
	if (!signal_is_set(port)) {
		local_irq_restore(flags);
		dev_err(dev->pps->dev, "lost the signal\n");
		goto out_assert;
	}

	/* poll the port until the signal is unset */
	for (i = dev->cw; i; i--)
		if (!signal_is_set(port)) {
			pps_get_ts(&ts_clear);
			local_irq_restore(flags);
			dev->cw_err = 0;
			goto out_both;
		}
	local_irq_restore(flags);

	/* timeout */
	dev->cw_err++;
	if (dev->cw_err >= CLEAR_WAIT_MAX_ERRORS) {
		dev_err(dev->pps->dev, "disabled clear edge capture after %d"
				" timeouts\n", dev->cw_err);
		dev->cw = 0;
		dev->cw_err = 0;
	}

out_assert:
	/* fire assert event */
	pps_event(dev->pps, &ts_assert,
			PPS_CAPTUREASSERT, NULL);
	return;

out_both:
	/* fire assert event */
	pps_event(dev->pps, &ts_assert,
			PPS_CAPTUREASSERT, NULL);
	/* fire clear event */
	pps_event(dev->pps, &ts_clear,
			PPS_CAPTURECLEAR, NULL);
	return;
}

static void parport_attach(struct parport *port)
{
	struct pps_client_pp *device;
	struct pps_source_info info = {
		.name		= KBUILD_MODNAME,
		.path		= "",
		.mode		= PPS_CAPTUREBOTH | \
				  PPS_OFFSETASSERT | PPS_OFFSETCLEAR | \
				  PPS_ECHOASSERT | PPS_ECHOCLEAR | \
				  PPS_CANWAIT | PPS_TSFMT_TSPEC,
		.owner		= THIS_MODULE,
		.dev		= NULL
	};

	device = kzalloc(sizeof(struct pps_client_pp), GFP_KERNEL);
	if (!device) {
		pr_err("memory allocation failed, not attaching\n");
		return;
	}

	device->pardev = parport_register_device(port, KBUILD_MODNAME,
			NULL, NULL, parport_irq, PARPORT_FLAG_EXCL, device);
	if (!device->pardev) {
		pr_err("couldn't register with %s\n", port->name);
		goto err_free;
	}

	if (parport_claim_or_block(device->pardev) < 0) {
		pr_err("couldn't claim %s\n", port->name);
		goto err_unregister_dev;
	}

	device->pps = pps_register_source(&info,
			PPS_CAPTUREBOTH | PPS_OFFSETASSERT | PPS_OFFSETCLEAR);
	if (device->pps == NULL) {
		pr_err("couldn't register PPS source\n");
		goto err_release_dev;
	}

	device->cw = clear_wait;

	port->ops->enable_irq(port);

	pr_info("attached to %s\n", port->name);

	return;

err_release_dev:
	parport_release(device->pardev);
err_unregister_dev:
	parport_unregister_device(device->pardev);
err_free:
	kfree(device);
}

static void parport_detach(struct parport *port)
{
	struct pardevice *pardev = port->cad;
	struct pps_client_pp *device;

	/* FIXME: oooh, this is ugly! */
	if (strcmp(pardev->name, KBUILD_MODNAME))
		/* not our port */
		return;

	device = pardev->private;

	port->ops->disable_irq(port);
	pps_unregister_source(device->pps);
	parport_release(pardev);
	parport_unregister_device(pardev);
	kfree(device);
}

static struct parport_driver pps_parport_driver = {
	.name = KBUILD_MODNAME,
	.attach = parport_attach,
	.detach = parport_detach,
};

/* module staff */

static int __init pps_parport_init(void)
{
	int ret;

	pr_info(DRVDESC "\n");

	if (clear_wait > CLEAR_WAIT_MAX) {
		pr_err("clear_wait value should be not greater"
				" then %d\n", CLEAR_WAIT_MAX);
		return -EINVAL;
	}

	ret = parport_register_driver(&pps_parport_driver);
	if (ret) {
		pr_err("unable to register with parport\n");
		return ret;
	}

	return  0;
}

static void __exit pps_parport_exit(void)
{
	parport_unregister_driver(&pps_parport_driver);
}

module_init(pps_parport_init);
module_exit(pps_parport_exit);

MODULE_AUTHOR("Alexander Gordeev <lasaine@lvk.cs.msu.su>");
MODULE_DESCRIPTION(DRVDESC);
MODULE_LICENSE("GPL");
