/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __USB_OPS_H_
#define __USB_OPS_H_

#include <linux/version.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>

#define REALTEK_USB_VENQT_READ		0xC0
#define REALTEK_USB_VENQT_WRITE		0x40

#define ALIGNMENT_UNIT			16
#define MAX_VENDOR_REQ_CMD_SIZE	254	/* 8188cu SIE Support */
#define MAX_USB_IO_CTL_SIZE	(MAX_VENDOR_REQ_CMD_SIZE + ALIGNMENT_UNIT)

#include <usb_ops_linux.h>

#define USB_HIGH_SPEED_BULK_SIZE	512
#define USB_FULL_SPEED_BULK_SIZE	64

static inline u8 rtw_usb_bulk_size_boundary(struct adapter *padapter,
					    int buf_len)
{
	u8 rst = true;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	if (pdvobjpriv->ishighspeed)
		rst = (0 == (buf_len) % USB_HIGH_SPEED_BULK_SIZE) ?
		      true : false;
	else
		rst = (0 == (buf_len) % USB_FULL_SPEED_BULK_SIZE) ?
		      true : false;
	return rst;
}

#endif /* __USB_OPS_H_ */
