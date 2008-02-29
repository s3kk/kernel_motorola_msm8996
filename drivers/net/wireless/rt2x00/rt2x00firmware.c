/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 firmware loading routines.
 */

#include <linux/crc-ccitt.h>
#include <linux/crc-itu-t.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

static int rt2x00lib_request_firmware(struct rt2x00_dev *rt2x00dev)
{
	struct device *device = wiphy_dev(rt2x00dev->hw->wiphy);
	const struct firmware *fw;
	char *fw_name;
	int retval;
	u16 crc;

	/*
	 * Read correct firmware from harddisk.
	 */
	fw_name = rt2x00dev->ops->lib->get_firmware_name(rt2x00dev);
	if (!fw_name) {
		ERROR(rt2x00dev,
		      "Invalid firmware filename.\n"
		      "Please file bug report to %s.\n", DRV_PROJECT);
		return -EINVAL;
	}

	INFO(rt2x00dev, "Loading firmware file '%s'.\n", fw_name);

	retval = request_firmware(&fw, fw_name, device);
	if (retval) {
		ERROR(rt2x00dev, "Failed to request Firmware.\n");
		return retval;
	}

	if (!fw || !fw->size || !fw->data) {
		ERROR(rt2x00dev, "Failed to read Firmware.\n");
		return -ENOENT;
	}

	/*
	 * Perform crc validation on the firmware.
	 * The last 2 bytes in the firmware array are the crc checksum itself,
	 * this means that we should never pass those 2 bytes to the crc
	 * algorithm.
	 */
	if (test_bit(DRIVER_REQUIRE_FIRMWARE_CRC_ITU_T, &rt2x00dev->flags)) {
		/*
		 * Use the crc itu-t algorithm.
		 * Use 0 for the last 2 bytes to complete the checksum.
		 */
		crc = crc_itu_t(0, fw->data, fw->size - 2);
		crc = crc_itu_t_byte(crc, 0);
		crc = crc_itu_t_byte(crc, 0);
	} else if (test_bit(DRIVER_REQUIRE_FIRMWARE_CCITT, &rt2x00dev->flags)) {
		/*
		 * Use the crc ccitt algorithm.
		 * This will return the same value as the legacy driver which
		 * used bit ordering reversion on the both the firmware bytes
		 * before input input as well as on the final output.
		 * Obviously using crc ccitt directly is much more efficient.
		 */
		crc = crc_ccitt(~0, fw->data, fw->size - 2);
	} else {
		ERROR(rt2x00dev, "No checksum algorithm selected "
		      "for firmware validation.\n");
		retval = -ENOENT;
		goto exit;
	}

	if (crc != (fw->data[fw->size - 2] << 8 | fw->data[fw->size - 1])) {
		ERROR(rt2x00dev, "Firmware checksum error.\n");
		retval = -ENOENT;
		goto exit;
	}

	INFO(rt2x00dev, "Firmware detected - version: %d.%d.\n",
	     fw->data[fw->size - 4], fw->data[fw->size - 3]);

	rt2x00dev->fw = fw;

	return 0;

exit:
	release_firmware(fw);

	return retval;
}

int rt2x00lib_load_firmware(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	if (!test_bit(DRIVER_REQUIRE_FIRMWARE, &rt2x00dev->flags))
		return 0;

	if (!rt2x00dev->fw) {
		retval = rt2x00lib_request_firmware(rt2x00dev);
		if (retval)
			return retval;
	}

	/*
	 * Send firmware to the device.
	 */
	retval = rt2x00dev->ops->lib->load_firmware(rt2x00dev,
						    rt2x00dev->fw->data,
						    rt2x00dev->fw->size);
	return retval;
}

void rt2x00lib_free_firmware(struct rt2x00_dev *rt2x00dev)
{
	release_firmware(rt2x00dev->fw);
	rt2x00dev->fw = NULL;
}

