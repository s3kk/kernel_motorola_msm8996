/*
 * Michael MIC implementation - optimized for TKIP MIC operations
 * Copyright 2002-2003, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <asm/unaligned.h>

#include "michael.h"

static void michael_block(struct michael_mic_ctx *mctx, u32 val)
{
	mctx->l ^= val;
	mctx->r ^= rol32(mctx->l, 17);
	mctx->l += mctx->r;
	mctx->r ^= ((mctx->l & 0xff00ff00) >> 8) |
		   ((mctx->l & 0x00ff00ff) << 8);
	mctx->l += mctx->r;
	mctx->r ^= rol32(mctx->l, 3);
	mctx->l += mctx->r;
	mctx->r ^= ror32(mctx->l, 2);
	mctx->l += mctx->r;
}

static void michael_mic_hdr(struct michael_mic_ctx *mctx,
			    u8 *key, u8 *da, u8 *sa, u8 priority)
{
	mctx->l = get_unaligned_le32(key);
	mctx->r = get_unaligned_le32(key + 4);

	/*
	 * A pseudo header (DA, SA, Priority, 0, 0, 0) is used in Michael MIC
	 * calculation, but it is _not_ transmitted
	 */
	michael_block(mctx, get_unaligned_le32(da));
	michael_block(mctx, get_unaligned_le16(&da[4]) |
			    (get_unaligned_le16(sa) << 16));
	michael_block(mctx, get_unaligned_le32(&sa[2]));
	michael_block(mctx, priority);
}

void michael_mic(u8 *key, u8 *da, u8 *sa, u8 priority,
		 u8 *data, size_t data_len, u8 *mic)
{
	u32 val;
	size_t block, blocks, left;
	struct michael_mic_ctx mctx;

	michael_mic_hdr(&mctx, key, da, sa, priority);

	/* Real data */
	blocks = data_len / 4;
	left = data_len % 4;

	for (block = 0; block < blocks; block++)
		michael_block(&mctx, get_unaligned_le32(&data[block * 4]));

	/* Partial block of 0..3 bytes and padding: 0x5a + 4..7 zeros to make
	 * total length a multiple of 4. */
	val = 0x5a;
	while (left > 0) {
		val <<= 8;
		left--;
		val |= data[blocks * 4 + left];
	}

	michael_block(&mctx, val);
	michael_block(&mctx, 0);

	put_unaligned_le32(mctx.l, mic);
	put_unaligned_le32(mctx.r, mic + 4);
}
